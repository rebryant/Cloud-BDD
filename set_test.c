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

/* Array of strings.  These get set to NULL when element deleted */
static char **val_set;
/* Duplicate copy that remains unchanged */
static char **shadow_set;

/* Generate random string up to some maximum length.
   String must be at least maxlen/2 to eliminate chance of collisions.  */
char *rstring(size_t maxlen) {
    size_t len = maxlen/2 + random() % (maxlen + 1 - maxlen/2);
    char *s = malloc_or_fail(len+1, "rstring");
    size_t i;
    for (i = 0; i < len; i++) {
	char c = 'a' + random() % 26;
	s[i] = c;
    }
    s[i] = '\0';
    return s;
}

static void init_st() {
    st_table = set_new(string_hash, string_equal);
}

static void init_data(size_t nele) {
    int maxlen = 20;
    val_set = (char **) calloc_or_fail(nele, sizeof(char *), "init_data");
    shadow_set = (char **) calloc_or_fail(nele, sizeof(char *), "init_data");
    size_t i;
    for (i = 0; i < nele; i++) {
	val_set[i] = shadow_set[i] = rstring(maxlen);
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
	set_insert(st_table, (word_t) val_set[i]);
	report(3, "i = %d.  Inserted value '%s'", i, val_set[i]);
	insertion_cnt++;
    }
    size_t n = nele;
    while (n > 0) {
	size_t *rp = random_perm(nele);
	/* Make pass over all possible elements.  Randomly remove some, setting their values to NULL */
	for (j = 0; j < nele; j++) {
	    size_t i = rp[j];
	    if (val_set[i] == NULL) {
		/* Reinsert with declining probability */
		size_t weight = 8*nele/(n+1);
		bool insert = (random() % weight) == 0;
		bool mem = set_member(st_table, (word_t) shadow_set[i], false);
		find_cnt++;
		if (mem) {
		    err(true, "i = %lu.  Unexpectedly found value '%s'", i, shadow_set[i]);
		} else {
		    report(4, "i = %lu.  Confirmed that '%s' not in set", i, shadow_set[i]);
		}
		if (insert) {
		    val_set[i] = shadow_set[i];
		    set_insert(st_table, (word_t) val_set[i]);
		    report(3, "i = %d.  Reinserted pointer value '%s'", i, val_set[i]);
		    reinsertion_cnt++;
		    n++;
		}
	    } else {
		/* Remove with probability 1/2 */
		size_t weight = 2;
		bool remove = (random() % weight) == 0;
		bool mem = set_member(st_table, (word_t) val_set[i], remove);
		find_cnt++;
		if (mem) {
		    report(4, "i = %d.  Found expected value '%s'", i, val_set[i]);
		} else {
		    err(true, "i = %lu.  Didn't find expected value '%s'\n", i, val_set[i]);
		}
		if (remove) {
		    char *value = val_set[i];
		    val_set[i] = NULL;
		    deletion_cnt++;
		    report(3, "i = %d.  Removed value '%s'", i, value);
		    n--;
		}
	    }
	}
	free_array(rp, nele, sizeof(size_t));
    }
    printf("Fill/Empty: Insertions %d.  Reinsertions %d.  Deletions %d.  Finds %d\n", insertion_cnt, reinsertion_cnt, deletion_cnt, find_cnt);
}

static int apply_cnt = 0;

void apply_fun(word_t vvalue) {
    char *value = (char *) vvalue;
    bool mem = set_member(st_table, vvalue, false);
    if (mem) {
	report(3, "Refound key '%s' in set", value); 
    } else {
	err(true, "Couldn't refind key '%s' in set", value);
    }
    apply_cnt++;
}

void apply_test(size_t nele) {
    apply_cnt = 0;
    int insertion_cnt = 0;
    size_t i;
    for (i = 0; i < nele; i++) {
	set_insert(st_table, (word_t) val_set[i]);
	report(3, "i = %d.  Inserted value '%s'", i, val_set[i]);
	insertion_cnt++;
    }

    set_apply(st_table, apply_fun);
    if (apply_cnt != st_table->nelements) {
	err(true, "Applied function to %d elements.  Expected %d", apply_cnt, st_table->nelements);
    }
    printf("Apply test.  Insertions: %d.  Evaluations: %d\n", insertion_cnt, apply_cnt);
}

/* Perform this test after apply test.  No additional elements inserted */
void iter_test(size_t nele) {
    int insertion_cnt = 0;
    int iter_cnt = 0;
    int remove_cnt = 0;
    size_t i;
    set_iterstart(st_table);
    char *s;
    while (set_iternext(st_table, (word_t *) &s)) {
	report(5, "Nondestructive iterator got value '%s'", s);
	iter_cnt++;
	if (!set_member(st_table, (word_t) s, NULL)) {
	    err(true, "Nondestructive iterator found element '%s', but not in set", s);
	}
    }
    if (iter_cnt != nele)
	err(true, "Inserted %d elements, but nondestructive iterator got %d values", insertion_cnt, iter_cnt);
    else
	report(2, "Nondestructive iterator got %d elements", iter_cnt);
    while (set_removenext(st_table, (word_t *) &s)) {
	report(5, "Destructive iterator got value '%s'", s);
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
    size_t i;
    init_st();
    init_data(ecount);
    fill_empty_test(ecount);
    for (i = 0; i < ecount; i++)
	free_string(shadow_set[i]);
    free_array(val_set, ecount, sizeof(char *));
    free_array(shadow_set, ecount, sizeof(char *));

    init_data(ecount);
    apply_test(ecount);
    iter_test(ecount);
    for (i = 0; i < ecount; i++)
	free_string(shadow_set[i]);
    free_array(val_set, ecount, sizeof(char *));
    free_array(shadow_set, ecount, sizeof(char *));
    set_free(st_table);
    mem_status(stdout);
    return 0;
}

