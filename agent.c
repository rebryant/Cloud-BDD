#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdbool.h>

#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>

#include "dtype.h"
#include "table.h"
#include "chunk.h"
#include "report.h"
#include "msg.h"
#include "console.h"
#include "agent.h"

/* Global data */

/* Is this a client or a worker? */
static bool isclient = false;

/* What's my agent ID? */
unsigned own_agent = 0;

/* Connection to controller */
static int controller_fd;

/* Total number of workers */
static unsigned nworkers = 1;

/* Total number of routers */
static unsigned nrouters = 1;

/* Array of routers.  Each has designated file descriptor */
static int *router_fd_array = NULL;

/* Set of pending operations.  Indexed by operator id */
static keyvalue_table_ptr operator_table = NULL;
/* Set of deferred operands.  Indexed by operator id */

/* For each operator id, have linked list of operands */
typedef struct OPELE operand_ele, *operand_ptr;

struct OPELE {
    chunk_ptr operand;
    unsigned offset;
    operand_ptr next;
};

static keyvalue_table_ptr deferred_operand_table = NULL;

/* Own sequence number */
static unsigned seq_num = 0;

/* Number of bits available for sequence number */
static unsigned snb = 16;

/* Function to call when flush message received */
static flush_function flush_helper = NULL;

/* Function to call when stat message received */
static stat_function stat_helper = NULL;

/* Array of counters for accumulating statistics */
size_t agent_stat_counter[NSTATA] = {0};

/* Forward reference */
bool quit_agent(int argc, char *argv[]);
bool do_agent_kill(int argc, char *argv[]);
bool do_agent_flush(int argc, char *argv[]);

/* Represent set of operations as linked list */
typedef struct OELE op_ele, *op_ptr;

struct OELE {
    unsigned opcode;
    op_handler opfun;
    op_ptr next;
};

/* Set of operations */
op_ptr op_list = NULL;

/* Set function to be called when agent command to flush its state */
void set_agent_flush_helper(flush_function ff) {
    flush_helper = ff;
}

/* Set function to be called when agent command to flush its state */
void set_agent_stat_helper(stat_function sf) {
    stat_helper = sf;
}


/* Add an operation */
void add_op_handler(unsigned opcode, op_handler h) {
    op_ptr ele = (op_ptr) malloc_or_fail(sizeof(op_ele), "add_op_handler");
    ele->opcode = opcode;
    ele->opfun = h;
    ele->next = op_list;
    op_list = ele;
}

void init_agent(bool iscli, char *controller_name, unsigned controller_port) {
    operator_table = word_keyvalue_new();
    deferred_operand_table = word_keyvalue_new();
    size_t i;
    for (i = 0; i < NSTATA; i++)
	agent_stat_counter[i] = 0;

    chunk_ptr msg;
    bool eof;
    isclient = iscli;
    controller_fd = open_clientfd(controller_name, controller_port);
    if (controller_fd < 0)
	err(true, "Cannot create connection to controller at %s:%u", controller_name, controller_port);
    else
	report(2, "Connection to controller has descriptor %d", controller_fd);
    msg = isclient ? msg_new_register_client() : msg_new_register_worker();
    bool sok = chunk_write(controller_fd, msg);
    report(3, "Sent %s registration to controller", isclient ? "client" : "worker");
    chunk_free(msg);
    if (!sok)
	err(true, "Could not send registration message to controller");
    /* Get acknowledgement from controller */
    bool first = true;
    /* Anticipated number of routers (Will be updated when get first message from controller) */
    nrouters = 1;
    chunk_ptr amsg = NULL;
    unsigned ridx = 0;;
    while (ridx < nrouters) {
	msg = chunk_read(controller_fd, &eof);
	if (eof) {
	    err(true, "Unexpected EOF from controller while getting router map");
	}
	word_t h = chunk_get_word(msg, 0);
	unsigned code = msg_get_header_code(h);
	switch (code) {
	case MSG_ACK_AGENT:
	    if (first) {
		own_agent = msg_get_header_agent(h);
		amsg = msg_new_register_agent(own_agent);
		nworkers = msg_get_header_workercount(h);
		nrouters = msg_get_header_wordcount(h);
		snb = msg_get_header_snb(h);
		router_fd_array = calloc_or_fail(nrouters, sizeof(int), "init_agent");
		report(3, "Ack from controller.  Agent Id %u.  %d workers.  %d routers.", own_agent, nworkers, nrouters);
		first = false;
	    }
	    int i;
	    for (i = 1; i < msg->length; i++) {
		word_t h = chunk_get_word(msg, i);
		int fd;
		unsigned ip = msg_get_header_ip(h);
		unsigned port = msg_get_header_port(h);
		fd = open_clientfd_ip(ip, port);
		if (fd < 0) {
		    err(true, "Couldn't add router with ip 0x%x, port %d", ip, port);
		} else {
		    router_fd_array[ridx++] = fd;
		    report(3, "Added router %u with ip 0x%x, port %d, fd %d", ridx, ip, port, fd);
		    if (!chunk_write(fd, amsg)) {
			err(true, "Couldn't send agent registration message to router with ip 0x%x, port %u", ip, port);
		    }
		}
	    }
	    chunk_free(msg);
	    break;
	case MSG_NACK:
	    err(true, "Connection request refused.");
	    break;
	default:
	    err(false, "Unexpected message code %u while getting router information", code);
	    chunk_free(msg);
	    break;
	}
    }
    chunk_free(amsg);
    report(2, "All %d routers connected", nrouters);
    if (isclient) {
	add_quit_helper(quit_agent);
	add_cmd("kill", do_agent_kill,     "kill         | Shutdown system");
	add_cmd("flush", do_agent_flush,   "flush        | Flush system");
    } else {
	/* Worker must notify controller that it's ready */
	chunk_ptr rmsg = msg_new_worker_ready(own_agent);
	if (chunk_write(controller_fd, rmsg)) {
	    report(3, "Notified controller that worker is ready");
	} else {
	    err(true, "Couldn't notify controller that worker is ready");
	}
	chunk_free(rmsg);
    }
}

