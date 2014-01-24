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
    F      T      T     Local & distributed.  Make sure distributed implementation is working
    T      F      F     Cudd only.  Performance comparison.
    T      F      T     Cudd & dist.  Not likely.
    T      T      F     Cudd & local.  For debugging ref-based package
    T      T      T     Belt & suspenders!

    Requirements: When running local + dist, only works if ref's are identical at all times.
    This will stop working once two distinct refs generate same hash signature.
    When that happens, shift over to a single mode.
*/


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
} shadow_ele, *shadow_mgr;

shadow_mgr new_shadow_mgr(bool do_cudd, bool do_local, bool do_dist);
void free_shadow_mgr(shadow_mgr mgr);

ref_t shadow_one(shadow_mgr mgr);
ref_t shadow_zero(shadow_mgr mgr);
ref_t shadow_new_variable(shadow_mgr mgr);
void shadow_deref(shadow_mgr mgr, ref_t r);
void shadow_show(shadow_mgr mgr, ref_t r, char *buf);

/* Are we generating references? */
bool do_ref(shadow_mgr mgr);

ref_t shadow_negate(shadow_mgr mgr, ref_t r);
ref_t shadow_absval(shadow_mgr mgr, ref_t r);
ref_t shadow_ite(shadow_mgr mgr, ref_t iref, ref_t tref, ref_t eref);
ref_t shadow_and(shadow_mgr mgr, ref_t aref, ref_t bref);
ref_t shadow_or(shadow_mgr mgr, ref_t aref, ref_t bref);
ref_t shadow_xor(shadow_mgr mgr, ref_t aref, ref_t bref);

bool shadow_equal(shadow_mgr mgr, ref_t aref, ref_t bref);

