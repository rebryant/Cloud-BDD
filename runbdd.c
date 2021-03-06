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

/* Should combining be done linearly or as a tree */
int tree_reduction = 0;


/* Data structures */
shadow_mgr smgr;

/* Mapping from string names to shadow pointers */
/* All names */
keyvalue_table_ptr nametable;
/* Mapping from refs to variables names.  These are included in reference count */
/* Have separate copy of strings */
keyvalue_table_ptr inverse_varnametable;

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
bool do_delete(int argc, char *argv[]);
bool do_cofactor(int argc, char *argv[]);
bool do_count(int argc, char *argv[]);
bool do_equal(int argc, char *argv[]);
bool do_equant(int argc, char *argv[]);
bool do_load(int argc, char *argv[]);
bool do_local_flush(int argc, char *argv[]);
bool do_information(int argc, char *argv[]);
bool do_ite(int argc, char *argv[]);
bool do_or(int argc, char *argv[]);
bool do_nothing(int argc, char *argv[]);
bool do_not(int argc, char *argv[]);
bool do_restrict(int argc, char *argv[]);
bool do_satisfy(int argc, char *argv[]);
bool do_shift(int argc, char *argv[]);
bool do_size(int argc, char *argv[]);
bool do_soft_and(int argc, char *argv[]);
bool do_status(int argc, char *argv[]);
bool do_store(int argc, char *argv[]);
bool do_uquant(int argc, char *argv[]);
bool do_var(int argc, char *argv[]);
bool do_vector(int argc, char *argv[]);
bool do_xor(int argc, char *argv[]);
bool do_zconvert(int argc, char *argv[]);

chunk_ptr run_flush();

void root_deref(ref_t r);
ref_t get_ref(char *name);
static set_ptr get_refs(int cnt, char *names[]);

static void client_gc_start();
static void client_gc_finish();


static void bdd_init() {
    smgr = new_shadow_mgr(do_cudd, do_local, do_dist, chaining_type);
    nametable = keyvalue_new(string_hash, string_equal);
    inverse_varnametable = keyvalue_new(word_hash, word_equal);
    reftable = word_keyvalue_new();
    ref_t rzero = shadow_zero(smgr);
    ref_t rone = shadow_one(smgr);
    assign_ref("zero", rzero, false, false);
    assign_ref("one", rone, false, false);
    set_gc_handlers(client_gc_start, client_gc_finish);
}

static void console_init(bool do_dist) {
    add_cmd("aconvert", do_aconvert,
	    " af f ...       | Convert f to ADD and name af");
    add_cmd("and", do_and,
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
	    " fd f c         | fd <- Restrict(f,c) [Coudert/Madre's restriction operation]");
    add_cmd("softand", do_soft_and,
	    " fd f g         | fd <- SoftAnd(f,g) [Partial And operation]");
    add_cmd("zconvert", do_zconvert,
	    " zf f           | Convert f to ZDD and name zf");
    add_cmd("store", do_store,
	    " f file         | Store f in file");
    add_cmd("load", do_load,
	    " f file         | Load f from file");
    add_param("collect", &enable_collect, "Enable garbage collection", NULL);
    add_param("allvars", &all_vars, "Count all variables in support", NULL);
    add_param("tree", &tree_reduction, "Do reduction operations as tree", NULL);
    init_conjunct();
}

