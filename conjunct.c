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

/* Macros */
#define SMAX(x, y) ((x) < (y) ? (y) : (x))

/* Parameters */

/* Should I check results of conjunct (and possibly other) operations? */
int check_results = 0;
/*
  How should terms be combined?  
   type >=  2 ==> tree reduction.
   type ==  1 ==> dynamically sorted reduction
   type ==  0 ==> linear reduction
   type == -1 ==> similarity reduction 
   type == -2 ==> pairwise reduction 
 */

#define TERNARY_REDUCTION 3
#define BINARY_REDUCTION 2
#define DYNAMIC_REDUCTION 1
#define LINEAR_REDUCTION 0
#define SIMILARITY_REDUCTION -1
#define PAIRWISE_REDUCTION -2

int reduction_type = SIMILARITY_REDUCTION;

/* Should arguments to conjunction be preprocessed with soft and or Coudert/Madre restrict operation */
int preprocess = 0;
/* Should arguments to conjunction be sorted in descending order of sat count before preprocessing? */
int presort = 0;

/* Should arguments be reprocessed with Coudert/Madre restrict or soft and operation as conjunction proceeds? */
int reprocess = 0;

/* Should preprocessing and reprocessing be done using soft and */
int use_soft = 1;

/* Should preprocessing be done right-to-left */
int right_to_left = 0;

/* Maximum number of pairs to try when doing conjunction with aborts */
int abort_limit = 7;
/* Maximum expansion factor for conjunction (scaled 100x) */
int expansion_factor_scaled = 142;
/* Number of passes of conjunction before giving up */
int pass_limit = 3;

/* Representation of set of terms to be conjuncted */
/* Linked list elements */
typedef struct RELE {
    ref_t fun;
    double sat_count;
    size_t size;
    int support_count;
    int *support_indices;
    struct RELE *next;
} rset_ele;

struct RSET {
    rset_ele *head;
    rset_ele *tail;
    size_t length;
};

/* Data collected during conjunction */
typedef struct {
    size_t max_size;
    size_t sum_size;
    size_t total_size;
} conjunction_data;

/* String representation of conjunction options */
bool do_cstring(int argc, char *argv[]);
bool do_similar(int argc, char *argv[]);
static bool parse_cstring(char *cstring);
static void compute_cstring(char *cstring);


void init_conjunct(char *cstring) {
    /* Add commands and options */
    add_cmd("cstring", do_cstring,
	    "(L|B|T|P|D)(NN|UL|UR|SL|SR)(A|R|N)(N|Y) | Set options for conjunction");
    add_cmd("similar", do_similar,
	    "f1 f2 ...       | Compute pairwise support similarity for functions");
    add_param("check", &check_results, "Check results of conjunctoperations", NULL);
    add_param("abort", &abort_limit, "Maximum number of pairs to attempt in single conjunction step", NULL);
    add_param("pass", &pass_limit, "Maximum number of passes during single conjunction", NULL);
    add_param("expand", &expansion_factor_scaled, "Maximum expansion of successive BDD sizes (scaled by 100) for each pass", NULL);
    add_param("reduction", &reduction_type, "Reduction degree (2+: tree, 1:dynamic, 0:linear, -1:similarity, -2:pairwise)", NULL);
    add_param("preprocess", &preprocess, "Preprocess conjunction arguments with simplification operation", NULL);
    add_param("presort", &presort, "Sort by descending SAT count before preprocessing", NULL);
    add_param("reprocess", &preprocess, "Reprocess arguments with simplification operation during conjunction", NULL);
    add_param("right", &right_to_left, "Perform simplification starting with rightmost (rather than leftmost) element", NULL);
    add_param("soft", &use_soft, "Use soft and, rather than C/M restriction for simplifying conjunction operands", NULL);
    if (!parse_cstring(cstring)) {
	/* Revert to defaults */
	reduction_type = SIMILARITY_REDUCTION;
	preprocess = 0;
	presort = 0;
	use_soft = 1;
	reprocess = 0;
	right_to_left = 0;
    }
}

/*** Operations on rsets ***/
rset *rset_new() {
    rset *result = malloc_or_fail(sizeof(rset), "rset_new");
    result->head = NULL;
    result->tail = NULL;
    result->length = 0;
    return result;
}

