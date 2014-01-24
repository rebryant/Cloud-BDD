/* Implementation of a very general hash table package.
   Hashes a structure consisting of a block of bytes, where the initial entry
   is a next pointer for use by the hash table.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#include "dtype.h"
#include "table.h"
#include "report.h"

/* Some useful parameters */
/* Target load factors used to control resizing of bucket array */
#define MAX_LOAD 5.0
#define MIN_LOAD 1.5
#define BEST_LOAD 2.0

/* How many bytes are in different entities: */
#define POINTER_BYTES (sizeof(void *))
#define SIZET_BYTES (sizeof(size_t))
#define SIZET_BITS (8*SIZET_BYTES)

/* Where should we start indexing into the prime array */
#define INIT_PI 5
/* Good hash table sizes.  Primes just under a power of 2 */
static size_t  primes[] = {
			    2,
			    3,
			    7,
			    13,
			    23,
			    59,
			    113,
			    241,
			    503,
			    1019,
			    2039,
			    4091,
			    8179,
			    16369,
			    32749,
			    65521,
			    131063,
			    262139,
			    524269,
			    1048571,
			    2097143,
			    4194287,
			    8388593,
			    16777199,
			    33554393,
			    67108859,
			    134217689,
			    268435399,
			    536870879,
			    1073741789,
			    2147483629,
			    0
};

keyvalue_table_ptr keyvalue_new(hash_fun h, eq_fun eq)
{
    keyvalue_table_ptr result = malloc_or_fail(sizeof(keyvalue_table_ele), "keyvalue_new");
    size_t nbuckets = primes[INIT_PI];
    result->buckets = calloc_or_fail(nbuckets, sizeof(hash_ele_ptr), "keyvalue_new");
    result->nbuckets = nbuckets;
    result->nelements = 0;
    result->minindex = nbuckets;
    result->h = h;
    result->eq = eq;
    result->iter_index = (size_t) -1;
    result->iter_next = NULL;
    return result;
}

/* Apply function to every key/value pair in table. */
void keyvalue_apply(keyvalue_table_ptr kvt, keyvalue_operate_fun op_fun)
{
    size_t i;
    size_t n = kvt->nbuckets;
    for (i = 0; i < n; i++) {
	hash_ele_ptr ele = kvt->buckets[i];
	while (ele) {
	    op_fun(ele->key, ele->value);
	    ele = ele->next;
	}
    }
}


/* Dismantle key/value table, freeing all of its storage.
   Does not free the keys or values
*/
void keyvalue_free(keyvalue_table_ptr kvt)
{
    size_t i;
    size_t n = kvt->nbuckets;
    for (i = 0; i < n; i++) {
	hash_ele_ptr ele = kvt->buckets[i];
	while (ele) {
	    hash_ele_ptr nele = ele->next;
	    free_block(ele, sizeof(hash_ele));
	    ele = nele;
	}
    }
    free_array (kvt->buckets, kvt->nbuckets, sizeof(hash_ele_ptr));
    free_block(kvt, sizeof(keyvalue_table_ele));
}

/* Check whether need to resize table due to growing or shrinking */
static void kv_check_for_resize(keyvalue_table_ptr kvt, bool growing)
{
    size_t old_size = kvt->nbuckets;
    size_t new_size = old_size;
    size_t best_size = (size_t) ((float) kvt->nelements / BEST_LOAD);
    float load = (float) kvt->nelements/old_size;
    hash_ele_ptr *new_buckets;
    size_t i;
    if (growing && load >= MAX_LOAD) {
	int pi;
	/* Find size above best_size */
	for (pi = INIT_PI; primes[pi+1] != 0 && primes[pi] <= best_size; pi++)
	    ;
	new_size = primes[pi];
    } else if (!growing && new_size > primes[INIT_PI]
		 && load < MIN_LOAD) {
	int pi;
	/* Find size above best_size */
	for (pi = INIT_PI; primes[pi+1] != 0 && primes[pi] <= best_size; pi++)
	    ;
	new_size = primes[pi];
    }
    if (new_size == old_size)
	return;
    report(5, "Resizing hash table from %lu to %lu buckets", old_size, new_size);
    /* Generate new table of size new_size */
    new_buckets = calloc_or_fail(new_size, sizeof(hash_ele_ptr), "kv_check_for_resize");
    kvt->minindex = new_size;
    /* Rehash all of the entries into the new set of buckets */
    for (i = 0; i < old_size; i++) {
	hash_ele_ptr ele = kvt->buckets[i];
	while (ele) {
	    hash_ele_ptr nele = ele->next;
	    size_t pos = kvt->h(ele->key) % new_size;
	    ele->next = new_buckets[pos];
	    new_buckets[pos] = ele;
	    if (pos < kvt->minindex)
		kvt->minindex = pos;
	    ele = nele;
	}
    }
    free_array(kvt->buckets, kvt->nbuckets, sizeof(hash_ele_ptr));
    kvt->buckets= new_buckets;
    kvt->nbuckets = new_size;
}


