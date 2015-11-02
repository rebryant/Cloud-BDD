/* Implementation of ref-based BDD */
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

/*
  Parameters controlling garbage collection
 */

/* Max ratio between current number of unique nodes
   and number at end of previous GC */
#define GC_RATIO 2

/* Minimum node count below which will not trigger GC */
#define GC_THRESHOLD 100000

/* Unique table uses a linked list for each possible hash value. */

/*
  Distinguish among elements having same hash with unique ID,
  having value >= 1.
  These are encoded as part of the ref for the function
*/

typedef struct LELE {
    ref_t ref;
    chunk_ptr data; /* Chunk containing vref, hiref, loref */
    struct LELE *next;
} ulist_ele, *ulist_ptr;

/* Create new element for ulist */
static ulist_ptr ulist_new(chunk_ptr data, ref_t ref) {
    ulist_ptr result =  (ulist_ptr) malloc_or_fail(sizeof(ulist_ele),
						   "ulist_new");
    result->data = data;
    result->ref = ref;
    result->next = NULL;
    return result;
}
/* Free all elements in a ulist */
static void ulist_free(ulist_ptr ele) {
    while (ele) {
	ulist_ptr nele = ele->next;
	chunk_free(ele->data);
	free_block((void *) ele, sizeof(ulist_ele));
	ele = nele;
    }
}

/* Encapsulate 3 refs in chunk */
chunk_ptr ref3_encode(ref_t r1, ref_t r2, ref_t r3) {
    chunk_ptr ucp = chunk_new(3);
    chunk_insert_word(ucp, (word_t) r1, 0);
    chunk_insert_word(ucp, (word_t) r2, 1);
    chunk_insert_word(ucp, (word_t) r3, 2);
    return ucp;
}

/* Compute hash code for vref, hiref, loref.  Used to create hash for ref */
size_t utable_hash(chunk_ptr ucp) {
    size_t val = chunk_hash((word_t) ucp);
    return (val % (2147483629UL)) & REF_MASK_HASH;
}

/* Functions to use key/value table to implement unique table */
/* Use refs as keys, aliased to void* */
/* Use hash values as keys in unique table */
size_t ref_hash(word_t vval) {
    ref_t r = (ref_t) vval;
    size_t h = REF_GET_HASH(r);
    return h;
}

bool ref_hash_equal(word_t vval1, word_t vval2) {
    word_t h1 = REF_GET_HASH((ref_t) vval1);
    word_t h2 = REF_GET_HASH((ref_t) vval2);
    return h1 == h2;
}

ref_mgr new_ref_mgr() {
    ref_mgr mgr = malloc_or_fail(sizeof(ref_mgr_ele), "new_mgr");
    mgr->variable_cnt = 0;
    mgr->unique_table = keyvalue_new(word_hash, word_equal);
    mgr->ite_table = keyvalue_new(chunk_hash, chunk_equal);
    size_t i;
    for (i = 0; i < NSTAT; i++)
	mgr->stat_counter[i] = 0;
    mgr->last_nelements = 0;
    return mgr;
}

ref_t ref_new_variable(ref_mgr mgr) {
    int idx = mgr->variable_cnt++;
    return REF_VAR(idx);
}


/* Function to clear out lists in unique table */
static void clear_ulist(word_t key, word_t value) {
    ulist_ptr ulist = (ulist_ptr) value;
    ulist_free(ulist);
}

/* Function to remove keys from ITE cache */
static void clear_ite_entry(word_t key, word_t value) {
    chunk_ptr cp = (chunk_ptr) key;
    chunk_free(cp);
}

/* Clear ITE cache */
static void clear_ite_table(ref_mgr mgr) {
    keyvalue_apply(mgr->ite_table, clear_ite_entry);
    keyvalue_free(mgr->ite_table);
    mgr->ite_table = keyvalue_new(chunk_hash, chunk_equal);
    mgr->stat_counter[STATB_ITEC_CURR] = 0;
}

void free_ref_mgr(ref_mgr mgr) {
    keyvalue_apply(mgr->unique_table, clear_ulist);
    keyvalue_free(mgr->unique_table);
    clear_ite_table(mgr);
    keyvalue_free(mgr->ite_table);
    free_block(mgr, sizeof(ref_mgr_ele));
}

/* Create string representation of ref */
void ref_show(ref_t r, char *buf) {
    char nc = REF_GET_NEG(r) ? '-' : '+';
    int var = REF_GET_VAR(r);
    word_t hash = REF_GET_HASH(r);
    int uniq = REF_GET_UNIQ(r);
    switch(REF_GET_TYPE(r)) {
    case BDD_NULL:
	sprintf(buf, "NULL");
	break;
    case BDD_CONSTANT:
	sprintf(buf, "%cC", nc);
	break;
    case BDD_VARIABLE:
	sprintf(buf, "%cV.%d", nc, var);
	break;
    case BDD_FUNCTION:
	sprintf(buf, "%cF.%d.%" PRIx64 "+%d", nc, var, hash, uniq);
	break;
    case BDD_RECURSE:
	sprintf(buf, "%cR.%d.%" PRIx64 "+%d", nc, var, hash, uniq);
	break;
    case BDD_INVALID:
	sprintf(buf, "%cI.%d.%" PRIx64 "+%d", nc, var, hash, uniq);
	break;
    default:
	sprintf(buf, "%c?.%d.%" PRIx64 "+%d", nc, var, hash, uniq);
	break;
    }
}


/* Do preparatory steps in canonize.
   Return ref_t if completed, and either REF_RECURSE or its negation if not.
   Variants of REF_RECURSE indicate whether or not final value should be negated.
   In latter case, set cpp to chunk_ptr that can serve as key to unique table
*/
ref_t ref_canonize_local(ref_t vref, ref_t hiref, ref_t loref, chunk_ptr *ucpp) {
    if (hiref == REF_INVALID || loref == REF_INVALID)
	return REF_INVALID;
    word_t vlev = REF_GET_VAR(vref);
    word_t hilev = REF_GET_VAR(hiref);
    word_t lolev = REF_GET_VAR(loref);
    if (vlev >= hilev || vlev >= lolev) {
	err(false, "Invalid levels for canonize.  var:%lu, hi:%lu, lo:%lu",
	    vlev, hilev, lolev);
	return REF_INVALID;
    }
    if (hiref==loref)
	return hiref;
    if (hiref==REF_ONE && loref==REF_ZERO)
	return vref;
    if (hiref==REF_ZERO && loref==REF_ONE)
	return REF_NEGATE(vref);
    ref_t return_val = REF_RECURSE;
    if (REF_GET_NEG(hiref)) {
	return_val = REF_NEGATE(return_val);
	hiref = REF_NEGATE(hiref);
	loref = REF_NEGATE(loref);
    }
    chunk_ptr ucp = ref3_encode(vref, hiref, loref);
    *ucpp = ucp;
    return return_val;
}

/* Perform lookup / creation portion of canonize.
   Assumes arguments already fixed up and stored in form
   suitable for unique table
*/
static ref_t ref_canonize_lookup(ref_mgr mgr, chunk_ptr ucp) {
    size_t h = utable_hash(ucp);
    ref_t vref = chunk_get_word(ucp, 0);
    ulist_ptr ls = NULL;
    bool have_list = keyvalue_find(mgr->unique_table,
				   (word_t) h, (word_t *) &ls);
    bool found = false;
    /* Keep track of location pointing to last element in list */
    ulist_ptr *tailp = NULL;
    /* Keep track of largest uniquifier encountered in list */
    size_t largest_used = 0;
    ref_t r;
    /* Traverse list, looking for entry with matching var, hi, lo */
    while (ls) {
	if (chunk_equal((word_t) ucp, (word_t) ls->data)) {
	    /* Found entry */
	    r = ls->ref;
	    found = true;
	    break;
	}
	size_t uniquifier = REF_GET_UNIQ(ls->ref);
	if (uniquifier > largest_used)
	    largest_used = uniquifier;
	tailp = &ls->next;
	ls = ls->next;
    }
    if (found) {
	/* Don't need this chunk */
	chunk_free(ucp);
    } else {
	/* Came to end of list without finding matching entry.
	   Create a new one. */
	size_t uniquifier = largest_used + 1;
	/* See if have exceeded bounds for uniquifier */
	if (uniquifier > REF_MASK_UNIQ)
	    err(true, "Exceeded uniquifier bounds.  Hash = 0x%llx", h);
	r = PACK_REF(0, BDD_FUNCTION, REF_GET_VAR(vref), h, uniquifier);
	ls = ulist_new(ucp, r);
	if (verblevel >= 4) {
	    ref_t vref = chunk_get_word(ucp, 0);
	    ref_t hiref = chunk_get_word(ucp, 1);
	    ref_t loref = chunk_get_word(ucp, 2);
	    char vbuf[24], hibuf[24], lobuf[24], rbuf[24];
	    ref_show(vref, vbuf);
	    ref_show(hiref, hibuf);
	    ref_show(loref, lobuf);
	    ref_show(r, rbuf);
	    report(4, "Creating unique table entry [%s,%s,%s] --> %s",
		   vbuf, hibuf, lobuf, rbuf);
	}
	mgr->stat_counter[STATB_UNIQ_CURR]++;
	if (mgr->stat_counter[STATB_UNIQ_CURR]
	    > mgr->stat_counter[STATB_UNIQ_PEAK])
	    mgr->stat_counter[STATB_UNIQ_PEAK]
		= mgr->stat_counter[STATB_UNIQ_CURR];
	mgr->stat_counter[STATB_UNIQ_TOTAL]++;
	if (have_list) {
	    *tailp = ls;
	    mgr->stat_counter[STATB_UNIQ_COLLIDE]++;
	} else {
	    /* First entry for this hash */
	    keyvalue_insert(mgr->unique_table, (word_t) h, (word_t) ls);
	}
    }
    return r;
}

ref_t ref_canonize(ref_mgr mgr, ref_t vref, ref_t hiref, ref_t loref) {
    chunk_ptr ucp;
    ref_t r = ref_canonize_local(vref, hiref, loref, &ucp);
    if (REF_IS_RECURSE(r)) {
	size_t neg = REF_GET_NEG(r);
	r = ref_canonize_lookup(mgr, ucp);
	if (neg)
	    r = REF_NEGATE(r);
    }
    return r;
}

/* Do dereferencing steps that can be done locally.
   Return true if successful */
bool ref_deref_local(ref_t r, ref_t *vrefp, ref_t *hirefp, ref_t *lorefp) {
    switch (REF_GET_TYPE(r)) {
    case BDD_CONSTANT:
	*vrefp = r; *hirefp = r; *lorefp = r;
	return true;
    case BDD_VARIABLE:
	*vrefp = r;
	if (REF_GET_NEG(r)) {
	    *hirefp = REF_ZERO; *lorefp = REF_ONE;
	} else {
	    *hirefp = REF_ONE; *lorefp = REF_ZERO;
	}
	return true;
    case BDD_FUNCTION:
	return false;
    case BDD_RECURSE:
    case BDD_INVALID:
	err(false, "Invalid reference encountered during dereferencing");
	return false;
    default:
	err(false, "Unexpected ref type %d\n", REF_GET_TYPE(r));
	return false;
    }
}

chunk_ptr ref_deref_lookup(ref_mgr mgr, ref_t r) {
    if (REF_GET_TYPE(r) != BDD_FUNCTION) {
	err(false, "Attempted to dereference non-function node");
	return NULL;
    }
    size_t h = REF_GET_HASH(r);
    ulist_ptr ls = NULL;
    ulist_ptr ele = ls;
    keyvalue_find(mgr->unique_table, (word_t) h, (word_t *) &ls);
    ele = ls;
    while (ele) {
	if (ele->ref == r)
	    return ele->data;
	ele = ele->next;
    }
    if (ls && verblevel >= 3) {
	char buf[24];
	ref_show(r, buf);
	report(3, "Looking for ref %s.  Found list in hash table, but no entry",
	       buf);
	ele = ls;
	while (ele) {
	    char ebuf[24];
	    ref_show(ele->ref, ebuf);
	    report(3, "\tMismatch with %s", ebuf);
	    ele = ele->next;
	}
    }
    return NULL;
}

