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


/* Data flow operators to implement testing programs */

/********************************************************
 Supported operations:

 ifork(dest, width, val, cnt):
    If width == 1, then invoke incr(dest, val, cnt)
    Else
      Create operand destinations dest1 & dest2
      create join(dest, dest1, dest2)
      invoke fork(dest1, width/2, val, cnt)
             fork(dest2, width-width/2, val, cnt)
 incr(dest, val, cnt):
    If cnt == 0, send val to dest
    Else invoke incr(dest, val+1, cnt-1)

 join(dest, val1, val2)
    Send val1+val2 to dest

 sink(val):
    Notify agent (must be client) that have value val


********************************************************/

chunk_ptr flush_worker() {
    report(3, "Flushing state");
    /* Gather statistics information */
    agent_stat_counter[STATA_BYTE_PEAK] = last_peak_bytes;
    reset_peak_bytes();
    chunk_ptr msg = msg_new_stat(1, NSTATA, agent_stat_counter);
    return msg;
}

chunk_ptr build_ifork(dword_t dest, word_t width, word_t val, word_t cnt) {
    word_t worker = choose_random_worker();
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_IFORK, worker, id,
				    1*OPER_SIZE + 3 + OP_HEADER_CNT);
    op_insert_dword(op, dest,  0*OPER_SIZE + 0 + OP_HEADER_CNT);
    op_insert_word(op, width, 1*OPER_SIZE + 0 + OP_HEADER_CNT);
    op_insert_word(op, val,   1*OPER_SIZE + 1 + OP_HEADER_CNT);
    op_insert_word(op, cnt,   1*OPER_SIZE + 2 + OP_HEADER_CNT);
    report(3,
"Created fork op.  Worker %u.  Operator Id 0x%lx.  Width %u, val %u, cnt %u",
	   worker, id, width, val, cnt);
    return op;
}

chunk_ptr build_incr(dword_t dest, word_t val, word_t cnt) {
    word_t worker = choose_random_worker();
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_INCR, worker, id,
				    1*OPER_SIZE + 2 + OP_HEADER_CNT);
    op_insert_dword(op, dest, 0*OPER_SIZE + 0 + OP_HEADER_CNT);
    op_insert_word(op, val,  1*OPER_SIZE + 0 + OP_HEADER_CNT);
    op_insert_word(op, cnt,  1*OPER_SIZE + 1 + OP_HEADER_CNT);
    report(3,
"Created incr operation.  Worker %u.  Operator Id 0x%lx.  val %u, cnt %u",
	   worker, id, val, cnt);
    return op;
}

chunk_ptr build_join(dword_t dest) {
    word_t worker = choose_random_worker();
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_JOIN, worker, id,
				    1*OPER_SIZE + 2 + OP_HEADER_CNT);
    op_insert_dword(op, dest, 0*OPER_SIZE + OP_HEADER_CNT);
    report(3,
	   "Created join operation.  Worker %u.  Operator Id 0x%lx",
	   worker, id);
    return op;
}

bool do_ifork_op(chunk_ptr op) {
    dword_t dh = chunk_get_dword(op, 0);
    word_t id = msg_get_dheader_op_id(dh);
    dword_t dest = chunk_get_dword(op, 0*OPER_SIZE + 0 + OP_HEADER_CNT);
    word_t width = chunk_get_word(op, 1*OPER_SIZE + 0 + OP_HEADER_CNT);
    word_t val = chunk_get_word(op, 1*OPER_SIZE + 1 + OP_HEADER_CNT);
    word_t cnt = chunk_get_word(op, 1*OPER_SIZE + 2 + OP_HEADER_CNT);
    bool ok = true;
    report(5, "Starting fork op.  Id 0x%x", id);
    if (width == 1) {
	chunk_ptr incr_op = build_incr(dest, val, cnt);
	report(5, "Fork op spawned incr op.  val %u, cnt %u");
	ok = ok && send_op(incr_op);
	if (ok)
	    report(5, "Sent incr operation");
	else
	    err(0, "Couldn't send incr operation");
	chunk_free(incr_op);
    } else {
	/* Build target join operation */
	chunk_ptr join_op = build_join(dest);
	report(5, "Fork op spawned join op");
	ok = ok && send_op(join_op);
	if (ok)
	    report(5, "Sent join operation");
	else
	    err(false, "Couldn't send join operation");
	/* Build fork operations */
	unsigned i;
	for (i = 0; ok && i < 2; i++) {
	    word_t w = i == 0 ? width/2 : (width - width/2);
	    dword_t ndest = msg_new_destination(join_op,
						1*OPER_SIZE + i + OP_HEADER_CNT);
	    chunk_ptr fork_op = build_ifork(ndest, w, val, cnt);
	    dword_t dh = chunk_get_dword(fork_op, 0);
	    word_t id = msg_get_dheader_op_id(dh);
	    report(5,
"Fork op spawned fork op.  width %u, val %u, cnt %u, Id 0x%lx", w, val, cnt, id);
	    if (send_op(fork_op))
		report(5, "Sent fork operation.  Id 0x%lx", id);
	    else {
		ok = false;
		err(false, "Couldn't send fork operation.  Id 0x%lx", id);
	    }
	    chunk_free(fork_op);
	}
	chunk_free(join_op);
    }
    return ok;
}

