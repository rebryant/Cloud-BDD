/* Command-line console for BDD manipulation */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <getopt.h>
#include <stdbool.h>
#include <sys/select.h>
#include <signal.h>

#include "dtype.h"
#include "table.h"
#include "chunk.h"
#include "report.h"

#include "msg.h"
#include "console.h"
#include "agent.h"
#include "bdd.h"
#include "cudd.h"
#include "shadow.h"


/* Global parameters */
/* Should I perform garbage collection? */
int enable_collect = 1;
int do_cudd = 0;
int do_local = 0;
int do_dist = 0;
/* When counting solutions,
   should I assume all variables are in support of function? */
int all_vars = 1;

/* Data structures */
shadow_mgr smgr;

/* Mapping from string names to shadow pointers */
keyvalue_table_ptr nametable;
/* Mapping from string names to vectors (represented as chunks) */
keyvalue_table_ptr vectable;

/*
  Maintain reference count for each ref reachable from nametable.
  Index by absolute values of refs
*/
keyvalue_table_ptr reftable;


/* Forward declarations */
bool do_and(int argc, char *argv[]);
bool do_local_collect(int argc, char *argv[]);
bool do_delete(int argc, char *argv[]);
bool do_cofactor(int argc, char *argv[]);
bool do_count(int argc, char *argv[]);
bool do_equal(int argc, char *argv[]);
bool do_equant(int argc, char *argv[]);
bool do_local_flush(int argc, char *argv[]);
bool do_information(int argc, char *argv[]);
bool do_ite(int argc, char *argv[]);
bool do_nothing(int argc, char *argv[]);
bool do_or(int argc, char *argv[]);
bool do_shift(int argc, char *argv[]);
bool do_size(int argc, char *argv[]);
bool do_status(int argc, char *argv[]);
bool do_uquant(int argc, char *argv[]);
bool do_var(int argc, char *argv[]);
bool do_xor(int argc, char *argv[]);
bool do_vector(int argc, char *argv[]);


chunk_ptr run_flush();

static ref_t get_ref(char *name);
static set_ptr get_refs(int cnt, char *names[]);
static void assign_ref(char *name, ref_t r, bool saturate);

static void client_gc_start();
static void client_gc_finish();


static void bdd_init() {
    smgr = new_shadow_mgr(do_cudd, do_local, do_dist);
    nametable = keyvalue_new(string_hash, string_equal);
    vectable = keyvalue_new(string_hash, string_equal);
    reftable = word_keyvalue_new();
    ref_t rzero = shadow_zero(smgr);
    ref_t rone = shadow_one(smgr);
    assign_ref("zero", rzero, true);
    assign_ref("one", rone, true);
    set_gc_handlers(client_gc_start, client_gc_finish);
}

static void console_init(bool do_dist) {
    add_cmd("and", do_and,
	    " fd f1 f2 ...   | fd <- f1 & f2 & ...");
    add_cmd("cofactor", do_cofactor,
	    " fd f l1 ...    | fd <- cofactor(f, l1, ...");
    if (!do_dist)
	add_cmd("collect", do_local_collect,
		"                | Perform garbage collection (local only)");
    add_cmd("count", do_count,
	    " f1 f2 ...      | Display function counts");
    add_cmd("delete", do_delete,
	    " f1 f2 ...      | Delete functions");
    add_cmd("equal", do_equal,
	    " f1 f2          | Test for equality");
    add_cmd("equant", do_equant,
	    " fd f v1 ...    | Existential quantification");
    if (!do_dist)
	add_cmd("flush", do_local_flush,
		"                | Flush local state");
    add_cmd("ite", do_ite,
	    " fd fi ft fe    | fd <- ITE(fi, ft, fe)");
    add_cmd("or", do_or,
	    " fd f1 f2 ...   | fd <- f1 | f2 | ...");
    add_cmd("info", do_information,
	    " f1 ..          | Display combined information about functions");
    add_cmd("shift", do_shift,
	    " fd f v1' v1 ...| Variable shift");
    add_cmd("size", do_nothing,
	    "                | Show number of nodes for each variable"); 
    add_cmd("status", do_status,
	    "                | Print statistics");
    add_cmd("uquant", do_uquant,
	    " fd f v1 ...    | Universal quantification");
    add_cmd("var", do_var,
	    " v1 v2 ...      | Create variables");
    add_cmd("xor", do_xor,
	    " fd f1 f2 ...   | fd <- f1 ^ f2 ^ ...");
    add_param("collect", &enable_collect, "Enable garbage collection", NULL);
    add_param("allvars", &all_vars, "Count all variables in support", NULL);
}