/* Insert key and value into table.
   Does not check for duplicates 
*/
void keyvalue_insert(keyvalue_table_ptr kvt, word_t key, word_t value)
{
    size_t pos;
    hash_ele_ptr ele = (hash_ele_ptr) malloc_or_fail(sizeof(hash_ele), "keyvalue_insert");
    ele->key = key;
    ele->value = value;
    kv_check_for_resize(kvt, true);
    pos = kvt->h(key) % kvt->nbuckets;
    ele->next = kvt->buckets[pos];
    kvt->buckets[pos] = ele;
    if (pos < kvt->minindex)
	kvt->minindex = pos;
    kvt->nelements++;
}

/* Look for element in key/value table.
   If found, sets *valp to its value.
   if remove true, then also removes table entry.
*/
bool keyvalue_find(keyvalue_table_ptr kvt, word_t key, word_t *valp)
{
    if (!kvt || kvt->nelements == 0)
	return false;
    size_t pos = kvt->h(key) % kvt->nbuckets;
    hash_ele_ptr ele = kvt->buckets[pos];
    while (ele) {
	if (kvt->eq(key, ele->key)) {
	    if (valp)
		*valp = ele->value;
	    return true;
	}
	ele = ele->next;
    }
    /* Didn't find matching element */
    return false;
}

/* Look for element in key/value table.
   If found, sets *valp to its value.
   if remove true, then also removes table entry.
*/
bool keyvalue_remove(keyvalue_table_ptr kvt, word_t key, word_t *oldkeyp, word_t *valp)
{
    size_t pos = kvt->h(key) % kvt->nbuckets;
    hash_ele_ptr pele = NULL;
    hash_ele_ptr ele = kvt->buckets[pos];
    while (ele) {
	if (kvt->eq(key, ele->key)) {
	    if (oldkeyp)
		*oldkeyp = ele->key;
	    if (valp)
		*valp = ele->value;
	    if (pele == NULL) {
		/* Element is at head of list */
		kvt->buckets[pos] = ele->next;
	    } else {
		/* Element is in middle of list */
		pele->next = ele->next;
	    }
	    free_block(ele, sizeof(hash_ele));
	    kvt->nelements--;
	    kv_check_for_resize(kvt, false);
	    return true;
	}
	pele = ele;
	ele = ele->next;
    }
    return false;
}

/*
  Implementation of an iterator.
  Removes and returns some element from table, updating keyp & valp.
  Returns false when no elements left
*/
bool keyvalue_removenext(keyvalue_table_ptr kvt, word_t *keyp, word_t *valp) {
    if (kvt->nelements == 0) {
	kvt->minindex = kvt->nbuckets;
	return false;
    }
    size_t pos;
    for (pos = kvt->minindex; pos < kvt->nbuckets; pos++) {
	if (kvt->buckets[pos] != NULL) {
	    hash_ele_ptr ele = kvt->buckets[pos];
	    kvt->buckets[pos] = ele->next;
	    if (keyp)
		*keyp = ele->key;
	    if (valp)
		*valp = ele->value;
	    free_block(ele, sizeof(hash_ele));
	    kvt->minindex = pos;
	    kvt->nelements--;
	    kv_check_for_resize(kvt, false);
	    return true;
	}
    }
    /* Shouldn't get here */
    return false;
}

/*
  Implementation of nondestructive iterator.
  Cannot insert or delete any elements while iteration taking place.
*/
void keyvalue_iterstart(keyvalue_table_ptr kvt) {
    kvt->iter_index = (size_t) -1;
    kvt->iter_next = NULL;
}

bool keyvalue_iternext(keyvalue_table_ptr kvt, word_t *keyp, word_t *valp) {
    size_t idx = kvt->iter_index;
    hash_ele_ptr list = kvt->iter_next;
    while (list == NULL) {
	idx++;
	if (idx >= kvt->nbuckets) {
	    /* Reached end of set.  Reset iterator */
	    kvt->iter_index = (size_t) -1;
	    kvt->iter_next = NULL;
	    return false;
	}
	list = kvt->buckets[idx];
    }
    kvt->iter_index = idx;
    kvt->iter_next = list->next;
    if (keyp)
	*keyp = list->key;
    if (valp)
	*valp = list->value;
    return true;
}


