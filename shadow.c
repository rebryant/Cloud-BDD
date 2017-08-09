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
#define CUDD_VERSION "3.0.0"
#endif

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


bool fatal = false;

bool do_ref(shadow_mgr mgr) {
    return mgr->do_local || mgr->do_dist;
}

/* When using only CUDD, ref is copy of DdNode for BDD, DdNode +2 for ZDD */
static ref_t dd2ref(DdNode *n, bool zdd) {
    ref_t r = (ref_t) n;
    if (zdd)
	r += 2;
    return r;
}

static bool is_zdd(shadow_mgr mgr, ref_t r) {
    if (do_ref(mgr))
	return false;
    return (r & 0x2) != 0;
}

static DdNode *ref2dd(shadow_mgr mgr, ref_t r) {
    if (is_zdd(mgr, r))
	r -= 2;
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
}

static void unreference_dd(shadow_mgr mgr, DdNode *n, bool zdd) {
    if (!mgr->do_cudd)
	return;
    if (zdd) {
	Cudd_RecursiveDerefZdd(mgr->bdd_manager, n);
    } else {
	Cudd_RecursiveDeref(mgr->bdd_manager, n);
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

void shadow_show(shadow_mgr mgr, ref_t r, char *buf) {
    if (do_ref(mgr)) {
	ref_show(r, buf);
    } else {
	bool zdd = is_zdd(mgr, r);
	DdNode *n = ref2dd(mgr, r);
	sprintf(buf, "%c%p", zdd ? 'Z' : 'B', n);
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

/* Associate n with reference r.  For non-Zdd's, also cross reference complements.
   Incoming n has recorded reference count.
   Create single reference to node.  For non-ZDD, create single reference to its complement
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

    bool zdd = is_zdd(mgr, r);

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
		unreference_dd(mgr, n, zdd);
	    } else {
		shadow_show(mgr, r, buf1);
		shadow_show(mgr, rother, buf2);
		err(fatal,
"Inconsistency.  New node 0x%p.  Old node 0x%p.  New ref %s.  Old ref %s",
		    n, nother, buf1, buf2);
	    }
	} else {
	    if (is_zdd(mgr, rother) == zdd) {
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
	    if (!zdd) {
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
	/* Modified CUDD Parameters */
	unsigned int numVars = 0; /* Default 0 */
	unsigned int numVarsZ = 0; /* Default 0 */
	unsigned int numSlots = 1u<<18; /* Default 256 */
	unsigned int cacheSize = 1u<<22; /* Default 262144 */
	/* Default 67,108,864 */
	unsigned long int maxMemory = (1u<<31) + (1ul << 34);
#if 0
	// Use defaults
	numSlots = 256;
	cacheSize = 262144;
	maxMemory = 67108864;
#endif
	mgr->bdd_manager = Cudd_Init(numVars, numVarsZ, numSlots, cacheSize, maxMemory);
#ifndef NO_CHAINING
	Cudd_ChainingType ct = CUDD_CHAIN_NONE;
	switch (chaining) {
	case CHAIN_NONE:
	    ct = CUDD_CHAIN_NONE;
	    printf("No chaining enabled\n");
	    break;
	case CHAIN_CONSTANT:
	    ct = CUDD_CHAIN_CONSTANT;
	    printf("Constant chaining enabled\n");
	    break;
	case CHAIN_ALL:
	    ct = CUDD_CHAIN_ALL;
	    printf("General chaining enabled\n");
	    break;
	default:
	    err(true, "Invalid chaining mode %d\n", chaining);
	}
	Cudd_SetChaining(mgr->bdd_manager, ct);
#endif	
	n = Cudd_ReadLogicZero(mgr->bdd_manager);
	printf("Using CUDD Version %s\n", CUDD_VERSION);
    }
    if (do_ref(mgr)) {
	mgr->ref_mgr = new_ref_mgr();
	if (!do_cudd) {
	    n = ref2dd(mgr, r);
	}
    } else {
	r = dd2ref(n, false);
    }
    reference_dd(mgr, n);
    add_ref(mgr, r, n);
    return mgr;
}

void free_shadow_mgr(shadow_mgr mgr) {
    if (mgr->do_cudd) {
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
	return dd2ref(n, false);
    }
}

ref_t shadow_zero(shadow_mgr mgr) {
    if (do_ref(mgr))
	return REF_ZERO;
    else {
	DdNode * n = Cudd_ReadLogicZero(mgr->bdd_manager);
	reference_dd(mgr, n);
	return dd2ref(n, false);
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
	r = dd2ref(n, false);
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
	r = dd2ref(n, false);
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
    bool zdd = false;

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
		zdd = true;
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
		    unreference_dd(mgr, in, true);
		if (!zt)
		    unreference_dd(mgr, tn, true);
		if (!ze)
		    unreference_dd(mgr, en, true);
	    }
	}
	if (!zdd) {
	    n = Cudd_bddIte(mgr->bdd_manager, in, tn, en);
	    reference_dd(mgr, n);
	}
    } else {
	n = ref2dd(mgr, r);
    }
    if (!do_ref(mgr))
	r = dd2ref(n, zdd);
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
	    unreference_dd(mgr, zone, true);
	    r = dd2ref(ann, true);
	    // For ZDDs, don't already have negated values recorded
	    add_ref(mgr, r, ann);
	} else {
	    DdNode *ann = Cudd_Not(an);
	    reference_dd(mgr, ann);
	    r = dd2ref(ann, false);
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
	bool zdd = is_zdd(mgr, r);
	DdNode *n = ref2dd(mgr, r);
	DdNode *an = Cudd_Regular(n);
	return dd2ref(an, zdd);
    }
}

