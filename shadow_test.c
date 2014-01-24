#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <getopt.h>
#include <stdbool.h>

#ifdef QUEUE
#include <pthread.h>
#endif


#include "dtype.h"
#include "table.h"
#include "chunk.h"
#include "report.h"
#include "bdd.h"

#include "cudd.h"
#include "shadow.h"

/* How many unique variables */
static int vcount = 1;

/*
  General strategy is to create as many unique references as possible
  by applying Boolean operations to the given set of variables.  Use
  hash tables to keep track of the possibilities.
 */

/* Those that have been fully evaluated */
static set_ptr old_refs;

/* Those that have been discovered, but still want to combine to generate more */
static set_ptr new_refs;

static shadow_mgr smgr = NULL;

static ref_t rglobal;

static void iter_and(word_t value) {
    char buf1[24], buf2[24], buf3[24];
    ref_t r = (ref_t) value;
    ref_t gr = shadow_and(smgr, rglobal, r);
    shadow_show(smgr, rglobal, buf1);
    shadow_show(smgr, r, buf2);
    shadow_show(smgr, gr, buf3);
    char *status = "exists";
    if (!set_member(old_refs, (word_t) gr, false) &&
	!set_member(new_refs, (word_t) gr, false)) {
	set_insert(new_refs, (word_t) gr);
	status = "new";
    }
    report(3, "%s AND %s --> %s (%s)", buf1, buf2, buf3, status);
}

static void iter_or(word_t value) {
    char buf1[24], buf2[24], buf3[24];
    ref_t r = (ref_t) value;
    ref_t gr = shadow_or(smgr, rglobal, r);
    shadow_show(smgr, rglobal, buf1);
    shadow_show(smgr, r, buf2);
    shadow_show(smgr, gr, buf3);
    char *status = "exists";
    if (!set_member(old_refs, (word_t) gr, false) &&
	!set_member(new_refs, (word_t) gr, false)) {
	set_insert(new_refs, (word_t) gr);
	status = "new";
    }
    report(3, "%s OR %s --> %s (%s)", buf1, buf2, buf3, status);
}

static void iter_xor(word_t value) {
    char buf1[24], buf2[24], buf3[24];
    ref_t r = (ref_t) value;
    ref_t gr = shadow_xor(smgr, rglobal, r);
    shadow_show(smgr, rglobal, buf1);
    shadow_show(smgr, r, buf2);
    shadow_show(smgr, gr, buf3);
    char *status = "exists";
    if (!set_member(old_refs, (word_t) gr, false) &&
	!set_member(new_refs, (word_t) gr, false)) {
	set_insert(new_refs, (word_t) gr);
	status = "new";
    }
    report(3, "%s XOR %s --> %s (%s)", buf1, buf2, buf3, status);
}


static size_t run(size_t nvars, size_t max_new, bool do_cudd, bool do_local, bool do_dist) {
    char buf[24];
    size_t i;
    ref_t r;
    size_t new_cnt = 2;
    old_refs = set_new(word_hash, word_equal);
    new_refs = set_new(word_hash, word_equal);
    smgr = new_shadow_mgr(do_cudd, do_local, do_dist);
    r = shadow_zero(smgr);
    shadow_show(smgr, r, buf);
    set_insert(old_refs, (word_t) r);
    r = shadow_one(smgr);
    shadow_show(smgr, r, buf);
    set_insert(old_refs, (word_t) r);
    for (i = 0; i < nvars; i++) {
	r = shadow_new_variable(smgr);
	set_insert(new_refs, (word_t) r);
    }
    word_t p;
    while (set_removenext(new_refs, &p) && new_cnt < max_new) {
	r = (ref_t) p;
	if (set_member(old_refs, (word_t) r, false)) {
	    continue;
	}
	set_insert(old_refs, (word_t) r);
	new_cnt++;
	rglobal = r;
	set_apply(old_refs, iter_and);
	set_apply(old_refs, iter_or);
	set_apply(old_refs, iter_xor);
    }
    size_t result = old_refs->nelements;
    set_free(old_refs);
    set_free(new_refs);
    free_shadow_mgr(smgr);
    return result;
}

static void usage(char *cmd) {
    printf("Usage: %s [-h] [-n NVAR] [-v VLEVEL] [-c][-l][-d]\n", cmd);
    printf("\t-h         Print this information\n");
    printf("\t-n NVAR    Set number of variables\n");
    printf("\t-f FMAX    Limit number of generated functions\n");
    printf("\t-v VLEVEL  Set verbosity level\n");
    printf("\t-c         Use CUDD\n");
    printf("\t-l         Use local refs\n");
    printf("\t-d         Use distributed refs\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    int nvar = 1;
    verblevel = 1;
    size_t max_new = 1000000;
    bool do_cudd = false;
    bool do_local = false;
    bool do_dist = false;
    int c;
    while ((c = getopt(argc, argv, "hn:v:f:cld")) != -1) {
	switch(c) {
	case 'h':
	    usage(argv[0]);
	    break;
	case 'v':
	    verblevel = atoi(optarg);
	    break;
	case 'n':
	    nvar = atoi(optarg);
	    break;
	case 'f':
	    max_new = (size_t) atoi(optarg);
	    break;
	case 'c':
	    do_cudd = true;
	    break;
	case 'l':
	    do_local = true;
	    break;
	case 'd':
	    do_dist = true;
	    break;
	default:
	    printf("Unknown option '%c'\n", c);
	    usage(argv[0]);
	    break;
	}
    }
    size_t nfun = run(nvar, max_new, do_cudd, do_local, do_dist);
    report(1, "%u functions generated", nfun);
    mem_status(stdout);
    return 0;
}