void ref_deref(ref_mgr mgr, ref_t r,
	       ref_t *vrefp, ref_t *hirefp, ref_t *lorefp) {
    if (ref_deref_local(r, vrefp, hirefp, lorefp))
	return;
    ref_t ar = REF_ABSVAL(r);
    chunk_ptr cp = ref_deref_lookup(mgr, ar);
    if (cp == NULL) {
	char buf[24];
	ref_show(ar, buf);
	err(false, "Could not find unique table entry for %s", buf);
	*vrefp = *hirefp = *lorefp = REF_INVALID;
	return;
    }
    *vrefp = (ref_t) chunk_get_word(cp, 0);
    ref_t hiref = (ref_t) chunk_get_word(cp, 1);
    ref_t loref = (ref_t) chunk_get_word(cp, 2);
    if (REF_GET_NEG(r)) {
	*hirefp = REF_NEGATE(hiref); *lorefp = REF_NEGATE(loref);
    } else {
	*hirefp = hiref; *lorefp = loref;
    }
}

/* Perform local parts of ITE.
   Return either result or (possibly negated) recurse ref.
   In latter case, set *iucpp to triple with ITE arguments
*/
ref_t ref_ite_local(ref_mgr mgr, ref_t iref,
		    ref_t tref, ref_t eref, chunk_ptr *ucpp) {
    ref_t r;
    ref_t siref = iref;
    ref_t stref = tref;
    ref_t seref = eref;
    mgr->stat_counter[STATB_ITE_CNT]++;
    /* Simple cases */
    if (iref == REF_ONE)
	r = tref;
    else if (iref == REF_ZERO)
	r = eref;
    else if (tref == eref)
	r = tref;
    else if (tref  == REF_ONE && eref == REF_ZERO)
	r = iref;
    else if (tref == REF_ZERO && eref == REF_ONE)
	r = REF_NEGATE(iref);
    else {
	/* Cannot be handled locally.  Fix up arguments */
	bool negate = false;
	/* Don't want iref to be complemented */
	if (REF_GET_NEG(iref)) {
	    ref_t sref = tref;
	    tref = eref;
	    eref = sref;
	    iref = REF_NEGATE(iref);
	}
	/*
	  Don't want tref to be complemented.
	  Handles case where tref is 0.
	  Also does And -> Or through DeMorgan's conversion
	*/
	if (REF_GET_NEG(tref)) {
	    negate = !negate;
	    tref = REF_NEGATE(tref);
	    eref = REF_NEGATE(eref);
	}
	/* Absorption rules */
	if (iref == tref) {
	    tref = REF_ONE;
	    /* Might have terminal case */
	    if (tref == eref)
		return negate ? REF_NEGATE(tref) : tref;
	    if (tref  == REF_ONE && eref == REF_ZERO)
		return negate ? REF_NEGATE(iref) : iref;
	}
	if (iref == eref) {
	    eref = REF_ZERO;
	    /* Might have terminal case */
	    if (tref == eref)
		return negate ? REF_NEGATE(tref) : tref;
	    if (tref  == REF_ONE && eref == REF_ZERO)
		return negate ? REF_NEGATE(iref) : iref;
	}
	if (iref == REF_NEGATE(eref)) {
	    eref = REF_ONE;
	}
	/* Mirrorings */
	/* Canonical ordering of And arguments */
	if (eref == REF_ZERO && iref > tref) {
	    ref_t sref = iref;
	    iref = tref;
	    tref = sref;
	}
	/* Canonical ordering of Xor arguments */
	if (tref == REF_NEGATE(eref) && iref > tref) {
	    ref_t sref = iref;
	    iref = tref;
	    tref = sref;
	    eref = REF_NEGATE(tref);
	}
	*ucpp = ref3_encode(iref, tref, eref);
	r = REF_RECURSE;
	if (negate)
	    r = REF_NEGATE(r);
    }
    if (!REF_IS_RECURSE(r))
	mgr->stat_counter[STATB_ITE_LOCAL_CNT]++;
    if (verblevel >= 4) {
	char sibuf[24], stbuf[24], sebuf[24];
	ref_show(siref, sibuf);
	ref_show(stref, stbuf);
	ref_show(seref, sebuf);
	if (REF_IS_RECURSE(r)) {
	    char ibuf[24], tbuf[24], ebuf[24];
	    ref_show(iref, ibuf);
	    ref_show(tref, tbuf);
	    ref_show(eref, ebuf);
	    char *ns = REF_GET_NEG(r) ? "!" : "";
	    report(4, "ITE Local(%s, %s, %s) -> %sITE(%s,%s,%s)",
		   sibuf, stbuf, sebuf, ns, ibuf, tbuf, ebuf);
	} else {
	    char rbuf[24];
	    ref_show(r, rbuf);
	    report(4, "ITE Local(%s, %s, %s) -> %s",
		   sibuf, stbuf, sebuf, rbuf);
	}
    }
    return r;
}

    
ref_t ref_ite_lookup(ref_mgr mgr, chunk_ptr ucp) {
    ref_t r;
    word_t w;
    if (keyvalue_find(mgr->ite_table, (word_t) ucp, (word_t *) &w)) {
	r = (ref_t) w;
	mgr->stat_counter[STATB_ITE_HIT_CNT]++;
    } else
	r = REF_RECURSE;

    if (verblevel >= 4) {
	ref_t iref = chunk_get_word(ucp, 0);
	ref_t tref = chunk_get_word(ucp, 1);
	ref_t eref = chunk_get_word(ucp, 2);
	char ibuf[24], tbuf[24], ebuf[24], rbuf[24];
	ref_show(iref, ibuf);
	ref_show(tref, tbuf);
	ref_show(eref, ebuf);
	ref_show(r, rbuf);
	report(4, "ITELookup(%s,%s,%s) --> %s",
	       ibuf, tbuf, ebuf, rbuf);
    }
    
    return r;
}

void ref_ite_store(ref_mgr mgr, chunk_ptr ucp, ref_t r) {
    if (verblevel >= 4) {
	ref_t iref = chunk_get_word(ucp, 0);
	ref_t tref = chunk_get_word(ucp, 1);
	ref_t eref = chunk_get_word(ucp, 2);
	char ibuf[24], tbuf[24], ebuf[24], rbuf[24];
	ref_show(iref, ibuf);
	ref_show(tref, tbuf);
	ref_show(eref, ebuf);
	ref_show(r, rbuf);
	report(4, "Storing ITE(%s,%s,%s) --> %s",
	       ibuf, tbuf, ebuf, rbuf);
    }
    keyvalue_insert(mgr->ite_table, (word_t) ucp, (word_t) r);
    mgr->stat_counter[STATB_ITEC_TOTAL]++;
    mgr->stat_counter[STATB_ITEC_CURR]++;
    if (mgr->stat_counter[STATB_ITEC_CURR] > mgr->stat_counter[STATB_ITEC_PEAK])
	mgr->stat_counter[STATB_ITEC_PEAK] = mgr->stat_counter[STATB_ITEC_CURR];
}

/* Recursive calls for ITE */
static void ref_ite_recurse(ref_mgr mgr,
			    ref_t irefhi, ref_t ireflo,
			    ref_t trefhi, ref_t treflo,
			    ref_t erefhi, ref_t ereflo,
			    ref_t *newhip, ref_t *newlop) {
    *newhip = ref_ite(mgr, irefhi, trefhi, erefhi);
    *newlop = ref_ite(mgr, ireflo, treflo, ereflo);
    mgr->stat_counter[STATB_ITE_NEW_CNT]++;
}

/* ITE operation */
ref_t ref_ite(ref_mgr mgr, ref_t iref, ref_t tref, ref_t eref) {
    chunk_ptr ucp;
    ref_t r = ref_ite_local(mgr, iref, tref, eref, &ucp);
    if (!REF_IS_RECURSE(r)) {
	return r;
    }
    bool neg = REF_GET_NEG(r) == 1;
    r = ref_ite_lookup(mgr, ucp);
    if (!REF_IS_RECURSE(r)) {
	chunk_free(ucp);
	if (neg)
	    r = REF_NEGATE(r);
	if (verblevel >= 4) {
	    char buf1[24];
	    ref_show(r, buf1);
	    report(4, "\tFound via lookup: %s", buf1);
	}
	return r;
    }
    /* Must do recursion */
    /* Retrieve possibly rearranged parameters */
    iref = (ref_t) chunk_get_word(ucp, 0);
    tref = (ref_t) chunk_get_word(ucp, 1);
    eref = (ref_t) chunk_get_word(ucp, 2);
    if (verblevel >= 4) {
	char buf1[24], buf2[24], buf3[24];
	iref = (ref_t) chunk_get_word(ucp, 0);
	tref = (ref_t) chunk_get_word(ucp, 1);
	eref = (ref_t) chunk_get_word(ucp, 2);
	ref_show(iref, buf1); ref_show(tref, buf2); ref_show(eref, buf3);
	char *ns = neg ? "!" : "";
	report(4, "Computing %sITE(%s, %s, %s)", ns, buf1, buf2, buf3);
    }
    size_t ivar = REF_GET_VAR(iref);
    size_t tvar = REF_GET_VAR(tref);
    size_t evar = REF_GET_VAR(eref);
    size_t var = ivar;
    if (tvar < var) { var = tvar; }
    if (evar < var) { var = evar; }
    if (verblevel >= 4) {
	char buf1[24];
	ref_show(REF_VAR(var), buf1);
	report(4, "\tSplitting on variable %s", buf1);
    }
    ref_t ivref, irefhi, ireflo, tvref, trefhi, treflo, evref, erefhi, ereflo;
    if (ivar == var)
	ref_deref(mgr, iref, &ivref, &irefhi, &ireflo);
    else
	irefhi = ireflo = iref;
    if (tvar == var)
	ref_deref(mgr, tref, &tvref, &trefhi, &treflo);
    else
	trefhi = treflo = tref;
    if (evar == var)
	ref_deref(mgr, eref, &evref, &erefhi, &ereflo);
    else
	erefhi = ereflo = eref;
    ref_t newhi, newlo;
    ref_ite_recurse(mgr, irefhi, ireflo, trefhi, treflo,
		    erefhi, ereflo, &newhi, &newlo);
    ref_t vref = REF_VAR(var);
    r = ref_canonize(mgr, vref, newhi, newlo);
    ref_ite_store(mgr, ucp, r);
    if (neg)
	r = REF_NEGATE(r);
    if (verblevel >= 4) {
	char buf1[24];
	ref_show(r, buf1);
	report(4, "\tGot recursively computed case: %s", buf1);
    }
    return r;
}

/* Special cases of ITE */
ref_t ref_and(ref_mgr mgr, ref_t aref, ref_t bref) {
    return ref_ite(mgr, aref, bref, REF_ZERO);
}
ref_t ref_or(ref_mgr mgr, ref_t aref, ref_t bref) {
    return ref_ite(mgr, aref, REF_ONE, bref);
}
ref_t ref_xor(ref_mgr mgr, ref_t aref, ref_t bref) {
    return ref_ite(mgr, aref, REF_NEGATE(bref), bref);
}

/* See if it's time to perform a GC */
bool ref_gc_check(ref_mgr mgr) {
    size_t n = mgr->stat_counter[STATB_UNIQ_CURR];
    if (n <= GC_THRESHOLD)
	return false;
    if (n <= GC_RATIO * mgr->last_nelements)
	return false;
    return true;
}

/*** Implementation of unary operations ****/

