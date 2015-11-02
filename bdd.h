/* Implementation of a ref-based BDD package */

/* Different classes of refs */
typedef enum {BDD_NULL, BDD_CONSTANT, BDD_VARIABLE, BDD_FUNCTION,
	      BDD_RECURSE, BDD_INVALID} ref_type_t;

typedef word_t ref_t;

/* Packing components into a ref */
/* Bit position of start of each field, from highest to lowest */
#define REF_FIELD_NEG  63
#define REF_FIELD_TYPE 60
#define REF_FIELD_VAR  44

/* Optionally force a small hash signature to stress code
   that handles hash collisions */
#ifdef SMALL_HASH
#define REF_FIELD_HASH 41
#else
#define REF_FIELD_HASH 12
#endif

#define REF_FIELD_UNIQ  0


#define REF_WIDTH_NEG  (64         -REF_FIELD_NEG)
#define REF_WIDTH_TYPE (REF_FIELD_NEG  -REF_FIELD_TYPE)
#define REF_WIDTH_VAR  (REF_FIELD_TYPE -REF_FIELD_VAR)
#define REF_WIDTH_HASH (REF_FIELD_VAR  -REF_FIELD_HASH)
#define REF_WIDTH_UNIQ (REF_FIELD_HASH -REF_FIELD_UNIQ)

#define REF_MAKE_MASK(w) ((1llu<<(w))-1)
#define REF_MASK_NEG  REF_MAKE_MASK(REF_WIDTH_NEG)
#define REF_MASK_TYPE REF_MAKE_MASK(REF_WIDTH_TYPE)
#define REF_MASK_VAR  REF_MAKE_MASK(REF_WIDTH_VAR)
#define REF_MASK_HASH REF_MAKE_MASK(REF_WIDTH_HASH)
#define REF_MASK_UNIQ REF_MAKE_MASK(REF_WIDTH_UNIQ)

#define PACK_REF(neg, typ, var, hash, uniq) \
  ((((neg)& REF_MASK_NEG)  <<REF_FIELD_NEG)  |  \
   (((typ)& REF_MASK_TYPE) <<REF_FIELD_TYPE) |  \
   (((var)& REF_MASK_VAR)  <<REF_FIELD_VAR)  |  \
   (((hash)&REF_MASK_HASH) <<REF_FIELD_HASH) |  \
   (((uniq)&REF_MASK_UNIQ) <<REF_FIELD_UNIQ))

#define REF_GET_NEG(ref)   (((ref)>>REF_FIELD_NEG)  &REF_MASK_NEG)
#define REF_GET_TYPE(ref)  (((ref)>>REF_FIELD_TYPE) &REF_MASK_TYPE)
#define REF_GET_VAR(ref)   (((ref)>>REF_FIELD_VAR)  &REF_MASK_VAR)
#define REF_GET_HASH(ref)  (((ref)>>REF_FIELD_HASH) &REF_MASK_HASH)
#define REF_GET_UNIQ(ref)  (((ref)>>REF_FIELD_UNIQ) &REF_MASK_UNIQ)

#define REF_IS_CONST(ref)   (REF_GET_TYPE(ref)==BDD_CONSTANT)
#define REF_IS_VAR(ref)     (REF_GET_TYPE(ref)==BDD_VARIABLE)
#define REF_IS_FUNCT(ref)   (REF_GET_TYPE(ref)==BDD_FUNCTION)
#define REF_IS_RECURSE(ref) (REF_GET_TYPE(ref)==BDD_RECURSE)
#define REF_IS_INVALID(ref) (REF_GET_TYPE(ref)==BDD_INVALID)

/* Level of constant */
#define CONST_VAR REF_MASK_VAR
/* Create different ref types */
#define REF_ZERO                 PACK_REF(1, BDD_CONSTANT, CONST_VAR, 0, 0)
#define REF_ONE                  PACK_REF(0, BDD_CONSTANT, CONST_VAR, 0, 0)
#define REF_VAR(var)             PACK_REF(0, BDD_VARIABLE, var, 0, 0)
#define REF_FUN(var, hash, uniq) PACK_REF(0, BDD_FUNCTION, var, hash, uniq)
#define REF_RECURSE PACK_REF(0, BDD_RECURSE, 0, 0, 0)
#define REF_INVALID PACK_REF(0, BDD_INVALID, 0, 0, 0)

/* Negate a ref */
#define REF_NEGATE(ref) ((ref)^((word_t)1<<REF_FIELD_NEG))

/* Remove negation from ref */
#define REF_ABSVAL(ref) ((ref)&~((word_t)1<<REF_FIELD_NEG))