/****** Set implementation *******/



set_ptr set_new(hash_fun h, eq_fun eq)
{
    set_ptr result = malloc_or_fail(sizeof(set_ele), "set_new");
    size_t nbuckets = primes[INIT_PI];
    result->buckets = calloc_or_fail(nbuckets, sizeof(set_list_ptr), "set_new");
    result->nbuckets = nbuckets;
    result->nelements = 0;
    result->minindex = nbuckets;
    result->h = h;
    result->eq = eq;
    result->iter_index = (size_t) -1;
    result->iter_next = NULL;
    return result;
}

/* Apply function to every value in table. */
void set_apply(set_ptr set, set_operate_fun op_fun)
{
    size_t i;
    size_t n = set->nbuckets;
    for (i = 0; i < n; i++) {
	set_list_ptr list = set->buckets[i];
	while (list) {
	    op_fun(list->value);
	    list = list->next;
	}
    }
}


/* Dismantle set, freeing all of its storage.
   Does not free the values
*/
void set_free(set_ptr set)
{
    size_t i;
    size_t n = set->nbuckets;
    for (i = 0; i < n; i++) {
	set_list_ptr list = set->buckets[i];
	while (list) {
	    set_list_ptr nlist = list->next;
	    free_block(list, sizeof(set_list_ele));
	    list = nlist;
	}
    }
    free_array(set->buckets, set->nbuckets, sizeof(set_list_ptr));
    free_block(set, sizeof(set_ele));
}

/* Check whether need to resize table due to growing or shrinking */
static void set_check_for_resize(set_ptr set, bool growing)
{
    size_t old_size = set->nbuckets;
    size_t new_size = old_size;
    size_t best_size = (size_t) ((float) set->nelements / BEST_LOAD);
    float load = (float) set->nelements/old_size;
    set_list_ptr *new_buckets;
    size_t i;
    if (growing && load >= MAX_LOAD) {
	int pi;
	/* Find size above best_size */
	for (pi = INIT_PI; primes[pi+1] != 0 && primes[pi] <= best_size; pi++)
	    ;
	new_size = primes[pi];
    } else if (!growing && new_size > primes[INIT_PI]
		 && load < MIN_LOAD) {
	int pi;
	/* Find size above best_size */
	for (pi = INIT_PI; primes[pi+1] != 0 && primes[pi] <= best_size; pi++)
	    ;
	new_size = primes[pi];
    }
    if (new_size == old_size)
	return;
    report(5, "Resizing set from %lu to %lu buckets", old_size, new_size);
    /* Generate new table of size new_size */
    new_buckets = calloc_or_fail(new_size, sizeof(set_list_ptr), "set_check_for_resize");
    set->minindex = new_size;
    /* Rehash all of the entries into the new set of buckets */
    for (i = 0; i < old_size; i++) {
	set_list_ptr list = set->buckets[i];
	while (list) {
	    set_list_ptr nlist = list->next;
	    size_t pos = set->h(list->value) % new_size;
	    list->next = new_buckets[pos];
	    new_buckets[pos] = list;
	    if (pos < set->minindex)
		set->minindex = pos;
	    list = nlist;
	}
    }
    free_array(set->buckets, set->nbuckets, sizeof(set_list_ptr));
    set->buckets= new_buckets;
    set->nbuckets = new_size;
}

set_ptr set_clone(set_ptr set, copy_fun_t cfun) {
    size_t i;
    set_ptr rset = set_new(set->h, set->eq);
    for (i = 0; i < set->nbuckets; i++) {
	set_list_ptr list = set->buckets[i];
	while (list) {
	    word_t nvalue = cfun ? cfun(list->value) : list->value;
	    set_insert(rset, nvalue);
	}
    }
    return rset;
}


/* Insert value into set.
   Does not check for duplicates 
*/
void set_insert(set_ptr set, word_t value)
{
    size_t pos;
    set_list_ptr list = (set_list_ptr) malloc_or_fail(sizeof(set_list_ele), "set_insert");
    list->value = value;
    set_check_for_resize(set, true);
    pos = set->h(value) % set->nbuckets;
    list->next = set->buckets[pos];
    set->buckets[pos] = list;
    if (pos < set->minindex)
	set->minindex = pos;
    set->nelements++;
}