/* Supported operations */
typedef enum {
    UOP_MARK,
    UOP_SUPPORT,
    UOP_DENSITY,
    UOP_PCOUNT,
    UOP_COFACTOR,
    UOP_EQUANT,
    UOP_SHIFT
} uop_type_t;

typedef struct UELE uop_mgr_ele, *uop_mgr_ptr;

struct UELE {
    ref_mgr mgr;
    unsigned id;
    uop_type_t operation;
    /* Mapping from nodes to values */
    keyvalue_table_ptr map;
    void *auxinfo;
    /* For distributed implementation */
    keyvalue_table_ptr deferred_uop_table;
    /* Form in linked list */
    uop_mgr_ptr next;
};

/* Unary functions defined in terms of operation performed at each node */
typedef word_t (*uop_node_fun)(uop_mgr_ptr umgr, ref_t r,
			       word_t hival, word_t loval, void *auxinfo);

/* Forward declarations */
static word_t uop_node_mark(uop_mgr_ptr umgr, ref_t r,
			    word_t hival, word_t loval, void *auxinfo);
static word_t uop_node_support(uop_mgr_ptr umgr, ref_t r,
			       word_t hival, word_t loval, void *auxinfo);
static word_t uop_node_density(uop_mgr_ptr umgr, ref_t r,
			       word_t hival, word_t loval, void *auxinfo);
static word_t uop_node_pcount(uop_mgr_ptr umgr, ref_t r,
			      word_t hival, word_t loval, void *auxinfo);
static word_t uop_node_cofactor(uop_mgr_ptr umgr, ref_t r,
				word_t hival, word_t loval, void *auxinfo);
static word_t uop_node_equant(uop_mgr_ptr umgr, ref_t r,
			      word_t hival, word_t loval, void *auxinfo);
static word_t uop_node_shift(uop_mgr_ptr umgr, ref_t r,
			     word_t hival, word_t loval, void *auxinfo);


static uop_node_fun uop_node_functions[] = {
    uop_node_mark, uop_node_support, uop_node_density,
    uop_node_pcount,
    uop_node_cofactor, uop_node_equant, uop_node_shift
};


/* Extra information included when distributed */
static uop_mgr_ptr new_uop(ref_mgr mgr, unsigned id,
			   uop_type_t op, void *auxinfo, bool dist) {
    uop_mgr_ptr umgr = (uop_mgr_ptr) malloc_or_fail(sizeof(uop_mgr_ele),
						    "new_uop");
    umgr->mgr = mgr;
    umgr->id = id;
    umgr->operation = op;
    umgr->map = word_keyvalue_new();
    umgr->auxinfo = auxinfo;
    umgr->deferred_uop_table = NULL;
    umgr->next = NULL;
    if (dist)
	umgr->deferred_uop_table = word_keyvalue_new();
    return umgr;
}

/* In distributed implementation, require tables to hold deferred operations */
/* Each table maps from a key to a set of deferred operations */
/* For ITEs, require destination plus whether to negate */
/* For Unary operations, only require destination */

/* Linked list to hold values in deferred ITE table */
typedef struct IELE ilist_ele, *ilist_ptr;

struct IELE {
    word_t dest;
    bool negate;
    ilist_ptr next;
};

/* Create a new list element */
static ilist_ptr ilist_new(word_t dest, bool negate) {
    ilist_ptr ele = malloc_or_fail(sizeof(ilist_ele), "ilist_new");
    ele->dest = dest;
    ele->negate = negate;
    ele->next = NULL;
    return ele;
}

/* Free all elements in a ilist */
static void ilist_free(ilist_ptr ls) {
    ilist_ptr ele = ls;
    while (ele) {
	ilist_ptr nele = ele->next;
	free_block((void *) ele, sizeof(ilist_ele));
	ele = nele;
    }
}

static void free_uop(uop_mgr_ptr umgr) {
    keyvalue_free(umgr->map);
    if (umgr->deferred_uop_table) {
	word_t wv;
	while (keyvalue_removenext(umgr->deferred_uop_table, NULL, &wv)) {
	    ilist_ptr ilist = (ilist_ptr) wv;
	    ilist_free(ilist);
	}
	keyvalue_free(umgr->deferred_uop_table);
    }
    free_block((void *) umgr, sizeof(uop_mgr_ele));
}

/* Depth-first recursive traversal to implement unary operation */
static word_t uop_traverse(uop_mgr_ptr umgr, ref_t r) {
    char buf[24];
    if (verblevel >= 4) {
	ref_show(r, buf);
	report(4, "Uop traversal hits %s", buf);
    }
    /* See if already done */
    word_t val;
    if (keyvalue_find(umgr->map, (word_t) r, &val)) {
	report(4, "\tRetrieved previous value 0x%llx", val);
	return val;
    }
    /* See if terminal case */
    if (REF_IS_CONST(r)) {
	val = uop_node_functions[umgr->operation](umgr, r, (word_t) 0,
						  (word_t) 0, umgr->auxinfo);
	report(4, "\tConstant node yields 0x%llx", val);
    } else {
	/* Apply recursively */
	ref_t vref, hiref, loref;
	ref_deref(umgr->mgr, r, &vref, &hiref, &loref);
	word_t hival = uop_traverse(umgr, hiref);
	word_t loval = uop_traverse(umgr, loref);
	val = uop_node_functions[umgr->operation](umgr, r, hival, loval,
						  umgr->auxinfo);
	report(4, "\tComputed value 0x%llx", val);
    }
    keyvalue_insert(umgr->map, (word_t) r, val);
    return val;
}

/* Initiate unary operation */
static void uop_go(uop_mgr_ptr umgr, set_ptr roots) {
    word_t w;
    set_iterstart(roots);
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	uop_traverse(umgr, r);
    }
}

/* Retrieve value computed for uop */
word_t uop_getval(uop_mgr_ptr umgr, ref_t r) {
    word_t val;
    if (!keyvalue_find(umgr->map, (word_t) r, (word_t *) &val)) {
	char buf[24];
	ref_show(r, buf);
	err(false, "Couldn't find ref %s in map for unary operation #%d",
	    buf, umgr->id);
	return (word_t) 0;
    }
    return val;
}

static word_t uop_node_mark(uop_mgr_ptr umgr, ref_t r, word_t hival,
			    word_t loval, void *auxinfo) {
    /* Aux info is a set indicating already reached nodes */
    ref_t ar = REF_ABSVAL(r);
    set_ptr rset = (set_ptr) auxinfo;
    if (!REF_IS_CONST(r) && !set_member(rset, (word_t) ar, false))
	set_insert(rset, (word_t) ar);
    return (word_t) 1;
}

/* Convert set of variables into bit vector */
static word_t vset2bv(set_ptr set) {
    word_t vset = 0;
    word_t wr;
    set_iterstart(set);
    while (set_iternext(set, &wr)) {
	ref_t r = (ref_t) wr;
	unsigned idx = REF_GET_VAR(r);
	vset |= ((word_t) 1<<idx);
    }
    return vset;
}

/* Convert bit vector into set of variables */
static set_ptr bv2vset(word_t vset) {
    unsigned idx;
    set_ptr set = word_set_new();
    for (idx = 0; vset; idx++) {
	if (vset & 0x1) {
	    ref_t r = REF_VAR(idx);
	    set_insert(set, (word_t) r);
	}
	vset >>= 1;
    }
    return set;
}

static word_t uop_node_support(uop_mgr_ptr umgr, ref_t r, word_t hival,
			       word_t loval, void *auxinfo) {
    /* Result is bit vector encoding local and downward support */
    /* Aux info is a set indicating already found support members */
    if (REF_IS_CONST(r))
	return (word_t) 0;
    /* Set form */
    set_ptr supset = (set_ptr) auxinfo;
    ref_t vr = REF_VAR(REF_GET_VAR(r));
    if (!set_member(supset, (word_t) vr, false)) {
	set_insert(supset, (word_t) vr);
    }
    /* Bit vector form */
    word_t lbv = (word_t) 1 << REF_GET_VAR(r);
    return lbv | hival | loval;
}

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

static word_t uop_node_density(uop_mgr_ptr umgr, ref_t r,
			       word_t hival, word_t loval, void *auxinfo) {
    /* Aux info is null */
    double val;
    if (r == REF_ONE) {
	val = 1.0;
    } else if (r == REF_ZERO) {
	val = 0.0;
    } else {
	double hval = w2d(hival);
	double lval = w2d(loval);
	val = (hval + lval)/2.0;
    }
    return d2w(val);
}

/* Uop counting packs index and count (relative to that index)
   into single word */
static word_t pack_count(unsigned idx, word_t cnt) {
    return ((word_t) idx << 48) | cnt;
}

static unsigned unpack_index(word_t pval) {
    return pval >> 48;
}

static word_t unpack_val(word_t pval) {
    return pval & ~((~0L) << 48);    
}

/* Unpack packed value and weight according to specified index */
word_t pval2cnt(word_t pval, unsigned idx) {
    unsigned pidx = unpack_index(pval);
    word_t pcnt = unpack_val(pval);
    word_t wt = 1L << (pidx-idx);
    return wt * pcnt;
}

static word_t uop_node_pcount(uop_mgr_ptr umgr, ref_t r, word_t hival,
			      word_t loval, void *auxinfo) {
    /* Aux info is count of number of variables */
    /* Compute count relative to top-level index.
       Pack index into high-order 16 bits */
    word_t nvars = *(word_t *) auxinfo;
    word_t cnt;
    word_t idx = REF_GET_VAR(r);
    if (idx > nvars)
	idx = nvars;
    if (r == REF_ONE) {
	cnt = 1;
    } else if (r == REF_ZERO) {
	cnt = 0;
    } else {
	word_t hcnt = pval2cnt(hival, idx+1);
	word_t lcnt = pval2cnt(loval, idx+1);
	cnt = hcnt + lcnt;
    }
#if 0
    {
	char buf[24];
	ref_show(r, buf);
	report(1, "Ref %s.  nvars = %lu.  idx = %u.  cnt = %lu",
	       buf, nvars, idx, cnt);
    }
#endif
    return pack_count(idx, cnt);
}

static word_t uop_node_cofactor(uop_mgr_ptr umgr, ref_t r,
				word_t hival, word_t loval, void *auxinfo) {
    if (REF_IS_CONST(r))
	return (word_t) r;
    /* Auxinfo is a set of literals */
    /* Return value is ref of cofactored function */
    set_ptr litset = (set_ptr) auxinfo;
    ref_t vr = REF_VAR(REF_GET_VAR(r));
    ref_t hiref = (ref_t) hival;
    ref_t loref = (ref_t) loval;
    ref_t nr;
    if (set_member(litset, (word_t) vr, false))
	/* Want positive cofactor */
	nr = hiref;
    else if (set_member(litset, (word_t) REF_NEGATE(vr), false))
	/* Want negative cofactor */
	nr = loref;
    else 
	nr = ref_canonize(umgr->mgr, vr, hiref, loref);
    return (word_t) nr;
}

static word_t uop_node_equant(uop_mgr_ptr umgr, ref_t r,
			      word_t hival, word_t loval, void *auxinfo) {
    if (REF_IS_CONST(r))
	return (word_t) r;
    /* Auxinfo is a set of variables */
    /* Return value is ref of quantified function */
    set_ptr varset = (set_ptr) auxinfo;
    ref_t vr = REF_VAR(REF_GET_VAR(r));
    ref_t hiref = (ref_t) hival;
    ref_t loref = (ref_t) loval;
    ref_t nr;
    if (set_member(varset, (word_t) vr, false))
	nr = ref_or(umgr->mgr, hiref, loref);
    else 
	nr = ref_canonize(umgr->mgr, vr, hiref, loref);
    return (word_t) nr;
}

