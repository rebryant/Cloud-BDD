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
#include "report.h"
#include "conjunct.h"

/* Keep, but do not compile, old versions of reduction */
#define OLD_REDUCE 0

/* Global parameters */
/* Should I perform garbage collection? */
int enable_collect = 1;

bool do_cudd = false;
bool do_local = false;
bool do_dist = false;


/* What type of chaining should be used? */
chaining_t chaining_type = CHAIN_NONE;

/* When counting solutions,
   should I assume all variables are in support of function? */
int all_vars = 1;

/* Data structures */
shadow_mgr smgr;

/* Mapping from string names to shadow pointers */
keyvalue_table_ptr nametable;

/*
  Maintain reference count for each ref reachable from nametable
  or as an intermediate result in a reduction operation.
  Index by absolute values of refs
*/
keyvalue_table_ptr reftable;


/* Forward declarations */
bool do_aconvert(int argc, char *argv[]);
bool do_and(int argc, char *argv[]);
bool do_collect(int argc, char *argv[]);
bool do_conjunct(int argc, char *argv[]);
bool do_delete(int argc, char *argv[]);
bool do_cofactor(int argc, char *argv[]);
bool do_count(int argc, char *argv[]);
bool do_equal(int argc, char *argv[]);
bool do_equant(int argc, char *argv[]);
bool do_local_flush(int argc, char *argv[]);
bool do_information(int argc, char *argv[]);
bool do_ite(int argc, char *argv[]);
bool do_or(int argc, char *argv[]);
bool do_nothing(int argc, char *argv[]);
bool do_not(int argc, char *argv[]);
bool do_restrict(int argc, char *argv[]);
bool do_satisfy(int argc, char *argv[]);
bool do_shift(int argc, char *argv[]);
bool do_simplify(int argc, char *argv[]);
bool do_size(int argc, char *argv[]);
bool do_status(int argc, char *argv[]);
bool do_uquant(int argc, char *argv[]);
bool do_var(int argc, char *argv[]);
bool do_vector(int argc, char *argv[]);
bool do_xor(int argc, char *argv[]);
bool do_zconvert(int argc, char *argv[]);

chunk_ptr run_flush();

static ref_t get_ref(char *name);
static set_ptr get_refs(int cnt, char *names[]);
static void assign_ref(char *name, ref_t r, bool fresh);

static void client_gc_start();
static void client_gc_finish();


static void bdd_init() {
    smgr = new_shadow_mgr(do_cudd, do_local, do_dist, chaining_type);
    nametable = keyvalue_new(string_hash, string_equal);
    reftable = word_keyvalue_new();
    ref_t rzero = shadow_zero(smgr);
    ref_t rone = shadow_one(smgr);
    assign_ref("zero", rzero, true);
    assign_ref("one", rone, true);
    set_gc_handlers(client_gc_start, client_gc_finish);
}

static void console_init(bool do_dist, char *cstring) {
    add_cmd("aconvert", do_aconvert,
	    " af f ...       | Convert f to ADD and name af");
    add_cmd("and", do_and,
	    " fd f1 f2 ...   | fd <- f1 & f2 & ...");
    add_cmd("conjunct", do_conjunct,
	    " fd f1 f2 ...   | fd <- f1 & f2 & ...");
    add_cmd("cofactor", do_cofactor,
	    " fd f l1 ...    | fd <- cofactor(f, l1, ...");
    if (do_local || do_cudd)
	add_cmd("collect", do_collect,
		"            | Perform garbage collection (local & cudd only)");
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
    add_cmd("not", do_not,
	    " fd f           | fd <- ~f");
    add_cmd("info", do_information,
	    " f1 ..          | Display combined information about functions");
    add_cmd("satisfy", do_satisfy,
	    " f1 ..          | Print satisfying values for functions");
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
    add_cmd("restrict", do_restrict,
	    " fd f c ...     | fd <- Restrict(f,c) [Coudert/Madre's restriction operation]");
    add_cmd("simplify", do_simplify,
	    " fnew fold  f1 f2 ... | Simplify fold with respect to f1 f2 ... while maintaining conjunction");
    add_cmd("zconvert", do_zconvert,
	    " zf f           | Convert f to ZDD and name zf");
    add_param("collect", &enable_collect, "Enable garbage collection", NULL);
    add_param("allvars", &all_vars, "Count all variables in support", NULL);
    init_conjunct(cstring);
}