ref_t shadow_and(shadow_mgr mgr, ref_t aref, ref_t bref) {
    ref_t r = REF_ZERO;
    bool zdd = false;
    if (mgr->do_cudd && mgr->nzvars > 0) {
	/* Check whether arguments are ZDDs */
	DdNode *an = get_ddnode(mgr, aref);
	DdNode *bn = get_ddnode(mgr, bref);
	DdNode *rn;
	bool za = is_zdd(mgr, aref);
	bool zb = is_zdd(mgr, bref);
	if (za || zb) {
	    zdd = true;
	    /* Make sure they're both ZDDs */
	    if (!za) {
		an = zconvert(mgr, an);
	    }
	    if (!zb) {
		bn = zconvert(mgr, bn);
	    }
	    rn = Cudd_zddIntersect(mgr->bdd_manager, an, bn);
	    reference_dd(mgr, rn);
	    r = dd2ref(rn, true);
	    add_ref(mgr, r, rn);
	    if (!za)
		unreference_dd(mgr, an, true);
	    if (!zb)
		unreference_dd(mgr, bn, true);
	}
    }
    if (!zdd)
	r = shadow_ite(mgr, aref, bref, shadow_zero(mgr));

#if RPT >= 4
    char buf1[24], buf2[24], buf3[24];
    shadow_show(mgr, aref, buf1);
    shadow_show(mgr, bref, buf2);
    shadow_show(mgr, r, buf3);
    report(4, "%s %s %s --> %s", buf1, zdd ? "ZAND" : "AND", buf2, buf3);
#endif
    return r;
}

ref_t shadow_or(shadow_mgr mgr, ref_t aref, ref_t bref) {
    ref_t r = REF_ONE;
    bool zdd = false;
    if (mgr->do_cudd && mgr->nzvars > 0) {
	/* Check whether arguments are ZDDs */
	DdNode *an = get_ddnode(mgr, aref);
	DdNode *bn = get_ddnode(mgr, bref);
	DdNode *rn;
	bool za = is_zdd(mgr, aref);
	bool zb = is_zdd(mgr, bref);
	if (za || zb) {
	    zdd = true;
	    /* Make sure they're both ZDDs */
	    if (!za) {
		an = zconvert(mgr, an);
	    }
	    if (!zb) {
		bn = zconvert(mgr, bn);
	    }
	    rn = Cudd_zddUnion(mgr->bdd_manager, an, bn);
	    reference_dd(mgr, rn);
	    r = dd2ref(rn, true);
	    add_ref(mgr, r, rn);
	    if (!za)
		unreference_dd(mgr, an, true);
	    if (!zb)
		unreference_dd(mgr, bn, true);
	}
    }
    if (!zdd)
	r = shadow_ite(mgr, aref, shadow_one(mgr), bref);

#if RPT >= 4
    char buf1[24], buf2[24], buf3[24];
    shadow_show(mgr, aref, buf1);
    shadow_show(mgr, bref, buf2);
    shadow_show(mgr, r, buf3);
    report(4, "%s %s %s --> %s", buf1, zdd ? "ZOR" : "OR", buf2, buf3);
#endif
    return r;
}