/* Relabel old variables to new.  Must maintain compatible variable ordering */
/* Auxinfo is table mapping old vref's to new ones */
static word_t uop_node_shift(uop_mgr_ptr umgr, ref_t r,
			     word_t hival, word_t loval, void *auxinfo) {
    if (REF_IS_CONST(r))
	return (word_t) r;
    /* Return value is ref of shifted function */
    keyvalue_table_ptr vmap = (keyvalue_table_ptr) auxinfo;
    ref_t vr = REF_VAR(REF_GET_VAR(r));
    ref_t hiref = (ref_t) hival;
    ref_t loref = (ref_t) loval;
    word_t wv;
    if (keyvalue_find(vmap, (word_t) vr, &wv)) {
	/* Shift variable */
	vr = (ref_t) wv;
    }
    ref_t nr = ref_canonize(umgr->mgr, vr, hiref, loref);
    return (word_t) nr;
}


/* Find all reachable nodes from a set of roots */
set_ptr ref_reach(ref_mgr mgr, set_ptr roots) {
    set_ptr rset = word_set_new();
    uop_mgr_ptr umgr = new_uop(mgr, 0, UOP_MARK, (void *) rset, false);
    uop_go(umgr, roots);
    free_uop(umgr);
    return rset;
}

/* Compute set of variables (given by refs) in support of set of roots */
set_ptr ref_support(ref_mgr mgr, set_ptr roots) {
    set_ptr supset = word_set_new();
    uop_mgr_ptr umgr = new_uop(mgr, 0, UOP_SUPPORT, (void *) supset, false);
    uop_go(umgr, roots);
    free_uop(umgr);
    return supset;
}

/* Create subset of map containing entries only for roots */
static keyvalue_table_ptr map_subset(keyvalue_table_ptr map, set_ptr roots) {
    keyvalue_table_ptr result = word_keyvalue_new();
    word_t wr, wv;
    set_iterstart(roots);
    while (set_iternext(roots, &wr)) {
	if (!keyvalue_find(map, wr, &wv)) {
	    char buf[24];
	    ref_show((ref_t) wr, buf);
	    err(false, "Couldn't find ref %s in map", buf);
	} else
	    keyvalue_insert(result, wr, wv);
    }
    return result;
}

/*
  Create key-value table mapping set of root nodes to their counts.
*/
keyvalue_table_ptr ref_count(ref_mgr mgr, set_ptr roots) {
    word_t *wstore = malloc_or_fail(sizeof(word_t), "ref_count");
    *wstore = mgr->variable_cnt;
    uop_mgr_ptr umgr = new_uop(mgr, 0, UOP_PCOUNT, (void *) wstore, false);
    uop_go(umgr, roots);
    free_block(wstore, sizeof(word_t));
    keyvalue_table_ptr pcnts = map_subset(umgr->map, roots);
    keyvalue_table_ptr result = word_keyvalue_new();
    word_t wk, wv;
    while (keyvalue_removenext(pcnts, &wk, &wv)) {
	word_t cnt = pval2cnt(wv, 0);
	keyvalue_insert(result, wk, cnt);
    }
    keyvalue_free(pcnts);
    free_uop(umgr);
    return result;
}

/* Create key-value table mapping set of root nodes to their densities. */
keyvalue_table_ptr ref_density(ref_mgr mgr, set_ptr roots) {
    uop_mgr_ptr umgr = new_uop(mgr, 0, UOP_DENSITY, NULL, false);
    uop_go(umgr, roots);
    keyvalue_table_ptr result = map_subset(umgr->map, roots);
    free_uop(umgr);
    return result;
}

/* Function for retrieving density from table */
double get_double(keyvalue_table_ptr map, ref_t r) {
    word_t w;
    if (!keyvalue_find(map, (word_t) r, &w)) {
	char buf[24];
	ref_show(r, buf);
	err(false, "Couldn't find ref %s in map", buf);
	return 0.0;
    }
    return w2d(w);
}

/* Create key-value table mapping set of root nodes to their restrictions,
   with respect to a set of literals (given as a set of refs)
*/
keyvalue_table_ptr ref_restrict(ref_mgr mgr, set_ptr roots, set_ptr lits) {
    uop_mgr_ptr umgr = new_uop(mgr, 0, UOP_COFACTOR, (void *) lits, false);
    uop_go(umgr, roots);
    keyvalue_table_ptr result = map_subset(umgr->map, roots);
    free_uop(umgr);
    return result;
}

/* Create key-value table mapping set of root nodes to their
   existential quantifications with respect to a set of variables
   (given as a set of refs)
*/
keyvalue_table_ptr ref_equant(ref_mgr mgr, set_ptr roots, set_ptr vars) {
    uop_mgr_ptr umgr = new_uop(mgr, 0, UOP_EQUANT, (void *) vars, false);
    uop_go(umgr, roots);
    keyvalue_table_ptr result = map_subset(umgr->map, roots);
    free_uop(umgr);
    return result;
}

/* Create key-value table mapping set of root nodes to their shifted versions
   with respect to a mapping from old variables to new ones 
*/
keyvalue_table_ptr ref_shift(ref_mgr mgr, set_ptr roots,
			     keyvalue_table_ptr vmap) {
    uop_mgr_ptr umgr = new_uop(mgr, 0, UOP_SHIFT, (void *) vmap, false);
    uop_go(umgr, roots);
    keyvalue_table_ptr result = map_subset(umgr->map, roots);
    free_uop(umgr);
    return result;
}

/* Do garbage collection, eliminating all but refs in rset */
static void complete_collection(ref_mgr mgr, set_ptr rset) {
    size_t start_cnt = 0;
    size_t end_cnt = 0;
    keyvalue_table_ptr old_uniq = mgr->unique_table;
    keyvalue_table_ptr new_uniq = word_keyvalue_new();
    ulist_ptr ls;
    word_t h;
    while (keyvalue_removenext(old_uniq, &h, (word_t *) &ls)) {
	ulist_ptr nls = NULL;
	while (ls) {
	    ulist_ptr ele = ls;
	    ref_t r = ele->ref;
	    ls = ele->next;
	    start_cnt++;
	    if (set_member(rset, (word_t) r, false)) {
		if (verblevel >= 4) {
		    char buf[24];
		    ref_show(r, buf);
		    report(4, "Keeping %s", buf);
		}
		/* Want to keep this entry.  Move over to new list */
		ele->next = nls;
		nls = ele;
		end_cnt++;
	    } else {
		/* Don't need this one */
		if (verblevel >= 4) {
		    char buf[24];
		    ref_show(r, buf);
		    report(4, "Removing %s", buf);
		}
		chunk_free(ele->data);
		free_block((void *) ele, sizeof(ulist_ele));
	    }
	}
	if (nls) {
	    keyvalue_insert(new_uniq, h, (word_t) nls);
	}
    }
    keyvalue_free(old_uniq);
    mgr->unique_table = new_uniq;
    clear_ite_table(mgr);
    mgr->stat_counter[STATB_UNIQ_CURR] = end_cnt;
    mgr->last_nelements = end_cnt;
    report(1, "Garbage Collection: %lu --> %lu function refs", start_cnt, end_cnt);
}


/* Local garbage collection.
   Find all nodes reachable from roots and keep only those in unique table */
void ref_collect(ref_mgr mgr, set_ptr roots) {
    set_ptr rset = ref_reach(mgr, roots);
    complete_collection(mgr, rset);
    set_free(rset);
}


void ref_show_stat(ref_mgr mgr) {
    /* Gather statistics information */
    size_t i;
    agent_stat_counter[STATA_BYTE_PEAK] = last_peak_bytes;
    for (i = 0; i < NSTATA; i++)
	mgr->stat_counter[i] = agent_stat_counter[i];
    report(0, "Peak bytes %" PRIu64,
	   mgr->stat_counter[STATA_BYTE_PEAK]);
    report(0,
"Operations.  Total generated %" PRIu64 ".  Routed locally %" PRIu64,
	   mgr->stat_counter[STATA_OPERATION_TOTAL],
	   mgr->stat_counter[STATA_OPERATION_LOCAL]);
    report(0,
"Operands.  Total generated %" PRIu64 ".  Routed locally %" PRIu64,
	   mgr->stat_counter[STATA_OPERAND_TOTAL],
	   mgr->stat_counter[STATA_OPERAND_LOCAL]);
    report(0,
"Unique table.  Total generated %" PRIu64 ".  Current %" PRIu64
".  Peak %" PRIu64 ".  Collisions %" PRIu64,
	   mgr->stat_counter[STATB_UNIQ_TOTAL],
	   mgr->stat_counter[STATB_UNIQ_CURR],
	   mgr->stat_counter[STATB_UNIQ_PEAK],
	   mgr->stat_counter[STATB_UNIQ_COLLIDE]);
    report(0,
"ITEs. Total %" PRIu64 ".  Done Locally %" PRIu64 
".  Hit cache %" PRIu64 ".  Cause recursion %" PRIu64,
	   mgr->stat_counter[STATB_ITE_CNT],
	   mgr->stat_counter[STATB_ITE_LOCAL_CNT],
	   mgr->stat_counter[STATB_ITE_HIT_CNT],
	   mgr->stat_counter[STATB_ITE_NEW_CNT]);
    report(0,
"ITE cache.  Total generated %" PRIu64 
".  Current %" PRIu64 ".  Peak %" PRIu64,
	   mgr->stat_counter[STATB_ITEC_TOTAL],
	   mgr->stat_counter[STATB_ITEC_CURR],
	   mgr->stat_counter[STATB_ITEC_PEAK]);
}

/*********** Distributed implementations ***************/

/* Information needed by agent performing distributed BDD operations */
typedef struct {
    ref_mgr rmgr; /* Reference manager */
    /* Operators waiting for completion of ITE operation stored in table
       Table provides mapping from chunk containing (iref, tref, eref) to ilist
       Use ilist entries to indicate destination & negation of each target */
    keyvalue_table_ptr deferred_ite_table;
    /* List of all outstanding unary operations */
    uop_mgr_ptr umgr_list;
} dref_mgr_ele, *dref_mgr;

/* Support single distributed reference manager per agent. */
static dref_mgr dmgr = NULL;

void init_dref_mgr() {
    dmgr = malloc_or_fail(sizeof(dref_mgr_ele), "init_dref_mgr");
    dmgr->rmgr = new_ref_mgr();
    dmgr->deferred_ite_table = keyvalue_new(chunk_hash, chunk_equal);
    dmgr->umgr_list = NULL;
}

void free_dref_mgr() {
    word_t wk, wv;
    while (keyvalue_removenext(dmgr->deferred_ite_table, &wk, &wv)) {
	chunk_ptr ucp = (chunk_ptr) wk;
	chunk_free(ucp);
	ilist_ptr ilist = (ilist_ptr) wv;
	ilist_free(ilist);
    }
    ref_show_stat(dmgr->rmgr);
    keyvalue_free(dmgr->deferred_ite_table);
    free_ref_mgr(dmgr->rmgr);
    uop_mgr_ptr ulist = dmgr->umgr_list;
    while (ulist) {
	uop_mgr_ptr next = ulist->next;
	free_uop(ulist);
	ulist = next;
    }
    free_block(dmgr, sizeof(dref_mgr_ele));
}

chunk_ptr flush_dref_mgr() {
    report(3, "Flushing state");
    /* Gather statistics information */
    size_t i;
    agent_stat_counter[STATA_BYTE_PEAK] = last_peak_bytes;
    reset_peak_bytes();
    for (i = 0; i < NSTATA; i++)
	dmgr->rmgr->stat_counter[i] = agent_stat_counter[i];
    chunk_ptr msg = msg_new_stat(1, NSTAT, dmgr->rmgr->stat_counter);
    free_dref_mgr();
    mem_status(stdout);
    init_dref_mgr();
    return msg;
}

