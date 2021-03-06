/* Support for conjunction and Coudert/Madre simplification operations */

/* Global variables defined in runbdd.c */
extern shadow_mgr smgr;
extern keyvalue_table_ptr reftable;

void root_addref(ref_t r, bool fresh);
void root_deref(ref_t r);
void root_checkref(ref_t r);
void assign_ref(char *name, ref_t r, bool fresh, bool variable);

/* Functions defined in runbdd.c */
ref_t get_ref(char *name);

/* Global variables defined in conjunct.c */
extern int preprocess_soft_and_threshold_scaled;
extern int inprocess_soft_and_threshold_scaled;
extern int soft_and_expansion_ratio_scaled;
extern int soft_and_allow_growth;
extern int preprocess_conjuncts;
extern int cache_soft_lookup_ratio;
extern int cache_hard_lookup_ratio;
extern int track_conjunction;
extern int quantify_threshold;

/* Functions defined in conjunct.c */
void init_conjunct();