/* Maintain an array of counters to keep track of statistics */
/* These stats combine STATA's from agent with STATB's from here */
enum {STATB_UNIQ_CURR = NSTATA, STATB_UNIQ_PEAK, STATB_UNIQ_TOTAL,
      STATB_UNIQ_COLLIDE,
      STATB_ITE_CNT, STATB_ITE_LOCAL_CNT, STATB_ITE_HIT_CNT, STATB_ITE_NEW_CNT,
      STATB_ITEC_CURR, STATB_ITEC_PEAK, STATB_ITEC_TOTAL,
      STATB_UOP_CNT, STATB_UOP_HIT_CNT, STATB_UOP_STORE_CNT, NSTAT};

typedef struct {
    int variable_cnt;
    keyvalue_table_ptr unique_table;
    keyvalue_table_ptr ite_table;
    size_t stat_counter[NSTAT];
    size_t last_nelements;
} ref_mgr_ele, *ref_mgr;

/* Create a new manager */
ref_mgr new_ref_mgr();

/* Free a new manager */
void free_ref_mgr(ref_mgr mgr);

/* Create string representation of ref.  Buf should be of length >= 24 */
void ref_show(ref_t r, char *buf);

/* Create a new variable */
ref_t ref_new_variable(ref_mgr mgr);

/* Get or create ref */
ref_t ref_canonize(ref_mgr mgr, ref_t vref, ref_t hiref, ref_t loref);

/* Create refs for var, hi, and lo */
void ref_deref(ref_mgr mgr, ref_t r, ref_t *vrefp, ref_t *trefp, ref_t *erefp);

/* ITE operation */
ref_t ref_ite(ref_mgr mgr, ref_t iref, ref_t tref, ref_t eref);

/* Special cases of ITE */
ref_t ref_and(ref_mgr mgr, ref_t aref, ref_t bref);
ref_t ref_or(ref_mgr mgr, ref_t aref, ref_t bref);
ref_t ref_xor(ref_mgr mgr, ref_t aref, ref_t bref);

/* Check whether have accumulated enough state to perform GC */
bool ref_gc_check(ref_mgr mgr);

/*** Unary operations ***/

/* Create set of ref's.  Use regular set calls to perform other functions */
set_ptr new_ref_set();

/* Find all reachable nodes from a set of roots */
set_ptr ref_reach(ref_mgr mgr, set_ptr roots);

/* Compute set of variables (given by refs) in support of set of roots */
set_ptr ref_support(ref_mgr mgr, set_ptr roots);

/* Create key-value table mapping set of root nodes to their densities. */
keyvalue_table_ptr ref_density(ref_mgr mgr, set_ptr roots);

/* Retrieve double from map */
double get_double(keyvalue_table_ptr map, ref_t r);

/*
  Create key-value table mapping set of root nodes to their counts.
*/
keyvalue_table_ptr ref_count(ref_mgr mgr, set_ptr roots);

/* Create key-value table mapping set of root nodes to their restrictions,
   with respect to a set of literals (given as a set of refs)
*/
keyvalue_table_ptr ref_restrict(ref_mgr mgr, set_ptr roots, set_ptr lits);

/* Create key-value table mapping set of root nodes to their
   existential quantifications with respect to a set of variables
   (given as a set of refs)
*/
keyvalue_table_ptr ref_equant(ref_mgr mgr, set_ptr roots, set_ptr vars);

/* Create key-value table mapping set of root nodes to their shifted versions
   with respect to a mapping from old variables to new ones 
*/
keyvalue_table_ptr ref_shift(ref_mgr mgr, set_ptr roots,
			     keyvalue_table_ptr vmap);

/* Garbage collection.
   Find all nodes reachable from roots and keep only those in unique table */
void ref_collect(ref_mgr mgr, set_ptr roots);

/* Show information about manager status */
void ref_show_stat(ref_mgr mgr);


/****** Support for distributed implementation *******************************/

