#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdbool.h>

#ifdef QUEUE
#include <pthread.h>
#endif

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

#ifndef CUDD_VERSION
#define CUDD_VERSION "3.1.0"
#endif

/* Debugging help */
static DdNode *dd_check(DdNode *f)  {
    if (f) {
	Cudd_Ref(f);
	Cudd_Deref(f);
    }
    return f;
}

/* From cuddInt.h */
extern int cuddGarbageCollect (DdManager *unique, int clearCache);


/* Trick:
   When running CUDD only, use DdNode * value serve as an artificial ref.
   When not running CUDD, use refs as DdNode *values
*/

/* Reference counting
   When DD has been recorded through add_ref, then its CUDD node is counted as a single reference no matter how often it gets used.
*/

/* Attempts to use CUDD's APA package failed.  Disable */
#define USE_APA 0

/* Support BDDs, ZDDs, and ADDs with Cudd */
typedef enum { IS_BDD, IS_ZDD, IS_ADD } dd_type_t;

bool fatal = false;

/* GC statistics */
unsigned int gc_count = 0;
long gc_milliseconds = 0;


bool do_ref(shadow_mgr mgr) {
    return mgr->do_local || mgr->do_dist;
}

/* When using only CUDD, ref is copy of DdNode for BDD, DdNode +2 for ZDD, +3 for ADD */
static ref_t dd2ref(DdNode *n, dd_type_t dtype) {
    if (n == NULL)
	return REF_INVALID;
    ref_t r = (ref_t) n;
    if (dtype == IS_ZDD)
	r += 2;
    else if (dtype == IS_ADD)
	r += 3;
    return r;
}

static dd_type_t find_type(shadow_mgr mgr, ref_t r) {
    if (do_ref(mgr))
	return IS_BDD;
    int suffix = r & 0x3;
    switch (suffix) {
    case 0:
    case 1:
	return IS_BDD;
    case 2:
	return IS_ZDD;
    case 3:
	return IS_ADD;
    }
    return IS_BDD;
}

static bool is_zdd(shadow_mgr mgr, ref_t r) {
    return find_type(mgr, r) == IS_ZDD;
}

static bool is_add(shadow_mgr mgr, ref_t r) {
    return find_type(mgr, r) == IS_ADD;
}

static DdNode *ref2dd(shadow_mgr mgr, ref_t r) {
    dd_type_t dtype = find_type(mgr, r);
    switch (dtype) {
    case IS_BDD:
	break;
    case IS_ZDD:
	r -= 2;
	break;
    case IS_ADD:
	r -= 3;
	break;
    default:
	err(false, "Internal DD error.  No DD type %d", dtype);
    }
    return (DdNode *) r;
}

static void reference_dd(shadow_mgr mgr, DdNode *n) {
    if (!mgr->do_cudd)
	return;
    if (!n) {
	err(fatal, "Attempt to reference NULL DdNode");
	return;
    }
    Cudd_Ref(n);
    report(5, "Called Cudd_Ref on node %p", n);
}

static void unreference_dd(shadow_mgr mgr, DdNode *n, dd_type_t dtype) {
    if (!mgr->do_cudd)
	return;
    if (dtype == IS_ZDD) {
	Cudd_RecursiveDerefZdd(mgr->bdd_manager, n);
	report(5, "Called Cudd_RecursiveDrefZdd on node %p", n);
    } else {
	Cudd_RecursiveDeref(mgr->bdd_manager, n);
	report(5, "Called Cudd_RecursiveDeref on node %p", n);
    }
}

/* ZDD support */
/* Convert function to ZDD.  This should only be done after all BDD variables have been declared */
static DdNode *zconvert(shadow_mgr mgr, DdNode *n) {
    if (mgr->nzvars < mgr->nvars) {
	Cudd_zddVarsFromBddVars(mgr->bdd_manager, 1);
	mgr->nzvars = mgr->nvars;
    }
    DdNode *zn = Cudd_zddPortFromBdd(mgr->bdd_manager, n);
    reference_dd(mgr, zn);
    return zn;
}

/* Convert function to ADD.  This should only be done after all BDD variables have been declared */
static DdNode *aconvert(shadow_mgr mgr, DdNode *n) {
    DdNode *an = Cudd_BddToAdd(mgr->bdd_manager, n);
    reference_dd(mgr, an);
    return an;
}

void shadow_show(shadow_mgr mgr, ref_t r, char *buf) {
    if (do_ref(mgr)) {
	ref_show(r, buf);
    } else {
	dd_type_t dtype = find_type(mgr, r);
	DdNode *n = ref2dd(mgr, r);
	sprintf(buf, "%c%p", dtype == IS_ZDD ? 'Z' : dtype == IS_ADD ? 'A' : 'B', n);
    }		
}

/* General conversion from ref to dd */
static DdNode *get_ddnode(shadow_mgr mgr, ref_t r) {
    DdNode *n;
    if (!do_ref(mgr)) {
	n = ref2dd(mgr, r);
    } else if (REF_IS_INVALID(r)) {
	err(fatal, "No node associated with invalid ref");
	n = Cudd_ReadLogicZero(mgr->bdd_manager);
	reference_dd(mgr, n);
    } else if (!keyvalue_find(mgr->r2c_table, (word_t ) r, (word_t *) &n)) {
	char buf[24];
	shadow_show(mgr, r, buf);
	err(fatal, "No node associated with ref %s (0x%llx)", buf, r);
	n = mgr->do_cudd ? Cudd_ReadLogicZero(mgr->bdd_manager) :
	    (DdNode *) REF_ZERO;
	reference_dd(mgr, n);
    }
    return n;
}

/* Associate n with reference r.  For non-Zdd/Add's, also cross reference complements.
   Incoming n has recorded reference count.
   Create single reference to node.  For non-ZDD/ADD, create single reference to its complement
*/
static void add_ref(shadow_mgr mgr, ref_t r, DdNode *n) {
    ref_t rother;
    DdNode *nother;
    if (do_ref(mgr) && REF_IS_INVALID(r)) {
	/* Don't record matches with invalid refs */
	return;
    }

    if (!do_ref(mgr))
	return;

    dd_type_t dtype = find_type(mgr, r);

    bool rother_found = keyvalue_find(mgr->c2r_table, (word_t ) n,
				      (word_t *) &rother);
    bool nother_found = keyvalue_find(mgr->r2c_table, (word_t ) r,
				      (word_t *) &nother);
    char buf1[24];
    char buf2[24];
    /* See if already found entry */
    if (rother_found) {
	if (nother_found) {
	    /* Already exists.  Check if match */
	    if (nother == n && rother == r) {
		// Remove reference 
		unreference_dd(mgr, n, dtype);
	    } else {
		shadow_show(mgr, r, buf1);
		shadow_show(mgr, rother, buf2);
		err(fatal,
"Inconsistency.  New node 0x%p.  Old node 0x%p.  New ref %s.  Old ref %s",
		    n, nother, buf1, buf2);
	    }
	} else {
	    if (find_type(mgr, rother) == dtype) {
		shadow_show(mgr, r, buf1);
		shadow_show(mgr, rother, buf2);
		err(fatal, "Ref Collision.  Refs %s and %s map to BDD node %p",
		    buf1, buf2, n);
	    }
	}
    } else {
	if (nother_found) {
	    shadow_show(mgr, r, buf1);
	    err(fatal, "Node collision.  Nodes 0x%p and 0x%p map to ref %s\n",
		n, nother, buf1);
	} else {
	    /* Normal case.  Create both entries */
#if RPT >= 5
	    char buf[24];
	    shadow_show(mgr, r, buf);
	    report(5, "Added ref %s for node %p", buf, n);
#endif
	    keyvalue_insert(mgr->c2r_table, (word_t ) n, (word_t ) r);
	    keyvalue_insert(mgr->r2c_table, (word_t ) r, (word_t ) n);
	    if (dtype == IS_BDD) {
		/* Create entries for negations */
		ref_t rn = shadow_negate(mgr, r);  // Creates reference
		DdNode *nn;
		if (mgr->do_cudd) {
		    nn = Cudd_Not(n);
		} else {
		    nn = ref2dd(mgr, rn);
		}
#if RPT >= 5
		shadow_show(mgr, rn, buf);
		report(5, "Added ref %s for node %p", buf, nn);
#endif
		keyvalue_insert(mgr->c2r_table, (word_t ) nn, (word_t ) rn);
		keyvalue_insert(mgr->r2c_table, (word_t ) rn, (word_t ) nn);
	    }
	}
    }
}

