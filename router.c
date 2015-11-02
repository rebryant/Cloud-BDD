/* Implementation of router for dataflow computation system */

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

/**** Global data ****/

/* Listening socket */
static int listen_fd;

/* Connection to controller */
static int controller_fd;

/* Routing table consists of map from agent IDs to socket file descriptors */
static keyvalue_table_ptr routing_table = NULL;
/* Inverted table.  From file descriptors to agent IDs */
static keyvalue_table_ptr inverse_table = NULL;

/* Set of accepted connections for which do not yet know identity */
static set_ptr new_conn_set = NULL;

/* Linked list of messages to be routed */
typedef struct QELE queue_ele, *queue_ptr;

struct QELE {
    chunk_ptr msg;
    int fd;
    unsigned agent;
    queue_ptr next;
};

/* Oldest item in queue */
static queue_ptr outq_head = NULL;
/* Youngest item in queue */
static queue_ptr outq_tail = NULL;

/* switch for deactivating buffering */
static int bufferingEnabled = 0;


static void init_router(char *controller_name, unsigned controller_port) {
    unsigned myport = 0;
    if (!new_server(0, &listen_fd, &myport))
	err(true, "Cannot set up server");
    report(3, "Listening socket has descriptor %d", listen_fd);
    controller_fd = open_clientfd(controller_name, controller_port);
    if (controller_fd < 0)
	err(true, "Cannot create connection to controller at %s:%u",
	    controller_name, controller_port);
    else
	report(3, "Connection to controller has descriptor %d", controller_fd);
    chunk_ptr msg = msg_new_register_router(myport);
    bool sok = chunk_write(controller_fd, msg);
    report(3, "Sent router registration to controller");
    chunk_free(msg);
    if (!sok)
	err(true, "Could not send registration message to controller");
    routing_table = word_keyvalue_new();
    inverse_table = word_keyvalue_new();
    new_conn_set = word_set_new();
    outq_head = NULL;
    outq_tail = NULL;

}

static void quit_router() {
    /* Close file connections */
    chunk_deinit();
    int fd;
    word_t w;
    set_iterstart(new_conn_set);
    while (set_iternext(new_conn_set, &w)) {
	fd = w;
	close(fd);
    }
    keyvalue_iterstart(routing_table);
    while (keyvalue_iternext(routing_table, NULL, &w)) {
	fd = w;
	close(fd);
    }
    /* Deallocate data structures */
    keyvalue_free(routing_table);
    keyvalue_free(inverse_table);
    set_free(new_conn_set);
    /* Delete any queued messages */
    queue_ptr ls = outq_head;
    while (ls) {
	queue_ptr ele = ls;
	ls = ls->next;
	chunk_free(ele->msg);
	free_block(ele, sizeof(queue_ele));
    }


}

#if 0
/* For debugging */
static size_t outq_len() {
    size_t n = 0;
    queue_ptr ls = outq_head;
    while (ls) {
	n++;
	ls = ls->next;
    }
    return n;
}
#endif

static void insert_queue(chunk_ptr msg) {
    dword_t dh = chunk_get_dword(msg, 0);
    unsigned agent = msg_get_dheader_agent(dh);
    word_t id = msg_get_dheader_op_id(dh);
    int fd;
    word_t w;
    if (keyvalue_find(routing_table, agent, &w)) {
	fd = w;
    } else {
	err(false, "Message with invalid destination agent %u (ignored)", agent);
	/* Delete message */
	chunk_free(msg);
	return;
    }
    queue_ptr ele = malloc_or_fail(sizeof(queue_ele), "insert_queue");
    ele->msg = msg;
    ele->fd = fd;
    ele->agent = agent;
    ele->next = NULL;
    if (outq_tail) {
	outq_tail->next = ele;
	outq_tail = ele;
    } else {
	outq_head = outq_tail = ele;
    }
    report(2, "Queued message with id 0x%lx for agent %u.", id, agent);
}

static fd_set inset;
static fd_set outset;
static int maxfd = 0;

static void add_infd(int fd) {
    report(6, "Adding fd %d to input set", fd);
    FD_SET(fd, &inset);
    if (fd > maxfd)
	maxfd = fd;
}

static void add_outfd(int fd) {
    report(6, "Adding fd %d to output set", fd);
    FD_SET(fd, &outset);
    if (fd > maxfd)
	maxfd = fd;
}


