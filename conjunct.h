/* Support for conjunction and Coudert/Madre simplification operations */

/* Global variables defined in runbdd.c */
extern shadow_mgr smgr;
extern keyvalue_table_ptr reftable;

/* Functions defined in runbdd.c */
void root_addref(ref_t r, bool fresh);
void root_deref(ref_t r);
ref_t get_ref(char *name);

void init_conjunct(char *cstring);

/* Data structure for representing lists of refs */
typedef struct RSET rset;

/* Create empty rset */
rset *rset_new();

/* Add function to set to be conjuncted */
void rset_add_term(rset *set, ref_t fun);

/* Clear elements of rset (for error recovery) and free all storage */
void rset_free(rset *set);

/* Compute conjunction.  rset is destructively modified */
ref_t rset_conjunct(rset *set);

/* Perform Coudert/Madre restriction to simplify function based on functions in rset */
ref_t simplify_with_rset(ref_t fun, rset *set);
