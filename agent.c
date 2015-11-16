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
#include <ifaddrs.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

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
static word_t seq_num = 0;

/* Function to call when flush message received */
static flush_function flush_helper = NULL;

/* Function to call when stat message received */
static stat_function stat_helper = NULL;

/* Array of counters for accumulating statistics */
size_t agent_stat_counter[NSTATA] = {0};

/* Helper functions associated with global operations */
static global_op_start_function gop_start_helper = NULL;
static global_op_finish_function gop_finish_helper = NULL;

/* Helper functions for garbage collection */
gc_handler start_gc_handler = NULL;
gc_handler finish_gc_handler = NULL;

/* Forward reference */
bool quit_agent(int argc, char *argv[]);
bool do_agent_kill(int argc, char *argv[]);
bool do_agent_flush(int argc, char *argv[]);
bool do_agent_gc(int arg, char *argv[]);

static void gc_start(unsigned code);
static void gc_finish(unsigned code);

/* Represent set of operations as linked list */
typedef struct OELE op_ele, *op_ptr;

struct OELE {
    unsigned opcode;
    op_handler opfun;
    op_ptr next;
};

/* Set of operations */
op_ptr op_list = NULL;

/* Set function to be called when agent commanded to flush its state */
void set_agent_flush_helper(flush_function ff) {
    flush_helper = ff;
}

/* Set function to be called when agent commanded to gather statistics */
void set_agent_stat_helper(stat_function sf) {
    stat_helper = sf;
}

/* Provide handlers to perform global operation by worker */
void set_agent_global_helpers(global_op_start_function gosf,
			      global_op_finish_function goff) {
    gop_start_helper = gosf;
    gop_finish_helper = goff;
}

/* Handlers to start & finish GC */
void set_gc_handlers(gc_handler start_handler, gc_handler finish_handler) {
    start_gc_handler = start_handler;
    finish_gc_handler = finish_handler;
}

typedef enum {
    GC_IDLE,
    GC_REQUESTED,
    GC_DEFER,
    GC_ACTIVE
} gc_state_t;

gc_state_t gc_state = GC_IDLE;

/* Sequence number for garbage collection phases */
unsigned gc_generation = 0;

/* Number representing the file descriptor of the router running
   on the same system, or -1 if no such router exists (by default) */
int local_router_fd = -1;

/* Add an operation */
void add_op_handler(unsigned opcode, op_handler h) {
    op_ptr ele = (op_ptr) malloc_or_fail(sizeof(op_ele), "add_op_handler");
    ele->opcode = opcode;
    ele->opfun = h;
    ele->next = op_list;
    op_list = ele;
}

int match_self_ip(unsigned hip) {
    char ipv4str[INET_ADDRSTRLEN];
    unsigned nip = ntohl(hip);
    inet_ntop(AF_INET, &nip, ipv4str, INET_ADDRSTRLEN);
    report(5, "Provided IP is: %s\n", ipv4str);

    struct ifaddrs *ifap;
    int x;
    if ((x = getifaddrs(&ifap)) == -1)
    {
        err(false, "Couldn't get self-interface information");
        return -1;
    }

    struct ifaddrs *curr = ifap;

    unsigned interface_ip;

    while (curr != NULL)
    {
        if (((struct sockaddr_in *)(curr->ifa_addr))->sin_family == AF_INET)
        {
            interface_ip =
		((struct sockaddr_in *)(curr->ifa_addr))->sin_addr.s_addr;
            inet_ntop(AF_INET, &interface_ip, ipv4str, INET_ADDRSTRLEN);
            report(5, "System's IP is: %s\n", ipv4str);

            if (interface_ip == nip)
                return 1;
        }
        curr = curr->ifa_next;
    }
    return 0;
}