void rset_free(rset *set) {
    rset_ele *ptr = set->head;
    while (ptr) {
	root_deref(ptr->fun);
	if (ptr->support_indices != NULL)
	    free(ptr->support_indices);
	rset_ele *nptr = ptr->next;
	free_block((void *) ptr, sizeof(rset_ele));
	ptr = nptr;
    }
    free_block((void *) set, sizeof(rset));
}

void rset_add_term(rset *set, ref_t fun) {
    root_addref(fun, false);
    rset_ele *ele = malloc_or_fail(sizeof(rset_ele), "rset_add_term");
    ele->next = NULL;
    ele->fun = fun;
    ele->sat_count = -1.0;
    ele->size = 0;
    ele->support_count = -1;
    ele->support_indices = NULL;
    if (set->tail) {
	set->tail->next = ele;
	set->tail = ele;
    } else {
	set->head = set->tail = ele;
    }
    set->length++;
}

void rset_add_term_first(rset *set, ref_t fun) {
    root_addref(fun, false);
    rset_ele *ele = malloc_or_fail(sizeof(rset_ele), "rset_add_term_first");
    ele->next = set->head;
    ele->fun = fun;
    ele->sat_count = -1.0;
    ele->size = 0;
    ele->support_count = -1;
    ele->support_indices = NULL;
    set->head = ele;
    if (!set->tail)
	set->tail = ele;
    set->length++;
}

ref_t rset_remove_first(rset *set) {
    if (set->length == 0)
	return shadow_one(smgr);
    ref_t rval = set->head->fun;
    rset_ele *ptr = set->head;
    set->head = ptr->next;
    if (set->tail == ptr)
	set->tail = set->head;
    free_block((void *) ptr, sizeof(rset_ele));
    set->length--;
    return rval;
}

bool rset_remove_element(rset *set, rset_ele *ele) {
    rset_ele *next = set->head;
    rset_ele *prev = NULL;
    while (next && next != ele) {
	prev = next;
	next = next->next;
    }
    if (!next)
	return false;
    /* next == ele, prev points to predecessor */
    if (prev)
	prev->next = ele->next;
    else
	set->head = ele->next;
    if (set->tail == ele) {
	set->tail = prev;
    }
    free_block((void *) ele, sizeof(rset_ele));
    set->length--;
    return true;
}

static size_t get_size(rset_ele *ptr) {
    if (ptr == NULL)
	return 0;
    if (ptr->size == 0)
	ptr->size = cudd_single_size(smgr, ptr->fun);
    return ptr->size;
}

static double get_sat_count(rset_ele *ptr) {
    if (ptr == NULL)
	return 0.0;
    if (ptr->sat_count <= 0.0)
	ptr->sat_count = cudd_single_count(smgr, ptr->fun);
    return ptr->sat_count;
}

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

/* Comparison function for sorting */
static bool elements_ordered(rset_ele *ptr1, rset_ele *ptr2, bool by_size, bool ascending) {
    /* Fill in fields, if necessary */
    if (by_size) {
	size_t n1 = get_size(ptr1);
	size_t n2 = get_size(ptr2);
	return ascending == (n1 <= n2);
    }
    double d1 = get_sat_count(ptr1);
    double d2 = get_sat_count(ptr2);
    return ascending == (d1 <= d2);
}

static void swap_contents(rset_ele *ptr1, rset_ele *ptr2) {
    ref_t tfun = ptr1->fun;
    double tsat_count = ptr1->sat_count;
    size_t tsize = ptr1->size;
    int tsupport_count = ptr1->support_count;
    int *tsupport_indices = ptr1->support_indices;

    ptr1->fun = ptr2->fun;
    ptr1->sat_count = ptr2->sat_count;
    ptr1->size = ptr2->size;
    ptr1->support_count = ptr2->support_count;
    ptr1->support_indices = ptr2->support_indices;

    ptr2->fun = tfun;
    ptr2->sat_count = tsat_count;
    ptr2->size = tsize;
    ptr2->support_count = tsupport_count;
    ptr2->support_indices = tsupport_indices;

}

/* Recursive step of sorting.  Elements modified */
static void rset_sort_recurse(rset_ele *ptr, bool by_size, bool ascending) {
    if (!ptr || ptr->next == NULL)
	return;
    rset_ele *ftail = ptr;
    rset_ele *rest = ptr->next;
    rset_sort_recurse(rest, by_size, ascending);
    /* Insert head element into list */
    while (rest && !elements_ordered(ftail, rest, by_size, ascending)) {
	swap_contents(ftail, rest);
	ftail = rest;
	rest = rest->next;
    }
}