/* Compare ref created locally with one created by distributed computation */
static bool check_refs(shadow_mgr mgr, ref_t rlocal, ref_t rdist) {
    if (rlocal != rdist) {
	char buflocal[24], bufdist[24];
	shadow_show(mgr, rlocal, buflocal);
	shadow_show(mgr, rdist, bufdist);
	err(false, "Mismatched refs.  Local = %s, Dist = %s", buflocal, bufdist);
	return false;
    } else {
#if RPT >= 4
	char buflocal[24];
	shadow_show(mgr, rlocal, buflocal);
	report(4, "Matching refs.  Local = Dist = %s", buflocal);
#endif
    }
    return true;
}

int pre_gc_hook(DdManager *mgr, const char *str, void * ptr) {
    gc_count = Cudd_ReadGarbageCollections(mgr) + 1;
    report(4, "Garbage collection #%d started", gc_count);
    return 1;
}

int post_gc_hook(DdManager *mgr, const char *str, void * ptr) {
    long new_gc_milliseconds = Cudd_ReadGarbageCollectionTime(mgr);
    double delta = 1e-3 * (new_gc_milliseconds - gc_milliseconds);
    gc_milliseconds = new_gc_milliseconds;
    report(2, "Garbage collection #%d completed in %.3f seconds", delta, gc_count);
    return 1;
}


shadow_mgr new_shadow_mgr(bool do_cudd, bool do_local, bool do_dist, chaining_t chaining) {
    if (!(do_cudd || do_local || do_dist)) {
	err(true, "Must have at least one active evaluation mode");
    }
    shadow_mgr mgr = (shadow_mgr) malloc_or_fail(sizeof(shadow_ele),
						 "new_shadow_mgr");
    mgr->do_cudd = do_cudd;
    mgr->do_local = do_local;
    mgr->do_dist = do_dist;

    mgr->c2r_table = word_keyvalue_new();
    mgr->r2c_table = word_keyvalue_new();
    mgr->nvars = 0;
    mgr->nzvars = 0;

    ref_t r = REF_ZERO;
    DdNode *n = NULL;

    if (do_cudd) {
	/* CUDD Parameters */
	unsigned int numVars = 0; /* Default 0 */
	unsigned int numVarsZ = 0; /* Default 0 */
#if 0
	/* Default Parameters */
	unsigned int numSlots = 256;
	unsigned int cacheSize = 262144;
	unsigned int maxMemory = 67108864;
#endif
	/* Modified Parameters */
	unsigned int numSlots = 1u<<18; /* Default 256 */
	unsigned int cacheSize = 1u<<22; /* Default 262144 */
	unsigned long int maxMemory = (1u<<31) + 32UL * 1024 * 1024 * 1024;
	if (mblimit > 0) {
	    unsigned long int newLimit = mblimit * 1024L * 1024L;
	    double mscale = (double) newLimit / maxMemory;
	    maxMemory = newLimit;
	    numSlots = (unsigned int) (mscale * numSlots);
	    cacheSize = (unsigned int) (mscale * cacheSize);
	}
	report(1, "Setting memory limit to %.2f MB.  numSlots to %d.  cacheSize to %d", (double) maxMemory / 1e6, numSlots, cacheSize);
	mgr->bdd_manager = Cudd_Init(numVars, numVarsZ, numSlots, cacheSize, maxMemory);
	Cudd_AutodynDisable(mgr->bdd_manager);
	Cudd_AutodynDisableZdd(mgr->bdd_manager);
	Cudd_AddHook(mgr->bdd_manager, pre_gc_hook, CUDD_PRE_GC_HOOK);
	Cudd_AddHook(mgr->bdd_manager, post_gc_hook, CUDD_POST_GC_HOOK);
#ifndef NO_CHAINING
	Cudd_ChainingType ct = CUDD_CHAIN_NONE;
	switch (chaining) {
	case CHAIN_NONE:
	    ct = CUDD_CHAIN_NONE;
	    report(0, "No chaining enabled");
	    break;
	case CHAIN_CONSTANT:
	    ct = CUDD_CHAIN_CONSTANT;
	    report(0, "Constant chaining enabled");
	    break;
	case CHAIN_ALL:
	    ct = CUDD_CHAIN_ALL;
	    report(0, "Or chaining enabled");
	    break;
	default:
	    err(true, "Invalid chaining mode %d\n", chaining);
	}
	Cudd_SetChaining(mgr->bdd_manager, ct);
#endif	
	n = Cudd_ReadLogicZero(mgr->bdd_manager);
	report(0, "Using CUDD Version %s", CUDD_VERSION);
    }
    if (do_ref(mgr)) {
	mgr->ref_mgr = new_ref_mgr();
	if (!do_cudd) {
	    n = ref2dd(mgr, r);
	}
    } else {
	r = dd2ref(n, IS_BDD);
    }
    reference_dd(mgr, n);
    add_ref(mgr, r, n);
    return mgr;
}

void free_shadow_mgr(shadow_mgr mgr) {
    if (mgr->do_cudd) {
	int left = Cudd_CheckZeroRef(mgr->bdd_manager);
	report(1, "Cudd reports %d nodes with nonzero reference counts", left);
	Cudd_Quit(mgr->bdd_manager);
    }
    if (do_ref(mgr)) 
	free_ref_mgr(mgr->ref_mgr);
    keyvalue_free(mgr->c2r_table);
    keyvalue_free(mgr->r2c_table);
    free_block((void *) mgr, sizeof(shadow_ele));
}

ref_t shadow_one(shadow_mgr mgr) {
    if (do_ref(mgr))
	return REF_ONE;
    else {
	DdNode *n = Cudd_ReadOne(mgr->bdd_manager);
	reference_dd(mgr, n);
	return dd2ref(n, IS_BDD);
    }
}

ref_t shadow_zero(shadow_mgr mgr) {
    if (do_ref(mgr))
	return REF_ZERO;
    else {
	DdNode * n = Cudd_ReadLogicZero(mgr->bdd_manager);
	reference_dd(mgr, n);
	return dd2ref(n, IS_BDD);
    }
}

ref_t shadow_new_variable(shadow_mgr mgr) {
    ref_t r = REF_INVALID;
    DdNode *n = NULL;
    if (mgr->do_cudd) {
	n = Cudd_bddNewVar(mgr->bdd_manager);
	reference_dd(mgr, n);
    }
    if (mgr->do_local) {
	r = ref_new_variable(mgr->ref_mgr);
    }
    if (mgr->do_dist) {
	ref_t rdist = dist_var(mgr->ref_mgr);
	if (mgr->do_local) {
	    if (!check_refs(mgr, r, rdist))
		return REF_INVALID;
	} else
	    r = rdist;
    }
    if (!mgr->do_cudd)
	n = ref2dd(mgr, r);
    if (!do_ref(mgr)) 
	r = dd2ref(n, IS_BDD);
    add_ref(mgr, r, n);
    mgr->nvars++;
    return r;
}