void init_agent(bool iscli, char *controller_name, unsigned controller_port,
		bool try_local_router) {
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
	err(true, 
	    "Cannot create connection to controller at %s:%u",
	    controller_name, controller_port);
    else
	report(2, "Connection to controller has descriptor %d", controller_fd);
    msg = isclient ? msg_new_register_client() : msg_new_register_worker();
    bool sok = chunk_write(controller_fd, msg);
    report(3, "Sent %s registration to controller",
	   isclient ? "client" : "worker");
    chunk_free(msg);
    if (!sok)
	err(true, "Could not send registration message to controller");
    /* Get acknowledgement from controller */
    bool first = true;
    /* Anticipated number of routers 
      (Will be updated when get first message from controller) */
    nrouters = 1;
    chunk_ptr amsg = NULL;
    unsigned ridx = 0;;
    while (ridx < nrouters) {
	msg = chunk_read_unbuffered(controller_fd, &eof);
	if (eof) {
	    err(true,
		"Unexpected EOF from controller while getting router map");
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
		router_fd_array = calloc_or_fail(nrouters, sizeof(int),
						 "init_agent");
		report(3,
"Ack from controller.  Agent Id %u.  %d workers.  %d routers.",
		       own_agent, nworkers, nrouters);
		first = false;
	    }
	    int i;
	    for (i = 1; i < msg->length; i++) {
		word_t h = chunk_get_word(msg, i);
		int fd;
		unsigned ip = msg_get_header_ip(h);
		unsigned port = msg_get_header_port(h);
		report(4, "Attempting to add router %u with ip 0x%x, port %d",
		       ridx, ip, port);
		fd = open_clientfd_ip(ip, port);
		if (fd < 0) {
		    err(true, "Couldn't add router with ip 0x%x, port %d",
			ip, port);
		} else {
		    router_fd_array[ridx++] = fd;
		    report(3, "Added router %u with ip 0x%x, port %d, fd %d",
			   ridx, ip, port, fd);
		    if (!chunk_write(fd, amsg)) {
			err(true, 
"Couldn't send agent registration message to router with ip 0x%x, port %u",
			    ip, port);
		    }

                    if (try_local_router && local_router_fd == -1 &&
			match_self_ip(ip))
                    {
                        local_router_fd = fd;
                        report(5,
"Router with fd %d designated as local router and prioritized for sending packets",
			       fd);
                    }
		}
	    }
	    chunk_free(msg);
	    break;
	case MSG_NACK:
	    err(true, "Connection request refused.");
	    break;
	default:
	    err(false,
"Unexpected message code %u while getting router information", code);
	    chunk_free(msg);
	    break;
	}
    }
    chunk_free(amsg);
    report(2, "All %d routers connected", nrouters);
    if (isclient) {
	add_quit_helper(quit_agent);
	add_cmd("kill", do_agent_kill,
		"              | Shutdown system");
	add_cmd("flush", do_agent_flush,   
		"              | Flush system");
	add_cmd("collect", do_agent_gc,
		"              | Initiate garbage collection");
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
    chunk_deinit();
    return true;
}

void agent_show_stat() {
    /* Gather statistics information */
    agent_stat_counter[STATA_BYTE_PEAK] = last_peak_bytes;
    report(0, "Peak bytes %" PRIu64,
	   agent_stat_counter[STATA_BYTE_PEAK]);
    report(0,
"Operations.  Total generated %" PRIu64 ".  Routed locally %" PRIu64,
	   agent_stat_counter[STATA_OPERATION_TOTAL],
	   agent_stat_counter[STATA_OPERATION_LOCAL]);
    report(0,
"Operands.  Total generated %" PRIu64 ".  Routed locally %" PRIu64,
	   agent_stat_counter[STATA_OPERAND_TOTAL],
	   agent_stat_counter[STATA_OPERAND_LOCAL]);
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
    /* Further command processing must wait until received statistics from controller */
    block_console();
    bool ok = chunk_write(controller_fd, msg);
    if (ok)
	report(3, "Notified controller that want to flush system");
    else
	err(false, "Couldn't notify controller that want to flush system");
    chunk_free(msg);
    gc_state = GC_IDLE;
    gc_generation = 0;
   return ok;
}

