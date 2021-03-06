/*
  Execute some combination of 3 different BDD evaluations:
    cudd: using the CUDD BDD Package
    local: Locally-evaluated ref-based package
    dist: Distributed ref-based package
    Make sure all executing versions are kept consistent.
    Relation between local & dist requires identical refs.
    This may fail when there are hash collisions in the ref naming

    Possible combinations:
    cudd   local  dist
    F      F      T     Distributed only.  (Should be fastest)
    F      T      F     Local only.  Check functionality and measure performance
    F      T      T     Local & distributed.  Check distributed implementation
    T      F      F     Cudd only.  Performance comparison.
    T      F      T     Cudd & dist.  Not likely.
    T      T      F     Cudd & local.  For debugging ref-based package
    T      T      T     Belt & suspenders!

    Requirements:
    When running local + dist, only works if ref's are identical at all times.
    This will stop working once two distinct refs generate same hash signature.
    When that happens, shift over to a single mode.
*/


typedef enum { CHAIN_NONE, CHAIN_CONSTANT, CHAIN_ALL } chaining_t;

typedef struct {
    DdManager *bdd_manager;
    ref_mgr ref_mgr;
    /* Keep tables cross referencing values in both directions */
    /* c stands for cudd, r stands for ref */
    keyvalue_table_ptr c2r_table;
    keyvalue_table_ptr r2c_table;
    /* Which modes to run */
    bool do_cudd;
    bool do_local;
    bool do_dist;
    /* Total number of variables created */
    size_t nvars;
    /* Total number of ZDD variables created */
    size_t nzvars;
} shadow_ele, *shadow_mgr;

/* Maintaining set of variable indices */
typedef struct {
    int count;
    int *indices;
} index_set;

shadow_mgr new_shadow_mgr(bool do_cudd, bool do_local, bool do_dist, chaining_t chaining);
void free_shadow_mgr(shadow_mgr mgr);

ref_t shadow_one(shadow_mgr mgr);
ref_t shadow_zero(shadow_mgr mgr);
ref_t shadow_new_variable(shadow_mgr mgr);
ref_t shadow_get_variable(shadow_mgr mgr, size_t index);
void shadow_deref(shadow_mgr mgr, ref_t r);
void shadow_show(shadow_mgr mgr, ref_t r, char *buf);

/* Are we generating references? */
bool do_ref(shadow_mgr mgr);

ref_t shadow_negate(shadow_mgr mgr, ref_t r);
ref_t shadow_absval(shadow_mgr mgr, ref_t r);
ref_t shadow_ite(shadow_mgr mgr, ref_t iref, ref_t tref, ref_t eref);
ref_t shadow_cm_restrict(shadow_mgr mgr, ref_t fref, ref_t cref);
ref_t shadow_and(shadow_mgr mgr, ref_t aref, ref_t bref);
ref_t shadow_soft_and(shadow_mgr mgr, ref_t aref, ref_t bref, size_t nodeLimit, size_t lookupLimit);
ref_t shadow_and_limit(shadow_mgr mgr, ref_t aref, ref_t bref, size_t nodeLimit, size_t lookupLimit);
ref_t shadow_or(shadow_mgr mgr, ref_t aref, ref_t bref);
ref_t shadow_xor(shadow_mgr mgr, ref_t aref, ref_t bref);

bool shadow_equal(shadow_mgr mgr, ref_t aref, ref_t bref);

bool shadow_gc_check(shadow_mgr mgr);

/* Convert function to ZDD.  This should only be done after all BDD variables have been declared */
ref_t shadow_zconvert(shadow_mgr mgr, ref_t r);

/* Convert function to ADD */
ref_t shadow_aconvert(shadow_mgr mgr, ref_t r);

/* Print satisfying values for ADD/BDD/ZDD.  Only works for CUDD */
void shadow_satisfy(shadow_mgr mgr, ref_t r);

/* Create key-value table mapping set of root nodes to their densities. */
keyvalue_table_ptr shadow_density(shadow_mgr mgr, set_ptr roots);


/* Compute similarity metric for support sets of two functions */
double shadow_similarity(shadow_mgr mgr, ref_t r1, ref_t r2);

/* Index sets */


/* Wrapper for Cudd_SupportIndices.  Creates new index set */
index_set *shadow_support_indices(shadow_mgr mgr, ref_t r);

/* Free index set */
void index_set_free(index_set *iset);

/* Duplicate index set */
index_set *index_set_duplicate(index_set *iset);

/* Remove indices from index set */
void index_set_remove(index_set *set, index_set *rset);

/* Existential quantification over variables in index set */
ref_t index_equant(shadow_mgr mgr, ref_t r, index_set *iset);

/* Based on indices retrieved by Cudd_SupportIndices() */
double index_similarity(index_set *iset1, index_set *iset2);

/* Computer coverage metric for r1 by r2 */
double shadow_coverage(shadow_mgr mgr, ref_t r1, ref_t r2);
/* Based on indices retrieved by Cudd_SupportIndices() */
double index_coverage(index_set *iset1, index_set *iset2);

/*
  Create key-value table mapping set of root nodes to their counts.
  Results are actual counts
*/
keyvalue_table_ptr shadow_count(shadow_mgr mgr, set_ptr roots);

double cudd_single_count(shadow_mgr mgr, ref_t r);

/* Compute set of variables (given by refs) in support of set of roots */
set_ptr shadow_support(shadow_mgr mgr, set_ptr roots);

/* Use CUDD to compute number of BDD nodes to represent function or set of functions */
size_t cudd_single_size(shadow_mgr mgr, ref_t r);
size_t cudd_set_size(shadow_mgr mgr, set_ptr roots);


/* Have CUDD perform garbage collection */
/* Returns number of nodes collected */
int cudd_collect(shadow_mgr mgr);

size_t shadow_peak_nodes(shadow_mgr mgr);

/* Create key-value table mapping set of root nodes to their restrictions,
   with respect to a set of literals (given as a set of refs)
*/
keyvalue_table_ptr shadow_restrict(shadow_mgr mgr, set_ptr roots, set_ptr lits);

/* Create key-value table mapping set of root nodes to their
   existential quantifications with respect to a set of variables
   (given as a set of refs)
*/
keyvalue_table_ptr shadow_equant(shadow_mgr mgr, set_ptr roots, set_ptr vars);

/* Create key-value table mapping set of root nodes to their shifted versions
   with respect to a mapping from old variables to new ones 
*/
keyvalue_table_ptr shadow_shift(shadow_mgr mgr, set_ptr roots,
				keyvalue_table_ptr vmap);


/* Garbage collection.
   Find all nodes reachable from roots and keep only those in unique table */
void shadow_collect(shadow_mgr mgr, set_ptr roots);

/* Generate status report from Cudd or ref manager */
void shadow_status(shadow_mgr mgr);

/* Load and store */
ref_t shadow_load(shadow_mgr mgr, FILE *infile);
bool shadow_store(shadow_mgr mgr, ref_t r, FILE *outfile);

/* Count of number of cache lookups since last call */
size_t shadow_delta_cache_lookups(shadow_mgr mgr);