static bool bdd_quit(int argc, char *argv[]) {
    word_t wk, wv;
    while (keyvalue_removenext(nametable, &wk, NULL)) {
	char *s = (char *) wk;
#if RPT >= 5
	report(5, "Freeing string '%s' from name table", s);
#endif
	free_string(s);
    }
    keyvalue_free(nametable);
    while (keyvalue_removenext(vectable, &wk, &wv)) {
	char *s = (char *) wk;
#if RPT >= 5
	report(5, "Freeing string '%s' from vector table", s);
#endif
	free_string(s);
	chunk_ptr cp = (chunk_ptr) wv;
	chunk_free(cp);
    }
    keyvalue_free(vectable);
    keyvalue_free(reftable);
    if (do_ref(smgr)) {
	ref_show_stat(smgr->ref_mgr);
    }
    free_shadow_mgr(smgr);
    return true;
}

static void usage(char *cmd) {
    printf(
"Usage: %s [-h] [-f FILE][-v VLEVEL] [-c][-l][-d][-H HOST] [-P PORT][-r][-L FILE]\n",
	   cmd);
    printf("\t-h         Print this information\n");
    printf("\t-f FILE    Read commands from file\n");
    printf("\t-v VLEVEL  Set verbosity level\n");
    printf("\t-c         Use CUDD\n");
    printf("\t-l         Use local refs\n");
    printf("\t-d         Use distributed refs\n");
    printf("\t-H HOST    Use HOST as controller host\n");
    printf("\t-P PORT    Use PORT as controller port\n");
    printf("\t-r         Try to use local router\n");
    printf("\t-L FILE    Echo results to FILE\n");
    exit(0);
}


#define BUFSIZE 256

int main(int argc, char *argv[]) {
    /* To hold input file name */
    char buf[BUFSIZE];
    char *infile_name = NULL;
    char lbuf[BUFSIZE];
    char *logfile_name = NULL;
    int level = 1;
    int c;
    char hbuf[BUFSIZE] = "localhost";
    unsigned port = CPORT;
    bool try_local_router = false;

    do_cudd = 0;
    do_local = 0;
    do_dist = 0;
    while ((c = getopt(argc, argv, "hn:v:f:cldH:P:rL:")) != -1) {
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
	case 'r':
	    try_local_router = true;
	    break;
	case 'L':
	    logfile_name = strncpy(lbuf, optarg, BUFSIZE-1);
	    lbuf[BUFSIZE-1] = '\0';
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
	init_agent(true, hbuf, port, true, try_local_router);
	set_agent_flush_helper(run_flush);
	set_agent_stat_helper(do_summary_stat);
    }
    console_init(do_dist);
    set_verblevel(level);
    if (logfile_name)
	set_logfile(logfile_name);
    add_quit_helper(bdd_quit);
    if (signal(SIGTERM, sigterm_handler) == SIG_ERR)
	err(false, "Couldn't install signal handler");
    if (do_dist) {
	run_client(infile_name);
    } else {
	run_console(infile_name);
    }
    finish_cmd();
    mem_status(stdout);
    chunk_status(stdout);
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
    if (REF_IS_INVALID(r))
	return;
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
#if RPT >= 5
	    report(5, "Removed entry %s:%s from name table", name, buf);
#endif
	}
    } else {
	sname = strsave_or_fail(name, "assign_ref");
    }
    keyvalue_insert(nametable, (word_t) sname, (word_t) r);
    if (verblevel >= 5) {
	char buf[24];
	shadow_show(smgr, r, buf);
#if RPT >= 5
	report(5, "Added %s:%s to name table", name, buf);
#endif
    }
}


