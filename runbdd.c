/* Command-line console for BDD manipulation */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <getopt.h>
#include <stdbool.h>
#include <sys/select.h>

#include "dtype.h"
#include "table.h"
#include "chunk.h"
#include "report.h"
#include "bdd.h"

#include "cudd.h"
#include "shadow.h"
#include "console.h"
#include "msg.h"
#include "agent.h"

/* Global parameters */
/* Should I perform garbage collection? */
int enable_collect = 1;
int do_cudd = 0;
int do_local = 0;
int do_dist = 0;

/* Data structures */
shadow_mgr smgr;

/* Mapping from string names to shadow pointers */
keyvalue_table_ptr nametable;
/*
  Maintain reference count for each ref reachable from nametable.
  Index by absolute values of refs
*/
keyvalue_table_ptr reftable;


/* Forward declarations */
bool do_and(int argc, char *argv[]);
bool do_collect(int argc, char *argv[]);
bool do_delete(int argc, char *argv[]);
bool do_density(int argc, char *argv[]);
bool do_equal(int argc, char *argv[]);
bool do_flush(int argc, char *argv[]);
bool do_information(int argc, char *argv[]);
bool do_ite(int argc, char *argv[]);
bool do_nothing(int argc, char *argv[]);
bool do_or(int argc, char *argv[]);
bool do_size(int argc, char *argv[]);
bool do_status(int argc, char *argv[]);
bool do_var(int argc, char *argv[]);
bool do_xor(int argc, char *argv[]);

static ref_t get_ref();
static void assign_ref(char *name, ref_t r, bool saturate);


static void bdd_init() {
    smgr = new_shadow_mgr(do_cudd, do_local, do_dist);
    nametable = keyvalue_new(string_hash, string_equal);
    reftable = word_keyvalue_new();
    ref_t rzero = shadow_zero(smgr);
    ref_t rone = shadow_one(smgr);
    assign_ref("zero", rzero, true);
    assign_ref("one", rone, true);
}

static void console_init(bool do_dist) {
    add_cmd("and", do_and,                 "and  fd f1 f2 ... | fd <- f1 & f2 & ...");
    add_cmd("collect", do_collect,         "collect           | Perform garbage collection");
    add_cmd("delete", do_delete,           "delete f1 f2 ...  | Delete functions");
    add_cmd("density", do_density,         "density f1 f2 ... | Display function densities");
    add_cmd("equal", do_equal,             "equal f1 f2       | Test for equality");
    if (!do_dist)
	add_cmd("flush", do_flush,             "flush             | Reset");
    add_cmd("ite", do_ite,                 "ite  fd fi ft fe  | fd <- ITE(fi, ft, fe)");
    add_cmd("or", do_or,                   "or   fd f1 f2 ... | fd <- f1 | f2 | ...");
    add_cmd("information", do_information, "information f1 .. | Display combined information about functions");
    add_cmd("size", do_nothing,            "size              | Show number of nodes for each variable"); 
    add_cmd("status", do_status,      "print statistics");
    add_cmd("var", do_var,            "var   v1 v2 ...   | Create variables");
    add_cmd("xor", do_xor,            "xor  fd f1 f2 ... | fd <- f1 ^ f2 ^ ...");
    add_param("collect", &enable_collect, "Enable garbage collection");
}

static bool bdd_quit(int argc, char *argv[]) {
    word_t wk;
    while (keyvalue_removenext(nametable, &wk, NULL)) {
	char *s = (char *) wk;
	report(5, "Freeing string '%s' from name table", s);
	free_string(s);
    }
    keyvalue_free(nametable);
    keyvalue_free(reftable);
    free_shadow_mgr(smgr);
    return true;
}

static void usage(char *cmd) {
    printf("Usage: %s [-h] [-f FILE][-v VLEVEL] [-c][-l][-d][-H HOST] [-P PORT]\n", cmd);
    printf("\t-h         Print this information\n");
    printf("\t-f FILE    Read commands from file\n");
    printf("\t-v VLEVEL  Set verbosity level\n");
    printf("\t-c         Use CUDD\n");
    printf("\t-l         Use local refs\n");
    printf("\t-d         Use distributed refs\n");
    printf("\t-H HOST    Use HOST as controller host\n");
    printf("\t-P PORT    Use PORT as controller port\n");
    exit(0);
}


