/* Support for conjunction and Coudert/Madre simplification operations */

/* Global variables defined in runbdd.c */
extern shadow_mgr smgr;
extern keyvalue_table_ptr reftable;

/* Functions defined in runbdd.c */
void root_addref(ref_t r, bool fresh);
void root_deref(ref_t r);


void init_conjunct();

/* Add function to set to be conjuncted */
void conjunct_add_term(ref_t fun);

/* Clear partially computed conjunction */
void clear_conjunction();

ref_t compute_conjunction();