/* Command implementations */
/* Retrieve reference, given its name. */
/* Can prefix name with '!' to indicate negation */
static ref_t get_ref(char *name) {
    ref_t r;
    word_t wv;
    bool negate = (*name == '!');
    if (negate)
	name++;
    if (keyvalue_find(nametable, (word_t) name, &wv)) {
	r = (ref_t) wv;
	if (negate)
	    r = shadow_negate(smgr, r);
	return r;
    } else {
	report(0, "Function '%s' undefined", name);
	return REF_INVALID;
    }
}

/* Create set of refs from collection of names */
static set_ptr get_refs(int cnt, char *names[]) {
    set_ptr rset = word_set_new();
    int i;
    bool ok = true;
    for (i = 0; i < cnt; i++) {
	ref_t r = get_ref(names[i]);
	if (REF_IS_INVALID(r)) {
	    err(false, "Name '%s' invalid", names[i]);
	    ok = false;
	} else {
	    set_insert(rset, (word_t) r);
	}
    }
    if (ok)
	return rset;
    else {
	set_free(rset);
	return NULL;
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
	if (REF_IS_INVALID(rarg))
	    return false;
	ref_t nval = shadow_and(smgr, rval, rarg);
	rval = nval;
    }
    assign_ref(argv[1], rval, false);
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_local_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    shadow_show(smgr, rval, buf);
#if RPT >= 1
    report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
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
	if (REF_IS_INVALID(rarg))
	    return false;
	ref_t nval = shadow_or(smgr, rval, rarg);
	rval = nval;
    }
    assign_ref(argv[1], rval, false);
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_local_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    shadow_show(smgr, rval, buf);
#if RPT >= 1
    report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
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
	ref_t nval = shadow_xor(smgr, rval, rarg);
	rval = nval;
    }
    assign_ref(argv[1], rval, false);
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_local_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    shadow_show(smgr, rval, buf);
#if RPT >= 2
    report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
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
    if (do_ref(smgr) &&
	(REF_IS_INVALID(ri) || REF_IS_INVALID(rt) || REF_IS_INVALID(re)))
	return false;
    ref_t rval = shadow_ite(smgr, ri, rt, re);
    if (do_ref(smgr) && REF_IS_INVALID(rval))
	return false;
    assign_ref(argv[1], rval, false);
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_local_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    shadow_show(smgr, rval, buf);
#if RPT >= 2
    report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
    return true;
}

bool do_local_collect(int argc, char *argv[]) {
    bool ok = true;
    if (!enable_collect) {
#if RPT >= 1
	report(1, "Garbage collection disabled");
#endif
	return ok;
    }
    if (smgr->do_local) {
	set_ptr roots = word_set_new();
	word_t wk, wv;
	keyvalue_iterstart(nametable);
	while (keyvalue_iternext(nametable, &wk, &wv)) {
	    ref_t r = (ref_t) wv;
	    set_insert(roots, (word_t) r);
	}
	ref_collect(smgr->ref_mgr, roots);
	set_free(roots);
    }
    return ok;
}

static void client_gc_start() {
    set_ptr roots = word_set_new();
    word_t wk, wv;
    keyvalue_iterstart(nametable);
    while (keyvalue_iternext(nametable, &wk, &wv)) {
	ref_t r = (ref_t) wv;
	if (!set_member(roots, wv, false)  && REF_IS_FUNCT(r)) {
	    set_insert(roots, wv);
	    if (verblevel >= 5) {
		char buf[24];
		ref_show(r, buf);
#if RPT >= 5
		char *name = (char *) wk;
		report(5, "Using root %s = %s", name, buf);
#endif
	    }
	}
    }
    if (smgr->do_local) {
#if RPT >= 4
	report(4, "Performing local garbage collection");
#endif
	ref_collect(smgr->ref_mgr, roots);
    }
    if (smgr->do_dist) {
	dist_mark(smgr->ref_mgr, roots);
    }
    set_free(roots);
}

static void client_gc_finish() {
#if RPT >= 4
    report(4, "GC completed");
#endif
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
#if RPT >= 5
		report(5, "Removed entry %s:%s from name table", olds, buf);
#endif
	    }
	    free_string(olds);
	} else {
	    report(0, "Function '%s' not found", argv[i]);
	    return false;
	}
    }
    return true;
}

