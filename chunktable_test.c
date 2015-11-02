#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>


#include "dtype.h"
#include "report.h"
#include "table.h"
#include "chunk.h"

/* Testing code for key/value table based on chunk */

typedef struct {
    chunk_ptr key;
    chunk_ptr value;
} kv_ele, *kv_ptr;

/* Key value table being tested */
static keyvalue_table_ptr kv_table;

/* Array of key/value pairs.  NULL value indicates that element not in table */
static kv_ptr kv_set;

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

static void init_kt() {
    kv_table = chunk_table_new();
}

/* Create chunk holding single value */
static chunk_ptr word2chunk(word_t v) {
    chunk_ptr cp = chunk_new(1);
    chunk_insert_word(cp, v, 0);
    return cp;
}

/* Get value stored as single-word chunk */
static word_t chunk2word(chunk_ptr cp) {
#ifdef VMASK
    if (cp == NULL || cp->length != 1 || (cp->vmask & 0x1) != 1) {
	err(true, "Invalid single-word chunk");
    }
#else
    if (cp == NULL || cp->length != 1) {
	err(true, "Invalid single-word chunk");
    }
#endif /* VMASK */
    return cp->words[0];
}
    

static void init_data(size_t nele) {
    int maxlen = 20;
    kv_set = (kv_ptr) calloc_or_fail(nele, sizeof(kv_ele), "init_data");
    size_t i;
    for (i = 0; i < nele; i++) {
	char *k = rstring(maxlen);
	word_t v = random();
	kv_set[i].key = str2chunk(k);
	kv_set[i].value = word2chunk(v);
	free_string(k);
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
	keyvalue_insert(kv_table, (word_t) kv_set[i].key,
			(word_t) kv_set[i].value);
	size_t w = chunk2word(kv_set[i].value);
	char *k = chunk2str(kv_set[i].key);
	report(3,
	       "i = %d.  Inserted value 0x%llx (pointer %p, for key '%s'",
	       i, w, kv_set[i].value, k);
	free_string(k);
	insertion_cnt++;
    }
    size_t n = nele;
    while (n > 0) {
	size_t *rp = random_perm(nele);
	/* Make pass over all possible elements.
	   Randomly remove some, setting their values to 0 */
	for (j = 0; j < nele; j++) {
	    size_t i = rp[j];
	    char *k = chunk2str(kv_set[i].key);
	    if (kv_set[i].value == NULL) {
		/* Reinsert with declining probability */
		size_t weight = 8*nele/(n+1);
		bool insert = (random() % weight) == 0;
		chunk_ptr vcp; 
		find_cnt++;
		if (keyvalue_find(kv_table, (word_t) kv_set[i].key,
				  (word_t *) &vcp)) {
		    err(true,
"i = %lu.  Found pointer %p (word 0x%llx) for key '%s'.  Expected NULL",
			i, vcp, chunk_get_word(vcp, 0), k);
		} else {
		    report(5,
"i = %lu.  As expected, didn't find entry for key '%s'.", i, k);
		}
		if (insert) {
		    word_t v = random();
		    kv_set[i].value = word2chunk(v);
		    keyvalue_insert(kv_table, (word_t) kv_set[i].key,
				    (word_t) kv_set[i].value);
		    report(3, "i = %d.  Reinserted value 0x%llx for key '%s'",
			   i, v, k);
		    reinsertion_cnt++;
		    n++;
		}
	    } else {
		/* Remove with probability 1/2 */
		size_t weight = 2;
		bool remove = (random() % weight) == 0;
		chunk_ptr vcp; 
		find_cnt++;
		if (keyvalue_find(kv_table, (word_t) kv_set[i].key,
				  (word_t *) &vcp)) {
		    report(5, "i = %lu.  Found pointer %p for key '%s'",
			   i, (void *) vcp, k);
		} else {
		    err(true, "i = %lu.  Didn't find entry for key '%s'", i, k);
		}
		word_t rv = chunk2word(vcp);
		word_t ev = chunk2word(kv_set[i].value);
		if (rv != ev) {
		    if (vcp != kv_set[i].value) {
			err(false,
"i = %lu.  Pointer corruption for key '%s'.  Expected %p.  Got %p",
			    i, k, kv_set[i].value, vcp);
		    }
		    err(true,
"i = %lu.  Retrieved value 0x%llx for key '%s'.  Expected 0x%llx",
			i, rv, k, ev);
		}
		if (remove) {
		    report(3,
"i = %d.  Removing word 0x%llx (pointer %p) for key '%s'",
			   i, chunk_get_word(kv_set[i].value, 0),
			   kv_set[i].value, k);
		    keyvalue_remove(kv_table, (word_t) kv_set[i].key, NULL, NULL);
		    chunk_free(kv_set[i].value);
		    kv_set[i].value = NULL;
		    deletion_cnt++;
		    n--;
		}
	    }
	    free_string(k);
	}
	free_array(rp, nele, sizeof(size_t));
    }
    printf(
"Fill/Empty: Insertions %d.  Reinsertions %d.  Deletions %d.  Finds %d\n",
           insertion_cnt, reinsertion_cnt, deletion_cnt, find_cnt);
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
    init_kt();
    init_data(ecount);
    fill_empty_test(ecount);
    for (i = 0; i < ecount; i++) {
	chunk_free(kv_set[i].key);
	if (kv_set[i].value)
	    chunk_free(kv_set[i].key);
    }
    free_array(kv_set, ecount, sizeof(kv_ele));
    keyvalue_free(kv_table);
    mem_status(stdout);
    return 0;
}