/* Initiate global operation and retrieve ref result */
static ref_t fire_wait_and_get(ref_mgr mgr, chunk_ptr msg) {
    chunk_ptr rmsg = fire_and_wait_defer(msg);
    if (!rmsg) {
	err(false, "Attempt to perform operation failed");
	return REF_ZERO;
    }
    ref_t r = (ref_t) chunk_get_word(rmsg, 1);
    chunk_free(rmsg);
    return r;
}

chunk_ptr build_var(word_t dest) {
    word_t worker = choose_hashed_worker(0);
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_VAR, worker, id, 1 + OP_HEADER_CNT);
    op_insert_word(op, dest, 0+OP_HEADER_CNT);
    report(4, "Created Var operation.  Worker %u.  Operator ID 0x%x.",
	   worker, id);
    return op;
}

chunk_ptr build_canonize(word_t dest, ref_t vref) {
    word_t worker = choose_some_worker();
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_CANONIZE, worker, id, 4 + OP_HEADER_CNT);
    op_insert_word(op, dest,            0+OP_HEADER_CNT);
    op_insert_word(op, (word_t) vref,   1+OP_HEADER_CNT);
    report(4, "Created Canonize operation.  Worker %u.  Operator ID 0x%x.",
	   worker, id);
    return op;
}

chunk_ptr build_canonize_lookup(word_t dest, word_t hash, ref_t vref,
				ref_t hiref, ref_t loref, bool negate) {
    word_t worker = choose_hashed_worker(hash);
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_CANONIZE_LOOKUP, worker,
				    id, 6 + OP_HEADER_CNT);
    op_insert_word(op, dest,            0+OP_HEADER_CNT);
    op_insert_word(op, hash,            1+OP_HEADER_CNT);
    op_insert_word(op, (word_t) vref,   2+OP_HEADER_CNT);
    op_insert_word(op, (word_t) hiref,  3+OP_HEADER_CNT);
    op_insert_word(op, (word_t) loref,  4+OP_HEADER_CNT);
    op_insert_word(op, (word_t) negate, 5+OP_HEADER_CNT);
    report(4,
"Created Canonize Lookup operation.  Worker %u.  Operator ID 0x%x.",
	   worker, id);
    return op;
}

chunk_ptr build_retrieve_lookup(word_t dest, ref_t ref) {
    word_t hash = REF_GET_HASH(ref);
    word_t worker = choose_hashed_worker(hash);
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_RETRIEVE_LOOKUP, worker, id,
				    2 + OP_HEADER_CNT);
    op_insert_word(op, dest,            0+OP_HEADER_CNT);
    op_insert_word(op, (word_t) ref,    1+OP_HEADER_CNT);
    report(4,
"Created Retrieve Lookup operation.  Worker %u.  Operator ID 0x%x.",
	   worker, id);
    return op;
}

chunk_ptr build_ite_lookup(word_t dest, ref_t iref,
			   ref_t tref, ref_t eref, bool negate) {
    chunk_ptr ucp = ref3_encode(iref, tref, eref);
    word_t hash = chunk_hash((word_t) ucp);
    word_t worker = choose_hashed_worker(hash);
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_ITE_LOOKUP, worker, id,
				    5 + OP_HEADER_CNT);
    op_insert_word(op, dest,            0+OP_HEADER_CNT);
    op_insert_word(op, (word_t) iref,   1+OP_HEADER_CNT);
    op_insert_word(op, (word_t) tref,   2+OP_HEADER_CNT);
    op_insert_word(op, (word_t) eref,  3+OP_HEADER_CNT);
    op_insert_word(op, (word_t) negate, 4+OP_HEADER_CNT);
    report(4,
"Created ITE Lookup operation.  Worker %u.  Operator ID 0x%x.", worker, id);
    chunk_free(ucp);
    return op;
}

chunk_ptr build_ite_recurse(word_t dest, ref_t vref) {
    word_t worker = choose_some_worker();
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_ITE_RECURSE, worker, id,
				    8 + OP_HEADER_CNT);
    op_insert_word(op, dest,          0+OP_HEADER_CNT);
    op_insert_word(op, (word_t) vref, 1+OP_HEADER_CNT);
    report(4, "Created ITE Recurse operation.  Worker %u.  Operator ID 0x%x.",
	   worker, id);
    return op;
}

chunk_ptr build_ite_store(word_t dest, word_t iref, word_t tref,
			  word_t eref, bool negate) {
    word_t worker = choose_own_worker();
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_ITE_STORE, worker, id, 6 + OP_HEADER_CNT);
    op_insert_word(op, dest,            0+OP_HEADER_CNT);
    op_insert_word(op, (word_t) iref,   1+OP_HEADER_CNT);
    op_insert_word(op, (word_t) tref,   2+OP_HEADER_CNT);
    op_insert_word(op, (word_t) eref,  3+OP_HEADER_CNT);
    op_insert_word(op, (word_t) negate, 5+OP_HEADER_CNT);
    report(4, "Created ITE Store operation.  Worker %u.  Operator ID 0x%x.",
	   worker, id);
    return op;
}

chunk_ptr build_uop_down(word_t dest, unsigned uid, ref_t ref) {
    word_t hash = REF_GET_HASH(ref);
    word_t worker = choose_hashed_worker(hash);
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_UOP_DOWN, worker, id, 3 + OP_HEADER_CNT);
    op_insert_word(op, dest,            0+OP_HEADER_CNT);
    op_insert_word(op, (word_t) uid,    1+OP_HEADER_CNT);
    op_insert_word(op, (word_t) ref,    2+OP_HEADER_CNT);
    report(4,
"Created UOP Down operation.  Uid %u.  Worker %u.  Operator ID 0x%x.",
	   uid, worker, id);
    return op;
}

chunk_ptr build_uop_up(word_t dest, unsigned uid, ref_t ref) {
    word_t worker = choose_own_worker();
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_UOP_UP, worker, id, 5 + OP_HEADER_CNT);
    op_insert_word(op, dest,            0+OP_HEADER_CNT);
    op_insert_word(op, (word_t) uid,    1+OP_HEADER_CNT);
    op_insert_word(op, (word_t) ref,    2+OP_HEADER_CNT);
    report(4,
"Created UOP Up operation.  Uid %u.  Worker %u.  Operator ID 0x%x.",
	   uid, worker, id);
    return op;
}

chunk_ptr build_uop_store(word_t dest, unsigned uid, ref_t ref) {
    word_t worker = choose_own_worker();
    word_t id = new_operator_id();
    chunk_ptr op = msg_new_operator(OP_UOP_STORE, worker, id, 4 + OP_HEADER_CNT);
    op_insert_word(op, dest,            0+OP_HEADER_CNT);
    op_insert_word(op, (word_t) uid,    1+OP_HEADER_CNT);
    op_insert_word(op, (word_t) ref,    2+OP_HEADER_CNT);
    report(4,
"Created UOP Store operation.  Uid %u.  Worker %u.  Operator ID 0x%x.",
	   uid, worker, id);
    return op;
}

static bool send_ref_as_operand(word_t dest, ref_t ref) {
    if (verblevel >= 4) {
	char buf[24];
	ref_show(ref, buf);
	report(4, "Sending ref result %s.  Agent %u, Op Id 0x%x", buf,
	       msg_get_dest_agent(dest), msg_get_dest_op_id(dest));
    }
    return send_as_operand(dest, (word_t) ref);

}

bool do_var_op(chunk_ptr op) {
    ref_mgr mgr = dmgr->rmgr;
    word_t dest = chunk_get_word(op, 0+OP_HEADER_CNT);
    ref_t vref = ref_new_variable(mgr);
    bool ok = send_ref_as_operand(dest, vref);
    return ok;
}

bool do_canonize_op(chunk_ptr op) {
    word_t dest = chunk_get_word(op, 0+OP_HEADER_CNT);
    ref_t vref = (ref_t) chunk_get_word(op,  1+OP_HEADER_CNT);
    ref_t hiref = (ref_t) chunk_get_word(op, 2+OP_HEADER_CNT);
    ref_t loref = (ref_t) chunk_get_word(op, 3+OP_HEADER_CNT);
    chunk_ptr ucp;
    bool ok = true;
    ref_t r = ref_canonize_local(vref, hiref, loref, &ucp);
    if (!REF_IS_RECURSE(r)) {
	if (verblevel >= 4) {
	}
	ok = send_ref_as_operand(dest, r);
	return ok;
    }
    word_t negate = REF_GET_NEG(r);
    word_t hash = utable_hash(ucp);
    ref_t nvref = (ref_t) chunk_get_word(ucp, 0);
    ref_t nhiref = (ref_t) chunk_get_word(ucp, 1);
    ref_t nloref = (ref_t) chunk_get_word(ucp, 2);
    chunk_ptr cop = build_canonize_lookup(dest, hash, nvref,
					  nhiref, nloref, negate);
    ok = send_op(cop);
    chunk_free(cop);
    chunk_free(ucp);
    return ok;
}

bool do_canonize_lookup_op(chunk_ptr op) {
    ref_mgr mgr = dmgr->rmgr;
    word_t dest = chunk_get_word(op, 0+OP_HEADER_CNT);
    /*    word_t hash = chunk_get_word(op, 1+OP_HEADER_CNT); */
    ref_t vref = (ref_t) chunk_get_word(op,  2+OP_HEADER_CNT);
    ref_t hiref = (ref_t) chunk_get_word(op, 3+OP_HEADER_CNT);
    ref_t loref = (ref_t) chunk_get_word(op, 4+OP_HEADER_CNT);
    bool negate = (bool) chunk_get_word(op,  5+OP_HEADER_CNT);
    chunk_ptr ucp = ref3_encode(vref, hiref, loref);
    ref_t r = ref_canonize_lookup(mgr, ucp);
    if (negate)
	r = REF_NEGATE(r);
    bool ok = send_ref_as_operand(dest, r);
    if (ref_gc_check(mgr))
	/* Request a GC */
	request_gc();
    return ok;
}

bool do_retrieve_lookup_op(chunk_ptr op) {
    ref_mgr mgr = dmgr->rmgr;
    word_t dest = chunk_get_word(op, 0+OP_HEADER_CNT);
    ref_t ref =   (ref_t) chunk_get_word(op, 1+OP_HEADER_CNT);
    ref_t vref, tref, eref;
    ref_deref(mgr, ref, &vref, &tref, &eref);
    chunk_ptr oper = msg_new_operand(dest, 3);
    chunk_insert_word(oper, (word_t) tref, 0 + OPER_HEADER_CNT);
    chunk_insert_word(oper, (word_t) eref, 1 + OPER_HEADER_CNT);
    if (verblevel >= 4) {
	char tbuf[24], ebuf[24];
	ref_show(tref, tbuf);
	ref_show(eref, ebuf);
	report(4, "Sending Retrieve Lookup result %s, %s", tbuf, ebuf);
    }
    bool ok = send_op(oper);
    chunk_free(oper);
    return ok;
}

static bool send_retrieve(word_t dest, ref_t ref) {
    chunk_ptr op = build_retrieve_lookup(dest, ref);
    bool ok = send_op(op);
    chunk_free(op);
    return ok;
}

