/* Support for conjunction and Coudert/Madre simplification operations */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <getopt.h>
#include <stdbool.h>
#include <sys/select.h>
#include <signal.h>
#include <math.h>

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

/* Compiled options */

/* Macros */
#define SMAX(x, y) ((x) < (y) ? (y) : (x))

/* Parameters */

/* Should I check results of conjunct (and possibly other) operations? */
int check_results = 0;

/* Should arguments to conjunction be preprocessed with soft and operation */
int preprocess = 1;

/* Should arguments be reprocessed with Coudert/Madre restrict or soft and operation as conjunction proceeds? */
int reprocess = 1;

/* Maximum number of pairs to try when doing conjunction with aborts */
int abort_limit = 7;
/* Maximum expansion factor for conjunction (scaled 100x) */
int expansion_factor_scaled = 142;
/* Number of passes of conjunction before giving up */
int pass_limit = 3;

/* Lower bound on support coverage metric required to attempt soft and
   (Scaled by 100)
 */
int soft_and_threshold = 80;

/* Maximum amount by which support coverage and similarities can be discounted for large arguments */
double max_large_argument_penalty = 0.4;

/* Size of the smallest and largest BDDs in the conjunction */
size_t max_argument_size = 0;
size_t min_argument_size = 0;

/* Log of the smallest and largest BDDs in the conjunction */
/* These get updated as conjuncts are removed and added */
double log10_min_size = 4.0;
double log10_max_size = 8.0;


/* Representation of set of terms to be conjuncted */
/* Linked list elements */
/* Also use as standalone representation of function + metadata */
typedef struct RELE {
    ref_t fun;
    double sat_count;
    size_t size;
    int support_count;
    int *support_indices;
    struct RELE *next;
} rset_ele;

/* Data collected during conjunction */
typedef struct {
    size_t max_size;
    size_t sum_size;
    size_t total_size;
} conjunction_data;

bool do_similar(int argc, char *argv[]);
bool do_coverage(int argc, char *argv[]);
bool do_conjunct(int argc, char *argv[]);

void init_conjunct() {
    /* Add commands and options */
    add_cmd("conjunct", do_conjunct,
	    " fd f1 f2 ...   | fd <- f1 & f2 & ...");
    add_cmd("similar", do_similar,
	    "f1 f2 ...       | Compute pairwise support similarity for functions");
    add_cmd("cover", do_coverage,
	    "f1 f2 ...       | Compute pairwise support coverage for functions");
    add_param("check", &check_results, "Check results of conjunctoperations", NULL);
    add_param("abort", &abort_limit, "Maximum number of pairs to attempt in single conjunction step", NULL);
    add_param("pass", &pass_limit, "Maximum number of passes during single conjunction", NULL);
    add_param("expand", &expansion_factor_scaled, "Maximum expansion of successive BDD sizes (scaled by 100) for each pass", NULL);
    preprocess = 0;
    reprocess = 0;
}

/*** Operations on rsets ***/
static void rset_ele_new_fun(rset_ele *ele, ref_t fun);

static rset_ele *rset_new(ref_t fun) {
    rset_ele *ele = malloc_or_fail(sizeof(rset_ele), "rset_new");
    rset_ele_new_fun(ele, fun);
    ele->next = NULL;
    return ele;
}

/* Free entire list */
void rset_free(rset_ele *set) {
    rset_ele *ptr = set;
    while (ptr) {
	root_deref(ptr->fun);
	if (ptr->support_indices != NULL)
	    free(ptr->support_indices);
	rset_ele *nptr = ptr->next;
	free_block((void *) ptr, sizeof(rset_ele));
	ptr = nptr;
    }
}

static void rset_ele_new_fun(rset_ele *ele, ref_t fun) {
    ele->fun = fun;
    ele->sat_count = -1.0;
    ele->size = 0;
    ele->support_count = -1;
    ele->support_indices = NULL;
}

rset_ele *rset_add_element(rset_ele *set, rset_ele *ele) {
    ele->next = set;
    return ele;
}

rset_ele *rset_remove_element(rset_ele *set, rset_ele *ele) {
    rset_ele *next = set;
    rset_ele *prev = NULL;
    rset_ele *nset = set;
    while (next && next != ele) {
	prev = next;
	next = next->next;
    }
    if (!next)
	return set;
    /* next == ele, prev points to predecessor */
    if (prev) {
	prev->next = ele->next;
    }
    else {
	nset = next;
    }
    /* Unlink */
    ele->next = NULL;
    return nset;
}