bool quit_agent(int argc, char *argv[]) {
    op_ptr op = op_list;
    word_t w;
    while (op) {
	op_ptr ele = op;
	op = ele->next;
	free_block(ele, sizeof(op_ele));
    }
    if (router_fd_array)
	free_array(router_fd_array, nrouters, sizeof(int));
    /* Free any pending operations */
    chunk_ptr msg;
    keyvalue_iterstart(operator_table);
    while (keyvalue_removenext(operator_table, NULL, (word_t *) &msg)) {
	chunk_free(msg);
    }
    keyvalue_free(operator_table);
    keyvalue_iterstart(deferred_operand_table);
    while (keyvalue_removenext(deferred_operand_table, NULL, &w)) {
	operand_ptr ls = (operand_ptr) w;
	while (ls) {
	    chunk_free(ls->operand);
	    operand_ptr ele = ls;
	    ls = ls->next;
	    free_block(ele, sizeof(operand_ele));
	}
    }
    keyvalue_free(deferred_operand_table);
    return true;
}

bool do_agent_kill(int argc, char *argv[]) {
    chunk_ptr msg = msg_new_kill();
    if (chunk_write(controller_fd, msg)) {
	report(3, "Notified controller that want to kill system");
    } else {
	err(false, "Couldn't notify controller that want to kill system");
    }
    chunk_free(msg);
    return true;
}

bool do_agent_flush(int argc, char *argv[]) {
    chunk_ptr msg = msg_new_flush();
    if (chunk_write(controller_fd, msg)) {
	report(3, "Notified controller that want to flush system");
    } else {
	err(false, "Couldn't notify controller that want to flush system");
    }
    chunk_free(msg);
    return true;
}

/* Create a new operator id */
unsigned new_operator_id() {
    unsigned mask = (unsigned) ((word_t) 1 << snb) - 1;
    return (own_agent << snb) | (seq_num++ & mask);
}


/* Get agent ID for worker based on some hashed value */
unsigned choose_hashed_worker(word_t hash) {
    return hash % nworkers;
}

/* Get agent ID for random worker */
unsigned choose_some_worker() {
    return
#if 0
	choose_hashed_worker(random());
#else
	choose_own_worker(random());
#endif
}

/* Get agent ID for local worker */
unsigned choose_own_worker() {
    return own_agent;
}


static void receive_operation(chunk_ptr op);
static void receive_operand(chunk_ptr oper);

/* Should bypass router when message destined to self? */
static bool self_route = true;

/* Send single-valued operand */
bool send_as_operand(word_t dest, word_t val) {
    chunk_ptr oper = msg_new_operand(dest, 2);
    chunk_insert_word(oper, val, 1);
    bool ok = send_op(oper);
    chunk_free(oper);
    return ok;
}