/** 
Supported Operations

(Arguments marked with asterisk are possibly
 undetermined at the time the operator is created)


   Var(dest):
      Register new variable.
      Performed by worker #0

   Canonize(dest, vref, *hiref, *loref):
      Find or create ref for node.
      Performed by arbitrary worker.
      Typically must then call CanonizeLookup

   CanonizeLookup(dest, hash, vref, hiref, loref, negate):
      Either find or create ref for node.
      Performed by worker #(hash % w)
      Sent result optionally negated.

   RetrieveLookup(dest, ref):
      Get tref and eref associated with node.
      Performed by worker #(hash % w), where hash is extracted from ref.
      Generates operand with two data words

   ITELookup(dest, iref, tref, eref, negate):
      Inner portion of ITE.  Arguments must first be put into canonical form.
      Performed at hash(iref, tref, eref) % w
      
   ITERecurse(dest, vref, *irefhi, *ireflo, *trefhi, *treflo, *erefhi, *ereflo):
      Recursive portion of ITE based on cofactors of arguments
      Performed by arbitrary worker
      
   ITEStore(dest, iref, tref, eref, *ref, negate):
      Store result of ITE operation in operation cache
      Performed locally.
      Result (possibly) negated and sent to dest.

   UOPDown(dest, id, ref)
      Outward propagation of unary operation.
      Performed by worker #(hash % w), where hash is extracted from ref.

   UOPUp(dest, id, ref, *hival, *loval)
      Upward return of unary operation
      Performed locally

   UOPStore(dest, id, ref, *val)
      Completion of upward unary operation when require
      additional operations when moving upward
      Performed locally

**/


typedef enum { OP_VAR, OP_CANONIZE, OP_CANONIZE_LOOKUP, OP_RETRIEVE_LOOKUP,
	       OP_ITE_LOOKUP, OP_ITE_RECURSE, OP_ITE_STORE,
	       OP_UOP_DOWN, OP_UOP_UP, OP_UOP_STORE } opcode_t;

void init_dref_mgr();
void free_dref_mgr();
chunk_ptr flush_dref_mgr();

chunk_ptr build_var(word_t dest);

chunk_ptr build_canonize(word_t dest, ref_t vref);

chunk_ptr build_canonize_lookup(word_t dest, word_t hash, ref_t vref,
				ref_t hiref, ref_t loref, bool negate);

chunk_ptr build_retrieve_lookup(word_t dest, ref_t ref);

chunk_ptr build_ite_lookup(word_t dest, ref_t iref, ref_t tref,
			   ref_t eref, bool negate);

chunk_ptr build_ite_recurse(word_t dest, ref_t vref);

chunk_ptr build_ite_store(word_t dest, word_t iref,
			  word_t tref, word_t eref, bool negate);

chunk_ptr build_uop_down(word_t dest, unsigned uid, ref_t ref);

chunk_ptr build_uop_up(word_t dest, unsigned uid, ref_t ref);

chunk_ptr build_uop_store(word_t dest, unsigned uid, ref_t ref);


bool do_var_op(chunk_ptr op);
bool do_canonize_op(chunk_ptr op);
bool do_canonize_lookup_op(chunk_ptr op);
bool do_retrieve_lookup_op(chunk_ptr op);
bool do_ite_lookup_op(chunk_ptr op);
bool do_ite_recurse_op(chunk_ptr op);
bool do_ite_store_op(chunk_ptr op);
bool do_uop_down_op(chunk_ptr op);
bool do_uop_up_op(chunk_ptr op);
bool do_uop_store_op(chunk_ptr op);

/* Distance operations available to client */

ref_t dist_var(ref_mgr mgr);
ref_t dist_ite(ref_mgr mgr, ref_t iref, ref_t tref, ref_t eref);
keyvalue_table_ptr dist_density(ref_mgr mgr, set_ptr roots);
keyvalue_table_ptr dist_count(ref_mgr mgr, set_ptr roots);
void dist_mark(ref_mgr mgr, set_ptr roots);
set_ptr dist_support(ref_mgr mgr, set_ptr roots);

/* Create key-value table mapping set of root nodes to their restrictions,
   with respect to a set of literals (given as a set of refs)
*/
keyvalue_table_ptr dist_restrict(ref_mgr mgr, set_ptr roots, set_ptr lits);

/* Create key-value table mapping set of root nodes to their shifted versions
   with respect to a mapping from old variables to new ones 
*/
keyvalue_table_ptr dist_shift(ref_mgr mgr, set_ptr roots,
			      keyvalue_table_ptr vmap);


/* Create key-value table mapping set of root nodes to their
   existential quantifications with respect to a set of variables
   (given as a set of refs)
*/
keyvalue_table_ptr dist_equant(ref_mgr mgr, set_ptr roots, set_ptr vars);


/* For processing summary statistics information */
void do_summary_stat(chunk_ptr smsg);

/* Distributed unary operations */

/* Worker UOP functions */
void uop_start(unsigned id, unsigned opcode, unsigned nword, word_t *data);
void uop_finish(unsigned id);

/* GC operarations */
void worker_gc_start();
void worker_gc_finish();
