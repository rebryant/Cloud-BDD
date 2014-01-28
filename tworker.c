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

static void init(char *controller_name, unsigned controller_port) {
    init_agent(false, controller_name, controller_port);
    set_agent_flush_helper(flush_worker);
    add_op_handler(OP_IFORK, do_ifork_op);
    add_op_handler(OP_INCR, do_incr_op);
    add_op_handler(OP_JOIN, do_join_op);
}

static void usage(char *cmd) {
    printf("Usage: %s [-h] [-v VLEVEL] [-H HOST] [-P PORT]\n", cmd);
    printf("\t-h         Print this information\n");
    printf("\t-v VLEVEL  Set verbosity level\n");
    printf("\t-H HOST    Use HOST as controller host\n");
    printf("\t-P PORT    Use PORT as controller port\n");
    exit(0);
}

#define BUFSIZE 256

int main(int argc, char *argv[]) {
    char buf[256] = "localhost";
    unsigned port = CPORT;
    int c;
    int level = 1;
    while ((c = getopt(argc, argv, "hv:H:P:")) != -1) {
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
	default:
	    printf("Unknown option '%c'\n", c);
	    usage(argv[0]);
	    break;
	}
    }
    set_verblevel(level);
    init(buf, port);
    run_worker();
    mem_status(stdout);
    return 0;
}