int rset_length(rset_ele *set) {
    int len = 0;
    rset_ele *ptr;
    for (ptr = set; ptr; ptr = ptr->next)
	len++;
    return len;
}

bool rset_is_empty(rset_ele *set) {
    return set == NULL;
}

bool rset_is_singleton(rset_ele *set) {
    return !rset_is_empty(set) && rset_is_empty(set->next);
}

/*** Operations on rset elements ***/

static size_t get_size(rset_ele *ptr) {
    if (ptr == NULL)
	return 0;
    if (ptr->size == 0)
	ptr->size = cudd_single_size(smgr, ptr->fun);
    return ptr->size;
}

#if INCLUDE_SORT
static double get_sat_count(rset_ele *ptr) {
    if (ptr == NULL)
	return 0.0;
    if (ptr->sat_count <= 0.0)
	ptr->sat_count = cudd_single_count(smgr, ptr->fun);
    return ptr->sat_count;
}
#endif /* INCLUDE_SORT */

static int get_support_count(rset_ele *ptr) {
    if (ptr == NULL)
	return 0;
    if (ptr->support_count < 0) {
	ptr->support_count = shadow_support_indices(smgr, ptr->fun, &ptr->support_indices);
    }
    return ptr->support_count;
}

static int *get_support_indices(rset_ele *ptr) {
    if (ptr == NULL)
	return 0;
    if (ptr->support_count < 0) {
	ptr->support_count = shadow_support_indices(smgr, ptr->fun, &ptr->support_indices);
    }
    return ptr->support_indices;
}

/*** Operations on pairs of rsets ***/

/* Assign weight between 1.0-max_large_argument_penalty and 1.0 according to argument sizes */
static double size_weight(rset_ele *ptr1, rset_ele *ptr2) {
    size_t size1 = get_size(ptr1);
    size_t size2 = get_size(ptr2);
    size_t size = SMAX(size1, size2);
    double lsize = log10((double) size);
    double penalty = 0.0;
    if (lsize <= log10_min_size)
	penalty = max_large_argument_penalty;
    else if (lsize <= log10_max_size) {
	penalty = max_large_argument_penalty * (double) (lsize - log10_min_size) / (log10_max_size - log10_min_size);
    }
    return 1.0 - penalty;
}

static double get_support_similarity(rset_ele *ptr1, rset_ele *ptr2, bool weighted) {
    int support_count1 = get_support_count(ptr1);
    int *indices1 = get_support_indices(ptr1);
    int support_count2 = get_support_count(ptr2);
    int *indices2 = get_support_indices(ptr2);
    double sim = index_similarity(support_count1, indices1, support_count2, indices2);
    if (weighted) {
	double weight = size_weight(ptr1, ptr2);
	sim *= weight;
    }
    return sim;
}

static double get_support_coverage(rset_ele *ptr1, rset_ele *ptr2, bool weighted) {
    int support_count1 = get_support_count(ptr1);
    int *indices1 = get_support_indices(ptr1);
    int support_count2 = get_support_count(ptr2);
    int *indices2 = get_support_indices(ptr2);
    double cov = index_coverage(support_count1, indices1, support_count2, indices2);
    if (weighted) {
	double weight = size_weight(ptr1, ptr2);
	cov *= weight;
    }
    return cov;
}

/* Find largest element in set */
static size_t rset_max_size(rset_ele *set) {
    size_t size = 0;
    rset_ele *ptr;
    for (ptr = set; ptr; ptr = ptr->next) {
	size_t esize = get_size(ptr);
	if (esize > size)
	    size = esize;
    }
    return size;
}

/* Find largest element in set */
static size_t rset_min_size(rset_ele *set) {
    size_t size = (size_t) -1;  /* Wraps around to max size_t value */
    rset_ele *ptr;
    for (ptr = set; ptr; ptr = ptr->next) {
	size_t esize = get_size(ptr);
	if (esize < size)
	    size = esize;
    }
    return size;
}

static void compute_size_range(rset_ele *set) {
    max_argument_size = rset_max_size(set);
    min_argument_size = rset_min_size(set);
    log10_max_size = log10(max_argument_size == 0 ? 1.0 : (double) max_argument_size);
    log10_min_size = log10(min_argument_size == 0 ? 1.0 : (double) min_argument_size);
}