static bool bdd_quit(int argc, char *argv[]) {
    word_t wk, wv;

    while (keyvalue_removenext(nametable, &wk, &wv)) {
	char *s = (char *) wk;
	ref_t rold = (ref_t) wv;
	report(5, "Removing function %s from name table", s);
	root_deref(rold);
#if RPT >= 5
	report(5, "Freeing string '%s' from name table", s);
#endif
	free_string(s);
    }
    keyvalue_free(nametable);

    while (keyvalue_removenext(inverse_varnametable, &wk, &wv)) {
	ref_t rold = (ref_t) wk;
	char *s = (char *) wv;
	report(5, "Removing function %s from name table", s);
	root_deref(rold);
#if RPT >= 5
	report(5, "Freeing string '%s' from name table", s);
#endif
	free_string(s);
    }
    keyvalue_free(inverse_varnametable);

    if (reftable->nelements > 0) {
	report(2, "Still have references to %zd functions", reftable->nelements);
	if (verblevel >= 2) {
	    char buf[24];
	    word_t wk, wv;
	    size_t sum = 0;
	    keyvalue_iterstart(reftable);
	    while (keyvalue_iternext(reftable, &wk, &wv)) {
		ref_t r = (ref_t) wk;
		size_t rcount = (size_t) wv;
		sum += rcount;
		shadow_show(smgr, r, buf);
		report(4, "Ref %s.  Count = %zd", buf, rcount);
	    }
	    report(2, "Total remaining references = %zd", sum);
	}
    } else {
	report(2, "Reference table empty");
    }
    keyvalue_free(reftable);
    if (do_ref(smgr)) {
	ref_show_stat(smgr->ref_mgr);
    }
    free_shadow_mgr(smgr);
    return true;
}

static void usage(char *cmd) {
    printf(
"Usage: %s [-h] [-f FILE][-v VLEVEL] [-M MBYTES] [-c][-l][-d][-H HOST] [-P PORT][-r][-L FILE][-t LIMIT][-C chain][-K LOOKUP][-G GEN][-g][-p][-q QTHRES][-T]\n",
	   cmd);
    printf("\t-h         Print this information\n");
    printf("\t-f FILE    Read commands from file\n");
    printf("\t-v VLEVEL  Set verbosity level\n");
    printf("\t-M MBYTES  Set memory limit to MBYTES megabytes\n");
    printf("\t-L FILE    Echo results to FILE\n");
    printf("\t-t LIMIT   Set time limit (in seconds)\n");
    printf("\t-C CHAIN   n: No chaining; c: constant chaining; a: Or chaining, z: Zero chaining\n");
    printf("\t-K LOOKUP  Limit cache lookups during (soft) and (ratio wrt argument sizes, scaled by 100)\n");
    printf("\t-G GEN     Limit nodes generated during soft and (ratio wrt argument size, scaled by 100)\n");
    printf("\t-g         Allow growth from soft-and simplification\n");
    printf("\t-p         Preprocess conjuncts with soft-and simplification\n");
    printf("\t-q QTHRES  Set BDD size threshold at which attempt existential quantification of conjuncts\n");    
    printf("\t-T         Generate tracking information on conjunctions\n");
    printf("Distributed BDD options\n");
    printf("\t-c         Use CUDD\n");
    printf("\t-l         Use local refs\n");
    printf("\t-d         Use distributed refs\n");
    printf("\t-H HOST    Use HOST as controller host\n");
    printf("\t-P PORT    Use PORT as controller port\n");
    printf("\t-r         Try to use local router\n");
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
    
    do_cudd = 1;
    do_local = 0;
    do_dist = 0;
    chaining_type = CHAIN_ALL;


    while ((c = getopt(argc, argv, "hv:M:f:cldH:P:rL:t:C:R:K:G:gpq:T")) != -1) {
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
	case 'M':
	    mblimit = atoi(optarg);
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
	case 'K':
	    cache_hard_lookup_ratio = atoi(optarg);
	    cache_soft_lookup_ratio = cache_hard_lookup_ratio;
	    break;
	case 'G':
	    soft_and_expansion_ratio_scaled = atoi(optarg);
	    break;
	case 'g':
	    soft_and_allow_growth = 1;
	    break;
	case 'p':
	    preprocess_conjuncts = 1;
	    break;
	case 'q':
	    quantify_threshold = atoi(optarg);
	    break;
	case 'T':
	    track_conjunction = 1;
	    break;
	default:
	    printf("Unknown option '%c'\n", c);
	    usage(argv[0]);
	    break;
	}
    }
    set_verblevel(level);
    if (logfile_name)
	set_logfile(logfile_name);
    bdd_init();
    init_cmd();
    if (do_dist) {
	init_agent(true, hbuf, port, true, try_local_router);
	set_agent_flush_helper(run_flush);
	set_agent_stat_helper(do_summary_stat);
    }
    console_init(do_dist);
    show_options(1);
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
/* I.e., was it created by an operation that called dd_reference? */
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

