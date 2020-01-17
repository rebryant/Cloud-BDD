/* Implemention of conjunction engine */

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

/* How many hex digits should there be in randomly generated file names */
#define NAME_DIGITS  8

/* Parameters */

/* Should I check results of conjunct (and possibly other) operations? */
int check_results = 0;

/* Should arguments to conjunction be preprocessed with soft and operation */
int preprocess = 1;

/* Should arguments be reprocessed with Coudert/Madre restrict or soft and operation as conjunction proceeds? */
int reprocess = 1;

/* Maximum number of pairs to try when doing conjunction with aborts */
/* Was 7 */
int abort_limit = 5;
/* Maximum expansion factor for conjunction (scaled 100x) */

int expansion_factor_scaled = 200;
/* Number of passes of conjunction before giving up */
int pass_limit = 3;

/* Lower bound on support coverage metric required to attempt soft and
   (Scaled by 100).
   Distinguish between use in initial preprocessing step, vs. during conjunction operations 
 */

int preprocess_soft_and_threshold_scaled = 80;
int inprocess_soft_and_threshold_scaled = 80;

/* Upper bound on size of other function for soft and */
/* Piecewise linear function determined by size of argument */
#define SRR_PIECES 3
size_t srr_threshold[SRR_PIECES] = { 0, 100000, 1000000 };
double srr_ratio[SRR_PIECES] =     { 2.0, 1.5, 0.75 };

/* Attempt preprocessing with soft and */
int preprocess_conjuncts = 0;

/* Allow growth during soft and? */
int soft_and_allow_growth = 0;

/* How many nodes are allowed when computing soft and.  Set to ratio with current size. (Scaled by 100) */
int soft_and_expansion_ratio_scaled = 200;

/* How many cache looks are allowed when computing (soft) and.  Set to ratio of sum of argument sizes */
int cache_lookup_ratio = 200;

/* Maximum amount by which support coverage and similarities can be discounted for large arguments */
/* (Scaled by 100) */
double max_large_argument_penalty_scaled = 40;

/* Set to force more load/stores & more GCs */
// #define STRESS

#ifdef STRESS
size_t memory_store_threshold = 1;
size_t stored_gc_limit = 0;
#else /* !STRESS */
/* What is the maximum number nodes to keep a conjunct term stored in memory */
size_t memory_store_threshold = 1000;
/* What is the maximum number of nodes stored to trigger GC */
size_t stored_gc_limit = 10000;
#endif /* STRESS */

/* How many more nodes should be stored in order to trigger GC? */

/* Should old conjunct files be kept */
bool keep_conjunct_files = false;

/* Size of the smallest and largest BDDs in the conjunction */
size_t max_argument_size = 0;
size_t min_argument_size = 0;

/* Log of the smallest and largest BDDs in the conjunction */
/* These get updated as conjuncts are removed and added */
double log10_min_size = 4.0;
double log10_max_size = 8.0;

/* Performance counters */
size_t total_soft_and = 0;
size_t total_soft_and_success = 0;
size_t total_soft_and_node_fail = 0;
size_t total_soft_and_lookup_fail = 0;
size_t total_and_limit = 0;
size_t total_and = 0;
size_t total_skip = 0;
size_t total_replace = 0;
size_t total_noreplace = 0;
size_t total_loads = 0;
size_t total_loaded_nodes = 0;
size_t total_stores = 0;
size_t total_stored_nodes = 0;

/* Tracking activity to trigger GC */
size_t total_stored_nodes_last_gc = 0;


/* Representation of set of terms to be conjuncted */
/* Linked list elements */
/* Also use as standalone representation of function + metadata */
/* Possible status:
   fun INVALID, in_file = false: function is INVALID
   fun VALID, in_file = false: function only in memory
   fun INVALID, in_file = true: function only on disk
   fun VALID, in_file = true: Not possible
*/
typedef struct RELE {
    ref_t fun;
    size_t size;
    long support_count;
    int *support_indices;
    bool in_file;
    char *file_name;
    struct RELE *next;
} rset_ele;

