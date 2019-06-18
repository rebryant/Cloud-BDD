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
   type >= 2 ==> tree reduction.
   type == 1 ==> dynamically sorted reduction
   type == 0 ==> linear reduction
 */
int reduction_type = 0;

/* Should arguments to conjunction be preprocessed with Coudert/Madre restrict operation */
int preprocess = 0;

/* Should arguments be reprocessed with Coudert/Madre restrict operation as conjunction proceeds? */
int reprocess = 0;

/* Should preprocessing be done right-to-left */
int right_to_left = 0;

/* Representation of set of terms to be conjuncted */


/* Linked list elements */
typedef struct RELE {
    ref_t fun;
    double sat_count;
    size_t size;
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
    size_t total_size;
} conjunction_data;

void init_conjunct() {

    /* Add commands and options */
    add_param("check", &check_results, "Check results of conjunctoperations", NULL);
    add_param("reduction", &reduction_type, "Reduction degree (2+: tree, 1:dynamic, 0:linear)", NULL);
    add_param("preprocess", &preprocess, "Preprocess conjunction arguments with Coudert/Madre restrict", NULL);
    add_param("reprocess", &preprocess, "Reprocess arguments with Coudert/Madre restrict during conjunction", NULL);
    add_param("right", &right_to_left, "Perform C/M restriction starting with rightmost (rather than leftmost) element", NULL);
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
    rset_ele *ele = malloc_or_fail(sizeof(rset_ele), "rset_add_term");
    ele->next = set->head;
    ele->fun = fun;
    ele->sat_count = -1.0;
    ele->size = 0;
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

/* Comparison function for sorting */
static bool elements_ordered(rset_ele *ptr1, rset_ele *ptr2, bool by_size, bool ascending) {
    /* Fill in fields, if necessary */
    if (by_size) {
	if (ptr1->size == 0)
	    ptr1->size = cudd_single_size(smgr, ptr1->fun);
	if (ptr2->size == 0)
	    ptr2->size = cudd_single_size(smgr, ptr2->fun);
	size_t n1 = ptr1->size;
	size_t n2 = ptr2->size;
	return ascending == (n1 <= n2);
    }
    if (ptr1->sat_count < 0.0)
	ptr1->sat_count = cudd_single_count(smgr, ptr1->fun);
    if (ptr2->sat_count < 0.0)
	ptr2->sat_count = cudd_single_count(smgr, ptr2->fun);
    double d1 = ptr1->sat_count;
    double d2 = ptr2->sat_count;
    return ascending == (d1 <= d2);
}

static void swap_contents(rset_ele *ptr1, rset_ele *ptr2) {
    ref_t tfun = ptr1->fun;
    double tsat_count = ptr1->sat_count;
    size_t tsize = ptr1->size;
    ptr1->fun = ptr2->fun;
    ptr1->sat_count = ptr2->sat_count;
    ptr1->size = ptr2->size;
    ptr2->fun = tfun;
    ptr2->sat_count = tsat_count;
    ptr2->size = tsize;
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
	    report_noreturn(2, " %zd", ptr->size);
	else
	    report_noreturn(2, " %.0f", ptr->sat_count);
	ptr = ptr->next;
    }
    report(2, "");
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
    if (verblevel < 1)
	return;
    /* Must convert to other form of set */
    set_ptr pset = word_set_new();
    set_insert(pset, (word_t) sofar);
    size_t result_size = cudd_single_size(smgr, sofar);
    size_t max_size = result_size;
    rset_ele *ptr = set->head;
    while (ptr) {
	ref_t r = ptr->fun;
	set_insert(pset, (word_t) r);
	size_t nsize = cudd_single_size(smgr, r);
	max_size = SMAX(max_size, nsize);
	ptr = ptr->next;
    }
    size_t total_size = cudd_set_size(smgr, pset);
    report(1, "Partial result with %d values.  Max size = %zd.  Combined size = %zd.  Computed size = %zd",
	   set->length+1, max_size, total_size, result_size);
    set_free(pset);
    if (data) {
	data->total_size = SMAX(data->total_size, total_size);
	data->max_size = SMAX(data->max_size, max_size);
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
	ref_t nval = shadow_cm_restrict(smgr, rval, arg);
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
	root_addref(nval, true);
	root_deref(fun);
	ptr->fun = nval;
	ptr->size = 0;
	ptr->sat_count = -1.0;
    }
    simplify_recurse(rest, preorder);
    if (!preorder) {
	ref_t fun = ptr->fun;
	ref_t nval = simplify_downstream(fun, rest);
	root_addref(nval, true);
	root_deref(fun);
	ptr->fun = nval;
	ptr->size = 0;
	ptr->sat_count = -1.0;
    }
}

/* Simplify all functions in rset */
static void simplify_rset(rset *set, bool right_first) {
    if (verblevel >= 2) {
	report_noreturn(2, "Before simplification:");
	rset_ele *ptr = set->head;
	while (ptr) {
	    if (ptr->size == 0)
		ptr->size = cudd_single_size(smgr, ptr->fun);
	    report_noreturn(2, " %zd", ptr->size);
	    ptr = ptr->next;
	}
	report(2, "");
    }

    simplify_recurse(set->head, !right_first);

    if (verblevel >= 2) {
	report_noreturn(2, "After simplification:");
	rset_ele *ptr = set->head;
	while (ptr) {
	    if (ptr->size == 0)
		ptr->size = cudd_single_size(smgr, ptr->fun);
	    report_noreturn(2, " %zd", ptr->size);
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
	root_deref(rval);
	rval = nval;
	root_deref(rarg);
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
    for (d = 0; d < degree; d++) {
	ref_t subval = tree_combiner(set, degree, sub_count, data);
	ref_t nval = shadow_and(smgr, rval, subval);
	root_addref(nval, true);
	root_deref(rval);
	root_deref(subval);
	report_combination(set, rval, data);
	rval = nval;
    }
    return rval;
}

ref_t tree_combine(rset *set, size_t degree, conjunction_data *data) {
    return tree_combiner(set, degree, set->length, data);
}

/* Compute conjunction of set (destructive) */
ref_t rset_conjunct(rset *set) {
    conjunction_data cdata;
    cdata.max_size = 0;
    cdata.total_size = 0;
    ref_t rprod = check_results ? and_check(set) : REF_INVALID;

    if (preprocess)
	simplify_rset(set, right_to_left);

    ref_t rval = shadow_one(smgr);

    if (reduction_type > 1)
	rval = tree_combine(set, reduction_type, &cdata);
    else if (reduction_type == 1)
	rval = sorted_combine(set, &cdata);
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
    report(0, "Generated conjunction with %zd nodes.  Max BDD size = %zd nodes.  Max combined size = %zd nodes",
	   rsize, cdata.max_size, cdata.total_size);

    return rval;
}



