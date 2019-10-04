/* Support for conjunction and Coudert/Madre simplification operations */

/* Global variables defined in runbdd.c */
extern shadow_mgr smgr;
extern keyvalue_table_ptr reftable;

void root_addref(ref_t r, bool fresh);
void root_deref(ref_t r);
void assign_ref(char *name, ref_t r, bool fresh, bool variable);

/* Functions defined in runbdd.c */
void init_conjunct();
