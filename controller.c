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

/**** Global data ****/

/* Listening socket */
static int listen_fd;

/* Id to assign to next agent */
static int next_agent = 0;

/* How many routers are still required */
static int need_routers = 100000;

/* How many workers are expected */
static int worker_cnt = 0;

static int need_workers = 100000;

/* How many bits are available for sequence numbers */
static unsigned snb = 16;

/* How many clients are allowed */
static unsigned maxclients = 5;

/* Set of connections from which have not received any messages.
   Given as map from file descriptor to IP address (Since some of these will be routers)
*/
static keyvalue_table_ptr new_conn_map = NULL;

/* Set of router addresses, indicated by node ids */
static set_ptr router_addr_set = NULL;

/* Set of router file descriptors */
static set_ptr router_fd_set = NULL;

/* Set of worker file descriptors */
static set_ptr worker_fd_set = NULL;

/* Set of client file descriptors */
static set_ptr client_fd_set = NULL;

/* Collect statistics from workers as a set of messages */
static int stat_message_cnt = 0;
static chunk_ptr *stat_messages = NULL;
/* File descriptor of client that requested flush */
static int flush_requestor_fd = -1;

/* Information about outstanding global operation */
typedef struct GELE global_op_ele, *global_op_ptr;

struct GELE {
    unsigned id;         /* Identity */
    int worker_ack_cnt;  /* How many workers have acknowledged initial request */
    int client_fd;
    global_op_ptr next;
};

static global_op_ptr global_ops = NULL;

static void add_global_op(unsigned id, int fd) {
    global_op_ptr ele = malloc_or_fail(sizeof(global_op_ele), "add_global_op");
    ele->id = id;
    ele->client_fd = fd;
    ele->worker_ack_cnt = 0;
    ele->next = global_ops;
    global_ops = ele;
}

static global_op_ptr find_global_op(unsigned id) {
    global_op_ptr ele = global_ops;
    while (ele && ele->id != id)
	ele = ele->next;
    return ele;
}

static void free_global_ops() {
    global_op_ptr ls = global_ops;
    while (ls) {
	global_op_ptr ele = ls;
	ls = ls->next;
	free_block(ele, sizeof(global_op_ele));
    }
    global_ops = NULL;
}

/* Called receive ACK for global operation from worker */
/* Return client fd if all acks received (-1 otherwise).  Remove list entry when that's the case */
static int receive_global_op_worker_ack(unsigned id) {
    global_op_ptr prev = NULL;
    global_op_ptr ele = global_ops;
    while (ele && ele->id != id) {
	prev = ele;
	ele = ele->next;
    }
    if (ele) {
	ele->worker_ack_cnt++;
	if (ele->worker_ack_cnt >= worker_cnt) {
	    /* Remove entry */
	    if (prev)
		prev->next = ele->next;
	    else
		global_ops = ele->next;
	    free_block(ele, sizeof(global_op_ele));
	    return ele->client_fd;
	}
    } else {
	err(false, "Failed to find entry for global operation with id %u", id);
    }
    return -1;
}


/* Forward declarations */
bool quit_controller(int argc, char *argv[]);
bool do_controller_flush_cmd(int argc, char *argv[]);

bool do_controller_status_cmd(int argc, char *argv[]) {
    report(0, "Connections: %u routers, %u workers, %u clients",
	   router_fd_set->nelements, worker_fd_set->nelements, client_fd_set->nelements);
    report(0, "%d/%u worker stat messages received", stat_message_cnt, worker_fd_set->nelements);
    return true;
}

/* Find power of two bigger than given number */
unsigned biglog2(unsigned v) {
    unsigned n = 0;
    while (((word_t) 1 << n) < v)
	n++;
    return n;
}