/* Data collected during conjunction */
typedef struct {
    size_t result_size;  // Result of last And
    size_t max_size;     // Max size of all terms
    size_t sum_size;     // Sum of sizes of all terms
    size_t resident_size; // Number of nodes of terms resident in memory
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
    add_param("soft", &inprocess_soft_and_threshold_scaled, "Threshold for attempting soft-and simplification (0-100)", NULL);
    add_param("grow", &soft_and_allow_growth, "Allow growth from soft-and simplification", NULL);
    add_param("preprocess", &preprocess_conjuncts, "Attempt to simplify conjuncts with soft and", NULL);
    add_param("lookup", &cache_lookup_ratio, "Max cache lookups during and/soft-and (ratio to arg sizes)", NULL);
    add_param("generate", &soft_and_expansion_ratio_scaled, "Limit on nodes generated during soft and", NULL);
    preprocess = 0;
    reprocess = 0;
}

/*** Operations on rsets ***/
static void rset_ele_new_fun(rset_ele *ele, ref_t fun) {
    /* Get rid of stuff associated with existing function */
    /* See if there's a DD stored in a file */
    if (ele->in_file && !keep_conjunct_files) {
	bool done = remove(ele->file_name) == 0;
	if (done)
	    report(4, "DD file '%s' removed", ele->file_name);
	else
	    report(3, "Attempt to delete DD file '%s' failed", ele->file_name);
    }
    ele->in_file = false;
    if (ele->support_indices != NULL)
	free(ele->support_indices);
    if (!REF_IS_INVALID(ele->fun))
	root_deref(ele->fun);
    /* Assign new function */
    ele->fun = fun;
    if (REF_IS_INVALID(fun)) {
	ele->size = 0;
	ele->support_count = 0;
	ele->support_indices = NULL;
    } else {
	ele->size = cudd_single_size(smgr, ele->fun);
	ele->support_count = shadow_support_indices(smgr, ele->fun, &ele->support_indices);
	root_addref(fun, false);
    }
}

static char *generate_name() {
    static char hexbuf[NAME_DIGITS+1];
    static int instance;
    static bool initialized = false;
    if (!initialized) {
	random_hex(hexbuf, NAME_DIGITS);	
	hexbuf[NAME_DIGITS] = '\0';
	instance = 1;
	initialized = true;
    }
    int name_length = strlen("dd-") + NAME_DIGITS + strlen("-XXXXXX.bdd") + 1;
    char buf[name_length];
    sprintf(buf, "dd-%s-%.6d.bdd", hexbuf, instance);
    instance++;
    report(5, "Generated file name '%s'", buf);
    return strsave_or_fail(buf, "generate_name");
}

static rset_ele *rset_new(ref_t fun) {
    rset_ele *ele = malloc_or_fail(sizeof(rset_ele), "rset_new");
    ele->in_file = false;
    ele->file_name = generate_name();
    ele->fun = REF_INVALID;
    ele->size = 0;
    ele->support_count = 0;
    ele->support_indices = NULL;
    rset_ele_new_fun(ele, fun);
    ele->next = NULL;
    return ele;
}

/* Free single element */
static void rset_ele_free(rset_ele *set) {
    rset_ele *ptr = set;
    rset_ele_new_fun(ptr, REF_INVALID);
    free_string(ptr->file_name);
    free_block((void *) ptr, sizeof(rset_ele));
}

/* Free entire set */
static void rset_free(rset_ele *set) {
    rset_ele *ptr = set;
    while (ptr) {
	rset_ele *next = ptr->next;
	ptr->next = NULL;
	rset_ele_free(ptr);
	ptr = next;
    }
}

static rset_ele *rset_add_element(rset_ele *set, rset_ele *ele) {
    ele->next = set;
    return ele;
}

static rset_ele *rset_remove_element(rset_ele *set, rset_ele *ele) {
    rset_ele *next = set;
    rset_ele *prev = NULL;
    rset_ele *nset = set;
    while (next && next != ele) {
	prev = next;
	next = next->next;
    }
    if (!next) {
	err(false, "Internal error.  Did not find element in set");
	return set;
    }
    /* next == ele, prev points to predecessor */
    if (prev) {
	prev->next = ele->next;
    } else {
	nset = ele->next;
    }
    /* Unlink */
    ele->next = NULL;
    return nset;
}

static int rset_length(rset_ele *set) {
    int len = 0;
    rset_ele *ptr;
    for (ptr = set; ptr; ptr = ptr->next)
	len++;
    return len;
}

static bool rset_is_empty(rset_ele *set) {
    return set == NULL;
}