ref_t shadow_get_variable(shadow_mgr mgr, size_t index) {
    if (index >= mgr->nvars) {
	err(fatal, "Invalid variable index %lu", index);
	index = 0;
    }
    ref_t r = 0;
    DdNode *n = NULL;
    if (mgr->do_cudd) {
	n = Cudd_bddIthVar(mgr->bdd_manager, index);
	reference_dd(mgr, n);
    }
    if (do_ref(mgr)) {
	r = REF_VAR(index);
	if (!mgr->do_cudd)
	    n = ref2dd(mgr, r);
    } else {
	r = dd2ref(n, IS_BDD);
    }
    add_ref(mgr, r, n);
    return r;
}


ref_t shadow_cm_restrict(shadow_mgr mgr, ref_t fref, ref_t cref) {
    DdNode *fn = get_ddnode(mgr, fref);
    DdNode *cn = get_ddnode(mgr, cref);
    DdNode *n = NULL;
    ref_t r = REF_INVALID;
    dd_type_t dtype = IS_BDD;

    if (do_ref(mgr))
	return r;

    if (mgr->do_cudd) {
	if (is_zdd(mgr, fref) || is_zdd(mgr, cref)) {
	    err(false, "Can't perform Coudert/Madre restriction on ZDDs");
	    return r;
	}
	bool af = is_add(mgr, fref);
	bool ac = is_add(mgr, cref);
	if (af || ac) {
	    dtype = IS_ADD;
	    if (!af)
		fn = aconvert(mgr, fn);
	    if (!ac)
		cn = aconvert(mgr, cn);
	    n = Cudd_addRestrict(mgr->bdd_manager, fn, cn);
	    reference_dd(mgr, n);
	    if (!af)
		unreference_dd(mgr, fn, IS_ADD);
	    if (!ac)
		unreference_dd(mgr, cn, IS_ADD);
	}
	if (dtype == IS_BDD) {
	    n = Cudd_bddRestrict(mgr->bdd_manager, fn, cn);
	    reference_dd(mgr, n);
	}
	r = dd2ref(n, dtype);
    }
    add_ref(mgr, r, n);
    return r;
}

ref_t shadow_ite(shadow_mgr mgr, ref_t iref, ref_t tref, ref_t eref) {
    DdNode *in = get_ddnode(mgr, iref);
    DdNode *tn = get_ddnode(mgr, tref);
    DdNode *en = get_ddnode(mgr, eref);
    DdNode *n = NULL;
    ref_t r = REF_INVALID;
    dd_type_t dtype = IS_BDD;

    if (mgr->do_local) {
	r = ref_ite(mgr->ref_mgr, iref, tref, eref);
    }
    if (mgr->do_dist) {
	ref_t rdist = dist_ite(mgr->ref_mgr, iref, tref, eref);
	if (mgr->do_local) {
	    if (!check_refs(mgr, r, rdist)) {
		return REF_INVALID;
	    }
	} else {
	    r = rdist;
	}
    }

    if (mgr->do_cudd) {

	if (mgr->nzvars > 0) {
	    bool zi = is_zdd(mgr, iref);
	    bool zt = is_zdd(mgr, tref);
	    bool ze = is_zdd(mgr, eref);
	    if (zi || zt || ze) {
		dtype = IS_ZDD;
		if (is_add(mgr, iref) || is_add(mgr, tref) || is_add(mgr, eref)) {
		    err(false, "Can't mix ADDs with ZDDs");
		}
		if (!zi) {
		    in = zconvert(mgr, in);
		}
		if (!zt) {
		    tn = zconvert(mgr, tn);
		}
		if (!ze) {
		    en = zconvert(mgr, en);
		}
		n = Cudd_zddIte(mgr->bdd_manager, in, tn, en);
		reference_dd(mgr, n);
		if (!zi)
		    unreference_dd(mgr, in, IS_ZDD);
		if (!zt)
		    unreference_dd(mgr, tn, IS_ZDD);
		if (!ze)
		    unreference_dd(mgr, en, IS_ZDD);
	    }
	}
	{
	    bool ai = is_add(mgr, iref);
	    bool at = is_add(mgr, tref);
	    bool ae = is_add(mgr, eref);

	    if (ai || at || ae) {
		dtype = IS_ADD;
		if (!ai) {
		    in = aconvert(mgr, in);
		}
		if (!at) {
		    tn = aconvert(mgr, tn);
		}
		if (!ae) {
		    en = aconvert(mgr, en);
		}
		n = Cudd_addIte(mgr->bdd_manager, in, tn, en);
		reference_dd(mgr, n);
		if (!ai)
		    unreference_dd(mgr, in, IS_ADD);
		if (!at)
		    unreference_dd(mgr, tn, IS_ADD);
		if (!ae)
		    unreference_dd(mgr, en, IS_ADD);
	    }
	}
	if (dtype == IS_BDD) {
	    n = Cudd_bddIte(mgr->bdd_manager, in, tn, en);
	    reference_dd(mgr, n);
	}
    } else {
	n = ref2dd(mgr, r);
    }
    if (!do_ref(mgr))
	r = dd2ref(n, dtype);
    add_ref(mgr, r, n);
    return r;
}

/* Compute negation.  Creates CUDD reference.  For ZDDs, records as reference */
ref_t shadow_negate(shadow_mgr mgr, ref_t a) {
    ref_t r = REF_INVALID;
    if (REF_IS_INVALID(a))
	return a;
    if (do_ref(mgr))
	r = REF_NEGATE(a);
    else {
	DdNode *an = get_ddnode(mgr, a);
	if (is_zdd(mgr, a)) {
	    DdNode *zone = Cudd_ReadZddOne(mgr->bdd_manager, 0);
	    reference_dd(mgr, zone);
	    DdNode *ann = Cudd_zddDiff(mgr->bdd_manager, zone, an);
	    reference_dd(mgr, ann);
	    unreference_dd(mgr, zone, IS_ZDD);
	    r = dd2ref(ann, IS_ZDD);
	    // For ZDDs, don't already have negated values recorded
	    add_ref(mgr, r, ann);
	} else if (is_add(mgr, a)) {
	    DdNode *ann = Cudd_addCmpl(mgr->bdd_manager, an);
	    reference_dd(mgr, ann);
	    r = dd2ref(ann, IS_ADD);
	    // For ADDs, don't already have negated values recorded
	    add_ref(mgr, r, ann);
	} else {
	    DdNode *ann = Cudd_Not(an);
	    reference_dd(mgr, ann);
	    r = dd2ref(ann, IS_BDD);
	}
    }
#if RPT >= 5
    char buf[24], nbuf[24];
    shadow_show(mgr, a, buf);
    shadow_show(mgr, r, nbuf);
    report(5, "Negated %s to get %s", buf, nbuf);
#endif
    return r;
}

ref_t shadow_absval(shadow_mgr mgr, ref_t r) {
    if (do_ref(mgr))
	return REF_ABSVAL(r);
    else {
	dd_type_t dtype = find_type(mgr, r);
	DdNode *n = ref2dd(mgr, r);
	DdNode *an = Cudd_Regular(n);
	return dd2ref(an, dtype);
    }
}

