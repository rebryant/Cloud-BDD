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
bool do_incr_cmd(int argc, char *argv[]);
bool do_fork_cmd(int argc, char *argv[]);
bool do_join_cmd(int argc, char *argv[]);
bool do_status_cmd(int argc, char *argv[]);
bool do_global_cmd(int argc, char *argv[]);
void gc_start();
void gc_finish();

static void init(char *controller_name, unsigned controller_port,
		 bool try_local_router) {
    init_cmd();
    init_agent(true, controller_name, controller_port, true, try_local_router);
    set_agent_stat_helper(do_summary_stat);
    set_gc_handlers(gc_start, gc_finish);
    add_cmd("incr", do_incr_cmd,
	    " val cnt      | Increment val cnt times");
    add_cmd("fork", do_fork_cmd,
	    " wdth val cnt | Perform width incrs and join results");
    add_cmd("join", do_join_cmd,
	    " v1 v2        | Compute v1+v2");
    add_cmd("status", do_status_cmd,
	    "              | Print statistics");
    add_cmd("global", do_global_cmd,
	    "              | Perform test of global command capability");
}

static void usage(char *cmd) {
    printf("Usage: %s [-h] [-v VLEVEL] [-H HOST] [-P PORT] [-f FILE][-r]\n", cmd);
    printf("\t-h         Print this information\n");
    printf("\t-v VLEVEL  Set verbosity level\n");
    printf("\t-H HOST    Use HOST as controller host\n");
    printf("\t-P PORT    Use PORT as controller port\n");
    printf("\t-F FILE    Read commands from FILE\n");
    printf("\t-r         Try to use local router\n");
    exit(0);
}


bool do_incr_cmd(int argc, char *argv[]) {
    if (argc != 3) {
	err(false, "Need 2 arguments: val cnt");
	return false;
    }
    int val, cnt;
    if (!get_int(argv[1], &val) || !get_int(argv[2], &cnt)) {
	err(false, "Arguments must be integers");
	return false;
    }
    dword_t d = msg_build_destination(own_agent, new_operator_id(), 0);
    chunk_ptr msg = build_incr(d, (word_t) val, (word_t) cnt);
    chunk_ptr rmsg = fire_and_wait(msg);
    chunk_free(msg);
    if (!rmsg) {
	err(false, "Incr command failed");
	return false;
    }
    word_t rval = chunk_get_word(rmsg, 0 + OPER_HEADER_CNT);
    chunk_free(rmsg);
    word_t expected_val = (word_t) val + (word_t) cnt;
    bool ok = rval == expected_val;
    if (ok)
	report(1, "Result: %lu (as expected)", rval);
    else
	err(false, "Result: %lu (expected %lu)", rval, expected_val);
    return ok;
}

bool do_fork_cmd(int argc, char *argv[]) {
    if (argc != 4) {
	err(false, "Need 3 arguments: width, val, cnt");
	return false;
    }
    int width, val, cnt;
    if (!get_int(argv[1], &width) || !get_int(argv[2], &val) ||
	!get_int(argv[3], &cnt)) {
	return false;
    }
    dword_t d = msg_build_destination(own_agent, new_operator_id(), 0);
    chunk_ptr msg = build_ifork(d, (word_t) width, (word_t) val, (word_t) cnt);
    chunk_ptr rmsg = fire_and_wait(msg);
    chunk_free(msg);
    if (!rmsg) {
	err(false, "Incr command failed");
	return false;
    }
    word_t rval = chunk_get_word(rmsg, 0 + OPER_HEADER_CNT);
    chunk_free(rmsg);
    word_t expected_val = (word_t) width * ((word_t) val + (word_t) cnt);
    bool ok = rval == expected_val;
    if (ok)
	report(1, "Result: %lu (as expected)", rval);
    else
	err(false, "Result: %lu (expected %lu)", rval, expected_val);
    return ok;
}

bool do_join_cmd(int argc, char *argv[]) {
    if (argc != 3) {
	err(false, "Need 2 arguments: val1 val2");
	return false;
    }
    int val1, val2;
    if (!get_int(argv[1], &val1) || !get_int(argv[2], &val2)) {
	err(false, "Arguments must be integers");
	return false;
    }
    dword_t d = msg_build_destination(own_agent, new_operator_id(), 0);
    chunk_ptr msg = build_join(d);
    op_insert_word(msg, (word_t) val1, OPER_SIZE + 0 + OP_HEADER_CNT);
    op_insert_word(msg, (word_t) val2, OPER_SIZE + 1 + OP_HEADER_CNT);
    chunk_ptr rmsg = fire_and_wait(msg);
    chunk_free(msg);
    if (!rmsg) {
	err(false, "Incr command failed");
	return false;
    }
    word_t rval = chunk_get_word(rmsg, OPER_HEADER_CNT);
    chunk_free(rmsg);
    word_t expected_val = (word_t) val1 + (word_t) val2;
    bool ok = rval == expected_val;
    if (ok)
	report(1, "Result: %lu (as expected)", rval);
    else
	err(false, "Result: %lu (expected %lu)", rval, expected_val);
    return ok;
}


#define BUFSIZE 256

int main(int argc, char *argv[]) {
    char buf[BUFSIZE] = "localhost";
    char fbuf[BUFSIZE];
    char *infilename = NULL;
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
	case 'f':
	    strncpy(fbuf, optarg, BUFSIZE-1);
	    fbuf[BUFSIZE-1] = '\0';
	    infilename = fbuf;
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
    run_client(infilename);
    finish_cmd();
    mem_status(stdout);
    return 0;
}

bool do_status_cmd(int argc, char *argv[]) {
    agent_show_stat();
    return true;
}

bool do_global_cmd(int argc, char *argv[]) {
    report(1, "Starting global command");
    set_ptr dset = word_set_new();
    size_t i;
    int sum = 0;
    for (i = 1; i < argc; i++) {
	int v = atoi(argv[i]);
	word_t w = (word_t) v;
	set_insert(dset, w);
	sum += v;
    }
    report(0, "Sum = %d", sum);
    size_t nword = set_marshal_size(dset);
    word_t *data = calloc_or_fail(nword, sizeof(word_t), "do_global_cmd");
    set_marshal(dset, data);
    start_client_global(0, nword, data);
    free_array(data, nword, sizeof(word_t));
    set_free(dset);
    report(1, "Global connection established");
    bool ok = finish_client_global();
    report(1, "Global command completed");
    return ok;
}

void gc_start() {
    sleep(2);
    report(1, "Starting Client GC");
}

void gc_finish() {
    sleep(2);
    report(1, "Finishing Client GC");
}
