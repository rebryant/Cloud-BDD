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
#include "util.h"
#include "shadow.h"

/* Trick:
   When running CUDD only, use DdNode * value serve as an artificial ref.
   When not running CUDD, use refs as DdNode *values
*/

bool fatal = false;

bool do_ref(shadow_mgr mgr) {
    return mgr->do_local || mgr->do_dist;
}

void shadow_show(shadow_mgr mgr, ref_t r, char *buf) {
    if (do_ref(mgr)) {
	ref_show(r, buf);
    } else {
	sprintf(buf, "%p", (DdNode *) r);
    }		
}

static DdNode *get_ddnode(shadow_mgr mgr, ref_t r) {
    DdNode *n;
    if (!do_ref(mgr)) {
	n = (DdNode *) r;
    } else if (REF_IS_INVALID(r)) {
	err(fatal, "No node associated with invalid ref");
	n = Cudd_ReadLogicZero(mgr->bdd_manager);
    } else if (!keyvalue_find(mgr->r2c_table, (word_t ) r, (word_t *) &n)) {
	char buf[24];
	shadow_show(mgr, r, buf);
	err(fatal, "No node associated with ref %s (0x%llx)", buf, r);
	n = mgr->do_cudd ? Cudd_ReadLogicZero(mgr->bdd_manager) :
	    (DdNode *) REF_ZERO;
    } 
    return n;
}

static void add_ref(shadow_mgr mgr, ref_t r, DdNode *n) {
    ref_t rother;
    DdNode *nother;
    if (do_ref(mgr) && REF_IS_INVALID(r)) {
	/* Don't record matches with invalid refs */
	return;
    }
    if (!do_ref(mgr))
	r = (ref_t) n;
    if (!mgr->do_cudd)
	n = (DdNode *) r;
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
	    if (nother != n || rother != r) {
		shadow_show(mgr, r, buf1);
		shadow_show(mgr, rother, buf2);
		err(fatal,
"Inconsistency.  New node 0x%p.  Old node 0x%p.  New ref %s.  Old ref %s",
		    n, nother, buf1, buf2);
	    }
	} else {
	    shadow_show(mgr, r, buf1);
	    shadow_show(mgr, rother, buf2);
	    err(fatal, "Ref Collision.  Refs %s and %s map to BDD node 0x%p",
		buf1, buf2, nother);
	}
    } else {
	if (nother_found) {
	    shadow_show(mgr, r, buf1);
	    err(fatal, "Node collision.  Nodes 0x%p and 0x%p map to ref %s\n",
		n, nother, buf1);
	} else {
	    /* Normal case.  Create both entries */
	    if (verblevel >= 5) {
		char buf[24];
		shadow_show(mgr, r, buf);
		report(5, "Added ref %s for node %p", buf, n);
	    }
	    keyvalue_insert(mgr->c2r_table, (word_t ) n, (word_t ) r);
	    keyvalue_insert(mgr->r2c_table, (word_t ) r, (word_t ) n);
	    /* Create entries for negations */
	    ref_t rn = shadow_negate(mgr, r);
	    DdNode *nn;
	    if (mgr->do_cudd) {
		nn = Cudd_Not(n);
	    } else {
		nn = (DdNode *) rn;
	    }
	    if (verblevel >= 5) {
		char buf[24];
		shadow_show(mgr, rn, buf);
		report(5, "Added ref %s for node %p", buf, nn);
	    }
	    keyvalue_insert(mgr->c2r_table, (word_t ) nn, (word_t ) rn);
	    keyvalue_insert(mgr->r2c_table, (word_t ) rn, (word_t ) nn);
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
    } else if (verblevel >= 4)  {
	char buflocal[24];
	shadow_show(mgr, rlocal, buflocal);
	report(4, "Matching refs.  Local = Dist = %s", buflocal);
    }
    return true;
}

shadow_mgr new_shadow_mgr(bool do_cudd, bool do_local, bool do_dist) {
    if (!(do_cudd || do_local || do_dist)) {
	err(true, "Must have at least one active evaluation mode");
    }
    shadow_mgr mgr = (shadow_mgr) malloc_or_fail(sizeof(shadow_ele),
						 "new_shadow_mgr");
    mgr->do_cudd = do_cudd;
    mgr->do_local = do_local;
    mgr->do_dist = do_dist;
    ref_t r = REF_ZERO;
    DdNode *n = NULL;
    if (do_cudd) {
      /* Modified CUDD Parameters */
      unsigned int numVars = 1u<<8; /* Default 0 */
      unsigned int numVarsZ = 0; /* Default 0 */
      unsigned int numSlots = 1u<<18; /* Default 256 */
      unsigned int cacheSize = 1u<<22; /* Default 262144 */
      unsigned int maxMemory = 1u<<31; /* Default 67,108,864 */
      mgr->bdd_manager = Cudd_Init(numVars, numVarsZ, numSlots, cacheSize, maxMemory);
        n = Cudd_ReadLogicZero(mgr->bdd_manager);
    }
    if (do_ref(mgr)) {
	mgr->ref_mgr = new_ref_mgr();
	if (!do_cudd) {
	    n = (DdNode *) r;
	}
    }
    mgr->c2r_table = word_keyvalue_new();
    mgr->r2c_table = word_keyvalue_new();
    add_ref(mgr, r, n);
    mgr->nvars = 0;
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
    else
	return (ref_t) Cudd_ReadOne(mgr->bdd_manager);
}

ref_t shadow_zero(shadow_mgr mgr) {
    if (do_ref(mgr))
	return REF_ZERO;
    else
	return (ref_t) Cudd_ReadLogicZero(mgr->bdd_manager);
}