bool do_ite_lookup_op(chunk_ptr op) {
    ref_mgr mgr = dmgr->rmgr;
    word_t dest = chunk_get_word(op, 0+OP_HEADER_CNT);
    ref_t iref = (ref_t) chunk_get_word(op, 1+OP_HEADER_CNT);
    ref_t tref = (ref_t) chunk_get_word(op, 2+OP_HEADER_CNT);
    ref_t eref = (ref_t) chunk_get_word(op, 3+OP_HEADER_CNT);
    bool negate = (bool) chunk_get_word(op, 4+OP_HEADER_CNT);
    if (verblevel >= 4) {
	char buf1[24], buf2[24], buf3[24];
	ref_show(iref, buf1); ref_show(tref, buf2); ref_show(eref, buf3);
	char *ns = negate ? "!" : "";
	report(4, "Computing %sITE(%s, %s, %s)", ns, buf1, buf2, buf3);
    }
    chunk_ptr ucp = ref3_encode(iref, tref, eref);
    ref_t rlook = ref_ite_lookup(mgr, ucp);
    if (!REF_IS_RECURSE(rlook)) {
	chunk_free(ucp);
	if (negate)
	    rlook = REF_NEGATE(rlook);
	if (verblevel >= 4) {
	    char buf1[24];
	    ref_show(rlook, buf1);
	    report(4, "\tFound via lookup: %s", buf1);
	}
	bool ok = send_ref_as_operand(dest, rlook);	
	return ok;
    }
    /* Check if pending ITE */
    word_t wk, wv;
    if (keyvalue_remove(dmgr->deferred_ite_table, (word_t) ucp, &wk, &wv)) {
	/* Add to deferred ITE table */
	chunk_ptr old_ucp = (chunk_ptr) wk;
	ilist_ptr ls = (ilist_ptr) wv;
	ilist_ptr ele = ilist_new(dest, negate);
	/* Add to list */
	ele->next = ls;
	keyvalue_insert(dmgr->deferred_ite_table, (word_t) old_ucp,
			(word_t) ele);
	chunk_free(ucp);
	if (verblevel >= 4) {
	    char ibuf[24], tbuf[24], ebuf[24];
	    ref_show(iref, ibuf); ref_show(tref, tbuf), ref_show(eref, ebuf);
	    char *sn = negate ? "!" : "";
	    unsigned op_id = msg_get_dest_op_id(dest);
	    unsigned agent = msg_get_dest_agent(dest);
	    report(4, "\tDeferring %sITE(%s, %s, %s).  Agent %u, Op Id 0x%x",
		   sn, ibuf, tbuf, ebuf, agent, op_id);
	    
	}
	return true;
    } else {
	/* Insert empty list as placeholder */
	ilist_ptr ls = NULL;
	keyvalue_insert(dmgr->deferred_ite_table, (word_t) ucp, (word_t) ls);
    }
    /* Must do recursion */
    size_t ivar = REF_GET_VAR(iref);
    size_t tvar = REF_GET_VAR(tref);
    size_t evar = REF_GET_VAR(eref);
    size_t var = ivar;
    if (tvar < var) { var = tvar; }
    if (evar < var) { var = evar; }
    ref_t vref = REF_VAR(var);
    if (verblevel >= 4) {
	char buf1[24];
	ref_show(vref, buf1);
	report(4, "\tSplitting on variable %s", buf1);
    }
    /* Create next operations */
    chunk_ptr sop = build_ite_store(dest, iref, tref, eref, negate);
    word_t sdest =  msg_new_destination(sop, 4+OP_HEADER_CNT);
    chunk_ptr rop = build_ite_recurse(sdest, vref);
    ref_t nvref, nhiref, nloref;
    bool ok = true;
    /* Fill in any known fields, and spawn retrieve operations for others */
    if (ivar == var) {
	if (ref_deref_local(iref, &nvref, &nhiref, &nloref)) {
	    op_insert_word(rop, (word_t) nhiref, 2+OP_HEADER_CNT);
	    op_insert_word(rop, (word_t) nloref, 3+OP_HEADER_CNT);
	} else {
	    word_t ndest = msg_new_destination(rop, 2+OP_HEADER_CNT);
	    ok = ok && send_retrieve(ndest, iref);
	}
    } else {
	op_insert_word(rop, (word_t) iref, 2+OP_HEADER_CNT);
	op_insert_word(rop, (word_t) iref, 3+OP_HEADER_CNT);
    }
    if (tvar == var) {
	if (ref_deref_local(tref, &nvref, &nhiref, &nloref)) {
	    op_insert_word(rop, (word_t) nhiref, 4+OP_HEADER_CNT);
	    op_insert_word(rop, (word_t) nloref, 5+OP_HEADER_CNT);
	} else {
	    word_t ndest = msg_new_destination(rop, 4+OP_HEADER_CNT);
	    ok = ok && send_retrieve(ndest, tref);
	}
    } else {
	op_insert_word(rop, (word_t) tref, 4+OP_HEADER_CNT);
	op_insert_word(rop, (word_t) tref, 5+OP_HEADER_CNT);
    }
    if (evar == var) {
	if (ref_deref_local(eref, &nvref, &nhiref, &nloref)) {
	    op_insert_word(rop, (word_t) nhiref, 6+OP_HEADER_CNT);
	    op_insert_word(rop, (word_t) nloref, 7+OP_HEADER_CNT);
	} else {
	    word_t ndest = msg_new_destination(rop, 6+OP_HEADER_CNT);
	    ok = ok && send_retrieve(ndest, eref);
	}
    } else {
	op_insert_word(rop, (word_t) eref, 6+OP_HEADER_CNT);
	op_insert_word(rop, (word_t) eref, 7+OP_HEADER_CNT);
    }
    ok = ok && send_op(sop);
    ok = ok && send_op(rop);
    chunk_free(sop);
    chunk_free(rop);
    return ok;
}

bool do_ite_recurse_op(chunk_ptr op) {
    ref_mgr mgr = dmgr->rmgr;
    mgr->stat_counter[STATB_ITE_NEW_CNT]++;
    word_t dest =          chunk_get_word(op, 0+OP_HEADER_CNT);
    ref_t vref =   (ref_t) chunk_get_word(op, 1+OP_HEADER_CNT);
    ref_t irefhi = (ref_t) chunk_get_word(op, 2+OP_HEADER_CNT);
    ref_t ireflo = (ref_t) chunk_get_word(op, 3+OP_HEADER_CNT);
    ref_t trefhi = (ref_t) chunk_get_word(op, 4+OP_HEADER_CNT);
    ref_t treflo = (ref_t) chunk_get_word(op, 5+OP_HEADER_CNT);
    ref_t erefhi = (ref_t) chunk_get_word(op, 6+OP_HEADER_CNT);
    ref_t ereflo = (ref_t) chunk_get_word(op, 7+OP_HEADER_CNT);
    bool ok = true;
    chunk_ptr hiucp, loucp;
    ref_t nhiref = ref_ite_local(mgr, irefhi, trefhi, erefhi, &hiucp);
    ref_t nloref = ref_ite_local(mgr, ireflo, treflo, ereflo, &loucp);
    /* Note for future:
       Could begin canonization for case where both hiref & loref are valid */
    chunk_ptr cop = build_canonize(dest, vref);
    if (REF_IS_RECURSE(nhiref)) {
	word_t hidest = msg_new_destination(cop, 2+OP_HEADER_CNT);
	ref_t iref = (ref_t) chunk_get_word(hiucp, 0);
	ref_t tref = (ref_t) chunk_get_word(hiucp, 1);
	ref_t eref = (ref_t) chunk_get_word(hiucp, 2);
	bool negate = REF_GET_NEG(nhiref);
	chunk_ptr sop = build_ite_lookup(hidest, iref, tref, eref, negate);
	ok = ok && send_op(sop);
	chunk_free(hiucp);
	chunk_free(sop);
    } else {
	op_insert_word(cop, nhiref, 2+OP_HEADER_CNT);
    }
    if (REF_IS_RECURSE(nloref)) {
	word_t lodest = msg_new_destination(cop, 3+OP_HEADER_CNT);
	ref_t iref = (ref_t) chunk_get_word(loucp, 0);
	ref_t tref = (ref_t) chunk_get_word(loucp, 1);
	ref_t eref = (ref_t) chunk_get_word(loucp, 2);
	bool negate = REF_GET_NEG(nloref);
	chunk_ptr sop = build_ite_lookup(lodest, iref, tref, eref, negate);
	ok = ok && send_op(sop);
	chunk_free(loucp);
	chunk_free(sop);
    } else {
	op_insert_word(cop, nloref, 3+OP_HEADER_CNT);
    }
    ok = ok && send_op(cop);
    chunk_free(cop);
    return ok;
}

bool do_ite_store_op(chunk_ptr op) {
    ref_mgr mgr = dmgr->rmgr;
    word_t dest =          chunk_get_word(op, 0+OP_HEADER_CNT);
    ref_t iref =   (ref_t) chunk_get_word(op, 1+OP_HEADER_CNT);
    ref_t tref =   (ref_t) chunk_get_word(op, 2+OP_HEADER_CNT);
    ref_t eref =   (ref_t) chunk_get_word(op, 3+OP_HEADER_CNT);
    ref_t ref =    (ref_t) chunk_get_word(op, 4+OP_HEADER_CNT);
    bool negate =   (bool) chunk_get_word(op, 5+OP_HEADER_CNT);
    chunk_ptr ucp = ref3_encode(iref, tref, eref);
    ref_ite_store(mgr, ucp, ref);
    ref_t r = ref;
    if (negate)
	r = REF_NEGATE(r);
    bool ok = send_ref_as_operand(dest, r);
    word_t wk, wv;
    if (keyvalue_remove(dmgr->deferred_ite_table, (word_t) ucp, &wk, &wv)) {
	chunk_ptr old_ucp = (chunk_ptr) wk;
	chunk_free(old_ucp);
	ilist_ptr ls = (ilist_ptr) wv;
	ilist_ptr ele = ls;
	while (ele) {
	    word_t ldest = ele->dest;
	    bool lnegate = ele->negate;
	    ref_t lr = ref;
	    if (lnegate)
		lr = REF_NEGATE(lr);
	    ok = ok && send_ref_as_operand(ldest, lr);
	    ele = ele->next;
	    if (verblevel >= 4) {
		char ibuf[24], tbuf[24], ebuf[24], rbuf[24];
		ref_show(iref, ibuf); ref_show(tref, tbuf), ref_show(eref, ebuf);
		ref_show(lr, rbuf);
		char *sn = lnegate ? "!" : "";
		unsigned op_id = msg_get_dest_op_id(dest);
		unsigned agent = msg_get_dest_agent(dest);
		report(4,
"\tSending deferred result %sITE(%s, %s, %s) --> %s.  Agent %u, Op Id 0x%x",
		       sn, ibuf, tbuf, ebuf, rbuf, agent, op_id);
	    
	    }
	}
	ilist_free(ls);
    }
    return ok;
}

static uop_mgr_ptr find_umgr(unsigned uid, bool remove) {
    uop_mgr_ptr ulist = dmgr->umgr_list;
    uop_mgr_ptr prev = NULL;
    while (ulist) {
	if (ulist->id == uid) {
	    if (remove) {
		if (prev) {
		    prev->next = ulist->next;
		} else {
		    dmgr->umgr_list = ulist->next;
		}
	    }
	    return ulist;
	}
	prev = ulist;
	ulist = ulist->next;
    }
    err(false, "Couldn't find manager for unary operation %u", uid);
    return NULL;
}

