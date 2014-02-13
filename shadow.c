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

/* Trick:
   When running CUDD only, use DdNode * value serve as an artificial ref.
   When not running CUDD, use refs as DdNode *values
*/

bool fatal = true;

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
    } else if (!keyvalue_find(mgr->r2c_table, (word_t ) r, (word_t *) &n)) {
	char buf[24];
	shadow_show(mgr, r, buf);
	err(fatal, "No node associated with ref %s (0x%llx)", buf, r);
    }
    return n;
}

static void add_ref(shadow_mgr mgr, ref_t r, DdNode *n) {
    ref_t rother;
    DdNode *nother;
    if (!do_ref(mgr))
	r = (ref_t) n;
    if (!mgr->do_cudd)
	n = (DdNode *) r;
    bool rother_found = keyvalue_find(mgr->c2r_table, (word_t ) n, (word_t *) &rother);
    bool nother_found = keyvalue_find(mgr->r2c_table, (word_t ) r, (word_t *) &nother);
    char buf1[24];
    char buf2[24];
    /* See if already found entry */
    if (rother_found) {
	if (nother_found) {
	    /* Already exists.  Check if match */
	    if (nother != n || rother != r) {
		shadow_show(mgr, r, buf1);
		shadow_show(mgr, rother, buf2);
		err(fatal, "Inconsistency.  New node 0x%p.  Old node 0x%p.  New ref %s.  Old ref %s",
		    n, nother, buf1, buf2);
	    }
	} else {
	    shadow_show(mgr, r, buf1);
	    shadow_show(mgr, rother, buf2);
	    err(fatal, "Ref Collision.  Refs %s and %s map to BDD node 0x%p", buf1, buf2, nother);
	}
    } else {
	if (nother_found) {
	    shadow_show(mgr, r, buf1);
	    err(fatal, "Node collision.  Nodes 0x%p and 0x%p map to ref %s\n", n, nother, buf1);
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
    shadow_mgr mgr = (shadow_mgr) malloc_or_fail(sizeof(shadow_ele), "new_shadow_mgr");
    mgr->do_cudd = do_cudd;
    mgr->do_local = do_local;
    mgr->do_dist = do_dist;
    ref_t r;
    DdNode *n;
    if (do_cudd) {
	mgr->bdd_manager = Cudd_Init(0, 0, CUDD_UNIQUE_SLOTS, CUDD_CACHE_SLOTS, 0);
	n = Cudd_ReadLogicZero(mgr->bdd_manager);
    }
    if (do_ref(mgr)) {
	mgr->ref_mgr = new_ref_mgr();
	r = REF_ZERO;
	if (!do_cudd) {
	    n = (DdNode *) r;
	}
    }
    mgr->c2r_table = word_keyvalue_new();
    mgr->r2c_table = word_keyvalue_new();
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
/* Create key-value table mapping set of root nodes to their densities. */
keyvalue_table_ptr shadow_density(shadow_mgr mgr, set_ptr roots) {
    keyvalue_table_ptr ldensity = NULL;
    keyvalue_table_ptr ddensity = NULL;
    if (mgr->do_local)
	ldensity = ref_density(mgr->ref_mgr, roots);
    if (mgr->do_dist) {
	ddensity = dist_density(mgr->ref_mgr, roots);
    }
    if (ldensity && ddensity) {
	set_iterstart(roots);
	word_t wr;
	double dl, dd;
	while (set_iternext(roots, &wr)) {
	    ref_t r = (ref_t) wr;
	    dl = get_double(ldensity, r);
	    dd = get_double(ddensity, r);
	    if (dl != dd) {
		char buf[24];
		ref_show(r, buf);
		err(false, "Density mismatch for %s.  local = %.2f, distance = %.2f",
		    buf, dl, dd);
	    }
	}
	keyvalue_free(ddensity);
    }
    if (ldensity)
	return ldensity;
    else
	return ddensity;
}

/* Compute set of variables (given by refs) in support of set of roots */
set_ptr shadow_support(shadow_mgr mgr, set_ptr roots) {
    set_ptr lsupport = NULL;
    set_ptr dsupport = NULL;
    if (mgr->do_local)
	lsupport = ref_support(mgr->ref_mgr, roots);
    if (mgr->do_dist)
	dsupport = dist_support(mgr->ref_mgr, roots);
    if (lsupport && dsupport) {
	word_t wr;
	set_iterstart(lsupport);
	while(set_iternext(lsupport, &wr)) {
	    if (!set_member(dsupport, wr, true)) {
		char buf[24];
		ref_t r = (ref_t) wr;
		ref_show(r, buf);
		err(false, "Found %s in local support, but not in dist support");
	    }
	}
	/* See if there are any remaining dist members */
	set_iterstart(dsupport);
	while (set_iternext(dsupport, &wr)) {
	    char buf[24];
	    ref_t r = (ref_t) wr;
	    ref_show(r, buf);
	    err(false, "Found %s in dist support, but not in local support");
	}
	set_free(dsupport);
    }
    if (lsupport)
	return lsupport;
    else
	return dsupport;
}



/* Create key-value table mapping set of root nodes to their restrictions,
   with respect to a set of literals (given as a set of refs)
*/
keyvalue_table_ptr shadow_restrict(shadow_mgr mgr, set_ptr roots, set_ptr lits) {
    keyvalue_table_ptr lrmap = NULL;
    keyvalue_table_ptr drmap = NULL;
    if (mgr->do_local)
	lrmap = ref_restrict(mgr->ref_mgr, roots, lits);
    if (mgr->do_dist)
	drmap = dist_restrict(mgr->ref_mgr, roots, lits);
    if (lrmap && drmap) {
	word_t wk, wv1, wv2;
	keyvalue_iterstart(lrmap);
	while(keyvalue_iternext(lrmap, &wk, &wv1)) {
	    if (!keyvalue_remove(drmap, wk, NULL, &wv2)) {
		char buf[24];
		ref_t r = (ref_t) wk;
		ref_show(r, buf);
		err(false, "Found %s in local restriction, but not in dist restriction");
	    }
	    else if (wv1 != wv2) {
		char buf[24], buf1[24], buf2[24];
		ref_show((ref_t) wk, buf); ref_show((ref_t) wv1, buf1); ref_show((ref_t) wv2, buf2);
		err(false, "Restriction of %s gives %s in local, but %s in dist", buf, buf1, buf2);
	    }
	}
	/* See if there are any remaining dist members */
	keyvalue_iterstart(drmap);
	while (keyvalue_iternext(drmap, &wk, &wv1)) {
	    char buf[24]; char buf1[24];
	    ref_show((ref_t) wk, buf); ref_show((ref_t) wv1, buf1);
	    err(false, "%s has restriction %s in dist map, but none in local map", buf, buf1);
	}
	keyvalue_free(drmap);
    }
    if (lrmap)
	return lrmap;
    else
	return drmap;
}

/* Create key-value table mapping set of root nodes to their
   existential quantifications with respect to a set of variables
   (given as a set of refs)
*/
keyvalue_table_ptr shadow_equant(shadow_mgr mgr, set_ptr roots, set_ptr vars) {
    keyvalue_table_ptr lmap = NULL;
    keyvalue_table_ptr dmap = NULL;
    if (mgr->do_local)
	lmap = ref_equant(mgr->ref_mgr, roots, vars);
    if (mgr->do_dist)
	dmap = dist_equant(mgr->ref_mgr, roots, vars);
    if (lmap && dmap) {
	word_t wk, wv1, wv2;
	keyvalue_iterstart(lmap);
	while(keyvalue_iternext(lmap, &wk, &wv1)) {
	    if (!keyvalue_remove(dmap, wk, NULL, &wv2)) {
		char buf[24];
		ref_t r = (ref_t) wk;
		ref_show(r, buf);
		err(false, "Found %s in local quantification, but not in dist quantification");
	    }
	    else if (wv1 != wv2) {
		char buf[24], buf1[24], buf2[24];
		ref_show((ref_t) wk, buf); ref_show((ref_t) wv1, buf1); ref_show((ref_t) wv2, buf2);
		err(false, "Quantification of %s gives %s in local, but %s in dist", buf, buf1, buf2);
	    }
	}
	/* See if there are any remaining dist members */
	keyvalue_iterstart(dmap);
	while (keyvalue_iternext(dmap, &wk, &wv1)) {
	    char buf[24]; char buf1[24];
	    ref_show((ref_t) wk, buf); ref_show((ref_t) wv1, buf1);
	    err(false, "%s has quantification %s in dist map, but none in local map", buf, buf1);
	}
	keyvalue_free(dmap);
    }
    if (lmap)
	return lmap;
    else
	return dmap;
}

/* Create key-value table mapping set of root nodes to their shifted versions
   with respect to a mapping from old variables to new ones 
*/
keyvalue_table_ptr shadow_shift(shadow_mgr mgr, set_ptr roots, keyvalue_table_ptr vmap) {
    keyvalue_table_ptr lmap = NULL;
    keyvalue_table_ptr dmap = NULL;
    if (mgr->do_local)
	lmap = ref_shift(mgr->ref_mgr, roots, vmap);
    if (mgr->do_dist)
	dmap = dist_shift(mgr->ref_mgr, roots, vmap);
    if (lmap && dmap) {
	word_t wk, wv1, wv2;
	keyvalue_iterstart(lmap);
	while(keyvalue_iternext(lmap, &wk, &wv1)) {
	    if (!keyvalue_remove(dmap, wk, NULL, &wv2)) {
		char buf[24];
		ref_t r = (ref_t) wk;
		ref_show(r, buf);
		err(false, "Found %s in local shift, but not in dist shift");
	    }
	    else if (wv1 != wv2) {
		char buf[24], buf1[24], buf2[24];
		ref_show((ref_t) wk, buf); ref_show((ref_t) wv1, buf1); ref_show((ref_t) wv2, buf2);
		err(false, "Shift of %s gives %s in local, but %s in dist", buf, buf1, buf2);
	    }
	}
	/* See if there are any remaining dist members */
	keyvalue_iterstart(dmap);
	while (keyvalue_iternext(dmap, &wk, &wv1)) {
	    char buf[24]; char buf1[24];
	    ref_show((ref_t) wk, buf); ref_show((ref_t) wv1, buf1);
	    err(false, "%s has shift %s in dist map, but none in local map", buf, buf1);
	}
	keyvalue_free(dmap);
    }
    if (lmap)
	return lmap;
    else
	return dmap;
}


/* Garbage collection.  Find all nodes reachable from roots and keep only those in unique table */
void shadow_collect(shadow_mgr mgr, set_ptr roots) {

}