bool do_agent_gc(int argc, char *argv[]) {
    chunk_ptr msg = msg_new_gc_start();
    /* Further command processing must wait until gc completes */
    block_console();
    bool ok = chunk_write(controller_fd, msg);
    chunk_free(msg);
    if (ok)
	report(4, "Notified controller that want to run garbage collection");
    else
	err(false,
	    "Couldn't notify controller that want to run garbage collection");
    return ok;
}

/* Create a new operator id */
word_t new_operator_id() {
    word_t mask = ((word_t) 1 << 48) - 1;
    return ((word_t) own_agent << 48) | (seq_num++ & mask);
}


/* Get agent ID for worker based on some hashed value */
unsigned choose_hashed_worker(word_t hash) {
    return hash % nworkers;
}

/* Get agent ID for random worker */
unsigned choose_random_worker() {
    return
	choose_hashed_worker(random());
}


/* Get agent ID for random worker */
unsigned choose_some_worker() {
    return
#if 0
	choose_random_worker();
#else
	choose_own_worker();
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
bool send_as_operand(dword_t dest, word_t val) {
    chunk_ptr oper = msg_new_operand(dest, 1 + OPER_HEADER_CNT);
    chunk_insert_word(oper, val, 0 + OPER_HEADER_CNT);
    bool ok = send_op(oper);
    chunk_free(oper);
    return ok;
}

bool send_op(chunk_ptr msg) {
    dword_t dh = chunk_get_dword(msg, 0);
    unsigned agent = msg_get_dheader_agent(dh);
    unsigned code = msg_get_dheader_code(dh);
    word_t id = msg_get_dheader_op_id(dh);

    if (code == MSG_OPERATION) {
	agent_stat_counter[STATA_OPERATION_TOTAL]++;
	if (self_route && agent == own_agent) {
	    agent_stat_counter[STATA_OPERATION_LOCAL]++;
	    report(6, "Routing operator with id 0x%lx to self", id);
	    receive_operation(chunk_clone(msg));
	    return true;
	}
    }
    if (code == MSG_OPERAND) {
	agent_stat_counter[STATA_OPERAND_TOTAL]++;
	if (self_route && agent == own_agent && !isclient) {
	    agent_stat_counter[STATA_OPERAND_LOCAL]++;
	    report(6, "Routing operand with id 0x%lx to self", id);
	    receive_operand(chunk_clone(msg));
	    return true;
	}
    }
    // Try to send to a local router if possible
    int rfd;
    if (local_router_fd == -1)
    {
        unsigned idx = random() % nrouters;
        rfd = router_fd_array[idx];
        report(5,
"Sending message with id 0x%x through router %u (fd %d)", id, idx, rfd);
    }
    else
    {
        rfd = local_router_fd;
        report(5,
"Sending message with id 0x%x through the local router (fd %d)", id, rfd);
    }

    bool ok = chunk_write(rfd, msg);
    if (ok)
	report(5, "Message sent");
    else
	err(false, "Failed");
    return ok;
}

/* Insert word into operator, updating its valid mask.
   Offset includes header size */
void op_insert_word(chunk_ptr op, word_t wd, size_t offset) {
    word_t vmask = chunk_get_word(op, 2);
    word_t nvmask;
    word_t idx = (word_t) 1 << offset;
    if (vmask & idx) {
	err(false,
"Inserting into already filled position in operator.  Offset = %lu", offset);
    }
    chunk_insert_word(op, wd, offset);
    nvmask = vmask | idx;
    chunk_insert_word(op, nvmask, 2);
    if (verblevel >= 6) {
	dword_t dh = chunk_get_dword(op, 0);
	word_t id = msg_get_dheader_op_id(dh);
	report(6,
	       "Inserted word, offset %d, operation with id 0x%lx.\n"
	       "  Total size = %d.  Vmask 0x%lx --> 0x%lx",
	       (int) offset, id, op->length, vmask, nvmask);
    }
}

/* Insert double word into operator, updating its valid mask.
   Offset includes header size.
   Offset is for first word.
*/
void op_insert_dword(chunk_ptr op, dword_t dwd, size_t offset) {
    word_t vmask = chunk_get_word(op, 2);
    word_t nvmask;
    word_t imask = (word_t) 3 << offset;
    if (vmask & imask) {
	err(false,
"Inserting into already filled position in operator.  Offsets = %lu,%lu",
	    offset, offset+1);
    }
    chunk_insert_dword(op, dwd, offset);
    nvmask = vmask | imask;
    chunk_insert_word(op, nvmask, 2);
    if (verblevel >= 6) {
	dword_t dh = chunk_get_dword(op, 0);
	word_t id = msg_get_dheader_op_id(dh);
	report(6,
"Inserted double word at offset %d into operation with id 0x%lx.\n"
"  Total size = %d.  Vmask 0x%lx --> 0x%lx",
	       (int) offset, id, op->length, vmask, nvmask);
    }
}

/* Insert an operand into an operation */
void op_insert_operand(chunk_ptr op, chunk_ptr oper, unsigned offset) {
    size_t i;
    size_t n = oper->length-OPER_HEADER_CNT;
    if (verblevel >= 6) {
	dword_t dh = chunk_get_dword(op, 0);
	word_t vmask = chunk_get_word(op, 2);
	unsigned opcode = msg_get_dheader_opcode(dh);
	report(5,
"Inserting operand with %u words into op with opcode %u at offset %u.  Mask 0x%lx",
	       (unsigned) n, opcode, offset, vmask);
    }
    for (i = 0; i < n; i++) {
	word_t w = chunk_get_word(oper, i+OPER_HEADER_CNT);
	op_insert_word(op, w, i+offset);
    }
}

/* Check whether all fields an an operator are valid */
bool op_check_full(chunk_ptr op) {
    if (op == NULL) {
	err(false, "op_check_full given null pointer");
	return false;
    }
    word_t len = op->length;
    word_t vmask = chunk_get_word(op, 2);
    /* Create checking vmask */
    word_t cmask = len == OP_MAX_LENGTH ? ~0ULL : (1ULL << len) - 1;
    return vmask == cmask;
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

static void add_deferred_operand(word_t operator_id, chunk_ptr operand,
				 unsigned offset) {
    word_t w;
    operand_ptr ele = malloc_or_fail(sizeof(operand_ele),
				     "add_deferred_operand");
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
    if (!op_check_full(op)) {
	return false;
    }
    dword_t dh = chunk_get_dword(op, 0);
    unsigned opcode = msg_get_dheader_opcode(dh);
    word_t id = msg_get_dheader_op_id(dh);
    op_ptr ls = op_list;
    op_handler opfun = NULL;
    report(5, "Firing operation with id 0x%lx", id);
    while (ls) {
	if (ls->opcode == opcode) {
	    opfun = ls->opfun;
	    break;
	}
	ls = ls->next;
    }
    if (opfun) {
	if (!opfun(op))
	    err(false, "Error encountered firing operator with id 0x%lx", id);
    } else
	err(false, "Unknown opcode %u for operator with id 0x%lx", opcode, id);
    return true;
}

/*
  Initiate global operation from client.
  Returns to client when all workers ready to perform their operations
*/
bool start_client_global(unsigned opcode, unsigned nword, word_t *data) {
    chunk_ptr rmsg = msg_new_cliop_data(own_agent, opcode, nword, data);
    if (!chunk_write(controller_fd, rmsg)) {
	err(false, "Could not send client operation message to controller");
	chunk_free(rmsg);
	return false;
    }
    chunk_free(rmsg);
    /* Read ACK, as well as any other messages the controller might have */
    bool done = false;
    bool ok = true;
    while (!done) {
	bool eof = false;
	chunk_ptr msg = chunk_read_unbuffered(controller_fd, &eof);
	if (eof) {
	    /* Unexpected EOF */
	    err(true, "Unexpected EOF from controller (fatal)");
	    close(controller_fd);
	    done = true; ok = false;
	}
	word_t h = chunk_get_word(msg, 0);
	unsigned code = msg_get_header_code(h);
	switch (code) {
	case MSG_DO_FLUSH:
	    chunk_free(msg);
	    report(5,
"Received flush message from controller, superceding client global operation");
	    if (flush_helper) {
		flush_helper();
	    }
	    done = true; ok = false;
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
	    report(5,
"Received kill message from controller, superceding client global operation");
	    finish_cmd();
	    done = true; ok = false;
	    break;
	case MSG_CLIOP_ACK:
	    chunk_free(msg);
	    report(5,
"Received acknowledgement for client global operation");
	    done = true; ok = true;
	    break;
	case MSG_GC_START:
	    /* Got notice that should initiate garbage collection.
	       Defer until current operation done */
	    report(3, "Deferring GC start");
	    chunk_free(msg);
	    gc_state = GC_DEFER;
	    break;
	default:
	    chunk_free(msg);
	    err(false,
"Unknown message code %u from controller (ignored)", code);
	}
    }
    return ok;
}

/*
  Client signals that it has completed the global operation
*/
bool finish_client_global() {
    chunk_ptr msg = msg_new_cliop_ack(own_agent);
    bool ok = chunk_write(controller_fd, msg);
    chunk_free(msg);
    return ok;
}



/* For workers, and possibly clients */
/* Accept operation and either fire it or defer it */
static void receive_operation(chunk_ptr op) {
    word_t w;
    dword_t dh = chunk_get_dword(op, 0);
    word_t id = msg_get_dheader_op_id(dh);
    report(5, "Received operation.  id 0x%lx", id);
    /* Check if there's already an outstanding operation with the same ID */
    if (keyvalue_find(operator_table, id, NULL)) {
	err(false, "Operator ID collision encountered.  Op id = 0x%lx", id);
	chunk_free(op);
	return;
    }
    /* See if there are any pending operands for this operation */
    if (keyvalue_remove(deferred_operand_table, id, NULL, &w)) {
	operand_ptr ls = (operand_ptr) w;
	while (ls) {
	    operand_ptr ele = ls;
	    op_insert_operand(op, ls->operand, ls->offset);
	    report(5,
"Inserted operand with offset %u into received operator with id 0x%lx",
		   ls->offset, id);
	    chunk_free(ls->operand);
	    ls = ls->next;
	    free_block(ele, sizeof(operand_ele));
	}
    }
    if (check_fire(op)) {
	report(5,
"Completed firing of newly received operation with id 0x%lx", id);
	chunk_free(op);
    } else {
	keyvalue_insert(operator_table, (word_t) id, (word_t) op);
	report(5, "Queued operation with id 0x%lx", id);
    }
}

/* For workers only */
static void receive_operand(chunk_ptr oper) {
    word_t w;
    dword_t dh = chunk_get_dword(oper, 0);
    word_t id = msg_get_dheader_op_id(dh);
    unsigned offset = msg_get_dheader_offset(dh);
    if (keyvalue_find(operator_table, id, &w)) {
	/* Operation exists */
	chunk_ptr op = (chunk_ptr) w;
	op_insert_operand(op, oper, offset);
	report(5,
"Inserted operand with offset %u into existing operator with id 0x%lx",
	       offset, id);
	chunk_free(oper);
	if (check_fire(op)) {
	    report(5, "Completed firing of dequeued operation with id 0x%lx", id);
	    keyvalue_remove(operator_table, id, NULL, NULL);
	    chunk_free(op);
	}
    } else {
	add_deferred_operand(id, oper, offset);
	report(5, "Deferred operand with offset %u for id 0x%lx", offset, id);
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

	buf_select(maxcfd+1, &cset, NULL, NULL, NULL);
	int fd;
	for (fd = 0; fd <= maxcfd; fd++) {
	    if (!FD_ISSET(fd, &cset))
		continue;
	    bool eof;
	    chunk_ptr msg = chunk_read(fd, &eof);
	    if (eof) {
		/* Unexpected EOF */
		if (fd == controller_fd) {
		    err(true, "Unexpected EOF from controller (fatal)");
		} else {
		    err(false,
			"Unexpected EOF from router with fd %d (ignored)", fd);
		}
		close(fd);
		continue;
	    }
	    if (msg == NULL) {
		err(false, "Could not read chunk from fd %d (ignored)", fd);
		continue;
	    }
	    word_t h = chunk_get_word(msg, 0);
	    /* Rely on fact that following message fields in same location
	       for both single and double-word headers */
	    unsigned code = msg_get_header_code(h);
	    unsigned agent = msg_get_header_agent(h); 
	    unsigned opcode = msg_get_header_opcode(h);
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
			    report(5,
				   "Sent statistics information to controller");
			} else {
			    err(false,
"Failed to send statistics information to controller");
			}
			chunk_free(msg);
		    }
		    break;
		case MSG_CLIOP_DATA:
		    report(5,
			   "Received client operation data.  Agent = %u", agent);
		    word_t *data = &msg->words[1];
		    unsigned nword = msg->length - 1;
		    if (gop_start_helper)
			gop_start_helper(agent, opcode, nword, data);
		    chunk_free(msg);
		    chunk_ptr rmsg = msg_new_cliop_ack(agent);
		    if (chunk_write(controller_fd, rmsg)) {
			report(5,
			       "Acknowledged client operation data.  Agent = %u",
			       agent);
		    } else {
			err(false,
"Failed to acknowledge client operation data.  Agent = %u",
			    agent);
		    }
		    chunk_free(rmsg);
		    break;
		case MSG_CLIOP_ACK:
		    report(5, "Received client operation ack.  Agent = %u", agent);
		    if (gop_finish_helper)
			gop_finish_helper(agent);
		    chunk_free(msg);
		    break;
		case MSG_GC_START:
		    chunk_free(msg);
		    gc_start(code);
		    break;
		case MSG_GC_FINISH:
		    chunk_free(msg);
		    gc_finish(code);
		    break;
		default:
		    chunk_free(msg);
		    err(false,
"Unknown message code %u from controller (ignored)", code);
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
		    err(false,
"Received message with unknown code %u (ignored)", code);
		}
	    }
	}
    }
    quit_agent(0, NULL);
}