static void init_controller(int port, int nrouters, int nworkers, int mc) {
    if (!new_server(port, &listen_fd, NULL))
	err(true, "Cannot set up server on port %u", port);
    report(2, "Listening socket has descriptor %d", listen_fd);
    router_addr_set = word_set_new();
    new_conn_map = word_keyvalue_new();
    router_fd_set = word_set_new();
    worker_fd_set = word_set_new();
    client_fd_set = word_set_new();
    init_cmd();
    add_cmd("status", do_controller_status_cmd,       "              | Determine status of connected nodes");
    add_cmd("flush", do_controller_flush_cmd,         "              | Flush state of all agents");
    add_quit_helper(quit_controller);
    need_routers = nrouters;
    need_workers = nworkers;
    snb = 32-biglog2(nworkers+maxclients);
    worker_cnt = nworkers;
    maxclients = mc;
    stat_message_cnt = 0;
    flush_requestor_fd = -1;
    stat_messages = calloc_or_fail(worker_cnt, sizeof(chunk_ptr), "init_controller");
    //    global_ops = NULL:
}
    
bool do_controller_flush_cmd(int argc, char *argv[]) {
    chunk_ptr msg = msg_new_flush();
    word_t w;
    int fd;
    bool ok = true;
    set_iterstart(worker_fd_set);
    while (set_iternext(worker_fd_set, &w)) {
	fd = w;
	if (!chunk_write(fd, msg)) {
	    err(false, "Failed to send flush message to worker with descriptor %d", fd);
	    ok = false;
	}
    }
    set_iterstart(client_fd_set);
    while (set_iternext(client_fd_set, &w)) {
	fd = w;
	if (!chunk_write(fd, msg)) {
	    err(false, "Failed to send flush message to client with descriptor %d", fd);
	    ok = false;
	}
    }
    chunk_free(msg);
    free_global_ops();
    return ok;
}

bool quit_controller(int argc, char *argv[]) {
    /* Send kill messages to other nodes and close file connections */
    chunk_ptr msg = msg_new_kill();
    word_t w;
    int fd;
    keyvalue_iterstart(new_conn_map);
    while (keyvalue_iternext(new_conn_map, &w, NULL)) {
	fd = w;
	close(fd);
    }
    set_iterstart(router_fd_set);
    while (set_iternext(router_fd_set, &w)) {
	fd = w;
	if (!chunk_write(fd, msg))
	    err(false, "Failed to send kill message to router with descriptor %d", fd);
	close(fd);
    }
    set_iterstart(worker_fd_set);
    while (set_iternext(worker_fd_set, &w)) {
	fd = w;
	if (!chunk_write(fd, msg))
	    err(false, "Failed to send kill message to worker with descriptor %d", fd);
	close(fd);
    }
    set_iterstart(client_fd_set);
    while (set_iternext(client_fd_set, &w)) {
	fd = w;
	if (!chunk_write(fd, msg))
	    err(false, "Failed to send kill message to client with descriptor %d", fd);
	close(fd);
    }
    /* Deallocate */
    chunk_free(msg);
    set_free(router_addr_set);
    keyvalue_free(new_conn_map);
    set_free(router_fd_set);
    set_free(worker_fd_set);
    set_free(client_fd_set);
    while (stat_message_cnt > 0) {
	chunk_free(stat_messages[--stat_message_cnt]);
    }
    free_array(stat_messages, sizeof(chunk_ptr), worker_cnt);
    free_global_ops();
    return true;
}

static fd_set set;
static int maxfd = 0;

static void add_fd(int fd) {
    FD_SET(fd, &set);
    if (fd > maxfd)
	maxfd = fd;
}

#define MAX_IDS (CHUNK_MAX_LENGTH-1)