ref_t shadow_and_limit(shadow_mgr mgr, ref_t aref, ref_t bref, size_t nodeLimit, size_t lookupLimit) {
    ref_t r = REF_ZERO;
    dd_type_t dtype = IS_BDD;
    bool za = is_zdd(mgr, aref);
    bool zb = is_zdd(mgr, bref);
    bool aa = is_add(mgr, aref);
    bool ab = is_add(mgr, bref);

    if (mgr->do_cudd && mgr->nzvars > 0) {
	/* Check whether arguments are ZDDs */
	DdNode *an = get_ddnode(mgr, aref);
	DdNode *bn = get_ddnode(mgr, bref);
	DdNode *rn;
	if (za || zb) {
	    dtype = IS_ZDD;
	    if (aa || ab) {
		err(false, "Can't mix ADDs and ZDDs");
	    }
	    /* Make sure they're both ZDDs */
	    if (!za) {
		an = zconvert(mgr, an);
	    }
	    if (!zb) {
		bn = zconvert(mgr, bn);
	    }
	    rn = Cudd_zddIntersect(mgr->bdd_manager, an, bn);
	    reference_dd(mgr, rn);
	    r = dd2ref(rn, IS_ZDD);
	    add_ref(mgr, r, rn);
	    if (!za)
		unreference_dd(mgr, an, IS_ZDD);
	    if (!zb)
		unreference_dd(mgr, bn, IS_ZDD);
	}
    }
    if (dtype == IS_BDD && mgr->do_cudd) {
	/* Check whether arguments are ADDs */
	DdNode *an = get_ddnode(mgr, aref);
	DdNode *bn = get_ddnode(mgr, bref);
	DdNode *rn;
	if (aa || ab) {
	    dtype = IS_ADD;
	    /* Make sure they're both ADDs */
	    if (!aa) {
		an = aconvert(mgr, an);
	    }
	    if (!ab) {
		bn = aconvert(mgr, bn);
	    }
	    rn = Cudd_addApply(mgr->bdd_manager, Cudd_addAnd, an, bn);
	    reference_dd(mgr, rn);
	    r = dd2ref(rn, IS_ADD);
	    add_ref(mgr, r, rn);
	    if (!aa)
		unreference_dd(mgr, an, IS_ADD);
	    if (!ab)
		unreference_dd(mgr, bn, IS_ADD);
	}
    }
    if (dtype == IS_BDD && mgr->do_cudd) {
	DdNode *an = get_ddnode(mgr, aref);
	DdNode *bn = get_ddnode(mgr, bref);
	DdNode *rn = nodeLimit == 0 && lookupLimit == 0 ?
	    dd_check(Cudd_bddAnd(mgr->bdd_manager, an, bn)) :
	    dd_check(Cudd_bddAndLimit2(mgr->bdd_manager, an, bn, (unsigned) nodeLimit, lookupLimit));
	r = dd2ref(rn, IS_BDD);
	if (!REF_IS_INVALID(r)) {
	    reference_dd(mgr, rn);
	    add_ref(mgr, r, rn);
	}
    } else
	r = shadow_ite(mgr, aref, bref, shadow_zero(mgr));

#if RPT >= 4
    char buf1[24], buf2[24], buf3[24];
    shadow_show(mgr, aref, buf1);
    shadow_show(mgr, bref, buf2);
    shadow_show(mgr, r, buf3);
    report(4, "%s %sAND %s --> %s", buf1, dtype == IS_ZDD ? "Z" : dtype == IS_ADD ? "A" : "" , buf2, buf3);
#endif
    return r;
}

ref_t shadow_and(shadow_mgr mgr, ref_t aref, ref_t bref) {
    return shadow_and_limit(mgr, aref, bref, 0, 0);
}
	

ref_t shadow_soft_and(shadow_mgr mgr, ref_t aref, ref_t bref, size_t nodeLimit, size_t lookupLimit) {
    ref_t r = REF_ZERO;
    bool za = is_zdd(mgr, aref);
    bool zb = is_zdd(mgr, bref);
    bool aa = is_add(mgr, aref);
    bool ab = is_add(mgr, bref);
    if (za || zb || aa || ab)
	/* Soft And only implemented with BDDs */
	return shadow_and(mgr, aref, bref);
    if (!mgr->do_cudd)
	/* Only implemented with CUDD */
	return shadow_and(mgr, aref, bref);

    DdNode *an = dd_check(get_ddnode(mgr, aref));
    DdNode *bn = dd_check(get_ddnode(mgr, bref));
    DdNode *rn = nodeLimit == 0 && lookupLimit == 0 ?
	dd_check(Cudd_bddNPAnd(mgr->bdd_manager, an, bn)) :
	dd_check(Cudd_bddNPAndLimit2(mgr->bdd_manager, an, bn, (unsigned) nodeLimit, lookupLimit));
    r = dd2ref(rn, IS_BDD);
    if (!REF_IS_INVALID(r)) {
	reference_dd(mgr, rn);
	add_ref(mgr, r, rn);
    }

#if RPT >= 4
    char buf1[24], buf2[24], buf3[24];
    shadow_show(mgr, aref, buf1);
    shadow_show(mgr, bref, buf2);
    shadow_show(mgr, r, buf3);
    report(4, "%s SOFT AND %s --> %s", buf1, buf2, buf3);
#endif
    return r;
}

ref_t shadow_or(shadow_mgr mgr, ref_t aref, ref_t bref) {
    ref_t r = REF_ONE;
    dd_type_t dtype = IS_BDD;
    bool za = is_zdd(mgr, aref);
    bool zb = is_zdd(mgr, bref);
    bool aa = is_add(mgr, aref);
    bool ab = is_add(mgr, bref);

    if (mgr->do_cudd && mgr->nzvars > 0) {
	/* Check whether arguments are ZDDs */
	DdNode *an = get_ddnode(mgr, aref);
	DdNode *bn = get_ddnode(mgr, bref);
	DdNode *rn;
	if (za || zb) {
	    if (aa || ab) {
		err(false, "Can't mix ADDs and ZDDs");
	    }
	    dtype = IS_ZDD;
	    /* Make sure they're both ZDDs */
	    if (!za) {
		an = zconvert(mgr, an);
	    }
	    if (!zb) {
		bn = zconvert(mgr, bn);
	    }
	    rn = Cudd_zddUnion(mgr->bdd_manager, an, bn);
	    reference_dd(mgr, rn);
	    r = dd2ref(rn, IS_ZDD);
	    add_ref(mgr, r, rn);
	    if (!za)
		unreference_dd(mgr, an, IS_ZDD);
	    if (!zb)
		unreference_dd(mgr, bn, IS_ZDD);
	}
    }
    if (dtype == IS_BDD && mgr->do_cudd) {
	/* Check whether arguments are ADDs */
	DdNode *an = get_ddnode(mgr, aref);
	DdNode *bn = get_ddnode(mgr, bref);
	DdNode *rn;
	if (aa || ab) {
	    dtype = IS_ADD;
	    /* Make sure they're both ADDs */
	    if (!aa) {
		an = aconvert(mgr, an);
	    }
	    if (!ab) {
		bn = aconvert(mgr, bn);
	    }
	    rn = Cudd_addApply(mgr->bdd_manager, Cudd_addOr, an, bn);
	    reference_dd(mgr, rn);
	    r = dd2ref(rn, IS_ADD);
	    add_ref(mgr, r, rn);
	    if (!aa)
		unreference_dd(mgr, an, IS_ADD);
	    if (!ab)
		unreference_dd(mgr, bn, IS_ADD);
	}
    }
    if (dtype == IS_BDD)
	r = shadow_ite(mgr, aref, shadow_one(mgr), bref);

#if RPT >= 4
    char buf1[24], buf2[24], buf3[24];
    shadow_show(mgr, aref, buf1);
    shadow_show(mgr, bref, buf2);
    shadow_show(mgr, r, buf3);
    report(4, "%s %sOR %s --> %s", buf1, dtype == IS_ZDD ? "Z" : dtype == IS_ADD ? "A" : "" , buf2, buf3);
#endif
    return r;
}