#define BUFSIZE 256

int main(int argc, char *argv[]) {
    /* To hold input file name */
    char buf[BUFSIZE];
    char *infile_name = NULL;
    int level = 1;
    int c;
    char hbuf[BUFSIZE] = "localhost";
    unsigned port = CPORT;

    do_cudd = 0;
    do_local = 0;
    do_dist = 0;
    while ((c = getopt(argc, argv, "hn:v:f:cldH:P:")) != -1) {
	switch(c) {
	case 'h':
	    usage(argv[0]);
	    break;
	case 'f':
	    infile_name = strncpy(buf, optarg, BUFSIZE-1);
	    buf[BUFSIZE-1] = '\0';
	    break;
	case 'v':
	    level = atoi(optarg);
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
	case 'H':
	    strncpy(hbuf, optarg, BUFSIZE-1);
	    hbuf[BUFSIZE-1] = '\0';
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
    bdd_init();
    init_cmd();
    if (do_dist) {
	init_agent(true, hbuf, port);
	set_agent_flush_helper(do_flush);
    }
    console_init(do_dist);
    set_verblevel(level);
    add_quit_helper(bdd_quit);
    if (do_dist) {
	run_client(infile_name);
    } else {
	run_console(infile_name);
    }
    finish_cmd();
    mem_status(stdout);
    return 0;
}

#define SATVAL ((word_t) 1<<20)

/* Increment reference count for new ref */
static void root_addref(ref_t r, bool saturate) {
    int cnt = 0;
    ref_t ar = shadow_absval(smgr, r);
    word_t wv;
    if (keyvalue_remove(reftable, (word_t) ar, NULL, &wv))
	cnt = wv;
    if (saturate || cnt == SATVAL)
	cnt = SATVAL;
    else
	cnt++;
    keyvalue_insert(reftable, (word_t) ar, cnt);
}


/* Decrement reference count for ref */
static void root_deref(ref_t r) {
    int cnt = 0;
    ref_t ar = shadow_absval(smgr, r);
    /* Decrement reference count */
    word_t wv;
    if (keyvalue_remove(reftable, (word_t) ar, NULL, &wv)) {
	cnt = wv;
	if (cnt < SATVAL)
	    cnt--;
	if (cnt < 0) {
	    char buf[24];
	    shadow_show(smgr, ar, buf);
	    err(true, "Negative ref count for %s", buf, cnt);
	}
	if (cnt > 0)
	    keyvalue_insert(reftable, (word_t) ar, cnt);
	else
	    shadow_deref(smgr, ar);
    }
}

/* Add reference to table.  Makes permanent copy of name */
static void assign_ref(char *name, ref_t r, bool saturate) {
    ref_t rold;
    char *sname;
    /* Add reference to new value */
    root_addref(r, saturate);
    /* (Try to) remove old value */
    word_t wk, wv;
    if (keyvalue_remove(nametable, (word_t) name, &wk, &wv)) {
	sname = (char *) wk;
	rold = (ref_t) wv;
	root_deref(rold);
	if (verblevel >= 5) {
	    char buf[24];
	    shadow_show(smgr, rold, buf);
	    report(5, "Removed entry %s:%s from name table", name, buf);
	}
    } else {
	sname = strsave_or_fail(name, "assign_ref");
    }
    keyvalue_insert(nametable, (word_t) sname, (word_t) r);
    if (verblevel >= 5) {
	char buf[24];
	shadow_show(smgr, r, buf);
	report(5, "Added %s:%s to name table", name, buf);
    }
}


/* Command implementations */
static ref_t get_ref(char *name) {
    ref_t r;
    word_t wv;
    if (keyvalue_find(nametable, (word_t) name, &wv)) {
	r = (ref_t) wv;
	return r;
    } else {
	report(0, "Function '%s' undefined", name);
	return REF_INVALID;
    }
}

/* Inefficient way to do inverse lookup in nametable */
static char *name_find(ref_t r) {
    keyvalue_iterstart(nametable);
    char *name;
    ref_t tr;
    word_t wk, wv;
    while (keyvalue_iternext(nametable, &wk, &wv)) {
	name = (char *) wk;
	tr = (ref_t) wv;
	if (tr == r)
	    return name;
    }
    return NULL;
}

/* Forward declarations */
bool do_and(int argc, char *argv[]) {
    int i;
    char buf[24];
    ref_t rval = shadow_one(smgr);
    if (argc < 2) {
	report(0, "Need destination name");
	return false;
    }
    for (i = 2; i < argc; i++) {
	ref_t rarg = get_ref(argv[i]);
	if (rarg == REF_INVALID)
	    return false;
	rval = shadow_and(smgr, rval, rarg);
    }
    assign_ref(argv[1], rval, false);
    shadow_show(smgr, rval, buf);
    report(1, "RESULT.  %s = %s", argv[1], buf);
    return true;
}

bool do_or(int argc, char *argv[]) {
    int i;
    char buf[24];
    ref_t rval = shadow_zero(smgr);
    if (argc < 2) {
	report(0, "Need destination name");
	return false;
    }
    for (i = 2; i < argc; i++) {
	ref_t rarg = get_ref(argv[i]);
	if (rarg == REF_INVALID)
	    return false;
	rval = shadow_or(smgr, rval, rarg);
    }
    assign_ref(argv[1], rval, false);
    shadow_show(smgr, rval, buf);
    report(1, "RESULT.  %s = %s", argv[1], buf);
    return true;
}

bool do_xor(int argc, char *argv[]) {
    int i;
    char buf[24];
    ref_t rval = shadow_zero(smgr);
    if (argc < 2) {
	report(0, "Need destination name");
	return false;
    }
    for (i = 2; i < argc; i++) {
	ref_t rarg = get_ref(argv[i]);
	if (REF_IS_INVALID(rarg))
	    return false;
	rval = shadow_xor(smgr, rval, rarg);
    }
    assign_ref(argv[1], rval, false);
    shadow_show(smgr, rval, buf);
    report(1, "RESULT.  %s = %s", argv[1], buf);
    return true;
}

bool do_ite(int argc, char *argv[]) {
    char buf[24];
    if (argc != 5) {
	report(0, "ITE needs 3 arguments");
	return false;
    }
    ref_t ri = get_ref(argv[2]);
    ref_t rt = get_ref(argv[3]);
    ref_t re = get_ref(argv[4]);
    if (ri == REF_INVALID || rt == REF_INVALID || re == REF_INVALID)
	return false;
    ref_t rval = shadow_ite(smgr, ri, rt, re);
    if (REF_IS_INVALID(rval))
	return false;
    assign_ref(argv[1], rval, false);
    shadow_show(smgr, rval, buf);
    report(1, "RESULT.  %s = %s", argv[1], buf);
    return true;
}

bool do_collect(int argc, char *argv[]) {
    if (!smgr->do_local)
	return true;
    if (!enable_collect) {
	report(1, "Garbage collection disabled");
	return true;
    }
    set_ptr roots = word_set_new();
    word_t wk, wv;
    keyvalue_iterstart(nametable);
    while (keyvalue_iternext(nametable, &wk, &wv)) {
	ref_t r = (ref_t) wv;
	set_insert(roots, (word_t) r);
    }
    ref_collect(smgr->ref_mgr, roots);
    set_free(roots);
    return true;
}

bool do_delete(int argc, char *argv[]) {
    size_t i;
    for (i = 1; i < argc; i++) {
	ref_t rold;
	char *olds;
	/* (Try to) remove old value */
	word_t wk, wv;
	if (keyvalue_remove(nametable, (word_t) argv[i], &wk, &wv)) {
	    olds = (char *) wk;
	    rold = (ref_t) wv;
	    root_deref(rold);
	    if (verblevel >= 5) {
		char buf[24];
		shadow_show(smgr, rold, buf);
		report(5, "Removed entry %s:%s from name table", olds, buf);
	    }
	    free_string(olds);
	} else {
	    report(0, "Function '%s' not found", argv[i]);
	    return false;
	}
    }
    return true;
}

bool do_density(int argc, char *argv[]) {
    if (!do_ref(smgr)) {
	report(0, "Cannot compute densities without refs");
	return false;
    }
    set_ptr roots = word_set_new();
    size_t i;
    for (i = 1; i < argc; i++) {
	ref_t r = get_ref(argv[i]);
	if (r == REF_INVALID) {
	    set_free(roots);
	    return false;
	}
	set_insert(roots, (word_t) r);
    }
    keyvalue_table_ptr map = ref_density(smgr->ref_mgr, roots);
    for (i = 1; i < argc; i++)
	report(1, "%s:\t%.4f", argv[i], get_double(map, get_ref(argv[i])));
    set_free(roots);
    keyvalue_free(map);
    return true;
}

bool do_equal(int argc, char *argv[]) {
    if (argc != 3) {
	report(0, "equal requires two arguments");
	return false;
    }
    ref_t ra, rb;
    char bufa[24], bufb[24];
    ra = get_ref(argv[1]);
    rb = get_ref(argv[2]);
    if (ra == REF_INVALID || rb == REF_INVALID)
	return false;
    shadow_show(smgr, ra, bufa);
    shadow_show(smgr, rb, bufb);
    bool eq = ra == rb;
    report(0, "TEST %s %s= %s", bufa, eq ? "" : "!", bufb);
    return true;
}

bool do_flush(int argc, char *argv[]) {
    report(1, "Flushing state");
    bdd_quit(0, NULL);
    mem_status(stdout);
    bdd_init();
    return true;
}


bool do_information(int argc, char *argv[]) {
    if (!smgr->do_local) {
	report(0, "Cannot compute information without local refs");
	return false;
    }
    ref_t r;
    set_ptr roots = word_set_new();
    size_t i;
    for (i = 1; i < argc; i++) {
	r = get_ref(argv[i]);
	if (r == REF_INVALID) {
	    set_free(roots);
	    return false;
	}
	set_insert(roots, (word_t) r);
    }
    set_ptr supset = ref_support(smgr->ref_mgr, roots);
    report_noreturn(0, "Support:");
    size_t idx;
    for (idx = 0; idx < smgr->ref_mgr->variable_cnt; idx++) {
	r = REF_VAR(idx);
	if (set_member(supset, (word_t) r, false)) {
	    char *name = name_find(r);	    
	    if (name)
		report_noreturn(0, " %s", name);
	    else {
		char buf[24];
		ref_show(r, buf);
		report_noreturn(0, " %s", buf);
	    }
	}
    }
    report_noreturn(0, "\n");
    set_free(supset);
    set_ptr rset = ref_reach(smgr->ref_mgr, roots);
    report(0, "Size: %lu nodes", rset->nelements);
    set_free(roots);
    set_free(rset);
    return true;
}


bool do_support(int argc, char *argv[]) {
    if (!do_ref(smgr)) {
	report(0, "Cannot compute support without refs");
	return false;
    }
    set_ptr roots = word_set_new();
    size_t i;
    for (i = 1; i < argc; i++) {
	ref_t r = get_ref(argv[i]);
	if (r == REF_INVALID) {
	    set_free(roots);
	    return false;
	}
	set_insert(roots, (word_t) r);
    }
    set_ptr supset = ref_support(smgr->ref_mgr, roots);
    word_t w;
    while (set_removenext(supset, &w)) {
	ref_t r = (ref_t) w;
	char buf[24];
	ref_show(r, buf);
	report(1, "\t%s", buf);
    }
    set_free(roots);
    set_free(supset);
    return true;
}



bool do_var(int argc, char *argv[]) {
    ref_t rv;
    char buf[24];
    size_t i;
    for (i = 1; i < argc; i++) {
	rv = shadow_new_variable(smgr);
	if (REF_IS_INVALID(rv))
	    return false;
	assign_ref(argv[i], rv, true);
	shadow_show(smgr, rv, buf);
	report(1, "VAR %s = %s", argv[i], buf);
    }
    return true;
}

bool do_nothing(int argc, char *argv[]) {
    report(0, "%s not implemented", argv[0]);
    return true;
}

bool do_status(int argc, char *argv[]) {
    if (do_ref(smgr))
	ref_show_stat(smgr->ref_mgr);
    if (smgr->do_cudd) {
	Cudd_PrintInfo(smgr->bdd_manager, stdout);
    }
    return true;
}