bool do_incr_op(chunk_ptr op) {
    dword_t dest = chunk_get_dword(op, 0*OPER_SIZE + OP_HEADER_CNT);
    unsigned agent = msg_get_dest_agent(dest);
    word_t operator_id = msg_get_dest_op_id(dest);
    unsigned offset = msg_get_dest_offset(dest);
    word_t val = chunk_get_word(op, 1*OPER_SIZE + 0 + OP_HEADER_CNT);
    word_t cnt = chunk_get_word(op, 1*OPER_SIZE + 1 + OP_HEADER_CNT);
    bool ok = true;
    if (cnt == 0) {
	chunk_ptr result = msg_new_operand(dest, 1 + OPER_HEADER_CNT);
	chunk_insert_word(result, val, 0 + OPER_HEADER_CNT);
	ok = ok && send_op(result);
	if (ok)
	    report(5,
"Sent incr result %lu.  Agent %u.  Operator Id 0x%lx.  Offset %u",
		   val, agent, operator_id, offset);
	else
	    err(false, 
"Couldn't send result %lu.  Agent %u.  Operator Id 0x%lx.  Offset %u",
		val, agent, operator_id, offset);
	chunk_free(result);
    } else {
	chunk_ptr incr_op = build_incr(dest, val+1, cnt-1);
	report(5, "incr op spawned incr op val %u, cnt %u", val+1, cnt-1);
	ok = ok && send_op(incr_op);
	if (ok)
	    report(5, "Sent incr op");
	else
	    err(0, "Couldn't send incr op");
	chunk_free(incr_op);
    }
    return ok;
}

bool do_join_op(chunk_ptr op) {
    dword_t dest = chunk_get_dword(op, 0*OPER_SIZE + OP_HEADER_CNT);
    unsigned agent = msg_get_dheader_agent(dest);
    word_t operator_id = msg_get_dest_op_id(dest);
    unsigned offset = msg_get_dest_offset(dest);
    word_t val1 = chunk_get_word(op, 1*OPER_SIZE + 0 + OP_HEADER_CNT);
    word_t val2 = chunk_get_word(op, 1*OPER_SIZE + 1 + OP_HEADER_CNT);
    word_t val = val1+val2;
    chunk_ptr result = msg_new_operand(dest, 1 + OPER_HEADER_CNT);
    chunk_insert_word(result, val, 0 + OPER_HEADER_CNT);
    bool ok = send_op(result);
    if (ok)
	report(5,
"Sent join result %lu.  Agent %u.  Operator Id 0x%lx.  Offset %u",
	       val, agent, operator_id, offset);
    else
	err(false,
"Couldn't send result %lu.  Agent %u.  Operator Id 0x%lx.  Offset %u",
	    val, agent, operator_id, offset);
    chunk_free(result);
    return ok;
}

/* Summary statistics */

/* Information for processing statistics information */
static char *stat_items[NSTAT] = {
    /* These come from stat_counter in agent */
    "Peak bytes allocated  ",
    "Total operations sent ",
    "Total local operations",
    "Total operands   sent ",
    "Total local operands  ",
};

/* For processing summary statistics information */
void do_summary_stat(chunk_ptr smsg) {
    size_t i;
    word_t h = chunk_get_word(smsg, 0);
    int nworker = msg_get_header_workercount(h);
    if (nworker <= 0) {
	err(false, "Invalid number of workers: %d", nworker);
	nworker = 1;
    }
    for (i = 0; i < NSTAT; i++) {
	word_t minval = chunk_get_word(smsg, 1 + i*3 + 0);
	word_t maxval = chunk_get_word(smsg, 1 + i*3 + 1);
	word_t sumval = chunk_get_word(smsg, 1 + i*3 + 2);
	report(1,
"%s: Min: %" PRIu64 "\tMax: %" PRIu64 "\tAvg: %.2f\tSum: %" PRIu64,
	       stat_items[i], minval, maxval, (double) sumval/nworker, sumval);
    }
}

/* Global operations */
/* Global operations by workers */
void start_global(unsigned id, unsigned opcode, unsigned nword, word_t *data) {
    int sum = 0;
    if (nword > 0) {
	/* Data should be set of ints */
	set_ptr dset = word_set_new();
	set_unmarshal(dset, data, nword);
	set_iterstart(dset);
	word_t w;
	while (set_iternext(dset, &w)) {
	    int v = (int) w;
	    sum += v;
	}
	set_free(dset);
    }
    report(0, "Starting global operation with id %u, opcode %u.  Sum = %d",
	   id, opcode, sum);
}

void finish_global(unsigned id) {
    report(0, "Finishing global operation with id %u", id);
}