/* Make sure have nonzero reference count for ref */
void root_checkref(ref_t r) {
    if (REF_IS_INVALID(r))
	return;
    ref_t ar = shadow_absval(smgr, r);
    word_t wv;
    if (!keyvalue_find(reftable, (word_t) ar, &wv)) {
	char buf[24];
	shadow_show(smgr, ar, buf);
	err(true, "Attempt to reference nonexistent entry %s", buf);
    }
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
	    err(true, "Negative ref count for %s (%d)", buf, ncnt);
	}
	if (ncnt > 0)
	    keyvalue_insert(reftable, (word_t) ar, ncnt);
	else
	    shadow_deref(smgr, ar);
    } else {
	char buf[24];
	shadow_show(smgr, ar, buf);
	err(true, "Attempt to dereference nonexistent entry %s", buf);
    }
#if RPT >= 5
    char buf[24];
    shadow_show(smgr, ar, buf);
    report(5, "Ref count for %s: %d --> %d", buf, ocnt, ncnt);
#endif
}

/* Add reference to table.  Makes permanent copy of name */
void assign_ref(char *name, ref_t r, bool fresh, bool variable) {
    ref_t rold;
    char *sname;
    /* Add reference to new value */
    root_addref(r, fresh);
    /* (Try to) remove old value */
    word_t wk, wv;
    /* See if name already defined */
    if (keyvalue_find(nametable, (word_t) name, &wv)) {
	word_t ws;
	if (keyvalue_find(inverse_varnametable, wv, &ws)) {
	    /* See if names match */
	    char *found_name = (char *) ws;
	    if (strcmp(found_name, name) == 0) {
		err(false, "Attempt to redefine variable %s.  Ignored", name);
		return;
	    }
	}
    }
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
    if (variable) {
	/* This counts as a reference, but it's no longer fresh */
	root_addref(r, false);
	sname = strsave_or_fail(name, "assign_ref");
	keyvalue_insert(inverse_varnametable, (word_t) r, (word_t) sname);
    }
    if (verblevel >= 5) {
	char buf[24];
	shadow_show(smgr, r, buf);
#if RPT >= 5
	if (variable)
	    report(5, "Added %s:%s to name and variable table", name, buf);
	else
	    report(5, "Added %s:%s to name table", name, buf);
#endif
    }
}


/* Command implementations */
/* Retrieve reference, given its name. */
/* Can prefix name with '!' to indicate negation */
ref_t get_ref(char *name) {
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
	    assign_ref(name, r, true, false);
	    return r;
	}
    }
    err(false, "Function '%s' undefined", name);
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