bool do_uop_down_op(chunk_ptr op) {
    ref_mgr mgr = dmgr->rmgr;
    word_t dest =          chunk_get_word(op, 0+OP_HEADER_CNT);
    unsigned uid =         chunk_get_word(op, 1+OP_HEADER_CNT);
    ref_t ref =    (ref_t) chunk_get_word(op, 2+OP_HEADER_CNT);
    mgr->stat_counter[STATB_UOP_CNT]++;
    char buf[24];
    if (verblevel >= 4) {
	ref_show(ref, buf);
	report(4, "Downward traversal hits %s", buf);
    }
    /* look for umgr */
    uop_mgr_ptr umgr = find_umgr(uid, false);
    if (!umgr)
	return false;
    /* See if already have result */
    word_t val;
    if (keyvalue_find(umgr->map, (word_t) ref, &val)) {
	report(4, "\tRetrieved previous value 0x%llx", val);
	mgr->stat_counter[STATB_UOP_HIT_CNT]++;
	return (send_as_operand(dest, val));
    }
    /* See if terminal case */
    if (REF_IS_CONST(ref)) {
	val = uop_node_functions[umgr->operation](umgr, ref, (word_t) 0,
						  (word_t) 0, umgr->auxinfo);
	report(4, "\tConstant node yields 0x%llx", val);
	return (send_as_operand(dest, val));
    }
    /* See if there is an outstanding downward call */
    word_t w;
    if (keyvalue_remove(umgr->deferred_uop_table, (word_t) ref, NULL, &w)) {
	ilist_ptr ilist = (ilist_ptr) w;
	ilist_ptr ele = ilist_new(dest, false);
	ele->next = ilist;
	keyvalue_insert(umgr->deferred_uop_table, (word_t) ref, (word_t) ele);
	report(4, "\tDeferred operation", val);
	return true;
    } else {
	/* Insert empty list as placeholder */
	keyvalue_insert(umgr->deferred_uop_table, (word_t) ref, (word_t) NULL);
    }
    /* Must do call */
    chunk_ptr upop = build_uop_up(dest, uid, ref);
    word_t hidest = msg_new_destination(upop, 3+OP_HEADER_CNT);
    word_t lodest = msg_new_destination(upop, 4+OP_HEADER_CNT);
    /* Apply recursively */
    ref_t vref, hiref, loref;
    ref_deref(mgr, ref, &vref, &hiref, &loref);
    chunk_ptr hiop = build_uop_down(hidest, uid, hiref);
    chunk_ptr loop = build_uop_down(lodest, uid, loref);
    bool ok = send_op(upop) && send_op(hiop) && send_op(loop);
    chunk_free(upop);
    chunk_free(hiop);
    chunk_free(loop);
    if (verblevel >= 4) {
	char hibuf[24], lobuf[24];
	ref_show(hiref, hibuf); ref_show(loref, lobuf);
	report(4, "\tCreated downward calls for children %s and %s",
	       hibuf, lobuf);
    }
    return ok;
}

/* Finish off unary operation */
static bool up_store(uop_mgr_ptr umgr, word_t dest, word_t ref, word_t val) {
    report(4, "\tComputed value 0x%llx", val);
    bool ok = send_as_operand(dest, val);

    if (verblevel >= 4) {
	unsigned op_id = msg_get_dest_op_id(dest);
	unsigned agent = msg_get_dest_agent(dest);
	report(4, "\tSent result.  Agent %u, Op Id 0x%x",
	       agent, op_id);
    }

    word_t w;
    if (keyvalue_remove(umgr->deferred_uop_table, (word_t) ref, NULL, &w)) {
	ilist_ptr ilist = (ilist_ptr) w;
	ilist_ptr ele = ilist;
	while (ele) {
	    word_t ndest = ele->dest;
	    ok = ok && send_as_operand(ndest, val);
	    if (verblevel >= 4) {
		unsigned op_id = msg_get_dest_op_id(ndest);
		unsigned agent = msg_get_dest_agent(ndest);
		report(4, "\tSent deferred result.  Agent %u, Op Id 0x%x",
		       agent, op_id);
	    }
	    ele = ele->next;
	}
	ilist_free(ilist);
    }
    keyvalue_insert(umgr->map, (word_t) ref, val);
    return ok;
}

/* Perform upward portion of cofactor operation */
static bool complete_cofactor(uop_mgr_ptr umgr, word_t dest, unsigned uid,
			      ref_t ref, ref_t hiref, ref_t loref,
			      word_t *valp) {
    /* Auxinfo is a set of literals */
    /* Return value is ref of cofactored function */
    bool done = false;
    set_ptr litset = (set_ptr) umgr->auxinfo;
    ref_t vr = REF_VAR(REF_GET_VAR(ref));
    if (set_member(litset, (word_t) vr, false)) {
	/* Want positive cofactor */
	*valp = (word_t) hiref;
	done = true;
    } else if (set_member(litset, (word_t) REF_NEGATE(vr), false)) {
	/* Want negative cofactor */
	*valp = (word_t) loref;
	done = true;
    } else {
	/* Must canonize (vr, hiref, loref) */
	chunk_ptr ucp;
	ref_t nr = ref_canonize_local(vr, hiref, loref, &ucp);
	if (REF_IS_RECURSE(nr)) {
	    size_t negate = REF_GET_NEG(nr);
	    ref_t nhiref = chunk_get_word(ucp, 1);
	    ref_t nloref = chunk_get_word(ucp, 2);
	    word_t hash = utable_hash(ucp);
	    chunk_ptr smsg = build_uop_store(dest, uid, ref);
	    word_t sdest = msg_new_destination(smsg, 3+OP_HEADER_CNT);
	    chunk_ptr cmsg = build_canonize_lookup(sdest, hash, vr,
						   nhiref, nloref, negate);
	    bool ok = send_op(cmsg);
	    ok = ok && send_op(smsg);
	    chunk_free(smsg);
	    chunk_free(cmsg);
	    chunk_free(ucp);
	} else {
	    done = true;
	    *valp = nr;
	}
    }
    return done;
}

static bool complete_equant(uop_mgr_ptr umgr, word_t dest, unsigned uid,
			    ref_t ref, ref_t hiref, ref_t loref, word_t *valp) {
    /* Auxinfo is a set of variables */
    /* Return value is ref of shifted function */
    bool done = false;
    set_ptr vset = (set_ptr) umgr->auxinfo;
    ref_t vr = REF_VAR(REF_GET_VAR(ref));
    if (set_member(vset, (word_t) vr, false)) {
	/* Want to compute loref or hiref */
	chunk_ptr ucp;
	ref_t nr = ref_ite_local(umgr->mgr, hiref, REF_ONE, loref, &ucp);
	if (REF_IS_RECURSE(nr)) {
	    bool negate = REF_GET_NEG(nr);
	    chunk_ptr smsg = build_uop_store(dest, uid, ref);
	    word_t sdest = msg_new_destination(smsg, 3+OP_HEADER_CNT);
	    ref_t iref = (ref_t) chunk_get_word(ucp, 0);
	    ref_t tref = (ref_t) chunk_get_word(ucp, 1);
	    ref_t eref = (ref_t) chunk_get_word(ucp, 2);
	    chunk_ptr imsg = build_ite_lookup(sdest, iref, tref, eref, negate);
	    bool ok = send_op(imsg);
	    ok = ok && send_op(smsg);
	    chunk_free(smsg);
	    chunk_free(imsg);
	    chunk_free(ucp);
	} else {
	    *valp = (word_t) nr;
	    done = true;
	}
    } else {
	/* Must canonize (vr, hiref, loref) */
	chunk_ptr ucp;
	ref_t nr = ref_canonize_local(vr, hiref, loref, &ucp);
	if (REF_IS_RECURSE(nr)) {
	    size_t negate = REF_GET_NEG(nr);
	    ref_t nhiref = (ref_t) chunk_get_word(ucp, 1);
	    ref_t nloref = (ref_t) chunk_get_word(ucp, 2);
	    word_t hash = utable_hash(ucp);
	    chunk_ptr smsg = build_uop_store(dest, uid, ref);
	    word_t sdest = msg_new_destination(smsg, 3+OP_HEADER_CNT);
	    chunk_ptr cmsg = build_canonize_lookup(sdest, hash, vr,
						   nhiref, nloref, negate);
	    bool ok = send_op(cmsg);
	    ok = ok && send_op(smsg);
	    chunk_free(smsg);
	    chunk_free(cmsg);
	    chunk_free(ucp);
	} else {
	    done = true;
	    *valp = nr;
	}
    }
    return done;
}

static bool complete_shift(uop_mgr_ptr umgr, word_t dest, unsigned uid,
			   ref_t ref, ref_t hiref, ref_t loref, word_t *valp) {
    bool done = false;
    /* Return value is ref of shifted function */
    keyvalue_table_ptr vmap = (keyvalue_table_ptr) umgr->auxinfo;
    ref_t vr = REF_VAR(REF_GET_VAR(ref));
    word_t wv;
    if (keyvalue_find(vmap, (word_t) vr, &wv)) {
	/* Shift variable */
	vr = (ref_t) wv;
    }
    /* Must canonize (vr, hiref, loref) */
    chunk_ptr ucp;
    ref_t nr = ref_canonize_local(vr, hiref, loref, &ucp);
    if (REF_IS_RECURSE(nr)) {
	size_t negate = REF_GET_NEG(nr);
	ref_t nhiref = chunk_get_word(ucp, 1);
	ref_t nloref = chunk_get_word(ucp, 2);
	word_t hash = utable_hash(ucp);
	chunk_ptr smsg = build_uop_store(dest, uid, ref);
	word_t sdest = msg_new_destination(smsg, 3+OP_HEADER_CNT);
	chunk_ptr cmsg = build_canonize_lookup(sdest, hash, vr,
					       nhiref, nloref, negate);
	bool ok = send_op(cmsg);
	ok = ok && send_op(smsg);
	chunk_free(smsg);
	chunk_free(cmsg);
	chunk_free(ucp);
    } else {
	done = true;
	*valp = nr;
    }
    return done;
}

bool do_uop_up_op(chunk_ptr op) {
    word_t dest =          chunk_get_word(op, 0+OP_HEADER_CNT);
    unsigned uid =         chunk_get_word(op, 1+OP_HEADER_CNT);
    ref_t ref =    (ref_t) chunk_get_word(op, 2+OP_HEADER_CNT);
    word_t hival =         chunk_get_word(op, 3+OP_HEADER_CNT);
    word_t loval =         chunk_get_word(op, 4+OP_HEADER_CNT);
    char buf[24];
    if (verblevel >= 4) {
	ref_show(ref, buf);
	report(4, "Upward traversal hits %s", buf);
    }
    /* look for umgr */
    uop_mgr_ptr umgr = find_umgr(uid, false);
    if (!umgr)
	return false;
    unsigned opcode = umgr->operation;
    word_t val = 0;
    bool done = false;
    switch(opcode) {
    case UOP_COFACTOR:
	done = complete_cofactor(umgr, dest, uid, ref,
				 (ref_t) hival, (ref_t) loval, &val);
	break;
    case UOP_EQUANT:
	done = complete_equant(umgr, dest, uid, ref,
			       (ref_t) hival, (ref_t) loval, &val);
	break;
    case UOP_SHIFT:
	done = complete_shift(umgr, dest, uid, ref,
			      (ref_t) hival, (ref_t) loval, &val);
	break;
    default:
	/* Other cases can use standard function */
	val = uop_node_functions[umgr->operation](umgr, ref,
						  hival, loval, umgr->auxinfo);
	done = true;
	break;
    }
    if (done)
	return up_store(umgr, dest, ref, val);
    else
	return true;
}

bool do_uop_store_op(chunk_ptr op) {
    word_t dest =          chunk_get_word(op, 0+OP_HEADER_CNT);
    unsigned uid =         chunk_get_word(op, 1+OP_HEADER_CNT);
    ref_t ref =    (ref_t) chunk_get_word(op, 2+OP_HEADER_CNT);
    word_t val =           chunk_get_word(op, 3+OP_HEADER_CNT);
    char buf[24];
    ref_mgr mgr = dmgr->rmgr;
    mgr->stat_counter[STATB_UOP_STORE_CNT]++;
    if (verblevel >= 4) {
	ref_show(ref, buf);
	report(4, "Upward store hits %s", buf);
    }
    /* look for umgr */
    uop_mgr_ptr umgr = find_umgr(uid, false);
    if (!umgr)
	return false;
    return up_store(umgr, dest, ref, val);
}


/* Operations available to client */
ref_t dist_var(ref_mgr mgr) {
    word_t dest = msg_build_destination(own_agent, new_operator_id(), 0);
    chunk_ptr msg = build_var(dest);
    ref_t r = fire_wait_and_get(mgr, msg);
    chunk_free(msg);
    unsigned idx = REF_GET_VAR(r);
    /* Keep track of highest numbered variable */
    if (idx >= mgr->variable_cnt)
	mgr->variable_cnt = idx+1;
    return r;
}