ref_t shadow_xor(shadow_mgr mgr, ref_t aref, ref_t bref) {
    ref_t r = REF_ZERO;
    dd_type_t dtype = IS_BDD;
    bool za = is_zdd(mgr, aref);
    bool zb = is_zdd(mgr, bref);
    bool aa = is_add(mgr, aref);
    bool ab = is_add(mgr, bref);

    if (mgr->do_cudd && mgr->nzvars > 0) {
	/* Check whether arguments are ZDDs */
	DdNode *an = get_ddnode(mgr, aref);
	DdNode *bn = get_ddnode(mgr, bref);
	DdNode *rn;
	if (za || zb) {
	    if (aa || ab) {
		err(false, "Can't mix ADDs and ZDDs");
	    }
	    dtype = IS_ZDD;
	    /* Make sure they're both ZDDs */
	    if (!za) {
		an = zconvert(mgr, an);
	    }
	    if (!zb) {
		bn = zconvert(mgr, bn);
	    }
	    rn = Cudd_zddSymmetricDiff(mgr->bdd_manager, an, bn);
	    reference_dd(mgr, rn);
	    r = dd2ref(rn, IS_ZDD);
	    add_ref(mgr, r, rn);
	    if (!za)
		unreference_dd(mgr, an, IS_ZDD);
	    if (!zb)
		unreference_dd(mgr, bn, IS_ZDD);
	}
    }
    if (dtype == IS_BDD && mgr->do_cudd) {
	/* Check whether arguments are ADDs */
	DdNode *an = get_ddnode(mgr, aref);
	DdNode *bn = get_ddnode(mgr, bref);
	DdNode *rn;
	if (aa || ab) {
	    dtype = IS_ADD;
	    /* Make sure they're both ADDs */
	    if (!aa) {
		an = aconvert(mgr, an);
	    }
	    if (!ab) {
		bn = aconvert(mgr, bn);
	    }
	    rn = Cudd_addApply(mgr->bdd_manager, Cudd_addXor, an, bn);
	    reference_dd(mgr, rn);
	    r = dd2ref(rn, IS_ADD);
	    add_ref(mgr, r, rn);
	    if (!aa)
		unreference_dd(mgr, an, IS_ADD);
	    if (!ab)
		unreference_dd(mgr, bn, IS_ADD);
	}
    }
    if (dtype == IS_BDD) {
	ref_t bn = shadow_negate(mgr, bref);
	r = shadow_ite(mgr, aref, bn, bref);
	shadow_deref(mgr, bn);
    }

#if RPT >= 4
    char buf1[24], buf2[24], buf3[24];
    shadow_show(mgr, aref, buf1);
    shadow_show(mgr, bref, buf2);
    shadow_show(mgr, r, buf3);
    report(4, "%s %sXOR %s --> %s", buf1, dtype == IS_ZDD ? "Z" : dtype == IS_ADD ? "A" : "" , buf2, buf3);
#endif
    return r;
}

bool shadow_gc_check(shadow_mgr mgr) {
    if (mgr->do_local)
	return ref_gc_check(mgr->ref_mgr);
    else
	return false;
}

void shadow_deref(shadow_mgr mgr, ref_t r) {
/* Only call this when removing r from unique table */

    if (!mgr->do_cudd)
	return;
    DdNode *n = get_ddnode(mgr, r);
    dd_type_t dtype = find_type(mgr, r);

#if RPT >= 5
    char buf[24];
    shadow_show(mgr, r, buf);
    report(5, "Deleting reference %s for node %p", buf, n);
#endif

    unreference_dd(mgr, n, dtype);

    if (!do_ref(mgr))
	return;

    keyvalue_remove(mgr->c2r_table, (word_t ) n, NULL, NULL);
    keyvalue_remove(mgr->r2c_table, (word_t ) r, NULL, NULL);


    if (dtype == IS_BDD) {
	ref_t nr = shadow_negate(mgr, r);
	DdNode *nn = get_ddnode(mgr, nr);
#if RPT >= 5
	shadow_show(mgr, nr, buf);
	report(5, "Deleting reference %s for node %p", buf, nn);
#endif
	keyvalue_remove(mgr->c2r_table, (word_t ) nn, NULL, NULL);
	keyvalue_remove(mgr->r2c_table, (word_t ) nr, NULL, NULL);
	unreference_dd(mgr, nn, IS_BDD);
    }
}

void shadow_addref(shadow_mgr mgr, ref_t r) {
    if (!mgr->do_cudd)
	return;
    DdNode *n = get_ddnode(mgr, r);
    reference_dd(mgr, n);
}

void shadow_satisfy(shadow_mgr mgr, ref_t r) {
    if (!mgr->do_cudd)
	return;
    DdNode *n = get_ddnode(mgr, r);
    bool zdd = is_zdd(mgr, r);
    FILE *logfile = get_logfile();
    if (zdd) {
	Cudd_zddPrintMinterm(mgr->bdd_manager, n);
	if (logfile) {
	    FILE *savefile = Cudd_ReadStdout(mgr->bdd_manager);
	    Cudd_SetStdout(mgr->bdd_manager, logfile);
	    Cudd_zddPrintMinterm(mgr->bdd_manager, n);
	    Cudd_SetStdout(mgr->bdd_manager, savefile);
	}
    }
    else {
	Cudd_PrintMinterm(mgr->bdd_manager, n);
	if (logfile) {
	    FILE *savefile = Cudd_ReadStdout(mgr->bdd_manager);
	    Cudd_SetStdout(mgr->bdd_manager, logfile);
	    Cudd_PrintMinterm(mgr->bdd_manager, n);
	    Cudd_SetStdout(mgr->bdd_manager, savefile);
	}
    }
}



/*** Unary Operations ***/

static word_t d2w(double d) {
    union {
	double d;
	word_t w;
    } x;
    x.d = d;
    return x.w;
}

static double w2d(word_t w) {
    union {
	double d;
	word_t w;
    } x;
    x.w = w;
    return x.d;
}

/* Implementations of operations in CUDD */
static keyvalue_table_ptr cudd_density(shadow_mgr mgr, set_ptr roots) {
    keyvalue_table_ptr dtable = word_keyvalue_new();
    set_iterstart(roots);
    word_t wr;
    while (set_iternext(roots, &wr)) {
	ref_t r = (ref_t) wr;
	DdNode *n = get_ddnode(mgr, r);
	double *vdensity = Cudd_CofMinterm(mgr->bdd_manager, n);
	double density = vdensity[mgr->nvars];
	free(vdensity);
	keyvalue_insert(dtable, wr, d2w(density));
    }
    return dtable;
}

/* Create key-value table mapping set of root nodes to their densities. */
keyvalue_table_ptr shadow_density(shadow_mgr mgr, set_ptr roots) {
    keyvalue_table_ptr ldensity = NULL;
    keyvalue_table_ptr ddensity = NULL;
    keyvalue_table_ptr cdensity = NULL;
    keyvalue_table_ptr density = NULL;
    word_t wk, wv1, wv2;
    if (mgr->do_local) {
	ldensity = ref_density(mgr->ref_mgr, roots);
	density = ldensity;
    }
    if (mgr->do_dist) {
	ddensity = dist_density(mgr->ref_mgr, roots);
	if (!density)
	    density = ddensity;
    }
    if (mgr->do_cudd) {
	cdensity = cudd_density(mgr, roots);
	if (!density)
	    density = cdensity;
    }
    if (ddensity && ddensity != density) {
	/* Have both local and dist versions */
	keyvalue_diff(ddensity, density, word_equal);
	while (keyvalue_removenext(ddensity, &wk, &wv1)) {
	    ref_t r = (ref_t) wk;
	    char buf[24];
	    double d1 = w2d(wv1);
	    shadow_show(mgr, r, buf);
	    if (keyvalue_find(density, wk, &wv2)) {
		double d2 = w2d(wv2);
		err(false,
		    "Density mismatch for %s.  local = %.2f, distance = %.2f",
		    buf, d2, d1);
	    } else {
		err(false, "Density error for %s.  No local entry", buf);
	    }
	}
	keyvalue_free(ddensity);
    }
    if (cdensity && cdensity != density) {
	/* Have local or dist, plus cudd */
	keyvalue_diff(cdensity, density, word_equal);
	while (keyvalue_removenext(cdensity, &wk, &wv1)) {
	    ref_t r = (ref_t) wk;
	    char buf[24];
	    double d1 = w2d(wv1);
	    shadow_show(mgr, r, buf);
	    if (keyvalue_find(density, wk, &wv2)) {
		double d2 = w2d(wv2);
		err(false, "Density mismatch for %s.  ref = %.2f, cudd = %.2f",
		    buf, d2, d1);
	    } else {
		err(false, "Density error for %s.  No local entry", buf);
	    }
	}
	keyvalue_free(cdensity);
    }
    return density;
}