/* Compute AND of terms without decrementing reference counts */
/* Used to check results of conjunction operation */
static ref_t and_check(rset_ele *set) {
    ref_t rval = shadow_one(smgr);
    root_addref(rval, false);
    rset_ele *ptr = set;
    while(ptr) {
	ref_t arg = ptr->fun;
	ref_t nval = shadow_and(smgr, rval, arg);
	root_addref(nval, false);
	root_deref(rval);
	rval = nval;
	ptr = ptr->next;
    }
    return rval;
}

/* Print statistics about partial results */
/* sofar is and of conjuncts to this point */
static void report_combination(rset_ele *set, conjunction_data *data) {
    size_t result_size = 0;
    size_t max_size = 0;
    size_t sum_size = 0;
    size_t count = rset_length(set);
    if (verblevel < 1)
	return;
    /* Must convert to other form of set */
    set_ptr pset = word_set_new();
    size_t set_size = count;
    rset_ele *ptr = set;
    while (ptr) {
	ref_t r = ptr->fun;
	if (REF_IS_INVALID(r)) {
	    count--;
	} else {
	    set_insert(pset, (word_t) r);
	    size_t nsize = cudd_single_size(smgr, r);
	    max_size = SMAX(max_size, nsize);
	    sum_size += nsize;
	}
	ptr = ptr->next;
    }
    size_t total_size = cudd_set_size(smgr, pset);
    double ratio = (double) total_size/sum_size;
    report(1, "Partial result with %zd values.  Max size = %zd.  Sum of sizes = %d.  Combined size = %zd.  (Sharing factor = %.2f) Computed size = %zd",
	   set_size, max_size, sum_size, total_size, ratio, result_size);
    set_free(pset);
    if (data) {
	data->total_size = SMAX(data->total_size, total_size);
	data->max_size = SMAX(data->max_size, max_size);
	data->sum_size = SMAX(data->sum_size, sum_size);
    }
}

/* Conditionally simplify elements of one set with those of another */
static void soft_simplify(rset_ele *set, rset_ele *other_set) {
    double threshold = 0.01 * soft_and_threshold;
    rset_ele *myptr, *otherptr;
    for (myptr = set; myptr; myptr = myptr->next) {
	ref_t myrval = myptr->fun;
	if (REF_IS_INVALID(myrval))
	    continue;
	for (otherptr = other_set; otherptr; otherptr = otherptr->next) {
	    ref_t otherrval = otherptr->fun;
	    if (REF_IS_INVALID(otherrval))
		continue;
	    /* Attempt to simplify myrval using otherrval */
	    double cov = get_support_coverage(otherptr, myptr, false);
	    if (cov >= threshold) {
		ref_t nval = shadow_soft_and(smgr, myrval, otherrval);
		if (REF_IS_INVALID(nval))
		    continue;
		root_addref(nval, false);
		root_deref(myrval);
		rset_ele_new_fun(myptr, nval);
	    }
	}
    }
}

/* Manage candidate argument pairs */
typedef struct {
    rset_ele *ptr1;
    rset_ele *ptr2;
    double sim;
} pair;

/* Manage set of candidates */
static void clear_candidates(pair *candidates) {
    int i;
    for (i = 0; i < abort_limit; i++) {
	candidates[i].ptr1 = NULL;
	candidates[i].ptr2 = NULL;
	candidates[i].sim = -1.0;
    }
}

/* Ordered insertion into candidates.  Element 0 has highest sim */
static void insert_candidate(pair *candidates, rset_ele *ptr1, rset_ele *ptr2, double sim)  {
    int idx;
    for (idx = 0; idx < abort_limit; idx++) {
	if (sim > candidates[idx].sim) {
	    /* Replace with new values and shift old values down */
	    rset_ele *nptr1 = candidates[idx].ptr1;
	    rset_ele *nptr2 = candidates[idx].ptr2;
	    double nsim = candidates[idx].sim;
	    candidates[idx].ptr1 = ptr1;
	    candidates[idx].ptr2 = ptr2;
	    candidates[idx].sim = sim;
	    ptr1 = nptr1;
	    ptr2 = nptr2;
	    sim = nsim;
	}
    }
}