bool send_op(chunk_ptr msg) {
    word_t h = chunk_get_word(msg, 0);
    unsigned agent = msg_get_header_agent(h);
    unsigned code = msg_get_header_code(h);
    unsigned id = msg_get_header_op_id(h);

    if (code == MSG_OPERATION) {
	agent_stat_counter[STATA_OPERATION_TOTAL]++;
	if (self_route && agent == own_agent) {
	    agent_stat_counter[STATA_OPERATION_LOCAL]++;
	    report(6, "Routing operator with id 0x%x to self", id);
	    receive_operation(chunk_clone(msg));
	    return true;
	}
    }
    if (code == MSG_OPERAND) {
	agent_stat_counter[STATA_OPERAND_TOTAL]++;
	if (self_route && agent == own_agent && !isclient) {
	    agent_stat_counter[STATA_OPERAND_LOCAL]++;
	    report(6, "Routing operand with id 0x%x to self", id);
	    receive_operand(chunk_clone(msg));
	    return true;
	}
    }
    unsigned idx = random() % nrouters;
    int rfd = router_fd_array[idx];
    report(5, "Sending message with id 0x%x through router %u (fd %d)", id, idx, rfd);
    bool ok = chunk_write(rfd, msg);
    if (ok)
	report(5, "Message sent");
    else
	err(false, "Failed");
    return ok;
}

/* Insert an operand into an operation */
void insert_operand(chunk_ptr op, chunk_ptr oper, unsigned offset) {
    size_t i;
    size_t n = oper->length-1;
    /* Debugging stuff */
    word_t h = chunk_get_word(op, 0);
    unsigned opcode = msg_get_header_opcode(h);
    report(6, "Inserting operand with %u words into operation with opcode %u at offset %u.  Mask 0x%lx",
	   (unsigned) n, opcode, offset, op->vmask);
    for (i = 0; i < n; i++) {
	word_t w = chunk_get_word(oper, i+1);
	chunk_insert_word(op, w, i+offset);
    }
}


/* For managing select command in main control loop */
static fd_set cset;
static int maxcfd = 0;

static void add_cfd(int fd) {
    report(6, "Adding fd %d to command set", fd);
    FD_SET(fd, &cset);
    if (fd > maxcfd)
	maxcfd = fd;
}

/* For managing select command when waiting for operand result */
static fd_set rset;
static int maxrfd = 0;

static void add_rfd(int fd) {
    report(6, "Adding fd %d to waiting operand set", fd);
    FD_SET(fd, &rset);
    if (fd > maxrfd)
	maxrfd = fd;
}

static void add_deferred_operand(unsigned operator_id, chunk_ptr operand, unsigned offset) {
    word_t w;
    operand_ptr ele = malloc_or_fail(sizeof(operand_ele), "add_deferred_operand");
    ele->operand = operand;
    ele->offset = offset;
    ele->next = NULL;
    if (keyvalue_find(deferred_operand_table, operator_id, &w)) {
	operand_ptr ls = (operand_ptr) w;
	/* Put as second element in list */
	ele->next = ls->next;
	ls->next = ele;
    } else {
	keyvalue_insert(deferred_operand_table, operator_id, (word_t) ele);
    }
}

/* Check status of operation and fire if enabled */
static bool check_fire(chunk_ptr op) {
    if (!chunk_filled(op)) {
	return false;
    }
    word_t h = chunk_get_word(op, 0);
    unsigned opcode = msg_get_header_opcode(h);
    unsigned id = msg_get_header_op_id(h);
    op_ptr ls = op_list;
    op_handler opfun = NULL;
    while (ls) {
	if (ls->opcode == opcode) {
	    opfun = ls->opfun;
	    break;
	}
	ls = ls->next;
    }
    if (opfun) {
	if (!opfun(op))
	    err(false, "Error encountered firing operator with id 0x%x", id);
    } else
	err(false, "Unkown opcode %u for operator with id 0x%x", opcode, id);	
    return true;
}