static bool rset_is_singleton(rset_ele *set) {
    return !rset_is_empty(set) && rset_is_empty(set->next);
}

static bool rset_contains_zero(rset_ele *set) {
    ref_t rzero = shadow_zero(smgr);
    rset_ele *ptr;
    for (ptr = set; ptr; ptr = ptr->next) {
	if (ptr->fun == rzero)
	    return true;
    }
    return false;
}


/*** Operations on rset elements ***/

static size_t get_size(rset_ele *ptr) {
    if (ptr == NULL)
	return 0;
    return ptr->size;
}

static int get_support_count(rset_ele *ptr) {
    if (ptr == NULL)
	return 0;
    return ptr->support_count;
}

static int *get_support_indices(rset_ele *ptr) {
    if (ptr == NULL)
	return 0;
    return ptr->support_indices;
}

static ref_t get_function(rset_ele *ptr) {
    if (REF_IS_INVALID(ptr->fun) && ptr->in_file) {
	FILE *infile = fopen(ptr->file_name, "r");
	if (infile == NULL) {
	    err(true, "Failed to open DD file '%s' to read", ptr->file_name);
	} else {
	    ptr->fun = shadow_load(smgr, infile);
	    if (REF_IS_INVALID(ptr->fun))
		err(true, "Failed to load DD from file '%s'", ptr->file_name);
	    root_addref(ptr->fun, true);
	    report(4, "Retrieved DD of size %zd from file '%s'", get_size(ptr), ptr->file_name);
	    total_loads++;
	    total_loaded_nodes += get_size(ptr);
	    fclose(infile);
	    bool done = remove(ptr->file_name) == 0;
	    if (done)
		report(4, "DD file '%s' removed", ptr->file_name);
	    else
		report(3, "Attempt to delete DD file '%s' failed", ptr->file_name);
	}
    } else
	report(5, "Retrieved DD for %s from memory", ptr->file_name);
    return ptr->fun;
}

static void release_function(rset_ele *ptr) {
    if (ptr->in_file) {
	report(5, "DD of size %zd already stored in file '%s'", get_size(ptr), ptr->file_name);
    } else if (REF_IS_INVALID(ptr->fun) || get_size(ptr) <= memory_store_threshold) {
	/* Either nothing to save, or it's already been saved */
	report(4, "Didn't store DD of size %zd to file '%s'", get_size(ptr), ptr->file_name);
    } else {
	/* Attempt to save file.  If successful, flush in-memory copy */
	FILE *outfile = fopen(ptr->file_name, "w");
	if (outfile == NULL) {
	    err(false, "Couldn't open DD file '%s' to write", ptr->file_name);
	} else {
	    if (shadow_store(smgr, ptr->fun, outfile)) {
		ptr->in_file = true;
		root_deref(ptr->fun);
		ptr->fun = REF_INVALID;
		fclose(outfile);
		report(4, "Flushed in-memory copy and stored DD of size %zd to file '%s'", get_size(ptr), ptr->file_name);
		total_stores++;
		total_stored_nodes += get_size(ptr);
	    } else {
		err(false, "Failed to store DD of size %zd to file '%'", get_size(ptr), ptr->file_name);
	    }
	}
    }
}

/*** Operations on pairs of rsets ***/