static ref_t similarity_combine(rset_ele *set, conjunction_data *data) {
    double expansion_factor = (double) expansion_factor_scaled / 100.0;
    pair candidates[abort_limit];
    size_t abort_count = 0;
    size_t argument_count = rset_length(set);
    size_t max_size_limit = 0;
    if (argument_count == 0) {
	ref_t rval = shadow_one(smgr);
	root_addref(rval, false);
	return rval;
    }
    while (!rset_is_singleton(set)) {
	compute_size_range(set);
	clear_candidates(candidates);
	rset_ele *ptr1 = NULL;
	rset_ele *ptr2 = NULL;
	int ccount = 0;
	for (ptr1 = set; ptr1; ptr1 = ptr1->next) {
	    for (ptr2 = ptr1->next; ptr2; ptr2 = ptr2->next) {
		double sim = get_support_similarity(ptr1, ptr2, true);
		insert_candidate(candidates, ptr1, ptr2, sim);
		if (ccount < abort_limit)
		    ccount++;
	    }
	}
	/* Loop around all cases.  If don't succeed with bounded AND on first pass
	   then do one more try with unbounded */
	ref_t arg1 = REF_INVALID;
	ref_t arg2 = REF_INVALID;
	ref_t nval = REF_INVALID;
	double sim = -1.0;
	/* Each pass allows a larger limit.  Final pass removes size bound */
	size_t size_limit = max_argument_size;
	max_size_limit = SMAX(max_size_limit, size_limit);
	int try_limit = ccount * pass_limit + 1;
	int try;
	for (try = 0; try <= try_limit; try++) {
	    bool final_try = try == try_limit;
	    int tidx = try % ccount;
	    if (tidx == 0) {
		/* Start of a new pass */
		/* Increase size limit for this pass */
		size_limit = (size_t) (size_limit * expansion_factor);
		max_size_limit = SMAX(max_size_limit, size_limit);
		report(5, "Setting size limit to %zd", size_limit);
	    }
	    ptr1 = candidates[tidx].ptr1;
	    ptr2 = candidates[tidx].ptr2;
	    sim = candidates[tidx].sim;
	    arg1 = ptr1->fun;
	    size_t asize = get_size(ptr1);
	    arg2 = ptr2->fun;
	    asize = get_size(ptr2);

	    nval = final_try ? shadow_and(smgr, arg1, arg2) : shadow_and_limit(smgr, arg1, arg2, size_limit);

	    if (!REF_IS_INVALID(nval))
		break;
	    abort_count ++;
	    if (verblevel >= 4) {
		char arg1_buf[24], arg2_buf[24];
		shadow_show(smgr, arg1, arg1_buf);
		shadow_show(smgr, arg2, arg2_buf);
		report(3, "%s & %s (sim = %.3f, try #%d) requires more than %zd nodes", arg1_buf, arg2_buf, sim, try+1, size_limit);
	    }

	}
	if (REF_IS_INVALID(nval))
	    err(true, "Couldn't compute conjunction");

	root_addref(nval, false);
	set = rset_remove_element(set, ptr1);
	set = rset_remove_element(set, ptr2);
	if (verblevel >= 3) {
	    char arg1_buf[24], arg2_buf[24], nval_buf[24];
	    shadow_show(smgr, arg1, arg1_buf);
	    shadow_show(smgr, arg2, arg2_buf);
	    shadow_show(smgr, nval, nval_buf);
	    report(3, "%s & %s (sim = %.3f, try #%d) --> %s", arg1_buf, arg2_buf, sim, try+1, nval_buf);
	}
	rset_free(ptr1);
	rset_free(ptr2);
	rset_ele *nset = rset_new(nval);
	/* Attempt to simplify in both directions */
	soft_simplify(nset, set);
	soft_simplify(set, nset);
	set = rset_add_element(set, nset);
	report_combination(set, data);
    }
    report(1, "Conjunction of %zd elements.  %zd aborts.  Max argument %zd.  Max size limit %zd",
	   argument_count, abort_count, max_argument_size, max_size_limit);

    ref_t rval = set->fun;
    root_addref(rval, true);
    rset_free(set);
    return rval;

}