/* For workers, and possibly clients */
/* Accept operation and either fire it or defer it */
static void receive_operation(chunk_ptr op) {
    word_t w;
    word_t h = chunk_get_word(op, 0);
    unsigned id = msg_get_header_op_id(h);
    /* Check if there's already an outstanding operation with the same ID */
    if (keyvalue_find(operator_table, id, NULL)) {
	err(false, "Operator ID collision encountered.  Op id = 0x%x", id);
	chunk_free(op);
	return;
    }
    /* See if there are any pending operands for this operation */
    if (keyvalue_remove(deferred_operand_table, (word_t) id, NULL, &w)) {
	operand_ptr ls = (operand_ptr) w;
	while (ls) {
	    operand_ptr ele = ls;
	    insert_operand(op, ls->operand, ls->offset);
	    report(5, "Inserted operand with offset %u into received operator with id 0x%x", ls->offset, id);
	    chunk_free(ls->operand);
	    ls = ls->next;
	    free_block(ele, sizeof(operand_ele));
	}
    }
    if (check_fire(op)) {
	report(5, "Completed firing of newly received operation with id 0x%x", id);
	chunk_free(op);
    } else {
	keyvalue_insert(operator_table, (word_t) id, (word_t) op);
	report(5, "Queued operation with id 0x%x", id);
    }
}

/* For workers only */
static void receive_operand(chunk_ptr oper) {
    word_t w;
    word_t h = chunk_get_word(oper, 0);
    unsigned id = msg_get_header_op_id(h);
    unsigned offset = msg_get_header_offset(h);
    if (keyvalue_find(operator_table, (word_t) id, &w)) {
	/* Operation exists */
	chunk_ptr op = (chunk_ptr) w;
	insert_operand(op, oper, offset);
	report(5, "Inserted operand with offset %u into existing operator with id 0x%x", offset, id);
	chunk_free(oper);
	if (check_fire(op)) {
	    report(5, "Completed firing of dequeued operation with id 0x%x", id);
	    keyvalue_remove(operator_table, (word_t) id, NULL, NULL);
	    chunk_free(op);
	}
    } else {
	add_deferred_operand(id, oper, offset);
	report(5, "Deferred operand with offset %u for id 0x%x", offset, id);
    }
}


void run_worker() {
    while (true) {
	/* Select among controller port, and connections to routers */
	FD_ZERO(&cset);
	maxcfd = 0;
	add_cfd(controller_fd);
	unsigned ridx;
	for (ridx = 0; ridx < nrouters; ridx++)
	    add_cfd(router_fd_array[ridx]);

	select(maxcfd+1, &cset, NULL, NULL, NULL);
	int fd;
	for (fd = 0; fd <= maxcfd; fd++) {
	    if (!FD_ISSET(fd, &cset))
		continue;
	    bool eof;
	    chunk_ptr msg = chunk_read(fd, &eof);
	    if (eof) {
		/* Unexpected EOF */
		if (fd == controller_fd) {
		    err(false, "Unexpected EOF from controller (ignored)");
		} else {
		    err(false, "Unexpected EOF from router with fd %d (ignored)", fd);
		}
		close(fd);
		continue;
	    }
	    if (msg == NULL) {
		err(false, "Could not read chunk from fd %d (ignored)", fd);
		continue;
	    }
	    word_t h = chunk_get_word(msg, 0);
	    unsigned code = msg_get_header_code(h);
	    if (fd == controller_fd) {
		/* Message from controller */
		switch(code) {
		case MSG_KILL:
		    chunk_free(msg);
		    report(5, "Received kill message from controller");
		    quit_agent(0, NULL);
		    return;
		case MSG_DO_FLUSH:
		    chunk_free(msg);
		    report(5, "Received flush message from controller");
		    if (flush_helper) {
			chunk_ptr msg = flush_helper();
			if (!msg)
			    break;
			if (chunk_write(controller_fd, msg)) {
			    report(5, "Sent statistics information to controller");
			} else {
			    err(false, "Failed to send statistics information to controller");
			}
			chunk_free(msg);
		    }
		    break;
		default:
		    chunk_free(msg);
		    err(false, "Unknown message code %u from controller (ignored)", code);
		}
	    } else {
		/* Must be message from router */
		switch (code) {
		case MSG_OPERATION:
		    receive_operation(msg);
		    break;
		case MSG_OPERAND:
		    receive_operand(msg);
		    break;
		default:
		    chunk_free(msg);
		    err(false, "Received message with unknown code %u (ignored)", code);
		}
	    }
	}
    }
    quit_agent(0, NULL);
}

