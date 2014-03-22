/* Implementation of a very general hash tables to implement a key-value package and set packages.
   keyvalue: Hashes word indicating key to store & retrieve words representing values.
   set: Hashes word indicating element.
   set insertion does not check for duplicates, and therefore can use to represent multisets
*/

/* Entries in key/value table are a word key and a word value. */
typedef struct HELE hash_ele, *hash_ele_ptr;

struct HELE {
    word_t key;
    word_t value;
    hash_ele_ptr next;
};

/* User must supply hash function that will take the key's
   word_t and return size_t */
typedef size_t (*hash_fun)(word_t);

/* User must supply function that tests two keys for equality */
typedef bool (*eq_fun)(word_t , word_t);

/* Functions that can be used to operate on keys and values */
typedef void (*keyvalue_operate_fun)(word_t key, word_t value);

/* Function for replicating values */
typedef word_t (*copy_fun_t)(word_t);

/* Actual representation of the key/value table */
typedef struct {
    hash_ele_ptr *buckets;
    size_t nbuckets;
    size_t nelements;
    size_t minindex;
    hash_fun h;
    eq_fun eq;
    /* Iterator components */
    size_t iter_index;
    hash_ele_ptr iter_next;
} keyvalue_table_ele, *keyvalue_table_ptr;

keyvalue_table_ptr keyvalue_new(hash_fun h, eq_fun eq);

/* Apply function to every key/value pair in table. */
void keyvalue_apply(keyvalue_table_ptr kvt, keyvalue_operate_fun op_fun);

/* Dismantle key/value table, freeing all of its storage.
   Does not free the keys or values
*/
void keyvalue_free(keyvalue_table_ptr kvt);

/* Insert key and value into table.
   Does not check for duplicates.
*/
void keyvalue_insert(keyvalue_table_ptr kvt, word_t key, word_t value);

/* Look for element in key/value table.
   If found, sets *valp to its value.
*/
bool keyvalue_find(keyvalue_table_ptr kvt, word_t key, word_t *valp);

/* Remove element from table.  Set key & value.  Return true if entry found */
bool keyvalue_remove(keyvalue_table_ptr kvt, word_t key, word_t *oldkey, word_t *oldval);

/*
  Implementation of an iterator.
  Removes and returns some element from table, updating keyp & valp.
  Does not invoke destructors.
  Returns false when no elements left
*/
bool keyvalue_removenext(keyvalue_table_ptr kvt, word_t *keyp, word_t *valp);

/*
  Implementation of nondestructive iterator.
  Cannot insert or delete any elements while iteration taking place.
*/
void keyvalue_iterstart(keyvalue_table_ptr kvt);

bool keyvalue_iternext(keyvalue_table_ptr kvt, word_t *keyp, word_t *valp);

/*
  Remove (k,v) values from kvt that match entries in okvt.
  Provide function to test whether value in kvt matches that in okvt.
 */
void keyvalue_diff(keyvalue_table_ptr kvt, keyvalue_table_ptr okvt, eq_fun val_equal);

/*
  Marshaling & unmarshaling.
  This is not done recursively, and so only works when keys and values are simple words
*/

/* How many words are required to hold this table? */
size_t keyvalue_marshal_size(keyvalue_table_ptr kvt);

/* Write out table as series of words */
void keyvalue_marshal(keyvalue_table_ptr kvt, word_t *dest);

/*
  Read marshaled table data that has been stored as len words and add to table.
*/
void keyvalue_unmarshal(keyvalue_table_ptr kvt, word_t *dest, size_t len);

/** Sets **/

/* Functions that can be used to operate on set members */
typedef void (*set_operate_fun)(word_t mem);

/* Entries in set are words */
typedef struct SELE set_list_ele, *set_list_ptr;

struct SELE {
    word_t value;
    set_list_ptr next;
};


/* Actual representation of the set */
typedef struct {
    set_list_ptr *buckets;
    size_t nbuckets;
    size_t nelements;
    size_t minindex;
    hash_fun h;
    eq_fun eq;
    /* Iterator components */
    size_t iter_index;
    set_list_ptr iter_next;
} set_ele, *set_ptr;

set_ptr set_new(hash_fun h, eq_fun eq);

/* Apply function to every key/value pair in table. */
void set_apply(set_ptr set, set_operate_fun op_fun);

/* Dismantle set, freeing all of its storage.
   Does not free the elements
*/
void set_free(set_ptr set);

/* Insert element into table.
   Does not check for duplicates.
*/
void set_insert(set_ptr set, word_t value);

/* Determine if member is in set.  Optionally remove if found */
bool set_member(set_ptr set, word_t value, bool remove);

/*
  Implementation of a destructive iterator.
  Removes and returns some element from table, updating keyp & valp.
  Returns false when no elements left
*/
bool set_removenext(set_ptr set, word_t *valp);

/*
  Implementation of nondestructive iterator.
  Cannot insert or delete any elements while iteration taking place.
*/
void set_iterstart(set_ptr set);

bool set_iternext(set_ptr set, word_t *valp);

word_t set_choose_random(set_ptr set);

set_ptr set_clone(set_ptr set, copy_fun_t cfun);

/*
  Remove values from set that match entries in oset.
 */
void set_diff(set_ptr set, set_ptr oset);


/*
  Marshaling & unmarshaling.
  This is not done recursively, and so only works when keys and values are simple words
*/

/* How many words are required to hold this table? */
size_t set_marshal_size(set_ptr set);

/* Write out table as series of words */
void set_marshal(set_ptr set, word_t *dest);

/*
  Read marshaled set data that has been stored as len words and add to set.
*/
void set_unmarshal(set_ptr set, word_t *dest, size_t len);

/** Utility functions **/


/* Some useful hash functions */
/* Hash string (after is is cast to word_t)*/
size_t string_hash(word_t sp);

/* Equality function for strings */
bool string_equal(word_t sp, word_t tp);

/* Hash array of words */
#ifdef VMASK
/* If submask nonzero, then it designates which words to hash */
size_t wordarray_hash(word_t *a, size_t cnt, word_t submask);
#else
size_t wordarray_hash(word_t *a, size_t cnt);
#endif

/* Equality function for word arrays */
bool wordarray_equal(word_t *a, word_t *b, size_t cnt, word_t submask);


/* Hash single word */
size_t word_hash(word_t w);

/* Word equality */
bool word_equal(word_t wa, word_t wb);

/* Create table of words */
keyvalue_table_ptr word_keyvalue_new();

/* Create set of words */
set_ptr word_set_new();