/* Assign weight between 1.0-max_large_argument_penalty and 1.0 according to argument sizes */
static double size_weight(rset_ele *ptr1, rset_ele *ptr2) {
    size_t size1 = get_size(ptr1);
    size_t size2 = get_size(ptr2);
    size_t size = SMAX(size1, size2);
    size = SMAX(size, 1);
    double lsize = log10((double) size);
    double penalty = 0.0;
    double max_penalty = 0.01 * max_large_argument_penalty_scaled;
    if (lsize <= log10_min_size)
	penalty = max_penalty;
    else if (lsize <= log10_max_size) {
	penalty = max_penalty * (double) (lsize - log10_min_size) / (log10_max_size - log10_min_size);
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
    if (rset_is_empty(set))
	return 0;
    size_t size = get_size(set);
    rset_ele *ptr;
    for (ptr = set->next; ptr; ptr = ptr->next) {
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

/* Compute AND of terms without decrementing reference counts of arguments */
/* Used to check results of conjunction operation */
static ref_t and_check(rset_ele *set) {
    ref_t rval = shadow_one(smgr);
    root_addref(rval, false);
    rset_ele *ptr = set;
    while(ptr) {
	ref_t arg = get_function(ptr);
	root_checkref(rval);
	root_checkref(arg);
	ref_t nval = shadow_and(smgr, rval, arg);
	total_and++;
	root_addref(nval, true);
	root_deref(rval);
	rval = nval;
	ptr = ptr->next;
    }
    return rval;
}

/* Print statistics about partial results */
static void report_combination(rset_ele *set, conjunction_data *data) {
    size_t result_size = data ? data->result_size : 0;
    size_t max_size = 0;
    size_t sum_size = 0;
    size_t resident_size = 0;
    size_t set_size = rset_length(set);
    double elapsed = elapsed_time();

    rset_ele *ptr;
    for (ptr = set; ptr; ptr = ptr->next) {
	size_t arg_size = get_size(ptr);
	max_size = SMAX(max_size, arg_size);
	sum_size += arg_size;
	if (!ptr->in_file)
	    resident_size += arg_size;
    }
    if (data) {
	data->result_size = SMAX(data->result_size, result_size);
	data->max_size = SMAX(data->max_size, max_size);
	data->sum_size = SMAX(data->sum_size, sum_size);
	data->resident_size = SMAX(data->resident_size, resident_size);
    }
    report(1, "Elapsed time %.1f.  Partial result with %zd values.  Max size = %zd.  Sum size = %zd.  Resident size = %zd.  Computed size = %zd",
	   elapsed, set_size, max_size, sum_size, resident_size, result_size);
}    

/* Perform GC if enough activity has occurred */
static void check_gc() {
    if (total_stored_nodes > total_stored_nodes_last_gc + stored_gc_limit) {
	size_t collected = cudd_collect(smgr);
	report(3, "CUDD reports %zd nodes collected", collected);
	total_stored_nodes_last_gc = total_stored_nodes;
    }
}

static size_t other_size_limit(size_t size) {
    int t;
    for (t = 0; t < SRR_PIECES; t++) {
	if (size > srr_threshold[t]) {
	    return srr_ratio[t] * size;
	}
    }
    err(false, "Failed to find other size limit for argument size %zd", size);
    return size;
}

/* Conditionally simplify elements of one set with those of another */
static void soft_simplify(rset_ele *set, rset_ele *other_set, double threshold, char *docstring) {
    rset_ele *myptr, *otherptr;
    for (myptr = set; myptr; myptr = myptr->next) {
	ref_t myrval = get_function(myptr);
	size_t start_size = get_size(myptr);
	int try_count = 0;
	int sa_count = 0;
	double elapsed = 0.0;
	double delta = 0.0;
	if (REF_IS_INVALID(myrval))
	    continue;
	root_addref(myrval, false);
	for (otherptr = other_set; otherptr; otherptr = otherptr->next) {
	    try_count++;
	    ref_t otherrval = get_function(otherptr);
	    if (REF_IS_INVALID(otherrval))
		continue;
	    root_addref(otherrval, false);
	    /* Attempt to simplify myrval using otherrval */
	    double cov = get_support_coverage(otherptr, myptr, false);
	    size_t current_size = get_size(myptr);
	    size_t other_size = get_size(otherptr);
	    size_t other_limit = other_size_limit(current_size);
	    if (cov > threshold && other_size <= other_limit) {
		double ratio = 0.01 * soft_and_expansion_ratio_scaled;
		unsigned node_limit = (unsigned) (current_size * ratio);
		size_t lookup_limit = current_size * cache_lookup_ratio;
		root_checkref(myrval);
		root_checkref(otherrval);
		shadow_delta_cache_lookups(smgr);
		double start = elapsed_time();
		size_t newNodeCount;

		ref_t nval = shadow_soft_and(smgr, myrval, otherrval, node_limit, lookup_limit, &newNodeCount);

		size_t lookups = shadow_delta_cache_lookups(smgr);
		elapsed = elapsed_time();
		delta = elapsed - start;
		total_soft_and++;
		if (REF_IS_INVALID(nval)) {
		    if (lookups >= lookup_limit) {
			total_soft_and_lookup_fail++;
			report(3, "Elapsed time %.1f.  Delta %.1f.  Soft_And.  %s.  cov = %.3f.  size = %zd.  Other size = %zd.  Lookups = %zd.  New nodes = %zd.  Too many cache lookups",
			       elapsed, delta, docstring, cov, current_size, other_size, newNodeCount, lookups);
		    } else {
			total_soft_and_node_fail++;
			report(3, "Elapsed time %.1f.  Delta %.1f.  Soft_And.  %s.  cov = %.3f.  size = %zd.  Other size = %zd.  Lookups = %zd.  New nodes = %zd.  Requires more than %u nodes",
			   elapsed, delta, docstring, cov, current_size, other_size, newNodeCount, lookups, node_limit);
		    }
		    root_deref(otherrval);
		    release_function(otherptr);
		    continue;
		}
		total_soft_and_success++;
		root_addref(nval, true);
		sa_count++;
		size_t new_size = cudd_single_size(smgr, nval);
		double reduction = (double) current_size/new_size;
		report(3, "Elapsed time %.1f.  Delta %.1f.  Soft_And.  %s.  cov = %.3f.  size = %zd.  Other size = %zd.  Lookups = %zd.  New nodes = %zd.  Size --> %zd (%.3fX)",
		       elapsed, delta, docstring, cov, current_size, other_size, lookups, newNodeCount, new_size, reduction);
		if (new_size < current_size || soft_and_allow_growth) {
#if RPT >= 3
		    {
			char obuf[24], nbuf[24];
			shadow_show(smgr, myrval, obuf);
			shadow_show(smgr, nval, nbuf);
			report(4, "Replacing %s with %s", obuf, nbuf);
		    }
#endif
		    root_deref(myrval);
		    rset_ele_new_fun(myptr, nval);
		    myrval = nval;
		    total_replace++;
		} else {
		    root_deref(nval);
		    total_noreplace++;
		}
		if (verblevel >= 5)
		    shadow_status(smgr);
	    } else {
		total_skip++;
		elapsed = elapsed_time();
		report(3, "Elapsed time %.1f.  Delta 0.0.  Soft_And.  %s.  cov = %.3f.  size = %zd.  Other size = %zd.  Skipping",
		       elapsed, docstring, cov, current_size, other_size);
	    }
	    root_deref(otherrval);
	    release_function(otherptr);
	    check_gc();
	}
	root_deref(myrval);
	release_function(myptr);
	/* Consider doing garbage collection here */
	size_t final_size = get_size(myptr);
	double reduction = (double) start_size / final_size;
	if (sa_count > 0)
	    report(3, "Soft and applied %d/%d times.  %zd --> %zd (%.3fX)", sa_count, try_count, start_size, final_size, reduction);
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
    double expansion_factor = (double) expansion_factor_scaled * 0.01;
    pair candidates[abort_limit];
    size_t abort_count = 0;
    size_t argument_count = rset_length(set);
    size_t max_size_limit = 0;
    double ithreshold = 0.01 * inprocess_soft_and_threshold_scaled;
    if (argument_count == 0) {
	ref_t rval = shadow_one(smgr);
	root_addref(rval, false);
	return rval;
    }
    
    while (!rset_is_singleton(set) && !rset_contains_zero(set)) {
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
	   then do one more try with unbounded.
	   Once get product, continue trying remaining cases to see if can improve
	*/
	size_t size1, size2, lookup_limit;
	rset_ele *best_ptr1 = NULL;
	rset_ele *best_ptr2 = NULL;
	rset_ele *best_nset = NULL;
	size_t best_lookups = 0;
	double best_sim = 0.0;
	int best_try = 0;
	/* Each pass allows a larger limit.  Final pass removes size bound */
	size_t size_limit = max_argument_size;
	max_size_limit = SMAX(max_size_limit, size_limit);
	int try_limit = ccount * pass_limit + 1;
	int try;
	size_t lookups = 0;
	for (try = 0; try <= try_limit; try++) {
	    ref_t arg1 = REF_INVALID;
	    ref_t arg2 = REF_INVALID;
	    ref_t nval = REF_INVALID;
	    double sim = -1.0;

	    bool final_try = try == try_limit;
	    int tidx = try % ccount;
	    if (tidx == 0) {
		/* Start of a new pass */
		if (best_nset != NULL)
		    /* Once have product, don't look for better ones at larger size */
		    break;
		/* Increase size limit for this pass */
		size_limit = (size_t) (size_limit * expansion_factor);
		max_size_limit = SMAX(max_size_limit, size_limit);
		report(5, "Setting size limit to %zd", size_limit);
	    }
	    ptr1 = candidates[tidx].ptr1;
	    ptr2 = candidates[tidx].ptr2;
	    sim = candidates[tidx].sim;
	    arg1 = get_function(ptr1);
	    arg2 = get_function(ptr2);
	    size1 = get_size(ptr1);
	    size2 = get_size(ptr2);
	    lookup_limit = (size1+size2) * cache_lookup_ratio;

	    root_checkref(arg1);
	    root_checkref(arg2);
	    shadow_delta_cache_lookups(smgr);
	    if (final_try) {
		nval = shadow_and(smgr, arg1, arg2);
		total_and++;
	    } else {
		nval = shadow_and_limit(smgr, arg1, arg2, size_limit, lookup_limit);
		total_and_limit++;
	    }
	    lookups = shadow_delta_cache_lookups(smgr);

	    if (REF_IS_INVALID(nval)) {
		abort_count ++;
		if (lookups >= lookup_limit) 
		    report(3, "%s & %s (sim = %.3f, try #%d) requires more than %zd cache lookups", ptr1->file_name, ptr2->file_name, sim, try+1, lookup_limit);
		else
		    report(3, "%s & %s (sim = %.3f, try #%d) requires more than %zd nodes", ptr1->file_name, ptr2->file_name, sim, try+1, size_limit);
	    } else {
		root_addref(nval, true);
		rset_ele *nset = rset_new(nval);
		root_deref(nval);
		report(3, "%s & %s (sim = %.3f, try #%d).  Success with product of size %zd. %zd cache lookups", ptr1->file_name, ptr2->file_name, sim, try+1, get_size(nset), lookups);
		if (best_nset == NULL) {
		    best_nset = nset;
		    best_ptr1 = ptr1;
		    best_ptr2 = ptr2;
		    best_lookups = lookups;
		    best_sim = sim;
		    best_try = try;
		    size_limit = get_size(best_nset);
		} else if (get_size(nset) < get_size(best_nset)) {
		    rset_free(best_nset);
		    best_nset = nset;
		    best_ptr1 = ptr1;
		    best_ptr2 = ptr2;
		    best_lookups = lookups;
		    best_sim = sim;
		    best_try = try;
		    size_limit = get_size(best_nset);
		} else {
		    rset_free(nset);
		}
	    }
	    release_function(ptr1);
	    release_function(ptr2);
	}
	if (best_nset == NULL)
	    err(true, "Couldn't compute conjunction");

	set = rset_remove_element(set, best_ptr1);
	set = rset_remove_element(set, best_ptr2);

	report(3, "%s (%zd nodes) & %s (%zd nodes) (sim = %.3f, try #%d) --> %s (%zd nodes).  %zd cache lookups",
	       best_ptr1->file_name, get_size(best_ptr1), best_ptr2->file_name, get_size(best_ptr2), best_sim,
	       best_try+1, best_nset->file_name, get_size(best_nset), best_lookups);
	rset_ele_free(best_ptr1);
	rset_ele_free(best_ptr2);


	/* Attempt to simplify in both directions */
	int length = rset_length(set);
	report(2, "Apply soft simplify to new argument based on existing %d arguments", length);
	soft_simplify(best_nset, set, ithreshold, "conj_old2new");
	report(2, "Apply soft simplify to existing %d arguments based on new argument", length);
	soft_simplify(set, best_nset, ithreshold, "conj_new2old");
	set = rset_add_element(set, best_nset);
	if (data)
	    data->result_size = get_size(best_nset);
	report_combination(set, data);
	release_function(best_nset);
	check_gc();
    }

    ref_t rval;

    if (rset_is_singleton(set)) {
	report(1, "Conjunction of %zd elements.  %zd aborts.  Max argument %zd.  Max size limit %zd",
	       argument_count, abort_count, max_argument_size, max_size_limit);
	rval = get_function(set);
	root_addref(rval, false);
	rset_free(set);
    } else {
	/* Must contain zero.  Delete remaining arguments */
	double elapsed = elapsed_time();
	report(1, "Elapsed time %.1f.  Conjunction of %zd elements.  Encountered zero-valued conjunct with %zd conjuncts remaining",
	       elapsed, argument_count, rset_length(set));
	rset_free(set);
	rval = shadow_zero(smgr);
    }
    return rval;
}

/* Compute conjunction of set (destructive) */
static ref_t rset_conjunct(rset_ele *set) {
    conjunction_data cdata;
    cdata.max_size = 0;
    cdata.sum_size = 0;
    cdata.resident_size = 0;

    ref_t rprod = REF_INVALID;

    if (check_results)
	rprod = and_check(set);

    ref_t rval = similarity_combine(set, &cdata);

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
    double elapsed = elapsed_time();
    report(0, "Elapsed time %.1f.  Conjunction result %zd Max_BDD %zd Max_sum %zd Max_resident %zd",
	   elapsed, rsize, cdata.max_size, cdata.sum_size, cdata.resident_size);
    return rval;
}

bool do_conjunct(int argc, char *argv[]) {
    if (argc < 2) {
	report(0, "Need destination name");
	return false;
    }

    double pthreshold = 0.01 * preprocess_soft_and_threshold_scaled;

    int i;
    rset_ele *set = NULL;
    /* Track statistics on pre/post simplification sizes */
    size_t itotal = 0;
    size_t imax = 0;

    ref_t rzero = shadow_zero(smgr);
    int zcount = 0;

    for (i = 2; i < argc; i++) {
	ref_t rarg = get_ref(argv[i]);
	if (REF_IS_INVALID(rarg)) {
	    // Fix: Must deference existing list elements before aborting
	    rset_free(set);
	    return rarg;
	}
	if (rarg == rzero) {
	    zcount++;
	    report(2, "Conjunct %s == 0", argv[i]);
	}
	rset_ele *ele = rset_new(rarg);
	size_t asize = get_size(ele);
	itotal += asize;
	imax = SMAX(asize, imax);
	/* This optimization is effective, but very time consuming */
	if (preprocess_conjuncts && i > 2 && zcount == 0) {
	    report(2, "Applying soft and to simplify argument %d using arguments 1-%d", i-1, i-2);
	    soft_simplify(ele, set, pthreshold, "preprocess_old2new");
	    report(2, "Applying soft and to simplify arguments 1-%d using argument %d", i-2, i-1);
	    soft_simplify(set, ele, pthreshold, "preprocess_new2old");
	}
	release_function(ele);
	set = rset_add_element(set, ele);
    }

    if (zcount > 0) {
	rset_free(set);
	assign_ref(argv[1], rzero, false, false);
	report(1, "Conjunction has %d zero-valued conjuncts, and so is trivially zero", zcount);
	return true;
    }

    /* Gather statistics about initial simplification */
    rset_ele *ele;
    size_t ntotal = 0;
    size_t nmax = 0;
    for (ele = set; ele; ele = ele->next) {
	size_t nsize = get_size(ele);
	ntotal += nsize;
	nmax = SMAX(nmax, nsize);
    }
    double tratio = (double) itotal/ntotal;
    double mratio = (double) imax/nmax;
    double elapsed = elapsed_time();
    report(1, "Elapsed time %.1f.  Initial simplification.  Total %zd --> %zd (%.3fX).  Max %zd --> %zd (%.3fX)",
	   elapsed, itotal, ntotal, tratio, imax, nmax, mratio);

    ref_t rval = rset_conjunct(set);
    if (REF_IS_INVALID(rval)) {
	return false;
    }
    assign_ref(argv[1], rval, false, false);
    root_deref(rval);

    report(3, "Total soft ands = %zd.  Succeed = %zd.  Node fail = %zd.  Lookup fail = %zd",
	   total_soft_and, total_soft_and_success, total_soft_and_node_fail, total_soft_and_lookup_fail);
    report(3, "Total replacements = %zd", total_replace);
    report(3, "Total non-replacements = %zd", total_noreplace);
    report(3, "Total skip soft and = %zd", total_skip);
    report(3, "Total limit ands = %zd", total_and_limit);
    report(3, "Total regular ands = %zd", total_and);
    report(3, "Total stores = %zd (%zd nodes)", total_stores, total_stored_nodes);
    report(3, "Total loads = %zd (%zd nodes)", total_loads, total_loaded_nodes);

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
