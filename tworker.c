/* Test worker */
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
#include "test_df.h"

/* Forward declarations */
void gc_start();
void gc_finish();


static void init(char *controller_name, unsigned controller_port,
		 bool try_local_router) {
    init_agent(false, controller_name, controller_port, true, try_local_router);
    set_agent_flush_helper(flush_worker);
    add_op_handler(OP_IFORK, do_ifork_op);
    add_op_handler(OP_INCR, do_incr_op);
    add_op_handler(OP_JOIN, do_join_op);
    set_agent_global_helpers(start_global, finish_global);
    set_gc_handlers(gc_start, gc_finish);
}

static void usage(char *cmd) {
    printf("Usage: %s [-h] [-v VLEVEL] [-H HOST] [-P PORT] [-r]\n", cmd);
    printf("\t-h         Print this information\n");
    printf("\t-v VLEVEL  Set verbosity level\n");
    printf("\t-H HOST    Use HOST as controller host\n");
    printf("\t-P PORT    Use PORT as controller port\n");
    printf("\t-r         Try to use local router\n");
    exit(0);
}

#define BUFSIZE 256

int main(int argc, char *argv[]) {
    char buf[256] = "localhost";
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
    mem_status(stdout);
    return 0;
}

void gc_start() {
    report(1, "Starting Worker GC");
    sleep(3);
}

void gc_finish() {
    sleep(1);
    report(3, "Finishing Worker GC");
}