/* Add new agent.  Send agent ID + number of workers + snb + router map */
static void add_agent(int fd, bool isclient) {
    unsigned agent = next_agent++;
    if (agent >= worker_cnt + maxclients) {
	/* Exceeded client limit */
	chunk_ptr msg = msg_new_nack();
	if (chunk_write(fd, msg))
	    report(3, "Sent nack to potential client due to client limit being exceeded.  Fd = %d", fd);
	else
	    report(3, "Couldn't send nack to potential client.  Fd = %d", fd);
	chunk_free(msg);
	return;
    }

    /* Need to break into sequence of messages according to maximum chunk length */
    chunk_ptr msg = NULL;
    size_t bcount = 0;
    size_t ncount = router_addr_set->nelements;
    set_iterstart(router_addr_set);
    word_t id;
    bool ok = true;
    while (ok && set_iternext(router_addr_set, &id)) {
	if (bcount == 0) {
	    /* Start new block */
	    size_t blen = ncount;
	    if (blen > MAX_IDS)
		blen = MAX_IDS;
	    msg = chunk_new(blen+1);
	}
	word_t wd = id << 16;
	chunk_insert_word(msg, wd, bcount+1);
	bcount++;
	if (bcount == MAX_IDS) {
	    /* This block is filled */
	    size_t h1 = ((word_t) agent << 48) | ((word_t) ncount << 32) | ((word_t) worker_cnt << 16) | (snb << 8) | MSG_ACK_AGENT;
	    chunk_insert_word(msg, h1, 0);
	    ok = chunk_write(fd, msg);
	    chunk_free(msg);
	    ncount -= bcount;
	    bcount = 0;
	}
    }
    if (ok && ncount > 0) {
	size_t h1 = ((word_t) agent << 48) | ((word_t) ncount << 32) | ((word_t) worker_cnt << 16) | (snb << 8) | MSG_ACK_AGENT;
	chunk_insert_word(msg, h1, 0);
	ok = chunk_write(fd, msg);
	chunk_free(msg);
	ncount -= bcount;
    }
    report(3, "Added agent %u with descriptor %d", agent, fd);
}

/* Accumulate worker messages with statistics */
static void add_stat_message(chunk_ptr msg) {
    size_t *stat_summary = NULL;
    stat_messages[stat_message_cnt++] = msg;
    /* See if we've accumulated a full set */
    if (stat_message_cnt >= worker_cnt) {
	size_t nstat = stat_messages[0]->length - 1;
	if (flush_requestor_fd >= 0)
	    stat_summary = calloc_or_fail(nstat * 3, sizeof(size_t), "add_stat_message");
	/* Accumulate and print */
	report(1, "Worker statistics:");
	size_t i, w;
	for (i = 0; i < nstat; i++) {
	    chunk_ptr msg = stat_messages[0];
	    word_t minval, maxval, sumval;
	    minval = maxval = sumval = chunk_get_word(msg, i+1);
	    for (w = 1; w < worker_cnt; w++) {
		chunk_ptr msg = stat_messages[w];		
		word_t val = chunk_get_word(msg, i+1);
		if (val < minval)
		    minval = val;
		if (val > maxval)
		    maxval = val;
		sumval += val;
	    }
	    if (stat_summary) {
		stat_summary[3*i + 0] = minval;
		stat_summary[3*i + 1] = maxval;
		stat_summary[3*i + 2] = sumval;
	    }
	    report(1, "Parameter %d\tMin: %" PRIu64 "\tMax: %" PRIu64 "\tAvg: %.2f\tSum: %" PRIu64,
		   (int) i, minval, maxval, (double) sumval/worker_cnt, sumval);
	}
	if (flush_requestor_fd >= 0) {
	    chunk_ptr msg = msg_new_stat(worker_cnt, nstat*3, stat_summary);
	    if (chunk_write(flush_requestor_fd, msg)) {
		report(5, "Sent statistical summary to client at fd %d", flush_requestor_fd);
	    } else {
		err(false, "Failed to send statistical summary to client at fd %d", flush_requestor_fd);
	    }
	    chunk_free(msg);
	    free_array(stat_summary, nstat*3, sizeof(size_t));
	}
	for (w = 0; w < worker_cnt; w++) {
	    chunk_free(stat_messages[w]);
	    stat_messages[w] = NULL;
	}
	stat_message_cnt = 0;
	flush_requestor_fd = -1;
    }
}

