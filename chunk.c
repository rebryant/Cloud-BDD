/******************************************************************************
Data structure for representing data as a sequence of 64-bit words
******************************************************************************/ 

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <inttypes.h>
#include <stdbool.h>

#include "dtype.h"
#include "table.h"
#include "chunk.h"
#include "report.h"

/* Some macros */
#define CHUNK_WORD_VALID(cp, idx) ((((cp)->vmask) >> (idx)) & 0x1)

#define CHUNK_ADD_WORD_VALID(cp, idx) ((cp)->vmask | (0x1 << (idx)))

/* Error handling */
/* Default error function does nothing */
static void default_err_fun(void) {
    return;
}


static err_fun efun = default_err_fun;

/* Set error function to be called when error detected */
void chunk_at_error(err_fun f) {
    efun = f;
}


/* What kind of correctness checks should be applied by package.  */
/* Possible levels:
   0: Nothing.
   1: Allocation/deallocation.
   2: Null pointer detection & bounds checking.  Make sure don't exceed maximum length constraints
   3: Overlap checking.  Invalid to insert data into chunk position that is already filled.
 */
unsigned chunk_check_level = 3;

/* Error message generated when violate chunk rules.  */
static void chunk_error(char *reason, chunk_ptr cp) {
    if (cp != NULL) {
	fprintf(stderr, "Chunk error: %s.  Chunk address = %p.  Length = %lu\n",
		reason, cp, cp->length);
    } else {
	fprintf(stderr, "Chunk error: %s\n", reason);
    }
    /* Call the designated error function */
    efun();
}

/* Create a new chunk */
chunk_ptr chunk_new(size_t len) {
    size_t more_bytes = len == 0  ? 0 : WORD_BYTES * (len - 1);
    chunk_ptr cp = (chunk_ptr) malloc_or_fail(sizeof(chunk_t) + more_bytes, "chunk_new");
    if (cp == NULL && chunk_check_level >= 1) {
	chunk_error("Could not allocate chunk", cp);
    }
    cp->length = len;
    cp->vmask = 0;
    return cp;
}

/* Free a chunk */
void chunk_free(chunk_ptr cp) {
    if (cp == NULL)
	return;
    size_t len = cp->length;
    size_t more_bytes = len == 0  ? 0 : WORD_BYTES * (len - 1);
    free_block((void *) cp, sizeof(chunk_t) + more_bytes);
}

/* Replicate a chunk */
chunk_ptr chunk_clone(chunk_ptr cp) {
    if (cp == NULL && chunk_check_level >= 2) {
	chunk_error("Null Pointer", cp);
	return NULL;
    }
    chunk_ptr ncp = chunk_new(cp->length);
    ncp->length = cp->length;
    ncp->vmask = cp ->vmask;
    size_t i;
    for (i = 0; i < cp->length; i++) {
	    ncp->words[i] = cp->words[i];
    }
    return ncp;
}

/* Test whether all words of chunk are valid */
bool chunk_filled(chunk_ptr cp) {
    if (cp == NULL && chunk_check_level >= 2) {
	chunk_error("Null Pointer", cp);
	return false;
    }
    word_t mask = cp->vmask;
    word_t len = cp->length;
    /* Create checking mask */
    word_t cmask = len == CHUNK_MAX_LENGTH ? ~0ULL : (1ULL << len) - 1;
    return mask == cmask;
}

/* Insert word into chunk */
void chunk_insert_word(chunk_ptr cp, word_t wd, size_t offset) {
    if (cp == NULL && chunk_check_level >= 2) {
	chunk_error("Null Pointer", cp);
	return;
    }
    if (chunk_check_level >= 2 && offset >= cp->length) {
	chunk_error("Out of bounds insertion", cp);
	return;
    }
    if (chunk_check_level >= 3 && CHUNK_WORD_VALID(cp, offset)) {
	char buf[128];
	sprintf(buf, "Insertion into occupied word.  Offset %u", (unsigned) offset);
	chunk_error(buf, cp);
	return;
    }
    cp->words[offset] = wd;
    cp->vmask = CHUNK_ADD_WORD_VALID(cp, offset);
}

/* Get word from chunk */
word_t chunk_get_word(chunk_ptr cp, size_t offset) {
    if (cp == NULL && chunk_check_level >= 2) {
	chunk_error("Null Pointer", cp);
    }
    if (chunk_check_level >= 2 && offset >= cp->length) {
	chunk_error("Out of bounds retrieval", cp);
    }
    if (chunk_check_level >= 3 && !CHUNK_WORD_VALID(cp, offset)) {
	chunk_error("Retrieval of invalid word", cp);
    }
    return cp->words[offset];
}

/* Insert words from source chunk into destination chunk with designated offset */
void chunk_insert_chunk(chunk_ptr cdestp, chunk_ptr csrcp, size_t offset) {
    if (csrcp == NULL && chunk_check_level >= 2) {
	chunk_error("Null Source Pointer", csrcp);
	return;
    }
    size_t i;
    size_t len = (size_t) csrcp->length;
    for(i = 0; i < len; i++) {
	if (CHUNK_WORD_VALID(csrcp, i))
	    chunk_insert_word(cdestp, csrcp->words[i], i + offset);
    }
}