/* Fire an operation and wait for returned operand */
chunk_ptr fire_and_wait(chunk_ptr msg) {
    if (!send_op(msg)) {
	err(false, "Failed to send message");
	return NULL;
    }
    while (!cmd_done()) {
	/* Select among controller port, and connections to routers.  Do not accept console input */
	FD_ZERO(&rset);
	maxrfd = 0;
	add_rfd(controller_fd);
	unsigned ridx;
	for (ridx = 0; ridx < nrouters; ridx++)
	    add_rfd(router_fd_array[ridx]);

	select(maxrfd+1, &rset, NULL, NULL, NULL);
	int fd;
	for (fd = 0; fd <= maxrfd; fd++) {
	    if (!FD_ISSET(fd, &rset))
		continue;
	    bool eof;
	    chunk_ptr msg = chunk_read(fd, &eof);
	    if (eof) {
		/* Unexpected EOF */
		if (fd == controller_fd) {
		    err(false, "Unexpected EOF from controller (ignored)");
		} else {
		    err(false, "Unexpected EOF from router with fd %d (ignored)", fd);
		}
		close(fd);
		continue;
	    }
	    if (msg == NULL) {
		err(false, "Could not read chunk from fd %d (ignored)", fd);
		continue;
	    }
	    word_t h = chunk_get_word(msg, 0);
	    unsigned code = msg_get_header_code(h);
	    unsigned id = msg_get_header_op_id(h);
	    if (fd == controller_fd) {
		/* Message from controller */
		switch(code) {
		case MSG_KILL:
		    chunk_free(msg);
		    report(1, "Received kill message from controller");
		    quit_agent(0, NULL);
		    break;
		case MSG_DO_FLUSH:
		    chunk_free(msg);
		    report(1, "Received flush message from controller");
		    if (flush_helper) {
			flush_helper(0, NULL);
		    }
		    break;
		default:
		    chunk_free(msg);
		    err(false, "Unknown message code %u from controller (ignored)", code);
		}
	    } else {
		/* Must be message from router */
		switch (code) {
		case MSG_OPERATION:
		    chunk_free(msg);
		    err(false, "Received unexpected operation.  Ignored.");
		    return NULL;
		case MSG_OPERAND:
		    report(5, "Received operand with id 0x%x", id);
		    return msg;
		default:
		    chunk_free(msg);
		    err(false, "Received message with unknown code %u (ignored)", code);
		    return NULL;
		}
	    }
	}
    }
    return NULL;
}

/* Client command loop only considers console inputs + controller messages */
void run_client(char *infile_name) {
    if (!start_cmd(infile_name))
	return;
    while (!cmd_done()) {
	/* Select among controller port, and connections to routers */
	FD_ZERO(&cset);
	maxcfd = 0;
	add_cfd(controller_fd);
	cmd_select(maxcfd+1, &cset, NULL, NULL, NULL);
	int fd;
	for (fd = 0; fd <= maxcfd; fd++) {
	    if (!FD_ISSET(fd, &cset))
		continue;
	    bool eof;
	    chunk_ptr msg = chunk_read(fd, &eof);
	    if (eof) {
		/* Unexpected EOF */
		if (fd == controller_fd) {
		    err(false, "Unexpected EOF from controller (ignored)");
		} else {
		    err(false, "Unexpected EOF from unexpected source. fd %d (ignored)", fd);
		}
		close(fd);
		continue;
	    }
	    if (msg == NULL) {
		err(false, "Could not read chunk from fd %d (ignored)", fd);
		continue;
	    }
	    word_t h = chunk_get_word(msg, 0);
	    unsigned code = msg_get_header_code(h);
	    if (fd == controller_fd) {
		/* Message from controller */
		switch(code) {
		case MSG_DO_FLUSH:
		    chunk_free(msg);
		    report(5, "Received flush message from controller");
		    if (flush_helper) {
			flush_helper();
		    }
		    break;
		case MSG_STAT:
		    report(5, "Received summary statistics from controller");
		    if (stat_helper) {
			/* Get a copy of the byte usage from memory allocator */
			stat_helper(msg);
		    }
		    chunk_free(msg);
		    break;
		case MSG_KILL:
		    chunk_free(msg);
		    report(5, "Received kill message from controller");
		    finish_cmd();
		    break;
		default:
		    chunk_free(msg);
		    err(false, "Unknown message code %u from controller (ignored)", code);
		}
	    } else {
		err(false, "Received message from unknown source.  fd %d (ignored)", fd);
		close(fd);
		continue;
	    }
	}
    }
}