static void run_controller(char *infile_name) {
    if (!start_cmd(infile_name))
	return;
    while (!cmd_done()) {
	FD_ZERO(&set);
	int fd;
	word_t w;
	unsigned ip;
	unsigned port;
	add_fd(listen_fd);
	keyvalue_iterstart(new_conn_map);
	/* Check for messages from newly connected clients, workers, and routers */
	while (keyvalue_iternext(new_conn_map, &w, NULL)) {
	    fd = w;
	    add_fd(fd);
	}
	if (need_routers == 0) {
	    /* Accept messages from workers */
	    set_iterstart(worker_fd_set);
	    while (set_iternext(worker_fd_set, &w)) {
		fd = w;
		add_fd(fd);
	    }
	    /* Accept messages from clients */
	    set_iterstart(client_fd_set);
	    while (set_iternext(client_fd_set, &w)) {
		fd = w;
		add_fd(fd);
	    }
	}

	cmd_select(maxfd+1, &set, NULL, NULL, NULL);

	for (fd = 0; fd <= maxfd; fd++) {
	    if (!FD_ISSET(fd, &set))
		continue;
	    if (fd == listen_fd) {
		unsigned ip;
		int connfd = accept_connection(fd, &ip);
		keyvalue_insert(new_conn_map, (word_t) connfd, (word_t) ip);
		report(4, "Accepted new connection.  Connfd = %d, IP = 0x%x", connfd, ip);
		continue;
	    }
	    bool eof;
	    chunk_ptr msg = chunk_read(fd, &eof);
	    if (eof) {
		/* Unexpected EOF */
		if (keyvalue_remove(new_conn_map, (word_t) fd, NULL, NULL)) {
		    err(false, "Unexpected EOF from new connection, fd %d", fd);
		} else if (set_member(worker_fd_set, (word_t) fd, true)) {
		    err(false, "Unexpected EOF from connected worker, fd %d", fd);
		} else if (set_member(client_fd_set, (word_t) fd, true)) {
		    report(3, "Disconnection from client (fd %d)", fd);
		} else {
		    err(false, "Unexpected EOF from unknown source, fd %d", fd);
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
	    report(5, "Received message with code %d from fd %d", code, fd);
	    if (keyvalue_remove(new_conn_map, (word_t) fd, NULL, &w)) {
		ip = w;
		chunk_free(msg);
		/* Should be a registration message */
		switch (code) {
		case MSG_REGISTER_ROUTER:
		    if (need_routers == 0) {
			err(false, "Unexpected router registration.  (Ignored)");
			close(fd);
			break;
		    }
		    port = msg_get_header_port(h);
		    word_t node_id = msg_build_node_id(port, ip);
		    set_insert(router_addr_set, node_id);
		    set_insert(router_fd_set, (word_t) fd);
		    report(4, "Added router with fd %d.  IP 0x%x.  Port %u", fd, ip, port);
		    need_routers --;
		    if (need_routers == 0) {
			report(3, "All routers connected");
			/* Have gotten all of the necessary routers.  Notify any registered workers */
			set_iterstart(worker_fd_set);
			int wfd;
			while (set_iternext(worker_fd_set, &w)) {
			    wfd = w;
			    add_agent(wfd, false);
			}
		    }
		    break;
		case MSG_REGISTER_WORKER:
		    if (worker_fd_set->nelements >= worker_cnt) {
			err(false, "Unexpected worker registration.  (Ignored)");
			close(fd);
			break;
		    }
		    set_insert(worker_fd_set, (word_t) fd);
		    report(4, "Added worker with fd %d", fd);
		    if (need_routers == 0)
			add_agent(fd, false);
		    break;
		case MSG_REGISTER_CLIENT:
		    set_insert(client_fd_set, (word_t) fd);
		    report(4, "Added router with fd %d", fd);
		    if (need_workers == 0)
			add_agent(fd, true);
		    break;
		default:
		    err(false, "Unexpected message code %u from new connection", code);
		    break;
		}
	    } else if (set_member(worker_fd_set, (word_t) fd, false)) {
		/* Message from worker */
		switch (code) {
		    unsigned agent;
		case MSG_READY_WORKER:
		    chunk_free(msg);
		    if (need_workers == 0) {
			err(false, "Unexpected worker ready.  (Ignored)");
			close(fd);
			break;
		    }
		    need_workers--;
		    if (need_workers == 0) {
			report(3, "All workers connected");
			/* Notify any pending clients */
			set_iterstart(client_fd_set);
			int cfd;
			while (set_iternext(client_fd_set, &w)) {
			    cfd = w;
			    add_agent(cfd, true);
			}
		    }
		    break;
		case MSG_STAT:
		    /* Message gets stashed away.  Don't free it */
		    add_stat_message(msg);
		    break;
		case MSG_CLIOP_ACK:
		    /* Worker acknowledging receipt of global operation information */
		    agent = msg_get_header_agent(h);
		    int client_fd = receive_global_op_worker_ack(agent);
		    if (client_fd >= 0) {
			/* Have received complete set of acknowledgements. */
			/* Send ack to client */
			if (chunk_write(client_fd, msg)) {
			    report(6, "Sent ack to client for global operation with id %u", agent);
			} else {
			    err(false, "Failed to send ack to client for global operation with id %u.  Fd %d", agent, client_fd);
			}
		    }
		    chunk_free(msg);
		    break;
		default:
		    chunk_free(msg);
		    err(false, "Unexpected message code %u from worker", code);
		}
	    } else if (set_member(client_fd_set, (word_t) fd, false)) {
		/* Message from client */
		switch(code){
		    unsigned agent;
		    word_t w;
		case MSG_KILL:
		    /* Shutdown entire system */
		    chunk_free(msg);
		    report(2, "Remote request to kill system");
		    finish_cmd();
		    return;
		case MSG_DO_FLUSH:
		    /* Iniitiate a flush operation */
		    chunk_free(msg);
		    flush_requestor_fd = fd;
		    do_controller_flush_cmd(0, NULL);
		    break;
		case MSG_CLIOP_DATA:
		    /* Request for global operation from client */
		    agent = msg_get_header_agent(h);
		    add_global_op(agent, fd);
		    /* Send message to all workers */
		    set_iterstart(worker_fd_set);
		    while (set_iternext(worker_fd_set, &w)) {
			int worker_fd = (int) w;
			if (chunk_write(worker_fd, msg)) {
			    report(6, "Sent global operation information with id %u to worker with fd %d",
				   agent, worker_fd);
			} else {
			    err(false, "Failed to send global operation information with id %u to worker with fd %d",
				agent, worker_fd);
			}
		    }
		    chunk_free(msg);
		    break;
		case MSG_CLIOP_ACK:
		    /* Completion of global operation by client */
		    agent = msg_get_header_agent(h);
		    /* Send message to all workers */
		    set_iterstart(worker_fd_set);
		    while (set_iternext(worker_fd_set, &w)) {
			int worker_fd = (int) w;
			if (chunk_write(worker_fd, msg)) {
			    report(6, "Sent global operation acknowledgement with id %u to worker with fd %d",
				   agent, worker_fd);
			} else {
			    err(false, "Failed to send global operation acknowledgement with id %u to worker with fd %d",
				agent, worker_fd);
			}
		    }
		    chunk_free(msg);
		    break;
		default:
		    err(false, "Unexpected message code %u from client", code);
		}
		    
	    } else {
		chunk_free(msg);
		err(false, "Unexpected message on fd %d (Ignored)", fd);
	    }
	}
    }
}


static void usage(char *cmd) {
    printf("Usage: %s [-h] [-v VLEVEL] [-p port] [-r RCNT] [-w WCNT]\n", cmd);
    printf("\t-h         Print this information\n");
    printf("\t-v VLEVEL  Set verbosity level\n");
    printf("\t-p PORT    Use PORT as controller port\n");
    printf("\t-r RCNT    Specify number of routers\n");
    printf("\t-w WCNT    Specify number of workers\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    unsigned port = CPORT;
    /* Actual number of workers & routers */
    int nworkers = 1;
    int nrouters = 1;
    /* Max number of clients */
    int maxclients = 5;
    int c;
    int level = 1;
    while ((c = getopt(argc, argv, "hv:p:r:w:c:")) != -1) {
	switch (c) {
	case 'h':
	    usage(argv[0]);
	    break;
	case 'v':
	    level = atoi(optarg);
	    break;
	case 'p':
	    port = atoi(optarg);
	    break;
	case 'r':
	    nrouters = atoi(optarg);
	    break;
	case 'w':
	    nworkers = atoi(optarg);
	    break;
	case 'c':
	    maxclients = atoi(optarg);
	    break;
	default:
	    printf("Unknown option '%c'\n", c);
	    usage(argv[0]);
	    break;
	}
    }
    set_verblevel(level);
    init_controller(port, nrouters, nworkers, maxclients);
    run_controller(NULL);
    mem_status(stdout);
    return 0;
}