/* Sort elements of an rset.
   Either by size or by sat count.
   Either ascending or descending */
static void rset_sort(rset *set, bool by_size, bool ascending) {
    rset_sort_recurse(set->head, by_size, ascending);
    report_noreturn(2, "Sorted order:");
    rset_ele *ptr = set->head;
    while (ptr) {
	if (by_size)
	    report_noreturn(2, " %zd", get_size(ptr));
	else
	    report_noreturn(2, " %.0f", get_sat_count(ptr));
	ptr = ptr->next;
    }
    report(2, "");
}

/* Find largest element in set */
static size_t rset_max_size(rset *set) {
    size_t size = 0;
    rset_ele *ptr;
    for (ptr = set->head; ptr; ptr = ptr->next) {
	size_t esize = get_size(ptr);
	if (esize > size)
	    size = esize;
    }
    return size;
}

/* Compute AND of terms without decrementing reference counts */
static ref_t and_check(rset *set) {
    ref_t rval = shadow_one(smgr);
    root_addref(rval, false);
    rset_ele *ptr = set->head;
    while(ptr) {
	ref_t arg = ptr->fun;
	ref_t nval = shadow_and(smgr, rval, arg);
	root_addref(nval, true);
	root_deref(rval);
	rval = nval;
	ptr = ptr->next;
    }
    return rval;
}