ref_t shadow_xor(shadow_mgr mgr, ref_t aref, ref_t bref) {
    ref_t r = REF_ZERO;
    bool zdd = false;
    if (mgr->do_cudd && mgr->nzvars > 0) {
	/* Check whether arguments are ZDDs */
	DdNode *an = get_ddnode(mgr, aref);
	DdNode *bn = get_ddnode(mgr, bref);
	DdNode *rn;
	bool za = is_zdd(mgr, aref);
	bool zb = is_zdd(mgr, bref);
	if (za || zb) {
	    zdd = true;
	    /* Make sure they're both ZDDs */
	    if (!za) {
		an = zconvert(mgr, an);
	    }
	    if (!zb) {
		bn = zconvert(mgr, bn);
	    }
	    rn = Cudd_zddSymmetricDiff(mgr->bdd_manager, an, bn);
	    reference_dd(mgr, rn);
	    r = dd2ref(rn, true);
	    add_ref(mgr, r, rn);
	    if (!za)
		unreference_dd(mgr, an, true);
	    if (!zb)
		unreference_dd(mgr, bn, true);
	}
    }
    if (!zdd)
	r = shadow_ite(mgr, aref, shadow_negate(mgr, bref), bref);

#if RPT >= 4
    char buf1[24], buf2[24], buf3[24];
    shadow_show(mgr, aref, buf1);
    shadow_show(mgr, bref, buf2);
    shadow_show(mgr, r, buf3);
    report(4, "%s %s %s --> %s", buf1, zdd ? "ZXOR" : "XOR", buf2, buf3);
#endif
    return r;
}

bool shadow_gc_check(shadow_mgr mgr) {
    if (mgr->do_local)
	return ref_gc_check(mgr->ref_mgr);
    else
	return false;
}

/* Only call this when removing r from unique table */
void shadow_deref(shadow_mgr mgr, ref_t r) {
    if (!mgr->do_cudd)
	return;
    DdNode *n = get_ddnode(mgr, r);
    bool zdd = is_zdd(mgr, r);

#if RPT >= 5
    char buf[24];
    shadow_show(mgr, r, buf);
    report(5, "Deleting reference %s for node %p", buf, n);
#endif

    unreference_dd(mgr, n, zdd);

    if (!do_ref(mgr))
	return;

    keyvalue_remove(mgr->c2r_table, (word_t ) n, NULL, NULL);
    keyvalue_remove(mgr->r2c_table, (word_t ) r, NULL, NULL);


    if (!zdd) {
	ref_t nr = shadow_negate(mgr, r);
	DdNode *nn = get_ddnode(mgr, nr);
#if RPT >= 5
	shadow_show(mgr, nr, buf);
	report(5, "Deleting reference %s for node %p", buf, nn);
#endif
	keyvalue_remove(mgr->c2r_table, (word_t ) nn, NULL, NULL);
	keyvalue_remove(mgr->r2c_table, (word_t ) nr, NULL, NULL);
	unreference_dd(mgr, nn, false);
    }
}

void shadow_satisfy(shadow_mgr mgr, ref_t r) {
    if (!mgr->do_cudd)
	return;
    DdNode *n = get_ddnode(mgr, r);
    bool zdd = is_zdd(mgr, r);
    if (zdd) {
	Cudd_zddPrintMinterm(mgr->bdd_manager, n);
	Cudd_zddPrintDebug(mgr->bdd_manager, n, mgr->nzvars, 4);
    }
    else {
	Cudd_PrintMinterm(mgr->bdd_manager, n);
	Cudd_PrintDebug(mgr->bdd_manager, n, mgr->nvars, 4);
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

static keyvalue_table_ptr cudd_count(shadow_mgr mgr, set_ptr roots) {
    word_t wk, wv;
    keyvalue_table_ptr result = word_keyvalue_new();
    set_iterstart(roots);
    while (set_iternext(roots, &wk)) {
	ref_t r = (ref_t) wk;
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
	wv = (word_t) fv;
#endif
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
size_t cudd_size(shadow_mgr mgr, set_ptr roots) {
    if (!mgr->do_cudd)
	return 0;
    size_t nele = roots->nelements;
    DdNode **croots = calloc_or_fail(nele, sizeof(DdNode *), "cudd_size");
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
    unreference_dd(mgr, cube, false);
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
    DdNode *zn = zconvert(mgr, n);
    ref_t zr = dd2ref(zn, true);
    add_ref(mgr, zr, zn);
    return zr;
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
    unreference_dd(mgr, cube, false);
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