/* Here's the main loop for the router */
static void run_router() {
    word_t w;
    int fd;


    while (true) {
	/* Inputs: Select among listening port, controller port, new connections,
	   and connections to workers & clients */
	FD_ZERO(&inset);
	add_infd(listen_fd);
	add_infd(controller_fd);
	keyvalue_iterstart(routing_table);
	while (keyvalue_iternext(routing_table, NULL, &w)) {
	    fd = w;
	    add_infd(fd);
	}
	set_iterstart(new_conn_set);
	while (set_iternext(new_conn_set, &w)) {
	    fd = w;
	    add_infd(fd);
	}
	/* Output set: Select among elements in output queue */
	FD_ZERO(&outset);
	size_t out_lim = routing_table->nelements;
	/* Bound number of elements when queue gets large */
	if (out_lim > 25)
	    out_lim = 25;
	size_t ecnt;
	queue_ptr ls = outq_head;
	for (ecnt = 0; ls && ecnt < out_lim; ecnt++) {
	    add_outfd(ls->fd);
	    ls = ls->next;
	}
            buf_select(maxfd+1, &inset, &outset, NULL, NULL);


	/* Go through inputs */
	for (fd = 0; fd <= maxfd; fd++) {
            if ((!(FD_ISSET(fd, &inset)))  ) {
                continue;
            }

	    if (fd == listen_fd) {
		int connfd = accept_connection(fd, NULL);
		set_insert(new_conn_set, (word_t) connfd);
		report(4, "New connection with fd %d", connfd);
		continue;
	    }
	    bool eof;
	    chunk_ptr msg;

            msg = chunk_read(fd, &eof);

	    if (eof) {
		/* Unexpected EOF */
		if (fd == controller_fd) {
		    err(false, "Unexpected EOF from controller");
		} else if (set_member(new_conn_set, (word_t) fd, true)) {
		    err(false, "Unexpected EOF from new connection, fd %d", fd);
		} else {
		    word_t wa;
		    if (keyvalue_remove(inverse_table, (word_t) fd, NULL, &wa)) {
			unsigned agent = wa;
			keyvalue_remove(routing_table, (word_t) agent, NULL, NULL);
			report(3, "Disconnecting agent %u (fd %d)", agent, fd);
		    } else {
			err(false, "EOF from unknown source, fd %d", fd);
		    }
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
	    unsigned agent = msg_get_header_agent(h);
            if (fd == controller_fd) {
		/* Message from controller */
		switch(code) {
		case MSG_KILL:
		    chunk_free(msg);
		    report(1, "Received kill message from controller");
		    return;
		default:
		    chunk_free(msg);
		    err(false,
			"Unknown message code %u from controller (ignored)", code);
		}
	    } else if (set_member(new_conn_set, (word_t) fd, true)) {
		/* New connection request */
		switch(code) {
		case MSG_REGISTER_AGENT:
		    chunk_free(msg);
		    keyvalue_insert(routing_table, (word_t) agent, (word_t) fd);
		    keyvalue_insert(inverse_table, (word_t) fd, (word_t) agent);
		    report(3, "Created routing table entry for agent %u, fd %d",
			   agent, fd);
		    break;
		default:
		    chunk_free(msg);
		    err(false,
"Unknown message code %u from newly connected agent %u (ignored)", code, agent);
		}
	    } else {
		/* Should be a routing request */
		switch (code) {
		case MSG_OPERATION:
		case MSG_OPERAND:
		    insert_queue(msg);
		    break;
		default:
		    chunk_free(msg);
		    err(false, "Unknown message code %u from agent %u",
			code, agent);
		}
	    }
	}

	/* Output messages */
	queue_ptr prev = NULL;
	ls = outq_head;
	for (ecnt = 0; ls && ecnt < out_lim; ecnt++) {
	    fd = ls->fd;
	    if (FD_ISSET(fd, &outset)) {
		/* Limit to one output message per connection */
		FD_CLR(fd, &outset);
		chunk_ptr outmsg = ls->msg;
		unsigned agent = ls->agent;
		queue_ptr ele = ls;
		/* Need to splice element out of queue */
		if (outq_head == ls)
		    outq_head = ls->next;
		else
		    prev->next = ls->next;
		if (outq_tail == ls)
		    outq_tail = prev;
		ls = ls->next;
		/* Send the message */
		if (chunk_write(fd, outmsg)) {
		    dword_t dh = chunk_get_dword(outmsg, 0);
		    word_t id = msg_get_dheader_op_id(dh);
		    report(2, "Routed message with id 0x%lx to agent %u",
			   id, agent);
		}
		else
		    err(false, "Couldn't send message to agent %u (ignored)",
			agent);
		free_block(ele, sizeof(queue_ele));
		chunk_free(outmsg);
	    } else {
		/* Skip this element */
		prev = ls;
		ls = ls->next;
	    }
	}
    }
}

static void usage(char *cmd) {
    printf("Usage: %s [-h] [-v VLEVEL] [-H HOST] [-P PORT]\n", cmd);
    printf("\t-h         Print this information\n");
    printf("\t-v VLEVEL  Set verbosity level\n");
    printf("\t-H HOST    Use HOST as controller host\n");
    printf("\t-P PORT    Use PORT as controller port\n");
    printf("\t-b BUF_ON  Set 1 or 0 to turn buffering on or off");
    printf(", respectively (default 1)\n");
    exit(0);
}

#define BUFSIZE 256

int main(int argc, char *argv[]) {
    char buf[256] = "localhost";
    unsigned port = CPORT;
    int c;
    int level = 1;
    while ((c = getopt(argc, argv, "hb:v:H:P:B:")) != -1) {
	switch (c) {
	case 'h':
	    usage(argv[0]);
	    break;
	case 'H':
	    strncpy(buf, optarg, BUFSIZE-1);
	    buf[BUFSIZE-1] = '\0';
	    break;
	case 'v':
	    level = atoi(optarg);
	    break;
	case 'P':
	    port = atoi(optarg);
	    break;
        case 'b':
            bufferingEnabled = (((atoi(optarg) & 1) == atoi(optarg)) ?
				atoi(optarg) : 1);
            break;
	default:
	    printf("Unknown option '%c'\n", c);
	    usage(argv[0]);
	    break;
	}
    }
    set_verblevel(level);
    init_router(buf, port);
    run_router();
    quit_router();
    mem_status(stdout);
    return 0;
}