/* Extract subchunk */
chunk_ptr chunk_get_chunk(chunk_ptr cp, size_t offset, size_t length) {
    chunk_ptr ncp = chunk_new(length);
    size_t i;
    for (i = 0; i < length; i++) {
	chunk_insert_word(ncp, chunk_get_word(cp, i+offset), i);
    }
    return ncp;
}


/* File I/O based on low-level Unix file descriptors.  These can be files or network connections */
/* Read chunk from file.  Return null pointer if fail. */
chunk_ptr chunk_read(int fd, bool *eofp) {
    unsigned char buf[CHUNK_MAX_SIZE];
    /* Must get enough bytes to read chunk length */
    size_t cnt = 0;
    size_t need_cnt = sizeof(chunk_t);
    while (cnt < need_cnt) {
	ssize_t n = read(fd, &buf[cnt], need_cnt-cnt);
	if (n < 0) {
	    chunk_error("Failed read", NULL);
	    if (eofp)
		*eofp = false;
	    return NULL;
	}
	if (n == 0) {
	    if (eofp)
		*eofp = true;
	    else
		chunk_error("Unexpected EOF", NULL);
	    return NULL;
	}
	cnt += n;
    }
    chunk_ptr creadp = (chunk_ptr) buf;
    size_t len = creadp->length;
    if (len > 1) {
	need_cnt += WORD_BYTES * (len - 1);
	while (cnt < need_cnt) {
	    ssize_t n = read(fd, &buf[cnt], need_cnt-cnt);
	    if (n < 0) {
		chunk_error("Failed read", NULL);
	    if (eofp)
		*eofp = false;
		return NULL;
	    }
	    cnt += n;
	}
    }
    if (eofp)
	*eofp = false;
    return chunk_clone(creadp);
}

/* Write chunk to file */
/* Return 1 if successful, 0 if failed */
bool chunk_write(int fd, chunk_ptr cp) {
    unsigned char *bytes = (unsigned char *) cp;
    size_t len = cp->length;
    size_t more_bytes = len == 0  ? 0 : WORD_BYTES * (len - 1);
    size_t cnt = sizeof(chunk_t) + more_bytes;
    while (cnt > 0) {
	ssize_t n = write(fd, bytes, cnt);
	if (n < 0) {
	    chunk_error("Failed write", cp);
	    return false;
	}
	bytes += n;
	cnt -= n;
    }
    return true;
}

/* Convert a string into a chunk.  Limited to strings of length <= WORD_BYTES */
chunk_ptr str2chunk(char *s) {
    char buf[WORD_BYTES * CHUNK_MAX_LENGTH];
    size_t len = (strnlen(s, WORD_BYTES * CHUNK_MAX_LENGTH) + WORD_BYTES - 1) / WORD_BYTES;
    chunk_ptr cp = chunk_new(len);
    size_t cidx, bidx;
    size_t sidx = 0;
    for (cidx = 0; cidx < len; cidx++) {
	for (bidx = 0; bidx < WORD_BYTES && s[sidx]; bidx++) {
	    buf[bidx] = s[sidx++];
	}
	for (; bidx < WORD_BYTES; bidx++) {
	    buf[bidx] = '\0';
	}
	word_t wd = *(word_t *) buf;
	chunk_insert_word(cp, wd, cidx);
    }
    return cp;
}

/* Get string that has been stored as chunk */
char * chunk2str(chunk_ptr cp) {
    char buf[WORD_BYTES * CHUNK_MAX_LENGTH+1];
    /* Make sure string is terminated */
    buf[WORD_BYTES * cp->length] = '\0';
    word_t *wp = (word_t *) buf;
    size_t cidx;
    for (cidx = 0; cidx < cp->length; cidx++) {
	wp[cidx] = chunk_get_word(cp, cidx);
    }
    return strsave_or_fail(buf, "chunk2str");
}

/* Compute hash signature for chunk */
size_t chunk_hash(word_t vcp) {
    chunk_ptr cp = (chunk_ptr) vcp;
    size_t result = wordarray_hash(cp->words, cp->length, cp->vmask);
    return result;
}

/* Equality function suitable for use in keyvalue table */
bool chunk_equal(word_t vcp1, word_t vcp2) {
    chunk_ptr cp1 = (chunk_ptr) vcp1;
    chunk_ptr cp2 = (chunk_ptr) vcp2;
    if (cp1 == NULL)
	return (cp2 == NULL);
    int result = 1;
    word_t vmask1 = cp1->vmask;
    word_t vmask2 = cp2->vmask;
    size_t i;
    /* OK for chunks to have different lengths, as long as mask bits cover same words */
    size_t len = cp1->length < cp2->length ? cp1->length : cp2->length;
    for (i = 0; i < len; i++) {
	int check1 = vmask1 & 0x1;
	int check2 = vmask2 & 0x1;
	if (check1 != check2) {
	    result = false;
	    break;
	}
	if (check1 && cp1->words[i] != cp2->words[i]) {
	    result = false;
	    break;
	}
	vmask1 >>= 1;
	vmask2 >>= 1;
    }
    result = result && vmask1 == 0 && vmask2 == 0;
    return result;
}