static bool bdd_quit(int argc, char *argv[]) {
    word_t wk;
    while (keyvalue_removenext(nametable, &wk, NULL)) {
	char *s = (char *) wk;
#if RPT >= 5
	report(5, "Freeing string '%s' from name table", s);
#endif
	free_string(s);
    }
    keyvalue_free(nametable);
    keyvalue_free(reftable);
    if (do_ref(smgr)) {
	ref_show_stat(smgr->ref_mgr);
    }
    free_shadow_mgr(smgr);
    return true;
}

static void usage(char *cmd) {
    printf(
"Usage: %s [-h] [-f FILE][-v VLEVEL] [-c][-l][-d][-H HOST] [-P PORT][-r][-L FILE][-C chain]\n",
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
    printf("\t-t LIMIT   Set time limit (in seconds)\n");
    printf("\t-C CHAIN   n: No chaining; c: constant chaining; a: Or chaining, z: Zero chaining\n");
    printf("\t-O (L|B|T|P|D)(NO|UL|UR|SL|SR)(N|Y) | Set options for conjunction\n");
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
    char cstring[6] = "LNON";
    
    do_cudd = 1;
    do_local = 0;
    do_dist = 0;
    chaining_type = CHAIN_ALL;


    while ((c = getopt(argc, argv, "hv:f:cldH:P:rL:t:C:O:")) != -1) {
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
	case 't':
	    timelimit = atoi(optarg);
	    change_timeout(0);
	    break;
	case 'C':
	    switch (optarg[0]) {
	    case 'n':
		chaining_type = CHAIN_NONE;
		break;
	    case 'c':
		chaining_type = CHAIN_CONSTANT;
		break;
	    case 'a':
	    case 'o':
		chaining_type = CHAIN_ALL;
		break;
	    default:
		err(true, "Invalid chaining type '%c'\n", optarg[0]);
	    }
	    break;
	case 'O':
	    strcpy(cstring, optarg);
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
    console_init(do_dist, cstring);
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

/* Infinite value for reference count */
#define SATVAL ((word_t) 1<<20)

/* Increment reference count for new ref */
/* Argument fresh indicates whether argument is result of new operation */
void root_addref(ref_t r, bool fresh) {
    int ocnt = 0;
    int ncnt = 0;
    bool saturate = false;
    if (REF_IS_INVALID(r))
	return;
    ref_t ar = shadow_absval(smgr, r);
    word_t wv;
    if (keyvalue_remove(reftable, (word_t) ar, NULL, &wv))
	ocnt = wv;
    if (saturate || ocnt == SATVAL)
	ncnt = SATVAL;
    else
	ncnt = ocnt+1;
    keyvalue_insert(reftable, (word_t) ar, ncnt);
    if (fresh && ocnt > 0 && ocnt != SATVAL)
	/* Only maintain single Cudd reference for all managed refs */
	shadow_deref(smgr, r);
#if RPT >= 5
    char buf[24];
    shadow_show(smgr, ar, buf);
    report(5, "Ref count for %s: %d --> %d", buf, ocnt, ncnt);
#endif
}


/* Decrement reference count for ref */
void root_deref(ref_t r) {
    int ocnt = 0;
    int ncnt = 0;
    if (REF_IS_INVALID(r))
	return;
    ref_t ar = shadow_absval(smgr, r);
    /* Decrement reference count */
    word_t wv;
    if (keyvalue_remove(reftable, (word_t) ar, NULL, &wv)) {
	ocnt = wv;
	ncnt = ocnt-1;
	if (ocnt >= SATVAL)
	    ncnt = SATVAL;
	if (ncnt < 0) {
	    char buf[24];
	    shadow_show(smgr, ar, buf);
	    err(true, "Negative ref count for %s", buf, ncnt);
	}
	if (ncnt > 0)
	    keyvalue_insert(reftable, (word_t) ar, ncnt);
	else
	    shadow_deref(smgr, ar);
    }
#if RPT >= 5
    char buf[24];
    shadow_show(smgr, ar, buf);
    report(5, "Ref count for %s: %d --> %d", buf, ocnt, ncnt);
#endif
}

/* Add reference to table.  Makes permanent copy of name */
static void assign_ref(char *name, ref_t r, bool fresh) {
    ref_t rold;
    char *sname;
    /* Add reference to new value */
    root_addref(r, fresh);
    /* (Try to) remove old value */
    word_t wk, wv;
    if (keyvalue_remove(nametable, (word_t) name, &wk, &wv)) {
	sname = (char *) wk;
	rold = (ref_t) wv;
	root_deref(rold);
#if RPT >= 5
	if (verblevel >= 5) {
	    char buf[24];
	    shadow_show(smgr, rold, buf);
	    report(5, "Removed entry %s:%s from name table", name, buf);
	}
#endif
	if (name[0] != '!') {
	    char nname[MAX_CHAR];
	    sprintf(nname, "!%s", name);
	    if (keyvalue_remove(nametable, (word_t) nname, &wk, &wv)) {
		rold = (ref_t) wv;
		root_deref(rold);
#if RPT >= 5
		if (verblevel >= 5) {
		    char buf[24];
		    shadow_show(smgr, rold, buf);
		    report(5, "Removed entry %s:%s from name table", name, buf);
		}
#endif
	    }
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

    // See if have value stored
    if (keyvalue_find(nametable, (word_t) name, &wv)) {
	r = (ref_t) wv;
	return r;
    }
    // See if this is a negated reference
    if (*name == '!') {
	char *pname = name+1;
	// See if have positive value stored
	if (keyvalue_find(nametable, (word_t) pname, &wv)) {
	    ref_t nr = (ref_t) wv;
	    r = shadow_negate(smgr, nr);
	    // Create record of resulting value
	    assign_ref(name, r, false);
	    return r;
	}
    }
    report(0, "Function '%s' undefined", name);
    return REF_INVALID;
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

/* Reduction operations */
typedef ref_t (*combine_fun_t)(shadow_mgr mgr, ref_t aref, ref_t bref);


/* Tree reduction.  Result has incremented reference count */
static ref_t tree_reduce(char *argv[], ref_t unit_ref, combine_fun_t cfun, int arglo, int arghi) {
    ref_t rval = unit_ref;
    if (arghi < arglo) {
	root_addref(rval, false);
    }
    if (arghi == arglo) {
	rval = get_ref(argv[arglo]);
	root_addref(rval, false);
    }
    if (arghi > arglo) {
	int argm = (arglo+arghi)/2;
	ref_t rlo = tree_reduce(argv, unit_ref, cfun, arglo, argm);
	if (REF_IS_INVALID(rlo))
	    rval = rlo;
	else {
	    ref_t rhi = tree_reduce(argv, unit_ref, cfun, argm+1, arghi);
	    if (REF_IS_INVALID(rhi)) {
		root_deref(rlo);
		rval = rhi;
	    } else {
		rval = cfun(smgr, rlo, rhi);
		root_addref(rval, true);
		root_deref(rlo);
		root_deref(rhi);
		/* Check for local garbage collection */
		if (shadow_gc_check(smgr))
		    do_collect(0, NULL);
		/* Initiate any deferred garbage collection */
		if (do_dist)
		    undefer();
	    }
	}
    }
    return rval;
}


/* Use tree reduction.  Enable GC after each operation */
static bool do_reduce(int argc, char *argv[], ref_t unit_ref, combine_fun_t cfun) {
    char buf[24];
    ref_t rval = unit_ref;
    if (argc < 2) {
	report(0, "Need destination name");
	return false;
    }
    rval = tree_reduce(argv, unit_ref, cfun, 2, argc-1);
    if (REF_IS_INVALID(rval))
	return false;
    assign_ref(argv[1], rval, false);
    /* Remove double counting of refs */
    root_deref(rval);
#if RPT >= 1
    shadow_show(smgr, rval, buf);
    report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
    return true;
}


#if OLD_REDUCE
/* Linear reduction.  Returns result with incremented reference count */
static ref_t linear_reduce(char *argv[], ref_t unit_ref, combine_fun_t cfun, int arglo, int arghi) {
    ref_t rval = unit_ref;
    int i;
    root_addref(rval, false);
    for (i = arglo; i <= arghi; i++) {
	ref_t rarg = get_ref(argv[i]);
	if (REF_IS_INVALID(rarg))
	    return rarg;
	ref_t nval = cfun(smgr, rval, rarg);
	root_addref(nval, true);
	root_deref(rval);
	rval = nval;
	/* Check for local garbage collection */
	if (shadow_gc_check(smgr))
	    do_collect(0, NULL);
	/* Initiate any deferred garbage collection */
	if (do_dist)
	    undefer();
    }
    return rval;
}
#endif /* OLD_REDUCE */

#if OLD_REDUCE
/* Use linear reduction.  Enable GC after each operation */
static bool do_reduce_monolithic(int argc, char *argv[], ref_t unit_ref, combine_fun_t cfun) {
    int i;
    char buf[24];
    ref_t rval = unit_ref;
    if (argc < 2) {
	report(0, "Need destination name");
	return false;
    }
    for (i = 2; i < argc; i++) {
	ref_t rarg = get_ref(argv[i]);
	if (REF_IS_INVALID(rarg))
	    return false;
	ref_t nval = cfun(smgr, rval, rarg);
	root_addref(nval, true);
	if (argc > 2)
	    root_deref(rval);
	rval = nval;
	/* Check for local garbage collection */
	if (shadow_gc_check(smgr))
	    do_collect(0, NULL);
	/* Initiate any deferred garbage collection */
	if (do_dist)
	    undefer();
    }
    assign_ref(argv[1], rval, false);
    /* Remove double counting of refs */
    if (argc > 2)
	root_deref(rval);
#if RPT >= 1
    shadow_show(smgr, rval, buf);
    report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
    return true;
}
#endif /* OLD_REDUCE */

#if OLD_REDUCE
/* This version waited until the very end to enable GC */
static bool do_reduce_old(int argc, char *argv[], ref_t unit_ref, combine_fun_t cfun) {
    int i;
    char buf[24];
    ref_t rval = unit_ref;
    if (argc < 2) {
	report(0, "Need destination name");
	return false;
    }
    for (i = 2; i < argc; i++) {
	ref_t rarg = get_ref(argv[i]);
	if (REF_IS_INVALID(rarg))
	    return false;
	ref_t nval = cfun(smgr, rval, rarg);
	rval = nval;
    }
    assign_ref(argv[1], rval, false);
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
#if RPT >= 1
    shadow_show(smgr, rval, buf);
    report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
    return true;
}
#endif /* OLD_REDUCE */

bool do_and(int argc, char *argv[]) {
    return do_reduce(argc, argv, shadow_one(smgr), shadow_and);
}

bool do_or(int argc, char *argv[]) {
    return do_reduce(argc, argv, shadow_zero(smgr), shadow_or);
}

bool do_xor(int argc, char *argv[]) {
    return do_reduce(argc, argv, shadow_zero(smgr), shadow_xor);
}

bool do_not(int argc, char *argv[]) {
    char buf[24];
    if (argc != 3) {
	report(0, "Not requires 1 argument");
	return false;
    }
    ref_t rf = get_ref(argv[2]);
    if (do_ref(smgr) && REF_IS_INVALID(rf))
	return false;
    ref_t rval = shadow_negate(smgr, rf);
    if (do_ref(smgr) && REF_IS_INVALID(rval))
	return false;
    assign_ref(argv[1], rval, false);
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
#if RPT >= 2
    shadow_show(smgr, rval, buf);
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
    if (REF_IS_INVALID(ri) || REF_IS_INVALID(rt) || REF_IS_INVALID(re))
	return false;
    ref_t rval = shadow_ite(smgr, ri, rt, re);
    if (do_ref(smgr) && REF_IS_INVALID(rval))
	return false;
    assign_ref(argv[1], rval, false);
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
#if RPT >= 2
    shadow_show(smgr, rval, buf);
    report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
    return true;
}

bool do_collect(int argc, char *argv[]) {
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
	keyvalue_iterstart(reftable);
	while (keyvalue_iternext(reftable, &wk, &wv)) {
	    ref_t r = (ref_t) wk;
	    set_insert(roots, (word_t) r);
	}
	ref_collect(smgr->ref_mgr, roots);
	set_free(roots);
    }
    if (smgr->do_cudd) {
	int result = cudd_collect(smgr);
	report(1, "%d nodes collected by Cudd", result);
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
#if RPT >= 5
	    if (verblevel >= 5) {
		char buf[24];
		shadow_show(smgr, rold, buf);
		report(5, "Removed entry %s:%s from name table", olds, buf);
	    }
#endif
	    root_deref(rold);
	    free_string(olds);
	} else {
	    report(0, "Function '%s' not found", argv[i]);
	    return false;
	}
    }
    return true;
}

bool do_restrict(int argc, char *argv[]) {
    char buf[24];
    if (argc != 4) {
	report(0, "Restrict requires 2 arguments");
	return false;
    }
    ref_t rf = get_ref(argv[2]);
    if (do_ref(smgr) && REF_IS_INVALID(rf))
	return false;
    ref_t rc = get_ref(argv[3]);
    if (do_ref(smgr) && REF_IS_INVALID(rc))
	return false;
    ref_t rval = shadow_cm_restrict(smgr, rf, rc);
    if (do_ref(smgr) && REF_IS_INVALID(rval))
	return false;
    assign_ref(argv[1], rval, false);
    /* Check for local garbage collection */
    if (shadow_gc_check(smgr))
	do_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
#if RPT >= 2
    shadow_show(smgr, rval, buf);
    report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
    return true;
}


bool do_simplify(int argc, char *argv[]) {
    size_t ocnt = 0, ncnt = 0;
    // Minimum is simplify fnew fold
    if (argc < 3) {
	report(0, "simplify needs at least 2 arguments");
	return false;
    }

    // Correctness checking
    // And of fold f1 f2 ...
    ref_t prod_init = tree_reduce(argv, shadow_one(smgr), shadow_and, 2, argc-1);

    ref_t fun = get_ref(argv[2]);
    ocnt = cudd_single_size(smgr, fun);

    int i;
    rset *set = rset_new();
    for (i = 3; i < argc; i++) {
	ref_t rarg = get_ref(argv[i]);
	if (REF_IS_INVALID(rarg)) {
	    rset_free(set);
	    return false;
	}
	rset_add_term(set, rarg);
    }

    ref_t rval = simplify_with_rset(fun, set);
    if (REF_IS_INVALID(rval)) {
	return false;
    }
    // Assign to fnew
    assign_ref(argv[1], rval, false);

    // Correctness checking
    // And of f1 f2 ...
    ref_t partial_prod_final = tree_reduce(argv, shadow_one(smgr), shadow_and, 3, argc-1);
    // Combine with fnew
    ref_t prod_final = shadow_and(smgr, partial_prod_final, rval);
    root_deref(partial_prod_final);

    if (prod_init != prod_final) {
	char init_buf[24], final_buf[24];
	shadow_show(smgr, prod_init, init_buf);
	shadow_show(smgr, prod_final, final_buf);
	report(0, "WARNING: Simplification did not preserve product %s --> %s", init_buf, final_buf);
    }
    root_deref(prod_init);
    root_deref(prod_final);

    if (smgr->do_cudd) {
	ncnt = cudd_single_size(smgr, rval);
	double ratio = (double) ocnt/ncnt;
	report(1, "Simplify of %s: Size %lu --> %lu (%.2fX reduction)", argv[1], ocnt, ncnt, ratio);
    }
    root_deref(rval);
    return true;
}

bool do_conjunct(int argc, char *argv[]) {
    if (argc < 2) {
	report(0, "Need destination name");
	return false;
    }

    int i;
    rset *set = rset_new();
    for (i = 2; i < argc; i++) {
	ref_t rarg = get_ref(argv[i]);
	if (REF_IS_INVALID(rarg)) {
	    rset_free(set);
	    return rarg;
	}
	rset_add_term(set, rarg);
    }

    ref_t rval = rset_conjunct(set);
    if (REF_IS_INVALID(rval)) {
	return false;
    }
    assign_ref(argv[1], rval, false);
    root_deref(rval);
    rset_free(set);
    return true;
}

bool do_zconvert(int argc, char *argv[]) {
    char bufold[24], bufnew[24];
    if (argc != 3) {
	report(0, "zconvert requires 2 arguments");
	return false;
    }
    ref_t rold, rnew;
    word_t wv;
    if (keyvalue_find(nametable, (word_t) argv[2], &wv)) {
	rold = (ref_t) wv;
	if (strcmp(argv[1], argv[2]) == 0) {
	    word_t wk;
	    keyvalue_remove(nametable, (word_t) argv[2], &wk, &wv);
	    char *olds = (char *) wk;
#if RPT >= 5
	    if (verblevel >= 5) {
		char buf[24];
		shadow_show(smgr, rold, buf);
		report(5, "Removed entry %s:%s from name table", olds, buf);
	    }
#endif
	    root_deref(rold);
	    free_string(olds);
	}
	rnew = shadow_zconvert(smgr, rold);
	assign_ref(argv[1], rnew, false);
#if RPT >= 2
	shadow_show(smgr, rold, bufold);
	shadow_show(smgr, rnew, bufnew);
	report(2, "%s: %s --> %s: %s", argv[2], bufold, argv[1], bufnew);
#endif
	return true;
    } else {
	report(0, "Function %s not found", argv[2]);
	return false;
    }
}

bool do_aconvert(int argc, char *argv[]) {
    char bufold[24], bufnew[24];
    if (argc != 3) {
	report(0, "aconvert requires 2 arguments");
	return false;
    }
    ref_t rold, rnew;
    word_t wv;
    if (keyvalue_find(nametable, (word_t) argv[2], &wv)) {
	rold = (ref_t) wv;
	if (strcmp(argv[1], argv[2]) == 0) {
	    word_t wk;
	    keyvalue_remove(nametable, (word_t) argv[2], &wk, &wv);
	    char *olds = (char *) wk;
#if RPT >= 5
	    if (verblevel >= 5) {
		char buf[24];
		shadow_show(smgr, rold, buf);
		report(5, "Removed entry %s:%s from name table", olds, buf);
	    }
#endif
	    root_deref(rold);
	    free_string(olds);
	}
	rnew = shadow_aconvert(smgr, rold);
	assign_ref(argv[1], rnew, false);
#if RPT >= 2
	shadow_show(smgr, rold, bufold);
	shadow_show(smgr, rnew, bufnew);
	report(2, "%s: %s --> %s: %s", argv[2], bufold, argv[1], bufnew);
#endif
	return true;
    } else {
	report(0, "Function %s not found", argv[2]);
	return false;
    }
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
    if (argc < 3) {
	report(0, "Require at least two arguments to cofactor");
	return false;
    }
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
	do_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    keyvalue_free(map);
   return ok;
}

bool do_equant(int argc, char *argv[]) {
    char buf[24];
    if (argc < 3) {
	report(0, "Require at least two arguments to equant");
	return false;
    }
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
	do_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    keyvalue_free(map);
    return ok;
}

bool do_uquant(int argc, char *argv[]) {
    char buf[24];
    if (argc < 3) {
	report(0, "Require at least two arguments to uquant");
	return false;
    }
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
	do_collect(0, NULL);
    /* Initiate any deferred garbage collection */
    if (do_dist)
	undefer();
    keyvalue_free(map);
    return ok;
}


bool do_satisfy(int argc, char *argv[]) {
    size_t i;
    for (i = 1; i < argc; i++) {
	ref_t r;
	word_t wv;
	if (keyvalue_find(nametable, (word_t) argv[i], &wv)) {
	    r = (ref_t) wv;
	    report(1, "%s:", argv[i]);
	    shadow_satisfy(smgr, r);
	} else {
	    report(1, "%s: NOT FOUND", argv[i]);
	}
    }
    return true;
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
	do_collect(0, NULL);
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
	report(0, "  Ref size: %lu nodes", rset->nelements);
	set_free(rset);
    }
    if (smgr->do_cudd) {
	size_t cnt = cudd_set_size(smgr, roots);
	report(0, "  Cudd size: %lu nodes", cnt);
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
	assign_ref(argv[i], rv, false);
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
	FILE *logfile = get_logfile();
	if (logfile)
	    Cudd_PrintInfo(smgr->bdd_manager, logfile);
    }
    return true;
}