/* Compute conjunction of set (destructive) */
ref_t rset_conjunct(rset_ele *set) {
    conjunction_data cdata;
    cdata.max_size = 0;
    cdata.total_size = 0;
    cdata.sum_size = 0;
    ref_t rprod = check_results ? and_check(set) : REF_INVALID;

    ref_t rval = shadow_one(smgr);

    rval = similarity_combine(set, &cdata);

    if (check_results) {
	if (rprod != rval) {
	    char prod_buf[24], conj_buf[24];
	    shadow_show(smgr, rprod, prod_buf);
	    shadow_show(smgr, rval, conj_buf);
	    report(0, "WARNING: Conjuncting (%s) != Product (%s)", conj_buf, prod_buf);
	}
	root_deref(rprod);
    }

    size_t rsize = cudd_single_size(smgr, rval);
    report(0, "Conjunction result %zd Max_BDD %zd Max_combined %zd Max_sum %zd",
	   rsize, cdata.max_size, cdata.total_size, cdata.sum_size);

    return rval;
}

bool do_conjunct(int argc, char *argv[]) {
    if (argc < 2) {
	report(0, "Need destination name");
	return false;
    }

    int i;
    rset_ele *set = NULL;
    for (i = 2; i < argc; i++) {
	ref_t rarg = get_ref(argv[i]);
	if (REF_IS_INVALID(rarg)) {
	    rset_free(set);
	    return rarg;
	}
	rset_ele *ele = rset_new(rarg);
	set = rset_add_element(set, ele);
    }

    ref_t rval = rset_conjunct(set);
    if (REF_IS_INVALID(rval)) {
	return false;
    }
    assign_ref(argv[1], rval, false, false);
    root_deref(rval);
    rset_free(set);
    return true;
}


bool do_similar(int argc, char *argv[]) {
    int r, c;
    /* Check refs */
    bool ok = true;
    for (r = 1; r < argc; r++) {
	ref_t ref = get_ref(argv[r]);
	if (REF_IS_INVALID(ref)) {
	    err(false, "Invalid function name: %s", argv[r]);
	    ok = false;
	}
    }
    if (!ok)
	return ok;
    /* Write column headings */
    for (c = 2; c < argc; c++)
	report_noreturn(0, "\t%s", argv[c]);
    report(0, "");
    for (r = 1; r < argc-1; r++) {
	report_noreturn(0, argv[r]);
	for (c = 2; c <= r; c++)
	    report_noreturn(0, "\t--");
	ref_t r1 = get_ref(argv[r]);
	for (; c < argc; c++) {
	    ref_t r2 = get_ref(argv[c]);
	    double s = shadow_similarity(smgr, r1, r2);
	    report_noreturn(0, "\t%.3f", s);
	}
	report(0, "");
    }
    return ok;
}


bool do_coverage(int argc, char *argv[]) {
    int r, c;
    /* Check refs */
    bool ok = true;
    for (r = 1; r < argc; r++) {
	ref_t ref = get_ref(argv[r]);
	if (REF_IS_INVALID(ref)) {
	    err(false, "Invalid function name: %s", argv[r]);
	    ok = false;
	}
    }
    if (!ok)
	return ok;
    /* Write column headings */
    for (c = 1; c < argc; c++)
	report_noreturn(0, "\t%s", argv[c]);
    report(0, "");
    for (r = 1; r < argc; r++) {
	report_noreturn(0, argv[r]);
	ref_t r1 = get_ref(argv[r]);
	for (c = 1; c < argc; c++) {
	    ref_t r2 = get_ref(argv[c]);
	    double s = shadow_coverage(smgr, r1, r2);
	    report_noreturn(0, "\t%.3f", s);
	}
	report(0, "");
    }
    return ok;
}


#if 0
bool do_coverage(int argc, char *argv[]) {
    int r;

    if (argc != 3) {
	err(false, "coverage command requires two arguments");
	return false;
    }
    /* Check refs */
    bool ok = true;
    for (r = 1; r < argc; r++) {
	ref_t ref = get_ref(argv[r]);
	if (REF_IS_INVALID(ref)) {
	    err(false, "Invalid function name: %s", argv[r]);
	    ok = false;
	}
    }
    if (!ok)
	return ok;

    ref_t r1 = get_ref(argv[1]);
    ref_t r2 = get_ref(argv[2]);

    double c = shadow_coverage(smgr, r1, r2);

    report(0, "Coverage(%s, %s) = %.3f", argv[1], argv[2], c);

    return true;
}
#endif
