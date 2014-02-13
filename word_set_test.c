/* Test set consisting of words */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>

#include "dtype.h"
#include "report.h"
#include "table.h"

/* set being tested */
static set_ptr st_table;

/* Array of nonzero-valued words.  These get set to 0 when element deleted */
static word_t *val_set;
/* Duplicate copy that remains unchanged */
static word_t *shadow_set;

/* Generate random value */
word_t rval() {
    word_t v = 0;
    while (v == 0) {
	unsigned hi = random();
	unsigned lo = random();
	v = ((word_t) hi << 32) | lo;
    }
    return v;
}

static void init_st() {
    st_table = word_set_new();
}

static void init_data(size_t nele) {
    val_set = (word_t *) calloc_or_fail(nele, sizeof(word_t), "init_data");
    shadow_set = (word_t *) calloc_or_fail(nele, sizeof(word_t), "init_data");
    size_t i;
    for (i = 0; i < nele; i++) {
	val_set[i] = shadow_set[i] = rval();
    }
}

/* Generate random permutation */
size_t *random_perm(size_t nele) {
    size_t *result = calloc_or_fail(nele, sizeof(size_t), "random_perm");
    size_t i, j;
    if (nele == 0)
	return NULL;
    for (j = 0; j < nele; j++) {
	result[j] = j;
    }
    for (j = nele-1; j != 0; j--) {
	i = random() % (j+1);
	size_t t = result[i];
	result[i] = result[j];
	result[j] = t;
    }
    return result;
}

/* Generate data, and then insert & delete all of it. */
static void fill_empty_test(size_t nele) {
    /* Statistics */
    int find_cnt = 0;
    int insertion_cnt = 0;
    int reinsertion_cnt = 0;
    int deletion_cnt = 0;
    size_t i, j;
    for (i = 0; i < nele; i++) {
	set_insert(st_table, val_set[i]);
	report(3, "i = %d.  Inserted value '0x%llx'", i, val_set[i]);
	insertion_cnt++;
    }
    size_t n = nele;
    while (n > 0) {
	size_t *rp = random_perm(nele);
	/* Make pass over all possible elements.  Randomly remove some, setting their values to 0 */
	for (j = 0; j < nele; j++) {
	    size_t i = rp[j];
	    if (val_set[i] == 0) {
		/* Reinsert with declining probability */
		size_t weight = 8*nele/(n+1);
		bool insert = (random() % weight) == 0;
		bool mem = set_member(st_table, shadow_set[i], false);
		find_cnt++;
		if (mem) {
		    err(true, "i = %lu.  Unexpectedly found value '0x%llx'", i, shadow_set[i]);
		} else {
		    report(4, "i = %lu.  Confirmed that '0x%llx' not in set", i, shadow_set[i]);
		}
		if (insert) {
		    val_set[i] = shadow_set[i];
		    set_insert(st_table, val_set[i]);
		    report(3, "i = %d.  Reinserted value '0x%llx'", i, val_set[i]);
		    reinsertion_cnt++;
		    n++;
		}
	    } else {
		/* Remove with probability 1/2 */
		size_t weight = 2;
		bool remove = (random() % weight) == 0;
		bool mem = set_member(st_table, val_set[i], remove);
		find_cnt++;
		if (mem) {
		    report(4, "i = %d.  Found expected value '0x%llx'", i, val_set[i]);
		} else {
		    err(true, "i = %lu.  Didn't find expected value '0x%llx'\n", i, val_set[i]);
		}
		if (remove) {
		    word_t value = val_set[i];
		    val_set[i] = 0;
		    deletion_cnt++;
		    report(3, "i = %d.  Removed value '0x%llx'", i, value);
		    n--;
		}
	    }
	}
	free_array(rp, nele, sizeof(size_t));
    }
    printf("Fill/Empty: Insertions %d.  Reinsertions %d.  Deletions %d.  Finds %d\n",
	   insertion_cnt, reinsertion_cnt, deletion_cnt, find_cnt);
}

static int apply_cnt = 0;

void apply_fun(word_t value) {
    bool mem = set_member(st_table, value, false);
    if (mem) {
	report(3, "Refound key '0x%llx' in set", value); 
    } else {
	err(true, "Couldn't refind key '0x%llx' in set", value);
    }
    apply_cnt++;
}