#if USE_APA
static word_t apa2word(DdApaNumber num, int digits) {
    word_t val = 0;
    int i;

     for (i = 0; i < digits; i++) {
	val = val * DD_APA_BASE + (word_t) num[i];
    }
    return val;
}
#endif

double cudd_single_count(shadow_mgr mgr, ref_t r) {
    if (!mgr->do_cudd)
	return 0.0;
    DdNode *n = get_ddnode(mgr, r);
    bool zdd = is_zdd(mgr, r);
#if USE_APA
    int digits;
    DdApaNumber num = Cudd_ApaCountMinterm(mgr->bdd_manager, n, mgr->nvars,
					   &digits);
    wv = apa2word(num, digits);
    FREE(num);
#else
    double fv = 0.0;
    if (zdd) {
	fv = Cudd_zddCountMinterm(mgr->bdd_manager, n, mgr->nzvars);
    } else {
	fv = Cudd_CountMinterm(mgr->bdd_manager, n, mgr->nvars);
    }
#endif
    return fv;
}

static keyvalue_table_ptr cudd_count(shadow_mgr mgr, set_ptr roots) {
    word_t wk, wv;
    keyvalue_table_ptr result = word_keyvalue_new();
    set_iterstart(roots);
    while (set_iternext(roots, &wk)) {
	ref_t r = (ref_t) wk;
	double fv = cudd_single_count(mgr, r);
	wv = (word_t) fv;
	keyvalue_insert(result, wk, wv);
    }
    return result;
}

/* Create key-value table mapping set of root nodes to their counts */
keyvalue_table_ptr shadow_count(shadow_mgr mgr, set_ptr roots) {
    keyvalue_table_ptr lcount = NULL;
    keyvalue_table_ptr dcount = NULL;
    keyvalue_table_ptr ccount = NULL;
    keyvalue_table_ptr count = NULL;
    word_t wk, wv1, wv2;
    if (mgr->do_local) {
	lcount = ref_count(mgr->ref_mgr, roots);
	count = lcount;
    }
    if (mgr->do_dist) {
	dcount = dist_count(mgr->ref_mgr, roots);
	if (!count)
	    count = dcount;
    }
    if (mgr->do_cudd) {
	ccount = cudd_count(mgr, roots);
	if (!count)
	    count = ccount;
    }
    if (dcount && dcount != count) {
	/* Have both local and dist versions */
	keyvalue_diff(dcount, count, word_equal);
	while (keyvalue_removenext(dcount, &wk, &wv1)) {
	    ref_t r = (ref_t) wk;
	    char buf[24];
	    word_t c1 = wv1;
	    shadow_show(mgr, r, buf);
	    if (keyvalue_find(count, wk, &wv2)) {
		word_t c2 = wv2;
		err(false, "Count mismatch for %s.  local = %lu, distance = %lu",
		    buf, c2, c1);
	    } else {
		err(false, "Count error for %s.  No local entry", buf);
	    }
	}
	keyvalue_free(dcount);
    }
    if (ccount && ccount != count) {
	/* Have local or dist, plus cudd */
	keyvalue_diff(ccount, count, word_equal);
	while (keyvalue_removenext(ccount, &wk, &wv1)) {
	    ref_t r = (ref_t) wk;
	    char buf[24];
	    word_t c1 = wv1;
	    shadow_show(mgr, r, buf);
	    if (keyvalue_find(count, wk, &wv2)) {
		word_t c2 = wv2;
		err(false, "Count mismatch for %s.  ref = %lu, cudd = %lu",
		    buf, c2, c1);
	    } else {
		err(false, "Count error for %s.  No local entry", buf);
	    }
	}
	keyvalue_free(ccount);
    }
    return count;
}


/* Uses CUDD to determine refs for all variable in support set of root functions */
static set_ptr cudd_support(shadow_mgr mgr, set_ptr roots) {
    /* Create vector of nodes */
    DdNode **vector = calloc_or_fail(roots->nelements, sizeof(DdNode *),
				     "cudd_support");
    size_t i = 0;
    word_t w;
    set_iterstart(roots);
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	vector[i++] = get_ddnode(mgr, r);
    }
    int *indices = NULL;
    int cnt = Cudd_VectorSupportIndices(mgr->bdd_manager, vector,
					roots->nelements, &indices);
    set_ptr sset = word_set_new();
    for (i = 0; i < cnt; i++) {
	ref_t rv = shadow_get_variable(mgr, indices[i]);
	set_insert(sset, (word_t) rv);
    }
    if (indices)
	free(indices);
    free_array(vector, roots->nelements, sizeof(DdNode *));
    return sset;
}


/* Compute set of variables (given by refs) in support of set of roots */
set_ptr shadow_support(shadow_mgr mgr, set_ptr roots) {
    set_ptr lsupport = NULL;
    set_ptr dsupport = NULL;
    set_ptr csupport = NULL;
    set_ptr support = NULL;
    if (mgr->do_local) {
	lsupport = ref_support(mgr->ref_mgr, roots);
	support = lsupport;
    }
    if (mgr->do_dist) {
	dsupport = dist_support(mgr->ref_mgr, roots);
	if (!support)
	    support = dsupport;
    }
    if (mgr->do_cudd) {
	csupport = cudd_support(mgr, roots);
	if (!support)
	    support = csupport;
    }
    if (dsupport && support != dsupport) {
	/* Have both local and dist results */
	word_t w;
	set_iterstart(support);
	while (set_iternext(support, &w)) {
	    if (!set_member(dsupport, w, false)) {
		char buf[24];
		ref_t r = (ref_t) w;
		ref_show(r, buf);
		err(false, "Found %s in local support, but not in dist", buf);
	    }
	}
	set_iterstart(dsupport);
	while (set_iternext(dsupport, &w)) {
	    if (!set_member(support, w, false)) {
		char buf[24];
		ref_t r = (ref_t) w;
		ref_show(r, buf);
		err(false, "Found %s in dist support, but not in local", buf);
	    }
	}
	set_free(dsupport);
    }
    if (csupport && support != csupport) {
	/* Have both ref and cudd results */
	word_t w;
	set_iterstart(support);
	while (set_iternext(support, &w)) {
	    if (!set_member(csupport, w, false)) {
		char buf[24];
		ref_t r = (ref_t) w;
		ref_show(r, buf);
		err(false, "Found %s in ref support, but not in cudd", buf);
	    }
	}
	set_iterstart(csupport);
	while (set_iternext(csupport, &w)) {
	    if (!set_member(support, w, false)) {
		char buf[24];
		ref_t r = (ref_t) w;
		ref_show(r, buf);
		err(false, "Found %s in cudd support, but not in ref", buf);
	    }
	}
	set_free(csupport);
    }
    return support;
}

/* Use CUDD to compute number of BDD nodes to represent set of functions */

size_t cudd_single_size(shadow_mgr mgr, ref_t r) {
    if (!mgr->do_cudd)
	return 0;
    DdNode *n = get_ddnode(mgr, r);
    int cnt = Cudd_DagSize(n);
    return (size_t) cnt;
}

