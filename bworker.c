/* Boolean Worker */
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>

#include "dtype.h"
#include "table.h"
#include "chunk.h"
#include "report.h"
#include "msg.h"
#include "console.h"
#include "agent.h"
#include "bdd.h"


static void init(char *controller_name, unsigned controller_port, bool try_local_router) {
    init_agent(false, controller_name, controller_port, try_local_router);
    init_dref_mgr();
    set_agent_flush_helper(flush_dref_mgr);
    set_agent_global_helpers(uop_start, uop_finish);
    set_gc_handlers(worker_gc_start, worker_gc_finish);
    add_op_handler(OP_VAR, do_var_op);
    add_op_handler(OP_CANONIZE, do_canonize_op);
    add_op_handler(OP_CANONIZE_LOOKUP, do_canonize_lookup_op);
    add_op_handler(OP_RETRIEVE_LOOKUP, do_retrieve_lookup_op);
    add_op_handler(OP_ITE_LOOKUP, do_ite_lookup_op);
    add_op_handler(OP_ITE_RECURSE, do_ite_recurse_op);
    add_op_handler(OP_ITE_STORE, do_ite_store_op);
    add_op_handler(OP_UOP_DOWN, do_uop_down_op);
    add_op_handler(OP_UOP_UP, do_uop_up_op);
    add_op_handler(OP_UOP_STORE, do_uop_store_op);
}

static void finish() {
    free_dref_mgr();
}

static void usage(char *cmd) {
    printf("Usage: %s [-h] [-v VLEVEL] [-H HOST] [-P PORT][-r]\n", cmd);
    printf("\t-h         Print this information\n");
    printf("\t-v VLEVEL  Set verbosity level\n");
    printf("\t-H HOST    Use HOST as controller host\n");
    printf("\t-P PORT    Use PORT as controller port\n");
    printf("\t-r         Try to use local router\n");
    exit(0);
}

#define BUFSIZE 256

int main(int argc, char *argv[]) {
    char buf[BUFSIZE] = "localhost";
    unsigned port = CPORT;
    int c;
    int level = 1;
    bool try_local_router = false;

    while ((c = getopt(argc, argv, "hv:H:P:r")) != -1) {
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
	case 'r':
	    try_local_router = true;
	    break;
	default:
	    printf("Unknown option '%c'\n", c);
	    usage(argv[0]);
	    break;
	}
    }
    set_verblevel(level);
    init(buf, port, try_local_router);
    run_worker();
    finish();
    mem_status(stdout);
    return 0;
}