/* Enable deferred GC operation */
void undefer() {
    /* Fire off any deferred garbage collection */
    if (gc_state == GC_DEFER)
	gc_start(MSG_GC_START);
}

/* Fire an operation and wait for returned operand */
chunk_ptr fire_and_wait_defer(chunk_ptr msg) {
    chunk_ptr rval = NULL;
    if (!send_op(msg)) {
	err(false, "Failed to send message");
	return NULL;
    }
    bool local_done = false;
    while (!(local_done || cmd_done())) {
	/* Select among controller port, and connections to routers.
	   Do not accept console input */
	FD_ZERO(&rset);
	maxrfd = 0;
	add_rfd(controller_fd);
	unsigned ridx;
	for (ridx = 0; ridx < nrouters; ridx++)
	    add_rfd(router_fd_array[ridx]);

	buf_select(maxrfd+1, &rset, NULL, NULL, NULL);
	int fd;
	for (fd = 0; fd <= maxrfd; fd++) {
	    if (!FD_ISSET(fd, &rset))
		continue;
	    bool eof;
	    chunk_ptr msg = chunk_read(fd, &eof);
	    if (eof) {
		/* Unexpected EOF */
		if (fd == controller_fd) {
		    err(true, "Unexpected EOF from controller (fatal)");
		} else {
		    err(false,
			"Unexpected EOF from router with fd %d (ignored)", fd);
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
		case MSG_GC_START:
		    /* Got notice that should initiate garbage collection.
		       Defer until current operation done */
		    report(3, "Deferring GC start");
		    chunk_free(msg);
		    gc_state = GC_DEFER;
		    break;
		case MSG_GC_FINISH:
		    err(false,
"Unexpected GC_FINISH message from controller when waiting for result (ignored)");
		    chunk_free(msg);
		    break;
		default:
		    chunk_free(msg);
		    err(false,
"Unknown message code %u from controller (ignored)", code);
		}
	    } else {
		dword_t dh;
		word_t id;
		/* Must be message from router */
		switch (code) {
		case MSG_OPERATION:
		    chunk_free(msg);
		    err(false, "Received unexpected operation.  Ignored.");
		    local_done = true;
		    break;
		case MSG_OPERAND:
		    dh = chunk_get_dword(msg, 0);
		    id = msg_get_dheader_op_id(dh);
		    report(5, "Received operand with id 0x%lx", id);
		    rval = msg;
		    local_done = true;
		    break;
		default:
		    chunk_free(msg);
		    err(false,
"Received message with unknown code %u (ignored)", code);
		    local_done = true;
		}
	    }
	}
    }
    return rval;
}