ref_t dist_ite(ref_mgr mgr, ref_t iref, ref_t tref, ref_t eref) {
    chunk_ptr ucp = NULL;
    if (verblevel >= 4) {
	char buf1[24], buf2[24], buf3[24];
	ref_show(iref, buf1); ref_show(tref, buf2); ref_show(eref, buf3);
	report(4, "Computing Distance ITE(%s, %s, %s)", buf1, buf2, buf3);
    }
    ref_t rlocal = ref_ite_local(mgr, iref, tref, eref, &ucp);
    if (REF_IS_RECURSE(rlocal)) {
	ref_t niref = (ref_t) chunk_get_word(ucp, 0);
	ref_t ntref = (ref_t) chunk_get_word(ucp, 1);
	ref_t neref = (ref_t) chunk_get_word(ucp, 2);
	chunk_free(ucp);
	bool negate = REF_GET_NEG(rlocal);
	word_t dest = msg_build_destination(own_agent, new_operator_id(), 0);
	chunk_ptr msg = build_ite_lookup(dest, niref, ntref, neref, negate);
	ref_t r = fire_wait_and_get(mgr, msg);
	chunk_free(msg);
	return r;
    } else {
	return rlocal;
    }
}

keyvalue_table_ptr dist_density(ref_mgr mgr, set_ptr roots) {
    keyvalue_table_ptr dtable = word_keyvalue_new();
    if (start_client_global(UOP_DENSITY, 0, NULL)) {
	report(5, "Started density operation");
    } else {
	err(false, "Couldn't start global operations");
	return NULL;
    }
    set_iterstart(roots);
    word_t w;
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	word_t dest = msg_build_destination(own_agent, new_operator_id(), 0);
	chunk_ptr msg = build_uop_down(dest, own_agent, r);

	chunk_ptr rmsg = fire_and_wait(msg);
	chunk_free(msg);
	if (rmsg) {
	    word_t v = chunk_get_word(rmsg, 1);
	    chunk_free(rmsg);
	    keyvalue_insert(dtable, (word_t) r, v);
	} else {
	    char buf[24];
	    ref_show(r, buf);
	    err(false, "Could not get density for %s", buf);
	}
    }
    finish_client_global(own_agent);
    return dtable;
}

keyvalue_table_ptr dist_count(ref_mgr mgr, set_ptr roots) {
    keyvalue_table_ptr ctable = word_keyvalue_new();
    word_t nvars = mgr->variable_cnt;
    if (start_client_global(UOP_PCOUNT, 1, &nvars)) {
	report(5, "Started count operation");
    } else {
	err(false, "Couldn't start global count operations");
	return NULL;
    }
    set_iterstart(roots);
    word_t w;
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	word_t dest = msg_build_destination(own_agent, new_operator_id(), 0);
	chunk_ptr msg = build_uop_down(dest, own_agent, r);
	chunk_ptr rmsg = fire_and_wait(msg);
	chunk_free(msg);
	if (rmsg) {
	    word_t v = chunk_get_word(rmsg, 1);
	    word_t cnt = pval2cnt(v, 0);
	    chunk_free(rmsg);
	    keyvalue_insert(ctable, (word_t) r, cnt);
	} else {
	    char buf[24];
	    ref_show(r, buf);
	    err(false, "Could not get count for %s", buf);
	}
    }
    finish_client_global(own_agent);
    return ctable;
}

void dist_mark(ref_mgr mgr, set_ptr roots) {
    set_iterstart(roots);
    word_t w;
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	word_t dest = msg_build_destination(own_agent, new_operator_id(), 0);
	/* Controller uses uid 0 */
	if (verblevel >= 5) {
	    char buf[24];
	    ref_show(r, buf);
	    report(5, "Starting mark at root %s", buf);
	}
	chunk_ptr msg = build_uop_down(dest, 0, r);
	chunk_ptr rmsg = fire_and_wait(msg);
	chunk_free(msg);
	if (rmsg)
	    chunk_free(rmsg);
    }
}

set_ptr dist_support(ref_mgr mgr, set_ptr roots) {
    /* Implement using bit vector representation of variable set */
    word_t vset = 0;
    if (start_client_global(UOP_SUPPORT, 0, NULL)) {
	report(5, "Started support operation");
    } else {
	err(false, "Couldn't start global operation");
	return NULL;
    }
    set_iterstart(roots);
    word_t w;
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	word_t dest = msg_build_destination(own_agent, new_operator_id(), 0);
	chunk_ptr msg = build_uop_down(dest, own_agent, r);
	chunk_ptr rmsg = fire_and_wait(msg);
	chunk_free(msg);
	if (rmsg) {
	    word_t v = chunk_get_word(rmsg, 1);
	    chunk_free(rmsg);
	    /* Set union */
	    vset |= v;
	} else {
	    char buf[24];
	    ref_show(r, buf);
	    err(false, "Could not get support for %s", buf);
	}
    }
    finish_client_global(own_agent);
    return bv2vset(vset);
}

/* Create key-value table mapping set of root nodes to their restrictions,
   with respect to a set of literals (given as a set of refs)
*/
keyvalue_table_ptr dist_restrict(ref_mgr mgr, set_ptr roots, set_ptr lits) {
    keyvalue_table_ptr dtable = word_keyvalue_new();
    size_t nword = set_marshal_size(lits);
    word_t *data = calloc_or_fail(nword, sizeof(word_t), "dist_restrict");
    set_marshal(lits, data);
    if (start_client_global(UOP_COFACTOR, nword, data)) {
	report(5, "Started restriction operation");
    } else {
	err(false, "Couldn't start global operations");
	return false;
    }
    set_iterstart(roots);
    word_t w;
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	word_t dest = msg_build_destination(own_agent, new_operator_id(), 0);
	chunk_ptr msg = build_uop_down(dest, own_agent, r);
	ref_t nr = fire_wait_and_get(mgr, msg);
	chunk_free(msg);
	keyvalue_insert(dtable, (word_t) r, (word_t) nr);
    }
    finish_client_global(own_agent);
    free_array(data, nword, sizeof(word_t));
    return dtable;
}


/* Create key-value table mapping set of root nodes to their
   existential quantifications with respect to a set of variables
   (given as a set of refs)
*/
keyvalue_table_ptr dist_equant(ref_mgr mgr, set_ptr roots, set_ptr vars) {
    keyvalue_table_ptr dtable = word_keyvalue_new();
    size_t nword = set_marshal_size(vars);
    word_t *data = calloc_or_fail(nword, sizeof(word_t), "dist_equant");
    set_marshal(vars, data);
    if (start_client_global(UOP_EQUANT, nword, data)) {
	report(5, "Started quantification operation");
    } else {
	err(false, "Couldn't start global operations");
	return false;
    }
    set_iterstart(roots);
    word_t w;
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	word_t dest = msg_build_destination(own_agent, new_operator_id(), 0);
	chunk_ptr msg = build_uop_down(dest, own_agent, r);
	ref_t nr = fire_wait_and_get(mgr, msg);
	chunk_free(msg);
	keyvalue_insert(dtable, (word_t) r, (word_t) nr);
    }
    finish_client_global(own_agent);
    free_array(data, nword, sizeof(word_t));
    return dtable;
}

/* Create key-value table mapping set of root nodes to their shifted versions
   with respect to a mapping from old variables to new ones 
*/
keyvalue_table_ptr dist_shift(ref_mgr mgr, set_ptr roots,
			      keyvalue_table_ptr vmap) {
    keyvalue_table_ptr dtable = word_keyvalue_new();
    size_t nword = keyvalue_marshal_size(vmap);
    word_t *data = calloc_or_fail(nword, sizeof(word_t), "dist_shift");
    keyvalue_marshal(vmap, data);
    if (start_client_global(UOP_SHIFT, nword, data)) {
	report(5, "Started shift operation");
    } else {
	err(false, "Couldn't start global operations");
	return false;
    }
    set_iterstart(roots);
    word_t w;
    while (set_iternext(roots, &w)) {
	ref_t r = (ref_t) w;
	word_t dest = msg_build_destination(own_agent, new_operator_id(), 0);
	chunk_ptr msg = build_uop_down(dest, own_agent, r);
	ref_t nr = fire_wait_and_get(mgr, msg);
	chunk_free(msg);
	keyvalue_insert(dtable, (word_t) r, (word_t) nr);
    }
    finish_client_global(own_agent);
    free_array(data, nword, sizeof(word_t));
    return dtable;
}

/* Worker UOP functions */
void uop_start(unsigned id, unsigned opcode, unsigned nword, word_t *data) {
    ref_mgr mgr = dmgr->rmgr;
    void *auxinfo = NULL;
    word_t *wstore = NULL;
    switch(opcode) {
	set_ptr dset = NULL;
	keyvalue_table_ptr dtable = NULL;
    case UOP_MARK:
    case UOP_SUPPORT:
	dset = word_set_new();
	auxinfo = (void *) dset;
	break;
    case UOP_DENSITY:
	/* Don't need auxinfo */
	break;
    case UOP_PCOUNT:
	/* Allocate word and copy nvars from controller */
	wstore = malloc_or_fail(sizeof(word_t), "uop_start");
	*wstore = *data;
	auxinfo = (void *) wstore;
	break;
    case UOP_COFACTOR:
    case UOP_EQUANT:
	dset = word_set_new();
	set_unmarshal(dset, data, nword);
	auxinfo = (void *) dset;
	break;
    case UOP_SHIFT:
	dtable = word_keyvalue_new();
	keyvalue_unmarshal(dtable, data, nword);
	auxinfo = (void *) dtable;
	break;
    default:
	err(false, "Unknown unary opcode %u in uop_start", opcode);
	break;
    }
    uop_mgr_ptr umgr = new_uop(mgr, id, opcode, auxinfo, true);
    umgr->next = dmgr->umgr_list;
    dmgr->umgr_list = umgr;
}

void uop_finish(unsigned id) {
    uop_mgr_ptr umgr = find_umgr(id, true);
    if (!umgr)
	return;
    ref_mgr mgr = dmgr->rmgr;
    set_ptr aset = NULL;
    keyvalue_table_ptr atable = NULL;
    switch(umgr->operation) {
    case UOP_MARK:
	aset = (set_ptr) umgr->auxinfo;
	complete_collection(mgr, aset);
	set_free(aset);
	break;
    case UOP_DENSITY:
    case UOP_PCOUNT:
	/* Deallocate saved word */
	free_block(umgr->auxinfo, sizeof(word_t));
	break;
    case UOP_SUPPORT:
    case UOP_COFACTOR:
    case UOP_EQUANT:
	aset = (set_ptr) umgr->auxinfo;
	set_free(aset);
	break;
    case UOP_SHIFT:
	atable = (keyvalue_table_ptr) umgr->auxinfo;
	keyvalue_free(atable);
	break;
    default:
	err(false, "Unknown unary opcode %u in uop_start", umgr->operation);
	break;
    }
    free_uop(umgr);
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
    /* These come from BDD package */
    "Current unique entries",
    "Peak unique entries   ",
    "Total unique entires  ",
    "Unique hash collisions",
    "Total number of ITEs  ",
    "ITEs handled locally  ",
    "ITEs found in table   ",
    "ITES causing recursion",
    "Current ITEc entries  ",
    "Peak ITEc entries     ",
    "Total ITEc entries    ",
    "Total unary operations",
    "Unary ops from table  ",
    "Unary stores          "
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
	report(1, "%s: Min: %" PRIu64 "\tMax: %" PRIu64
	       "\tAvg: %.2f\tSum: %" PRIu64,
	       stat_items[i], minval, maxval, (double) sumval/nworker, sumval);
    }
}

void worker_gc_start() {
    uop_start(0, UOP_MARK, 0, NULL);
}

void worker_gc_finish() {
    uop_finish(0);
}