/* Look for element in set.  Return true if in set */
bool set_member(set_ptr set, word_t value, bool remove)
{
    if (!set || set->nelements == 0)
	return false;
    size_t pos = set->h(value) % set->nbuckets;
    set_list_ptr plist = NULL;
    set_list_ptr list = set->buckets[pos];
    while (list) {
	if (set->eq(value, list->value)) {
	    if (remove) {
		if (plist == NULL) {
		    /* Element is at head of list */
		    set->buckets[pos] = list->next;
		} else {
		    /* Element is in middle of list */
		    plist->next = list->next;
		}
		free_block(list, sizeof(set_list_ele));
		set->nelements--;
		set_check_for_resize(set, false);
	    }
	    return true;
	}
	plist = list;
	list = list->next;
    }
    /* Didn't find matching element */
    return false;
}

/*
  Implementation of an iterator.
  Removes and returns some element from table, updating keyp & valp.
  Returns false when no elements left
*/
bool set_removenext(set_ptr set, word_t *valp) {
    if (set->nelements == 0) {
	set->minindex = set->nbuckets;
	return false;
    }
    size_t pos;
    for (pos = set->minindex; pos < set->nbuckets; pos++) {
	if (set->buckets[pos] != NULL) {
	    set_list_ptr list = set->buckets[pos];
	    set->buckets[pos] = list->next;
	    if (valp)
		*valp = list->value;
	    free_block(list, sizeof(set_list_ele));
	    set->minindex = pos;
	    set->nelements--;
	    set_check_for_resize(set, false);
	    return true;
	}
    }
    /* Shouldn't get here */
    return false;
}

/*
  Implementation of nondestructive iterator.
  Cannot insert or delete any elements while iteration taking place.
*/
void set_iterstart(set_ptr set) {
    set->iter_index = (size_t) -1;
    set->iter_next = NULL;
}

bool set_iternext(set_ptr set, word_t *valp) {
    size_t idx = set->iter_index;
    set_list_ptr list = set->iter_next;
    while (list == NULL) {
	idx++;
	if (idx >= set->nbuckets) {
	    /* Reached end of set.  Reset iterator */
	    set->iter_index = (size_t) -1;
	    set->iter_next = NULL;
	    return false;
	}
	list = set->buckets[idx];
    }
    set->iter_index = idx;
    set->iter_next = list->next;
    *valp = list->value;
    return true;
}

word_t set_choose_random(set_ptr set) {
    if (set->nelements == 0) {
	return 0;
    }
    size_t ecnt = random() % set->nelements;
    size_t idx;
    for (idx = 0; idx < set->nbuckets; idx++) {
	set_list_ptr list = set->buckets[idx];
	while (list) {
	    if (ecnt == 0)
		return list->value;
	    ecnt--;
	}
    }
    /* Shouldn't get here */
    return 0;
}

/****** Utility functions ******/



/* Hash functions */
size_t string_hash(word_t sp)
{
    char *s = (char *) sp;
    size_t val = 0;
    size_t c;
    int shift_left = 1;
    int shift_right = SIZET_BITS - shift_left;
    while ((c = *s++) != 0)
	val = ((val << shift_left) | (val >> shift_right)) ^ c;
    return val;    
}

/* Hash array of words */
/* If submask nonzero, then it designates which words to hash */
size_t wordarray_hash(word_t *a, size_t cnt, word_t submask)
{
    size_t i;
    size_t val = 0;
    int shift_left = 3;
    int shift_right = SIZET_BITS - shift_left;
    if (submask == 0)
	submask = ~(word_t) 0;
    for (i = 0; i < cnt; i++) {
	val = (val << shift_left) | (val >> shift_right);
	if (submask & 0x1)
	    val ^= (size_t) a[i];
	submask >>= 1;
    }
    return val * 997;    
}


/* Equality functions */
bool string_equal(word_t sp, word_t tp) {
    char *s = (char *) sp;
    char *t = (char *) tp;
    return strcmp(s, t) == 0;
}

bool wordarray_equal(word_t *a, word_t *b, size_t cnt, word_t submask) {
    if (submask == 0) {
	submask = ~(word_t) 0;
    }
    size_t i;
    for (i = 0; i < cnt; i++) {
	if (a[i] != b[i])
	    return false;
    }
    return true;
}

/* Hash pointer */
size_t word_hash(word_t wp) {
    return (size_t) ((wp * 997) % 2147483629ULL);
}


/* Pointer equality */
bool word_equal(word_t ap, word_t bp) {
    return ap == bp;
}

/* Create table of words */
keyvalue_table_ptr word_keyvalue_new() {
    return keyvalue_new(word_hash, word_equal);
}

/* Create set of words */
set_ptr word_set_new() {
    return set_new(word_hash, word_equal);
}