/* Do inverse lookup of variable name */
static char *name_find(ref_t r) {
    word_t ws;
    if (keyvalue_find(inverse_varnametable, (word_t) r, &ws)) {
	char *name = (char *) ws;
	return name;
    }
    char buf[24];
    shadow_show(smgr, r, buf);
    return strsave_or_fail(buf, "name_find");
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
		root_checkref(rlo);
		root_checkref(rhi);
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

static ref_t linear_reduce(char *argv[], ref_t unit_ref, combine_fun_t  cfun, int arglo, int arghi) {
    if (arghi < arglo) {
	root_addref(unit_ref, false);
	return unit_ref;
    }
    int i;
    ref_t rval = get_ref(argv[arglo]);
    if (REF_IS_INVALID(rval))
	return rval;
    root_addref(rval, false);
    for (i = arglo+1; i <= arghi; i++) {
	ref_t aval = get_ref(argv[i]);
	root_addref(aval, false);
	if (REF_IS_INVALID(aval)) {
	    root_deref(rval);
	    return aval;
	} else {
	    root_checkref(aval);
	    ref_t nval = cfun(smgr, rval, aval);
	    root_addref(nval, true);
	    root_deref(aval);
	    root_deref(rval);
	    rval = nval;
	}
	/* Check for local garbage collection */
	if (shadow_gc_check(smgr))
	    do_collect(0, NULL);
	/* Initiate any deferred garbage collection */
	if (do_dist)
	    undefer();
    }
    return rval;

}

/* Perform reduction. */
static bool do_reduce(int argc, char *argv[], ref_t unit_ref, combine_fun_t cfun) {
    char buf[24];
    ref_t rval = unit_ref;
    if (argc < 2) {
	report(0, "Need destination name");
	return false;
    }
    rval = tree_reduction ? tree_reduce(argv, unit_ref, cfun, 2, argc-1) : 
	linear_reduce(argv, unit_ref, cfun, 2, argc-1);
    if (REF_IS_INVALID(rval))
	return false;
    assign_ref(argv[1], rval, false, false);
    /* Remove double counting of refs */
    root_deref(rval);
#if RPT >= 1
    shadow_show(smgr, rval, buf);
    report(2, "RESULT.  %s = %s", argv[1], buf);
#endif
    return true;
}

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
    assign_ref(argv[1], rval, true, false);
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
    assign_ref(argv[1], rval, true, false);
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
	char nname[MAX_CHAR];
	sprintf(nname, "!%s", argv[i]);
	if (keyvalue_remove(nametable, (word_t) nname, &wk, &wv)) {
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
    assign_ref(argv[1], rval, true, false);
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

bool do_soft_and(int argc, char *argv[]) {
    char buf[24];
    if (argc != 4) {
	report(0, "Soft And requires 2 arguments");
	return false;
    }
    ref_t rf = get_ref(argv[2]);
    if (do_ref(smgr) && REF_IS_INVALID(rf))
	return false;
    ref_t rc = get_ref(argv[3]);
    if (do_ref(smgr) && REF_IS_INVALID(rc))
	return false;
    ref_t rval = shadow_soft_and(smgr, rf, rc, 0, 0);
    if (do_ref(smgr) && REF_IS_INVALID(rval))
	return false;
    assign_ref(argv[1], rval, true, false);
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
	assign_ref(argv[1], rnew, true, false);
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
	assign_ref(argv[1], rnew, true, false);
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
	    root_deref(r);
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
	assign_ref(argv[1], rnew, true, false);
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
	assign_ref(argv[1], rnew, true, false);
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
	assign_ref(argv[1], rnew, true, false);
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
	assign_ref(argv[1], rnew, false, false);
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
    if (verblevel >= 2) {
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
	    shadow_deref(smgr, r);
	}
	report(0, "");
	set_free(supset);
    }
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
	assign_ref(argv[i], rv, true, true);
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
    shadow_status(smgr);
    return true;
}

bool do_load(int argc, char *argv[]) {
    if (argc != 3) {
	report(0, "load requires two arguments");
	return false;
    }
    FILE *infile = fopen(argv[2], "r");
    if (infile == NULL) {
	report(0, "Couldn't open DD file '%s'", argv[2]);
	return false;
    }
    ref_t r = shadow_load(smgr, infile);
    fclose(infile);
    if (REF_IS_INVALID(r)) {
	report(0, "Load failed");
	return false;
    }
    assign_ref(argv[1], r, true, false);
#if RPT >= 1
    {
	char buf[24];
	shadow_show(smgr, r, buf);
	report(2, "RESULT.  %s = %s", argv[1], buf);
    }
#endif
    return true;
}

bool do_store(int argc, char *argv[]) {
    bool ok = true;
    if (argc != 3) {
	report(0, "store requires two arguments");
	return false;
    }
    FILE *outfile = fopen(argv[2], "w");
    if (outfile == NULL) {
	report(0, "Couldn't open DD file '%s'", argv[2]);
	return false;
    }
    ref_t r = get_ref(argv[1]);
    if (REF_IS_INVALID(r))
	return false;

    if (!shadow_store(smgr, r, outfile)) {
	report(0, "Store failed");
	ok = false;
    }
    fclose(outfile);
    return ok;
}