/* Print statistics about partial results */
static void report_combination(rset *set, ref_t sofar, conjunction_data *data) {
    size_t result_size = 0;
    size_t max_size = 0;
    size_t sum_size = 0;
    size_t count = set->length;
    if (verblevel < 1)
	return;
    /* Must convert to other form of set */
    set_ptr pset = word_set_new();
    if (!REF_IS_INVALID(sofar)) {
	set_insert(pset, (word_t) sofar);
	result_size = cudd_single_size(smgr, sofar);
	max_size = result_size;
	sum_size += result_size;
	count++;
    }
    size_t set_size = count;
    rset_ele *ptr = set->head;
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

/* Simplify function fun, based on functions at list element and beyond */
static ref_t simplify_downstream(ref_t fun, rset_ele *ptr) {
    ref_t rval = fun;
    if (REF_IS_INVALID(rval))
	return rval;
    root_addref(rval, false);
    while (ptr) {
	ref_t arg = ptr->fun;
	if (REF_IS_INVALID(arg)) {
	    root_deref(rval);
	    return arg;
	}
	ref_t nval = use_soft ? shadow_soft_and(smgr, rval, arg) : shadow_cm_restrict(smgr, rval, arg);
	if (REF_IS_INVALID(nval)) {
	    root_deref(rval);
	    return nval;
	}
	root_addref(nval, true);
	root_deref(rval);
	rval = nval;
	ptr = ptr->next;
    }
    return rval;
}

static void simplify_recurse(rset_ele *ptr, bool preorder) {
    if (!ptr)
	return;
    rset_ele *rest = ptr->next;
    if (preorder) {
	ref_t fun = ptr->fun;
	ref_t nval = simplify_downstream(fun, rest);
	root_deref(fun);
	ptr->fun = nval;
	ptr->size = 0;
	ptr->sat_count = -1.0;
    }
    simplify_recurse(rest, preorder);
    if (!preorder) {
	ref_t fun = ptr->fun;
	ref_t nval = simplify_downstream(fun, rest);
	root_deref(fun);
	ptr->fun = nval;
	ptr->size = 0;
	ptr->sat_count = -1.0;
    }
}

/* Simplify all functions in rset */
static void simplify_rset(rset *set) {
    if (verblevel >= 2) {
	report_noreturn(2, "Before simplification:");
	rset_ele *ptr = set->head;
	while (ptr) {
	    report_noreturn(2, " %zd", get_size(ptr));
	    ptr = ptr->next;
	}
	report(2, "");
    }

    simplify_recurse(set->head, !right_to_left);

    if (verblevel >= 2) {
	report_noreturn(2, "After simplification:");
	rset_ele *ptr = set->head;
	while (ptr) {
	    report_noreturn(2, " %zd", get_size(ptr));
	    ptr = ptr->next;
	}
	report(2, "");
    }

}


ref_t simplify_with_rset(ref_t fun, rset *set) {
    return simplify_downstream(fun, set->head);
}

static ref_t linear_combine(rset *set, conjunction_data *data) {
    ref_t rval = shadow_one(smgr);
    root_addref(rval, false);
    while (set->length > 0) {
	ref_t rarg = rset_remove_first(set);
	ref_t nval = shadow_and(smgr, rval, rarg);
	root_addref(nval, true);
	root_deref(rarg);
	root_deref(rval);
	if (reprocess) {
	    ref_t sval = simplify_downstream(nval, set->head);
	    root_deref(nval);
	    nval = sval;
	}
	rval = nval;
	report_combination(set, rval, data);
    }
    return rval;
}

static ref_t sorted_combine(rset *set, conjunction_data *data) {
    ref_t rval;
    if (set->length == 0) {
	rval = shadow_one(smgr);
	root_addref(rval, false);
	return rval;
    }
    while (set->length > 1) {
	rset_sort(set, true, true);
	ref_t arg1 = rset_remove_first(set);
	ref_t arg2 = rset_remove_first(set);
	ref_t nval = shadow_and(smgr, arg1, arg2);
	root_addref(nval, true);
	root_deref(arg1);
	root_deref(arg2);
	report_combination(set, nval, data);
	rset_add_term_first(set, nval);
	root_deref(nval);
    }
    rval = rset_remove_first(set);
    return rval;
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


static ref_t similarity_combine(rset *set, conjunction_data *data) {
    double expansion_factor = (double) expansion_factor_scaled / 100.0;
    pair candidates[abort_limit];
    size_t max_argument_size = 0;
    size_t abort_count = 0;
    size_t argument_count = set->length;
    size_t max_size_limit = 0;
    if (set->length == 0) {
	ref_t rval = shadow_one(smgr);
	root_addref(rval, false);
	return rval;
    }
    while (set->length > 1) {
	clear_candidates(candidates);
	rset_ele *ptr1 = NULL;
	rset_ele *ptr2 = NULL;
	int ccount = 0;
	for (ptr1 = set->head; ptr1; ptr1 = ptr1->next) {
	    for (ptr2 = ptr1->next; ptr2; ptr2 = ptr2->next) {
		int support_count1 = get_support_count(ptr1);
		int *indices1 = get_support_indices(ptr1);
		int support_count2 = get_support_count(ptr2);
		int *indices2 = get_support_indices(ptr2);
		double sim = index_similarity(support_count1, indices1, support_count2, indices2);
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
	size_t size_limit = rset_max_size(set);
	int try_limit = ccount * pass_limit + 1;
	int try;
	for (try = 0; try <= try_limit; try++) {
	    bool final_try = try == try_limit;
	    int tidx = try % ccount;
	    if (tidx == 0) {
		/* Start of a new pass */
		/* Increase size limit for this pass */
		size_limit = (size_t) (size_limit * expansion_factor);
		report(5, "Setting size limit to %zd", size_limit);
	    }
	    ptr1 = candidates[tidx].ptr1;
	    ptr2 = candidates[tidx].ptr2;
	    sim = candidates[tidx].sim;
	    arg1 = ptr1->fun;
	    size_t asize = get_size(ptr1);
	    max_argument_size = SMAX(max_argument_size, asize);
	    arg2 = ptr2->fun;
	    asize = get_size(ptr2);
	    max_argument_size = SMAX(max_argument_size, asize);

	    max_size_limit = SMAX(max_size_limit, size_limit);
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
	if (!rset_remove_element(set, ptr1)) {
	    if (!ptr1)
		err(true, "Internal error.  rset ptr1 = NULL");
	    char arg_buf[24];
	    shadow_show(smgr, ptr1->fun, arg_buf);
	    err(true, "Internal error.  Could not remove element containing reference %s from rset", arg_buf);
	}
	if (!rset_remove_element(set, ptr2)) {
	    if (!ptr2)
		err(true, "Internal error.  rset ptr2 = NULL");
	    char arg_buf[24];
	    shadow_show(smgr, ptr2->fun, arg_buf);
	    err(true, "Internal error.  Could not remove element containing reference %s from rset", arg_buf);
	}
	root_addref(nval, true);
	if (verblevel >= 3) {
	    char arg1_buf[24], arg2_buf[24], nval_buf[24];
	    shadow_show(smgr, arg1, arg1_buf);
	    shadow_show(smgr, arg2, arg2_buf);
	    shadow_show(smgr, nval, nval_buf);
	    report(3, "%s & %s (sim = %.3f, try #%d) --> %s", arg1_buf, arg2_buf, sim, try+1, nval_buf);
	}
	root_deref(arg1);
	root_deref(arg2);
	report_combination(set, nval, data);
	rset_add_term_first(set, nval);
	root_deref(nval);

	if (reprocess)
	    simplify_rset(set);


    }
    report(1, "Conjunction of %zd elements.  %zd aborts.  Max argument %zd.  Max size limit %zd",
	   argument_count, abort_count, max_argument_size, max_size_limit);

    return rset_remove_first(set);
}


static ref_t pairwise_combine(rset *set, conjunction_data *data) {
    ref_t rval;
    if (set->length == 0) {
	rval = shadow_one(smgr);
	root_addref(rval, false);
	return rval;
    }
    report_combination(set, REF_INVALID, data);
    while (set->length > 1) {
	rset_ele *ptr = set->head;
	rset_ele *best_ele = NULL;
	size_t best_score = 0;
	bool first = true;
	/* Find adjacent elements that minimize product of sizes */
	while (ptr->next) {
	    rset_ele *next = ptr->next;
	    size_t score = get_size(ptr) * get_size(next);
	    if (first || score < best_score) {
		first = false;
		best_score = score;
		best_ele = ptr;
	    }
	    ptr = next;
	}
	rset_ele *bnext = best_ele->next;
	/* Combine two elements */
	ref_t nval = shadow_and(smgr, best_ele->fun, bnext->fun);
	root_addref(nval, true);
	root_deref(best_ele->fun);
	root_deref(bnext->fun);
	// Hold spot in list
	best_ele->fun = REF_INVALID;
	best_ele->next = bnext->next;
	if (set->tail == bnext)
	    set->tail = best_ele;
	free_block(bnext, sizeof(rset_ele));
	set->length--;
	report_combination(set, nval, data);
	best_ele->fun = nval;
	if (reprocess)
	    simplify_rset(set);
    }
    rval = rset_remove_first(set);
    return rval;
}

/* Recursive helper function for tree combination */
static ref_t tree_combiner(rset *set, size_t degree, size_t count, conjunction_data *data) {
    ref_t rval = shadow_one(smgr);
    root_addref(rval, false);
    if (count == 0 || set->length == 0)
	return rval;
    if (count == 1) {
	return rset_remove_first(set);
    }
    size_t sub_count = (count + degree-1)/degree;
    int d;
    rval = tree_combiner(set, degree, sub_count, data);
    for (d = 1; d < degree; d++) {
	ref_t subval = tree_combiner(set, degree, sub_count, data);
	if (subval == shadow_one(smgr))
	    continue;
	ref_t nval = shadow_and(smgr, rval, subval);
	root_addref(nval, true);
	root_deref(rval);
	root_deref(subval);
	if (reprocess) {
	    ref_t sval = simplify_downstream(nval, set->head);
	    root_deref(nval);
	    nval = sval;
	}
	rval = nval;
	report_combination(set, rval, data);
    }
    return rval;
}

ref_t tree_combine(rset *set, size_t degree, conjunction_data *data) {
    report_combination(set, REF_INVALID, data);
    return tree_combiner(set, degree, set->length, data);
}

/* Compute conjunction of set (destructive) */
ref_t rset_conjunct(rset *set) {
    char cstring[6];
    conjunction_data cdata;
    cdata.max_size = 0;
    cdata.total_size = 0;
    cdata.sum_size = 0;
    ref_t rprod = check_results ? and_check(set) : REF_INVALID;

    if (presort)
	rset_sort(set, false, false);

    if (preprocess)
	simplify_rset(set);

    ref_t rval = shadow_one(smgr);

    if (reduction_type > 1)
	rval = tree_combine(set, reduction_type, &cdata);
    else if (reduction_type == DYNAMIC_REDUCTION) {
	if (reprocess && !use_soft) {
	    report(0, "WARNING: Cannot reprocess arguments with C/M restrict when using dynamic reduction.  Reprocessing disabled");
	    reprocess = 0;
	}
	rval = sorted_combine(set, &cdata);
    } else if (reduction_type == SIMILARITY_REDUCTION) {
	if (reprocess && !use_soft) {
	    report(0, "WARNING: Cannot reprocess arguments with C/M restrict when using similarity reduction.  Reprocessing disabled");
	    reprocess = 0;
	}
	rval = similarity_combine(set, &cdata);
    } else if (reduction_type == PAIRWISE_REDUCTION)
	rval = pairwise_combine(set, &cdata);
    else
	rval = linear_combine(set, &cdata);

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
    compute_cstring(cstring);
    report(0, "Conjunction %s Result %zd Max_BDD %zd Max_combined %zd Max_sum %zd",
	   cstring, rsize, cdata.max_size, cdata.total_size, cdata.sum_size);

    return rval;
}

bool do_cstring(int argc, char *argv[]) {
    if (argc != 2) {
	report(0, "Must provide single argument to cstring command");
	return false;
    }
    return parse_cstring(argv[1]);
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



static bool parse_cstring(char *cstring) {
    if (strlen(cstring) != 5) {
	report(0, "cstring must be of length 4, of form '(L|B|T|P|D|S)(NO|UL|UR|SL|SR)(A|R|N)(N|Y)'");
	return false;
    }
    switch(cstring[0]) {
    case 'T':
	reduction_type = TERNARY_REDUCTION;
	break;
    case 'B':
	reduction_type = BINARY_REDUCTION;
	break;
    case 'L':
	reduction_type = LINEAR_REDUCTION;
	break;
    case 'D':
	reduction_type = DYNAMIC_REDUCTION;
	break;
    case 'P':
	reduction_type = PAIRWISE_REDUCTION;
	break;
    case 'S':
	reduction_type = SIMILARITY_REDUCTION;
	break;
    default:
	report(0, "Must have reduction type T, B, L, D, P, or S");
	return false;
    }
    switch(cstring[1]) {
    case 'N':
	preprocess = 0;
	presort = 0;
	break;
    case 'U':
	preprocess = 1;
	presort = 0;
	break;
    case 'S':
	preprocess = 1;
	presort = 1;
	break;
    default:
	report(0, "cstring must be of form '(L|B|T|P|D)(NN|UL|UR|SL|SR)(A|R|N)(N|Y)'");
	return false;
    }
    if (preprocess) {
	switch(cstring[2]) {
	case 'L':
	    right_to_left = 0;
	    break;
	case 'R':
	    right_to_left = 1;
	    break;
	default:
	    report(0, "Must preprocess in either left-to-right (L) or right-to-left (R) mode");
	    return false;
	}
	switch(cstring[3]) {
	case 'A':
	    use_soft = 1;
	    break;
	case 'R':
	    use_soft = 0;
	    break;
	default:
	    report(0, "Preprocessing requires either soft and (A) or C/M restrict (R)");
	    return false;
	}
    } else {
	if (cstring[2] != 'N' || cstring[3] != 'N' || cstring[4] != 'N') {
	    report(0, "Without preprocessing, cstring must be of form (L|B|T|P|D)NNNN");
	    return false;
	}
    }
    switch (cstring[4]) {
    case 'N':
	reprocess = 0;
	break;
    case 'Y':
	reprocess = 0;
	if (reduction_type == DYNAMIC_REDUCTION && !use_soft)
	    report(0, "Cannot do reprocessing via C/M restrict with dynamic reduction.  Reprocessing disabled");
	else if (reduction_type == SIMILARITY_REDUCTION && !use_soft)
	    report(0, "Cannot do reprocessing via C/M restrict with similarity reduction.  Reprocessing disabled");
	else if (!preprocess)
	    report(0, "Can only reprocess if first preprocess.  Reprocessing disabled");
	else
	    reprocess = 1;
	break;
    default:
	report(0, "cstring must be of form '(L|B|T|P|D)(NO|UL|UR|SL|SR)(A|R|N)(N|Y)'");
	return false;
    }
    return true;
}

static void compute_cstring(char *cstring) {
    switch (reduction_type) {
    case TERNARY_REDUCTION:
	cstring[0] = 'T';
	break;
    case BINARY_REDUCTION:
	cstring[0] = 'B';
	break;
    case LINEAR_REDUCTION:
	cstring[0] = 'L';
	break;
    case DYNAMIC_REDUCTION:
	cstring[0] = 'D';
	break;
    case SIMILARITY_REDUCTION:
	cstring[0] = 'S';
	break;
    case PAIRWISE_REDUCTION:
	cstring[0] = 'P';
	break;
    default:
	cstring[0] = '?';
    }
    if (preprocess) {
	cstring[1] = presort ? 'S' : 'U';
	cstring[2] = right_to_left ? 'R' : 'L';
	cstring[3] = use_soft ? 'A' : 'R';
    } else {
	cstring[1] = 'N';
	cstring[2] = 'N';
	cstring[3] = 'N';
    }
    cstring[4] = reprocess ? 'Y' : 'N';
    cstring[5] = '\0';
}