ref_t shadow_new_variable(shadow_mgr mgr) {
    ref_t r = 0;
    DdNode *n = NULL;
    if (mgr->do_cudd) {
	n = Cudd_bddNewVar(mgr->bdd_manager);
	Cudd_Ref(n);
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
	n = (DdNode *) r;
    if (!do_ref(mgr)) 
	r = (ref_t) n;
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
    if (do_ref(mgr)) {
	r = REF_VAR(index);
    } else {
	DdNode *n = Cudd_bddIthVar(mgr->bdd_manager, index);
	r = (ref_t) n;
    }
    return r;
}

ref_t shadow_ite(shadow_mgr mgr, ref_t iref, ref_t tref, ref_t eref) {
    DdNode *in = get_ddnode(mgr, iref);
    DdNode *tn = get_ddnode(mgr, tref);
    DdNode *en = get_ddnode(mgr, eref);
    ref_t r = 0;
    DdNode *n = NULL;
    if (mgr->do_cudd) {
	n = Cudd_bddIte(mgr->bdd_manager, in, tn, en);
	Cudd_Ref(n);
    }
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
    if (!mgr->do_cudd)
	n = (DdNode *) r;
    if (!do_ref(mgr))
	r = (ref_t) n;
    add_ref(mgr, r, n);
    return r;
}

ref_t shadow_negate(shadow_mgr mgr, ref_t r) {
    if (do_ref(mgr))
	return REF_NEGATE(r);
    else 
	return (ref_t) Cudd_Not((DdNode *) r);
}

ref_t shadow_absval(shadow_mgr mgr, ref_t r) {
    if (do_ref(mgr))
	return REF_ABSVAL(r);
    else 
	return (ref_t) Cudd_Regular((DdNode *) r);
}

ref_t shadow_and(shadow_mgr mgr, ref_t aref, ref_t bref) {
    ref_t r = shadow_ite(mgr, aref, bref, shadow_zero(mgr));
    if (verblevel >= 4) {
	char buf1[24], buf2[24], buf3[24];
	shadow_show(mgr, aref, buf1);
	shadow_show(mgr, bref, buf2);
	shadow_show(mgr, r, buf3);
	report(4, "%s AND %s --> %s", buf1, buf2, buf3);
    }
    return r;
}

ref_t shadow_or(shadow_mgr mgr, ref_t aref, ref_t bref) {
    ref_t r = shadow_ite(mgr, aref, shadow_one(mgr), bref);
    if (verblevel >= 4) {
	char buf1[24], buf2[24], buf3[24];
	shadow_show(mgr, aref, buf1);
	shadow_show(mgr, bref, buf2);
	shadow_show(mgr, r, buf3);
	report(4, "%s OR %s --> %s", buf1, buf2, buf3);
    }
    return r;
}

ref_t shadow_xor(shadow_mgr mgr, ref_t aref, ref_t bref) {
    ref_t r = shadow_ite(mgr, aref, shadow_negate(mgr, bref), bref);
    if (verblevel >= 4) {
	char buf1[24], buf2[24], buf3[24];
	shadow_show(mgr, aref, buf1);
	shadow_show(mgr, bref, buf2);
	shadow_show(mgr, r, buf3);
	report(4, "%s XOR %s --> %s", buf1, buf2, buf3);
    }
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
    DdNode *n = get_ddnode(mgr, r);
    ref_t nr = shadow_negate(mgr, r);
    DdNode *nn = get_ddnode(mgr, nr);
    if (mgr->do_cudd) {
	Cudd_RecursiveDeref(mgr->bdd_manager, n);
    }
    if (verblevel >= 5) {
	char buf[24];
	shadow_show(mgr, r, buf);
	report(5, "Deleting reference %s for node %p", buf, n);
	shadow_show(mgr, nr, buf);
	report(5, "Deleting reference %s for node %p", buf, nn);
    }
    keyvalue_remove(mgr->c2r_table, (word_t ) n, NULL, NULL);
    keyvalue_remove(mgr->r2c_table, (word_t ) r, NULL, NULL);
    keyvalue_remove(mgr->c2r_table, (word_t ) nn, NULL, NULL);
    keyvalue_remove(mgr->r2c_table, (word_t ) nr, NULL, NULL);
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

static word_t apa2word(DdApaNumber num, int digits) {
    word_t val = 0;
    int plim = sizeof(word_t) / sizeof(DdApaDigit);
    int i;
    int pos;
    for (i = digits-1, pos = 0; i >= 0 && pos < plim; i--, pos++) {
	printf("digits = %u.  num[%d] = %u, pos = %d\n", digits, i, num[i], pos);
	val += (word_t) num[i] << (pos * sizeof(DdApaDigit) * 8);
    }
    return val;
}

static keyvalue_table_ptr cudd_count(shadow_mgr mgr, set_ptr roots) {
    word_t wk, wv;
    keyvalue_table_ptr result = word_keyvalue_new();
    size_t nvars = mgr->nvars;
    int digits;
    set_iterstart(roots);
    while (set_iternext(roots, &wk)) {
	DdNode *n = (DdNode *) wk;
	DdApaNumber num = Cudd_ApaCountMinterm(mgr->bdd_manager, n, nvars,
					       &digits);
	wv = apa2word(num, digits);
	FREE(num);
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
    Cudd_Ref(cube);
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
	Cudd_Ref(nr);
	keyvalue_insert(rtable, w, (word_t) nr);
    }
    Cudd_RecursiveDeref(mgr->bdd_manager, cube);
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
	    DdNode *n = (DdNode *) rr;
	    add_ref(mgr, rr, n);
	}
    }
    return map;
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
	Cudd_Ref(nr);
	keyvalue_insert(etable, w, (word_t) nr);
    }
    Cudd_RecursiveDeref(mgr->bdd_manager, cube);
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
	Cudd_Ref(ns);
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