size_t cudd_set_size(shadow_mgr mgr, set_ptr roots) {
    if (!mgr->do_cudd)
	return 0;
    size_t nele = roots->nelements;
    DdNode **croots = calloc_or_fail(nele, sizeof(DdNode *), "cudd_set_size");
    word_t wr;
    int i = 0;
    set_iterstart(roots);
    while (set_iternext(roots, &wr)) {
	ref_t r = (ref_t) wr;
	DdNode *n = get_ddnode(mgr, r);
	croots[i++] = n;
    }
    int cnt = Cudd_SharingSize(croots, nele);
    free_array(croots, nele, sizeof(DdNode *));
    return (size_t) cnt;
}

size_t shadow_peak_nodes(shadow_mgr mgr) {
    if (!mgr->do_cudd)
	return 0;
    return Cudd_ReadPeakLiveNodeCount(mgr->bdd_manager);
}

/* Have CUDD perform garbage collection.  Return number of nodes collected */
int cudd_collect(shadow_mgr mgr) {
    if (!mgr->do_cudd)
	return 0;
    int result = cuddGarbageCollect(mgr->bdd_manager, 1);
    return result;
}

/* Convert a set of literals into a CUDD cube */
static DdNode *cudd_lit_cube(shadow_mgr mgr, set_ptr lits) {
    size_t nele = lits->nelements;
    DdNode **vars = calloc_or_fail(nele, sizeof(DdNode *), "cudd_lit_cube");
    int *phase = calloc_or_fail(nele, sizeof(int), "cudd_lit_cube");
    word_t wr;
    int i = 0;
    set_iterstart(lits);
    while (set_iternext(lits, &wr)) {
	ref_t r = (ref_t) wr;
	int pos = 1;
	if (REF_GET_NEG(r)) {
	    r = REF_NEGATE(r);
	    pos = 0;
	}
	DdNode *n = get_ddnode(mgr, r);
	vars[i] = n;
	phase[i] = pos;
	i++;
    }
    DdNode *cube = Cudd_bddComputeCube(mgr->bdd_manager, vars, phase, nele);
    reference_dd(mgr, cube);
    free_array(vars, nele, sizeof(DdNode *));
    free_array(phase, nele, sizeof(int));
    return cube;
}

static keyvalue_table_ptr cudd_restrict(shadow_mgr mgr, set_ptr roots,
					set_ptr lits) {
    DdNode *cube = cudd_lit_cube(mgr, lits);
    keyvalue_table_ptr rtable = word_keyvalue_new();
    word_t w;
    set_iterstart(roots);
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	DdNode *n = get_ddnode(mgr, r);
	DdNode *nr = Cudd_Cofactor(mgr->bdd_manager, n, cube);
	reference_dd(mgr, nr);
	keyvalue_insert(rtable, w, (word_t) nr);
    }
    unreference_dd(mgr, cube, IS_BDD);
    return rtable;
}

/* Reconcile various maps that have created */
static keyvalue_table_ptr reconcile_maps(shadow_mgr mgr, keyvalue_table_ptr lmap,
					 keyvalue_table_ptr dmap,
					 keyvalue_table_ptr cmap) {
    keyvalue_table_ptr map = lmap ? lmap : dmap;
    if (dmap && map != dmap) {
	/* Have both local and dist results */
	word_t wk, wv1, wv2;
	keyvalue_diff(dmap, map, word_equal);
	while (keyvalue_removenext(dmap, &wk, &wv1)) {
	    char buf[24];
	    ref_t r = (ref_t) wk;
	    ref_show(r, buf);
	    if (keyvalue_find(map, wk, &wv2)) {
		char buf1[24], buf2[24];
		ref_show((ref_t) wv1, buf1); ref_show((ref_t) wv2, buf2);
		err(false,
		    "Unary operation on %s gives %s in local, but %s in dist",
		    buf, buf2, buf1);
	    } else {
		err(false, "Could not find %s in unary op map", buf);
	    }
	}
	keyvalue_free(dmap);
    }
    if (cmap) {
	if (map) {
	    /* Have ref & cudd versions */
	    word_t wk, wv1, wv2;
	    DdNode *n = NULL;
	    keyvalue_iterstart(map);
	    while (keyvalue_iternext(map, &wk, &wv1)) {
		ref_t rr = (ref_t) wv1;
		if (keyvalue_find(cmap, wk, &wv2)) {
		    n = (DdNode *) wv2;
		    add_ref(mgr, rr, n);
		}
	    }
	    keyvalue_free(cmap);
	} else {
	    /* Only doing CUDD */
	    word_t wk, wv;
	    keyvalue_iterstart(cmap);
	    while (keyvalue_iternext(cmap, &wk, &wv)) {
		DdNode *n = (DdNode *) wv;
		add_ref(mgr, 0, n);
	    }
	    map = cmap;
	}
    } else {
	/* Don't have CUDD.  Artificially add references */
	word_t wk, wv;
	keyvalue_iterstart(map);
	while (keyvalue_iternext(map, &wk, &wv)) {
	    ref_t rr = (ref_t) wv;
	    DdNode *n = ref2dd(mgr, rr);
	    add_ref(mgr, rr, n);
	}
    }
    return map;
}

/* ZDD conversion for external command */
ref_t shadow_zconvert(shadow_mgr mgr, ref_t r) {
    if (do_ref(mgr))
	/* Only applies when in pure CUDD mode */
	return r;
    DdNode *n = get_ddnode(mgr, r);
    if (is_zdd(mgr, r)) {
	return r;
    }
    if (is_add(mgr, r)) {
	err(false, "Can't convert ADD to ZDD");
	return REF_INVALID;
    }
    DdNode *zn = zconvert(mgr, n);
    ref_t zr = dd2ref(zn, IS_ZDD);
    add_ref(mgr, zr, zn);
    return zr;
}

/* ADD conversion for external command */
ref_t shadow_aconvert(shadow_mgr mgr, ref_t r) {
    if (do_ref(mgr))
	/* Only applies when in pure CUDD mode */
	return r;
    DdNode *n = get_ddnode(mgr, r);
    if (is_add(mgr, r)) {
	return r;
    }
    if (is_zdd(mgr, r)) {
	err(false, "Can't convert ADD to ZDD");
	return REF_INVALID;
    }
    DdNode *an = aconvert(mgr, n);
    ref_t ar = dd2ref(an, IS_ADD);
    add_ref(mgr, ar, an);
    return ar;
}

/* Create key-value table mapping set of root nodes to their restrictions,
   with respect to a set of literals (given as a set of refs)
*/
keyvalue_table_ptr shadow_restrict(shadow_mgr mgr, set_ptr roots, set_ptr lits) {
    keyvalue_table_ptr lmap = NULL;
    keyvalue_table_ptr dmap = NULL;
    keyvalue_table_ptr cmap = NULL;
    if (mgr->do_local) {
	lmap = ref_restrict(mgr->ref_mgr, roots, lits);
    }
    if (mgr->do_dist) {
	dmap = dist_restrict(mgr->ref_mgr, roots, lits);
    }
    if (mgr->do_cudd) {
	cmap = cudd_restrict(mgr, roots, lits);
    }
    return reconcile_maps(mgr, lmap, dmap, cmap);
}

static keyvalue_table_ptr cudd_equant(shadow_mgr mgr,
				      set_ptr roots, set_ptr vars) {
    DdNode *cube = cudd_lit_cube(mgr, vars);
    keyvalue_table_ptr etable = word_keyvalue_new();
    word_t w;
    set_iterstart(roots);
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	DdNode *n = get_ddnode(mgr, r);
	DdNode *nr = Cudd_bddExistAbstract(mgr->bdd_manager, n, cube);
	reference_dd(mgr, nr);
	keyvalue_insert(etable, w, (word_t) nr);
    }
    unreference_dd(mgr, cube, IS_BDD);
    return etable;
}