keyvalue_table_ptr chunk_table_new() {
    return keyvalue_new(chunk_hash, chunk_equal);
}


#ifdef QUEUE
/***** Chunk Queues ******/

/* Helper functions */

/* Flush queue.  Unsynchronized */
static void cq_flush(chunk_queue_ptr cq) {    
    if (cq->elements != NULL) {
	free_array(cq->elements, cq->alloc_length, sizeof(chunk_ptr));
	cq->alloc_length = 0;
	cq->elements = NULL;
    }
    cq->length = 0;
    cq->alloc_length = 0;
    cq->head = 0;
    cq->tail = 0;
}

static int check_err(int code, char *source) {
    char ebuf[100];
    if (code != 0) {
	sprintf(ebuf, "Error in %s.  Number %d\n", source, code);
	chunk_error(ebuf, NULL);
    }
    return code;
}

/**** Exported functions ****/

#define CQ_ALLOC 16

chunk_queue_ptr chunk_queue_new() {
    chunk_queue_ptr cq = malloc_or_fail(sizeof(chunk_queue), "chunk_queue_new");
    cq->length = 0;
    cq->alloc_length = 0;
    cq->tail = 0;
    cq->head = 0;
    cq->elements = NULL;
    pthread_mutex_init(&cq->mutex,NULL);
    pthread_cond_init(&cq->cvar,NULL);
    return cq;
}

void chunk_queue_free(chunk_queue_ptr cq) {
    check_err(pthread_mutex_lock(&cq->mutex), "chunk_queue_flush");
    cq_flush(cq);
    free_block(cq, sizeof(chunk_queue));
    check_err(pthread_mutex_unlock(&cq->mutex), "chunk_queue_free");
}

void chunk_queue_insert(chunk_queue_ptr cq, chunk_ptr item) {
    check_err(pthread_mutex_lock(&cq->mutex), "chunk_queue_insert");
    if (cq->length == 0) {
	/* Going from empty to nonempty */
	cq->elements = calloc_or_fail(CQ_ALLOC, sizeof(chunk_ptr), "chunk_queue_insert");
	cq->alloc_length = CQ_ALLOC;
    } else if (cq->length == cq->alloc_length) {
	/* Must expand queue.  Reset so that head is at 0 */
	cq->alloc_length *= 2;
	chunk_ptr *nelements = calloc_or_fail(cq->alloc_length, sizeof(chunk_ptr), "chunk_queue_insert");
	size_t i;
	size_t n = cq->length;
	for (i = 0; i < n; i++) {
	    nelements[i] = cq->elements[cq->head++];
	    if (cq->head >= cq->length) {
		cq->head = 0;
	    }
	}
	free_array(cq->elements, cq->alloc_length, sizeof(chunk_ptr));
	cq->elements = nelements;
	cq->tail = n-1;
    }
    cq->tail++;
    if (cq->tail >= cq->length) {
	cq->tail = 0;
    }
    cq->elements[cq->tail] = item;
    cq->length++;
    check_err(pthread_cond_signal(&cq->cvar), "chunk_queue_insert");
    check_err(pthread_mutex_unlock(&cq->mutex), "chunk_queue_insert");
}

size_t chunk_queue_length(chunk_queue_ptr cq) {
    check_err(pthread_mutex_lock(&cq->mutex), "chunk_queue_length");
    size_t len = cq->length;
    check_err(pthread_mutex_unlock(&cq->mutex), "chunk_queue_length");
    return len;
}

/* Remove & return oldest element.  Blocks until queue nonempty */
chunk_ptr chunk_queue_get(chunk_queue_ptr cq) {
    check_err(pthread_mutex_lock(&cq->mutex), "chunk_queue_remove");
    while (cq->length == 0) {
	check_err(pthread_cond_wait(&cq->cvar, &cq->mutex), "chunk_queue_remove");
    }
    chunk_ptr result = cq->elements[cq->head];
    cq->head++;
    if (cq->head >= cq->length) {
	cq->head = 0;
    }
    cq->length--;
    check_err(pthread_mutex_unlock(&cq->mutex), "chunk_queue_remove");
    return result;
}

/* Flush queue and free buffer storage */
void chunk_queue_flush(chunk_queue_ptr cq) {
    check_err(pthread_mutex_lock(&cq->mutex), "chunk_queue_flush");
    cq_flush(cq);
    check_err(pthread_mutex_unlock(&cq->mutex), "chunk_queue_flush");
}
#endif /* QUEUE */