bool do_count(int argc, char *argv[]) {
    set_ptr roots = get_refs(argc-1, argv+1);
    int i;
    if (!roots)
	return false;
    if (all_vars) {
	keyvalue_table_ptr map = shadow_count(smgr, roots);
	for (i = 1; i < argc; i++) {
	    ref_t r = get_ref(argv[i]);
	    word_t w;
	    if (keyvalue_find(map, (word_t) r, &w)) {
#if RPT >= 1
		report(1, "%s:\t%lu", argv[i], w);
#endif
	    } else {
#if RPT >= 1
		report(1, "%s:\t??", argv[i]);
#endif
	    }
	}
	keyvalue_free(map);
    } else {
	keyvalue_table_ptr map = shadow_density(smgr, roots);
	set_ptr supset = shadow_support(smgr, roots);
	report_noreturn(0, "Support:");
	size_t idx;
	for (idx = 0; idx < smgr->nvars; idx++) {
	    ref_t r = shadow_get_variable(smgr, idx);
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
	report(0, "");
#if RPT >= 1
	word_t wt = ((word_t) 1) << supset->nelements;
	for (i = 1; i < argc; i++) {
	    report(1, "%s:\t%.0f",
		   argv[i], wt * get_double(map, get_ref(argv[i])));
	}
#endif
	keyvalue_free(map);
	set_free(supset);
    }
    set_free(roots);

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
    if (do_ref(smgr) && (REF_IS_INVALID(ra) || REF_IS_INVALID(rb)))
	return false;
    shadow_show(smgr, ra, bufa);
    shadow_show(smgr, rb, bufb);
    bool eq = ra == rb;
    report(0, "TEST %s %s= %s", bufa, eq ? "" : "!", bufb);
    return true;
}

bool do_local_flush(int argc, char *argv[]) {
#if RPT >= 1
    report(1, "Flushing state");
#endif
    bdd_quit(0, NULL);
    mem_status(stdout);
    reset_peak_bytes();
    bdd_init();
    return true;
}

/* Version for remote flushes */
chunk_ptr run_flush() {
    do_local_flush(0, NULL);
    return NULL;
}


bool do_cofactor(int argc, char *argv[]) {
    char buf[24];
    ref_t rold = get_ref(argv[2]);
    if (do_ref(smgr) && REF_IS_INVALID(rold))
	return false;
    set_ptr litset = get_refs(argc-3, argv+3);
    if (!litset)
	return false;
    set_ptr roots = word_set_new();
    set_insert(roots, (word_t) rold);
    keyvalue_table_ptr map = shadow_restrict(smgr, roots, litset);
    set_free(litset);
    set_free(roots);
    word_t wr;
    bool ok = keyvalue_find(map, (word_t) rold, &wr);
    if (ok) {
	ref_t rnew = (ref_t) wr;
	assign_ref(argv[1], rnew, false);
	shadow_show(smgr, rnew, buf);
#if RPT >= 2
	report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
    }
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_local_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    keyvalue_free(map);
   return ok;
}

bool do_equant(int argc, char *argv[]) {
    char buf[24];
    ref_t rold = get_ref(argv[2]);
    if (do_ref(smgr) && REF_IS_INVALID(rold))
	return false;
    set_ptr vset = get_refs(argc-3, argv+3);
    if (!vset)
	return false;
    set_ptr roots = word_set_new();
    set_insert(roots, (word_t) rold);
    keyvalue_table_ptr map = shadow_equant(smgr, roots, vset);
    set_free(vset);
    set_free(roots);
    word_t wr;
    bool ok = keyvalue_find(map, (word_t) rold, &wr);
    if (ok) {
	ref_t rnew = (ref_t) wr;
	assign_ref(argv[1], rnew, false);
	shadow_show(smgr, rnew, buf);
#if RPT >= 2
	report(2, "RESULT.  %s = %s", argv[1], buf);
#endif	
    }
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_local_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    keyvalue_free(map);
    return ok;
}

bool do_uquant(int argc, char *argv[]) {
    char buf[24];
    ref_t rold = get_ref(argv[2]);
    /* Get universal through negation */
    rold = REF_NEGATE(rold);
    if (do_ref(smgr) && REF_IS_INVALID(rold))
	return false;
    set_ptr vset = get_refs(argc-3, argv+3);
    if (!vset)
	return false;
    set_ptr roots = word_set_new();
    set_insert(roots, (word_t) rold);
    keyvalue_table_ptr map = shadow_equant(smgr, roots, vset);
    set_free(vset);
    set_free(roots);
    word_t wr;
    bool ok = keyvalue_find(map, (word_t) rold, &wr);
    if (ok) {
	ref_t rnew = (ref_t) wr;
	rnew = REF_NEGATE(rnew);
	assign_ref(argv[1], rnew, false);
	shadow_show(smgr, rnew, buf);
#if RPT >= 2
	report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
    }
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_local_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    keyvalue_free(map);
    return ok;
}


bool do_shift(int argc, char *argv[]) {
    char buf[24];
    if (argc <= 3 || (argc-3) % 2 != 0) {
	err(false, "Invalid number of arguments");
	return false;
    }
    ref_t rold = get_ref(argv[2]);
    if (REF_IS_INVALID(rold))
	return false;
    keyvalue_table_ptr vmap = word_keyvalue_new();
    size_t i;
    bool ok = true;
    for (i = 3; i < argc; i+=2) {
	ref_t vnew = get_ref(argv[i]);
	if (do_ref(smgr)
	    && (REF_IS_INVALID(vnew) || REF_VAR(REF_GET_VAR(vnew)) != vnew)) {
	    err(false, "Invalid variable: %s", argv[i]);
	    ok = false;
	}
	ref_t vold = get_ref(argv[i+1]);
	if (do_ref(smgr)
	    && (REF_IS_INVALID(vold) || REF_VAR(REF_GET_VAR(vold)) != vold)) {
	    err(false, "Invalid variable: %s", argv[i+1]);
	    ok = false;
	}
	keyvalue_insert(vmap, (word_t) vold, (word_t) vnew);
    }
    if (!ok) {
	keyvalue_free(vmap);
	return false;
    }
    set_ptr roots = word_set_new();
    set_insert(roots, (word_t) rold);
    keyvalue_table_ptr map = shadow_shift(smgr, roots, vmap);
    keyvalue_free(vmap);
    set_free(roots);
    word_t wr;
    ok = keyvalue_find(map, (word_t) rold, &wr);
    if (ok) {
	ref_t rnew = (ref_t) wr;
	assign_ref(argv[1], rnew, false);
	shadow_show(smgr, rnew, buf);
#if RPT >= 2
	report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
    }
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_local_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    keyvalue_free(map);
    return ok;
}

bool do_information(int argc, char *argv[]) {
    ref_t r;
    size_t idx;
    set_ptr roots = get_refs(argc-1, argv+1);
    if (!roots)
	return false;
    for (idx = 1; idx < argc; idx++) {
	report_noreturn(0, "%s ", argv[idx]);
    }
    report_noreturn(0, "\n");
    set_ptr supset = shadow_support(smgr, roots);
    report_noreturn(0, "  Support:");
    for (idx = 0; idx < smgr->nvars; idx++) {
	r = shadow_get_variable(smgr, idx);
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
    if (smgr->do_local) {
	set_ptr rset = ref_reach(smgr->ref_mgr, roots);
	report(0, "  Size: %lu nodes", rset->nelements);
	set_free(rset);
    }
    set_free(roots);
    return true;
}


bool do_support(int argc, char *argv[]) {
    set_ptr roots = get_refs(argc-1, argv+1);
    if (!roots)
	return NULL;
    set_ptr supset = shadow_support(smgr, roots);
    word_t w;
    while (set_removenext(supset, &w)) {
	ref_t r = (ref_t) w;
	char buf[24];
	ref_show(r, buf);
#if RPT >= 1
	report(1, "\t%s", buf);
#endif
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
#if RPT >= 2
	report(2, "VAR %s = %s", argv[i], buf);
#endif
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