/* Create key-value table mapping set of root nodes to their
   existential quantifications with respect to a set of variables
   (given as a set of refs)
*/
keyvalue_table_ptr shadow_equant(shadow_mgr mgr, set_ptr roots, set_ptr vars) {
    keyvalue_table_ptr lmap = NULL;
    keyvalue_table_ptr dmap = NULL;
    keyvalue_table_ptr cmap = NULL;
    if (mgr->do_local) {
	lmap = ref_equant(mgr->ref_mgr, roots, vars);
    }
    if (mgr->do_dist) {
	dmap = dist_equant(mgr->ref_mgr, roots, vars);
    }
    if (mgr->do_cudd) {
	cmap = cudd_equant(mgr, roots, vars);
    }
    return reconcile_maps(mgr, lmap, dmap, cmap);
}

static keyvalue_table_ptr cudd_shift(shadow_mgr mgr, set_ptr roots,
				     keyvalue_table_ptr vmap) {
    DdNode **vector = calloc_or_fail(mgr->nvars, sizeof(DdNode *), "cudd_shift");
    size_t i;
    /* Must build up vector of composition functions */
    for (i = 0; i < mgr->nvars; i++) {
	ref_t vref = shadow_get_variable(mgr, i);
	word_t wv;
	ref_t nvref = vref;
	if (keyvalue_find(vmap, (word_t) vref, &wv))
	    nvref = (ref_t) wv;
	DdNode *nv = get_ddnode(mgr, nvref);
	vector[i] = nv;
    }
    keyvalue_table_ptr stable = word_keyvalue_new();
    word_t w;
    set_iterstart(roots);
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	DdNode *n = get_ddnode(mgr, r);
	DdNode *ns = Cudd_bddVectorCompose(mgr->bdd_manager, n, vector);
	reference_dd(mgr, ns);
	keyvalue_insert(stable, w, (word_t) ns);
    }
    free_array(vector, mgr->nvars, sizeof(DdNode *));
    return stable;
}


/* Create key-value table mapping set of root nodes to their shifted versions
   with respect to a mapping from old variables to new ones 
*/
keyvalue_table_ptr shadow_shift(shadow_mgr mgr, set_ptr roots,
				keyvalue_table_ptr vmap) {
    keyvalue_table_ptr lmap = NULL;
    keyvalue_table_ptr dmap = NULL;
    keyvalue_table_ptr cmap = NULL;
    if (mgr->do_local) {
	lmap = ref_shift(mgr->ref_mgr, roots, vmap);
    }
    if (mgr->do_dist) {
	dmap = dist_shift(mgr->ref_mgr, roots, vmap);
    }
    if (mgr->do_cudd) {
	cmap = cudd_shift(mgr, roots, vmap);
    }
    return reconcile_maps(mgr, lmap, dmap, cmap);
}

/* Compute similarity metric for support sets of two functions */
/* Modified 09/17/2019 with revised similarity count */

double index_similarity(int support_count1, int *indices1, int support_count2, int *indices2) {
    double score = 0.0;
    int intersection_count = 0;
    int r1_count = 0;
    int r2_count = 0;
    int idx1 = 0;
    int idx2 = 0;

    while (idx1 < support_count1 && idx2 < support_count2) {
	int next1 = indices1[idx1];
	int next2 = indices2[idx2];
	if (next1 == next2) {
	    intersection_count++;
	    r1_count++;
	    r2_count++;
	    idx1++;
	    idx2++;
	} else if (next1 < next2) {
	    r1_count++;
	    idx1++;
	} else {
	    r2_count++;
	    idx2++;
	}
    }
    r1_count += (support_count1-idx1);
    r2_count += (support_count2-idx2);
    int min_count = r1_count < r2_count ? r1_count : r2_count;
    double cov = min_count == 0 ? 1.0 : (double) intersection_count / min_count;
    int sum_count = r1_count + r2_count + intersection_count;
    double sim = sum_count == 0 ? 1.0 : (double) (3 * intersection_count)/sum_count;
    score = cov > sim ? cov : sim;
    return score;
}

double index_coverage(int support_count1, int *indices1, int support_count2, int *indices2) {
    int intersection_count = 0;
    int r1_count = 0;
    int idx1 = 0;
    int idx2 = 0;

    while (idx1 < support_count1 && idx2 < support_count2) {
	int next1 = indices1[idx1];
	int next2 = indices2[idx2];
	if (next1 == next2) {
	    intersection_count++;
	    r1_count++;
	    idx1++;
	    idx2++;
	} else if (next1 < next2) {
	    r1_count++;
	    idx1++;
	} else {
	    idx2++;
	}
    }
    r1_count += (support_count1-idx1);
    double cov = r1_count == 0 ? 1.0 : (double) intersection_count / r1_count;
    return cov;
}


int shadow_support_indices(shadow_mgr mgr, ref_t r, int **indicesp) {
    if (!mgr->do_cudd) {
	*indicesp = NULL;
	return -1;
    }
    DdNode *n = ref2dd(mgr, r);
    int support_count = Cudd_SupportIndices(mgr->bdd_manager, n, indicesp);
    return support_count;
}

/* Compute similarity metric for support sets of two functions */
double shadow_similarity(shadow_mgr mgr, ref_t r1, ref_t r2) {
    int *indices1;
    int *indices2;

    if (!mgr->do_cudd)
	return 0.0;
    int support_count1 = shadow_support_indices(mgr, r1, &indices1);
    int support_count2 = shadow_support_indices(mgr, r2, &indices2);

    double result = index_similarity(support_count1, indices1, support_count2, indices2);

    free(indices1);
    free(indices2);

    return result;
}

double shadow_coverage(shadow_mgr mgr, ref_t r1, ref_t r2) {
    int *indices1;
    int *indices2;

    if (!mgr->do_cudd)
	return 0.0;
    int support_count1 = shadow_support_indices(mgr, r1, &indices1);
    int support_count2 = shadow_support_indices(mgr, r2, &indices2);

    double result = index_coverage(support_count1, indices1, support_count2, indices2);

    free(indices1);
    free(indices2);

    return result;
}

/* Generate status report from Cudd or ref manager */
void shadow_status(shadow_mgr mgr) {
    if (do_ref(mgr))
	ref_show_stat(mgr->ref_mgr);
    if (mgr->do_cudd) {
	Cudd_PrintInfo(mgr->bdd_manager, stdout);
	FILE *logfile = get_logfile();
	if (logfile)
	    Cudd_PrintInfo(mgr->bdd_manager, logfile);
    }

}

bool shadow_store(shadow_mgr mgr, ref_t r, FILE *outfile) {
    if (!mgr->do_cudd)
	return false;
    DdNode *nd = ref2dd(mgr, r);;
    int ok = Cudd_bddStore(mgr->bdd_manager, nd, outfile);
    return (bool) ok;
}


ref_t shadow_load(shadow_mgr mgr, FILE *infile) {
    ref_t r = REF_INVALID;
    if (!mgr->do_cudd)
	return r;
    DdNode *nd = Cudd_bddLoad(mgr->bdd_manager, infile);
    reference_dd(mgr, nd);
    if (!nd)
	return r;
    r = dd2ref(nd, IS_BDD);
    return r;
}


/* Count of number of cache lookups since last call */
size_t shadow_delta_cache_lookups(shadow_mgr mgr) {
    static size_t last_count = 0;
    if (!mgr->do_cudd)
	return 0;
    size_t count = (size_t) Cudd_ReadCacheLookUps(mgr->bdd_manager);
    size_t delta = count - last_count;
    last_count = count;
    return delta;
}