void apply_test(size_t nele) {
    apply_cnt = 0;
    int insertion_cnt = 0;
    size_t i;
    for (i = 0; i < nele; i++) {
	set_insert(st_table, val_set[i]);
	report(3, "i = %d.  Inserted value '0x%llx'", i, val_set[i]);
	insertion_cnt++;
    }

    set_apply(st_table, apply_fun);
    if (apply_cnt != st_table->nelements) {
	err(true, "Applied function to %d elements.  Expected %d", apply_cnt, st_table->nelements);
    }
    printf("Apply test.  Insertions: %d.  Evaluations: %d\n", insertion_cnt, apply_cnt);
}

static set_ptr clone_word_set(set_ptr set) {
    set_ptr clone = word_set_new();
    size_t len = set_marshal_size(set);
    word_t *dat = calloc_or_fail(len, sizeof(word_t), "clone_word_set");
    set_marshal(set, dat);
    set_unmarshal(clone, dat, len);
    free_array(dat, len, sizeof(word_t));
    return clone;
}

static bool set_subset(set_ptr sub, set_ptr sup) {
    word_t w;
    set_iterstart(sub);
    while (set_iternext(sub, &w)) {
	if (!set_member(sup, w, false)) {
	    err(false, "Subset element 0x%llx not in superset", w);
	    return false;
	}
    }
    return true;
}

void marshal_test() {
    set_ptr clone = clone_word_set(st_table);
    if (!set_subset(clone, st_table)) {
	err(true, "Clone not subset of original");
    }
    if (!set_subset(st_table, clone)) {
	err(true, "Original not subset of clone");
    }
    printf("Marshal test completed.\n");
    set_free(clone);
}

/* Perform this test at end.  Flushes all elements from set */
void iter_test(size_t nele) {
    int insertion_cnt = 0;
    int iter_cnt = 0;
    int remove_cnt = 0;
    set_iterstart(st_table);
    word_t v;
    while (set_iternext(st_table, (word_t *) &v)) {
	report(5, "Nondestructive iterator got value '0x%llx'", v);
	iter_cnt++;
	if (!set_member(st_table, v, false)) {
	    err(true, "Nondestructive iterator found element '0x%llx', but not in set", v);
	}
    }
    if (iter_cnt != nele)
	err(true, "Inserted %d elements, but nondestructive iterator got %d values", insertion_cnt, iter_cnt);
    else
	report(2, "Nondestructive iterator got %d elements", iter_cnt);
    while (set_removenext(st_table, (word_t *) &v)) {
	report(5, "Destructive iterator got value '0x%llx'", v);
	remove_cnt++;
    }
    if (remove_cnt != nele)
	err(true, "Inserted %d elements, but destructive iterator got %d values", insertion_cnt, remove_cnt);
    else
	report(2, "Nondestructive iterator got %d elements", iter_cnt);
    printf("Iterator test.  Both iterators got %lu elements\n", nele);
}

int usage(char *cmd) {
    printf("Usage: %s [-h] [-n NELE] [-v VERB]\n", cmd);
    printf("\t-h      \tPrint this message\n");
    printf("\t-n NELE \tSet number of elements\n");
    printf("\t-v VERB \tSet verbosity level\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    verblevel = 3;
    int ecount = 5;
    int c;
    while ((c = getopt(argc, argv, "hv:n:")) != -1) {
	switch(c) {
	case 'h':
	    usage(argv[0]);
	    break;
	case 'v':
	    verblevel = atoi(optarg);
	    break;
	case 'n':
	    ecount = atoi(optarg);
	    break;
	default:
	    printf("Unrecognized option '%c'\n", c);
	    usage(argv[0]);
	}
    }
    init_st();
    init_data(ecount);
    fill_empty_test(ecount);
    free_array(val_set, ecount, sizeof(word_t));
    free_array(shadow_set, ecount, sizeof(word_t));

    init_data(ecount);
    apply_test(ecount);
    marshal_test();
    iter_test(ecount);
    free_array(val_set, ecount, sizeof(word_t));
    free_array(shadow_set, ecount, sizeof(word_t));
    set_free(st_table);
    mem_status(stdout);
    return 0;
}