chunk_ptr fire_and_wait(chunk_ptr msg) {
    chunk_ptr result = fire_and_wait_defer(msg);
    undefer();
    return result;
}

void request_gc() {
    if (gc_state != GC_IDLE) {
	report(4, "GC request when not in GC_IDLE state");
	return;
    }
    unsigned gen = gc_generation+1;
    chunk_ptr msg = msg_new_gc_request(gen);
    if (chunk_write(controller_fd, msg))
	report(4, "Requested garbage collection with generation %u", gen);
    else
	err(false,
	    "Failed to request garbage collection with generation %u", gen);
    chunk_free(msg);
    gc_state = GC_REQUESTED;
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
	if (cmd_done())
	    break;
	int fd;
	for (fd = 0; fd <= maxcfd; fd++) {
	    if (!FD_ISSET(fd, &cset))
		continue;
	    bool eof;
	    chunk_ptr msg = chunk_read(fd, &eof);
	    if (eof) {
		/* Unexpected EOF */
		if (fd == controller_fd) {
		    err(true, "Unexpected EOF from controller (fatal)");
		} else {
		    err(false,
"Unexpected EOF from unexpected source. fd %d (ignored)", fd);
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
			/* Get a copy of the byte usage from mem. allocator */
			stat_helper(msg);
		    }
		    chunk_free(msg);
		    /* Client can proceed with next command */
		    unblock_console();
		    break;
		case MSG_KILL:
		    chunk_free(msg);
		    report(5, "Received kill message from controller");
		    finish_cmd();
		    break;
		case MSG_GC_START:
		    chunk_free(msg);
		    gc_start(code);
		    break;
		case MSG_GC_FINISH:
		    chunk_free(msg);
		    gc_finish(code);
		    break;
		default:
		    chunk_free(msg);
		    err(false,
			"Unknown message code %u from controller (ignored)",
			code);
		}
	    } else {
		err(false,
		    "Received message from unknown source.  fd %d (ignored)",
		    fd);
		close(fd);
		continue;
	    }
	}
    }
}

static void gc_start(unsigned code) {
    gc_state = GC_ACTIVE;
    report(3, "Starting GC");
    if (isclient) {
	/* Client */
	if (start_gc_handler)
	    start_gc_handler();
	chunk_ptr msg = msg_new_gc_finish();
	if (!chunk_write(controller_fd, msg))
	    err(false, "Failed to send GC Finish message to controller");
	chunk_free(msg);
    } else {
	/* Worker */
	if (start_gc_handler)
	    start_gc_handler();
	chunk_ptr msg = msg_new_gc_start();
	if (!chunk_write(controller_fd, msg))
	    err(false, "Failed to send GC Start message to controller");
	chunk_free(msg);
    }
}

static void gc_finish(unsigned code) {
    report(3, "Finishing GC");
    if (isclient) {
	if (finish_gc_handler)
	    finish_gc_handler();
    } else {
	if (finish_gc_handler)
	    finish_gc_handler();
	chunk_ptr msg = msg_new_gc_finish();
	chunk_write(controller_fd, msg);
	chunk_free(msg);
    }
    gc_state = GC_IDLE;
    gc_generation++;
    /* Allow command processing to continue */
    unblock_console();
}

