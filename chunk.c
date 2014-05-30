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
#include <error.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>

#include "dtype.h"
#include "table.h"
#include "chunk.h"
#include "report.h"


/* Some macros */
#ifdef VMASK
#define CHUNK_WORD_VALID(cp, idx) ((((cp)->vmask) >> (idx)) & 0x1)

#define CHUNK_ADD_WORD_VALID(cp, idx) ((cp)->vmask | (0x1llu << (idx)))
#endif

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

/* Buffering reads */
buf_node* buf_list_head = NULL;

static int bufferReadBool = 1;

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
#ifdef VMASK
    cp->vmask = 0;
#endif
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
#ifdef VMASK
    ncp->vmask = cp ->vmask;
#endif
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
#ifdef VMASK
    word_t mask = cp->vmask;
    word_t len = cp->length;
    /* Create checking mask */
    word_t cmask = len == CHUNK_MAX_LENGTH ? ~0ULL : (1ULL << len) - 1;
    return mask == cmask;
#else
    /* Can't determine validity without vmask */
    return false;
#endif
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
#ifdef VMASK
    if (chunk_check_level >= 3 && CHUNK_WORD_VALID(cp, offset)) {
	char buf[128];
	sprintf(buf, "Insertion into occupied word.  Offset %u", (unsigned) offset);
	chunk_error(buf, cp);
	return;
    }
#endif
    cp->words[offset] = wd;
#ifdef VMASK
    cp->vmask = CHUNK_ADD_WORD_VALID(cp, offset);
#endif
}

/* Replace word in chunk */
void chunk_replace_word(chunk_ptr cp, word_t wd, size_t offset) {
    if (cp == NULL && chunk_check_level >= 2) {
	chunk_error("Null Pointer", cp);
	return;
    }
    if (chunk_check_level >= 2 && offset >= cp->length) {
	chunk_error("Out of bounds insertion", cp);
	return;
    }
    cp->words[offset] = wd;
#ifdef VMASK
    cp->vmask = CHUNK_ADD_WORD_VALID(cp, offset);
#endif
}

/* Get word from chunk */
word_t chunk_get_word(chunk_ptr cp, size_t offset) {
    if (cp == NULL && chunk_check_level >= 2) {
	chunk_error("Null Pointer", cp);
    }
    if (chunk_check_level >= 2 && offset >= cp->length) {
	err(false, "Out of bounds retrieval.  Length %lu, offset %lu",
	    cp->length, offset);
    }
#ifdef VMASK
    if (chunk_check_level >= 3 && !CHUNK_WORD_VALID(cp, offset)) {
	chunk_error("Retrieval of invalid word", cp);
    }
#endif
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
#ifdef VMASK
	if (CHUNK_WORD_VALID(csrcp, i))
	    chunk_insert_word(cdestp, csrcp->words[i], i + offset);
#else
	chunk_insert_word(cdestp, csrcp->words[i], i + offset);
#endif
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
chunk_ptr chunk_read_legacy(int fd, bool *eofp) {
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

static fd_set buf_set;
static fd_set in_set;
static int maxfd = 0;

int buf_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    int returnVal;
    if (nfds > maxfd)
    {
        // on first call, we zero the buffer set and inset
        if (maxfd == 0)
        {
            FD_ZERO(&buf_set);
            FD_ZERO(&in_set);
        }
        maxfd = nfds - 1;
    }
    report(3, "maxfd: %d, nfds: %d", maxfd, nfds);
    // if buffered, we do non-blocking select and make sure the returned
    // set sets both buffered and readable set
    // if no buffered input is waiting, we do a blocking select and
    // return the readable set
    int isBuffered = 0;
    int i;
    for (i = 0; i < nfds && isBuffered == 0; i++)
    {
        if (FD_ISSET(i, &buf_set))
        {
            isBuffered = 1;
        }
    }

    if (!isBuffered)
    {
        report(3, "unbuffered select on up through %d", maxfd);
        returnVal = select(maxfd+1, readfds, writefds, exceptfds, timeout);
        for (i = 0; i < maxfd+1; i++)
        {
            if (!FD_ISSET(i, readfds))
            {
                FD_CLR(i, &in_set);
            }
            else
            {
                FD_SET(i, &in_set);
            }
        }
    }
    else
    {
        struct timeval zeroval;
        zeroval.tv_sec = (long int)0;
        zeroval.tv_usec = (long int)0;

        FD_ZERO(&in_set);
        for (i = 0; i < maxfd+1 ; i++)
        {
            if (FD_ISSET(i, readfds))
            {
                FD_SET(i, &in_set);
            }
        }

        report(3, "buffered select on up through %d", maxfd);
        returnVal = select(maxfd+1, &in_set, writefds, exceptfds, (timeout == NULL ? &zeroval : timeout));

        if (returnVal >= 0)
            returnVal = 0;
        for (i = 0; i < maxfd+1; i++)
        {
            if (!FD_ISSET(i, &in_set) && !FD_ISSET(i, &buf_set))
            {
                FD_CLR(i, readfds);
            }
            else
            {
                if (returnVal >= 0)
                    returnVal++;
                FD_SET(i, readfds);
            }
        }

    }
    report(3, "leaving buf_select with returnval %d\n", returnVal);
    return returnVal;
}

static void toggle_buffered_in_set(buf_node* curr_node)
{
    if (curr_node->length > 0)
    {
        FD_SET(curr_node->fd, &buf_set);
    }
    else
    {
        FD_CLR(curr_node->fd, &buf_set);
    }
}

static ssize_t buf_read(buf_node* curr_node, bool* eofp, unsigned char* buf, int len)
{
    int cnt = 0;
    int copyLen = 0;
    while (cnt < len)
    {
        report(3, "waiting for %d bytes", (len - cnt));
        //if there's stuff in the buffer, copy it over
        if (curr_node->length > 0)
        {
            copyLen = ((len - cnt) < curr_node->length ? (len - cnt) : curr_node->length);
            report(3, "copying a buffer of length %d from total length %d, to the return buffer", copyLen, curr_node->length);
            report(3, "old length = %d, new length = %d", curr_node->length, curr_node->length - copyLen);
            memcpy((buf + cnt), ((curr_node->buf) + (curr_node->location)), copyLen);
            cnt = cnt + copyLen;
            curr_node->length = curr_node->length - copyLen;
            if (curr_node->length == 0)
            {
                curr_node->location = 0;
            }
            else
            {
                curr_node->location = curr_node->location + copyLen;
            }
            report(3, "new location: %d\n", curr_node->location);

        }
        //otherwise, we refill the buffer
        else
        {
            report(3, "fill the saved buffer!");
            ssize_t n = read(curr_node->fd, ((curr_node->buf) + (curr_node->location) + (curr_node->length)), CHUNK_MAX_SIZE);
            if (n < 0) {
                chunk_error("Failed read", NULL);
                if (eofp)
                    *eofp = false;
                toggle_buffered_in_set(curr_node);
                return n;
            }
            if (n == 0) {
                if (eofp)
                    *eofp = true;
                else
                    chunk_error("Unexpected EOF", NULL);
                toggle_buffered_in_set(curr_node);
                return n;
            }
            curr_node->length = curr_node->length + n;
            report(3, "added %d bytes to the saved buffer; length is now %d at location %d\n", n, curr_node->length, curr_node->location);
        }
    }


    toggle_buffered_in_set(curr_node);
    return (ssize_t)cnt;
}

chunk_ptr chunk_read(int fd, bool* eofp)
{
    if (fd > maxfd)
    {
        // on first call, we zero the buffer set
        if (maxfd == 0)
        {
            FD_ZERO(&buf_set);
            FD_ZERO(&in_set);
        }
        maxfd = fd;
    }

    buf_node* curr_node = NULL;
    buf_node* temp_node = NULL;
    //create new head
    if (buf_list_head == NULL) {
        buf_list_head = calloc_or_fail(sizeof(buf_node), 1, "chunk_read create head");
        buf_list_head->fd = fd;
        report(3, "created a node for fd %d as head\n", fd);
        buf_list_head->length = 0;
        buf_list_head->location = 0;
        buf_list_head->buf = calloc_or_fail(CHUNK_MAX_SIZE, 2, "chunk_read create head buf");
        curr_node = buf_list_head;
    }
    // search for the fd in the buffer list, if it exists
    else {
        temp_node = buf_list_head;
        while (temp_node != NULL && curr_node == NULL) {
            if (fd == temp_node->fd) {
                curr_node = temp_node;
                report(3, "found node for fd %d\n", fd);
            }
            temp_node = temp_node->next;
        }
    }
    // if it doesn't exist, create the new fd buffer at the head of the list
    if (curr_node == NULL) {
        curr_node = calloc_or_fail(sizeof(buf_node), 1, "chunk_read create node");
        curr_node->fd = fd;
        curr_node->length = 0;
        curr_node->location = 0;
        curr_node->next = buf_list_head;
        curr_node->buf = calloc_or_fail(CHUNK_MAX_SIZE, 2, "chunk_read create head buf");
        report(3, "created a node for fd %d at head\n", fd);
        buf_list_head = curr_node;
    }

    // if we can copy to the beginning, then we copy to the beginning
    // (if the read point is past the beginning, and if the end of
    // the buffered data is past the midway point of the buffer)
    if (curr_node->length + curr_node->location >= CHUNK_MAX_SIZE && curr_node->location > 0)
    {
        memmove(curr_node->buf, (char *)((curr_node->buf + curr_node->location)), curr_node->length);
        curr_node->location = 0;
    }

    // read if possible - if there is space, if the inset contains it, and if we
    // want to use buffering (otherwise we don't want random buffer refills)
    if (((curr_node->length + curr_node->location) < CHUNK_MAX_SIZE) && bufferReadBool && !(!(FD_ISSET(fd, &in_set))) )
    {
        report(3, "reading for %d\n", curr_node->fd);
        ssize_t n = read(curr_node->fd, ((curr_node->buf) + (curr_node->location) + (curr_node->length)), CHUNK_MAX_SIZE);
        curr_node->length += n;
    }

    report(3, "about to get header for %d\n", fd);
    // get header of chunk
    size_t need_cnt = sizeof(chunk_t);
    unsigned char buf[CHUNK_MAX_SIZE];
    unsigned char* buf_ptr = (unsigned char*)buf;
    ssize_t n = buf_read(curr_node, eofp, buf_ptr, need_cnt);
    //ssize_t n = read(curr_node->fd, buf, need_cnt);
    if (n <= 0)
    {
	return NULL;
    }

    report(3, "about to get rest of chunk for fd %d\n", fd);
    // get rest of chunk
    chunk_ptr creadp = (chunk_ptr) buf_ptr;
    size_t len = creadp->length;
    report(3, "len needed: %d", len);
    if (len > 1) {
	need_cnt = WORD_BYTES * (len - 1);
        report(3, "head buf pointer at %p", buf_ptr);
        buf_ptr = (unsigned char *)(buf_ptr + n);
        report(3, "moved pointer to %p for rest", buf_ptr);
        ssize_t n = buf_read(curr_node, eofp, buf_ptr, need_cnt);
        //ssize_t n = read(curr_node->fd, buf_ptr, need_cnt);

        if (n < 0) {
            chunk_error("Failed read", NULL);
	    if (eofp)
		*eofp = false;
            return NULL;
        }
    }

    report(3, "exiting chunk_read_buffered_builtin!\n");
    if (eofp)
	*eofp = false;
    return chunk_clone(creadp);
}

chunk_ptr chunk_read_unbuffered(int fd, bool *eofp)
{
    bufferReadBool = 0;
    chunk_ptr p = chunk_read(fd, eofp);
    bufferReadBool = 1;
    return p;
}

void chunk_deinit()
{
    buf_node* temp_node = buf_list_head;
    //create new head
    while (temp_node != NULL) {
        buf_list_head = temp_node;
        temp_node = temp_node->next;
        if (buf_list_head->buf != NULL)
            free_block(buf_list_head->buf, 2*CHUNK_MAX_SIZE*sizeof(char));
        else
            chunk_error("Chunk buffer was null in a buffer list node", NULL);

        free_block(buf_list_head, sizeof(buf_node));
    }

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
#ifdef VMASK
    size_t result = wordarray_hash(cp->words, cp->length, cp->vmask);
#else
    size_t result = wordarray_hash(cp->words, cp->length);
#endif
    return result;
}

/* Equality function suitable for use in keyvalue table */
bool chunk_equal(word_t vcp1, word_t vcp2) {
    chunk_ptr cp1 = (chunk_ptr) vcp1;
    chunk_ptr cp2 = (chunk_ptr) vcp2;
    if (cp1 == NULL)
	return (cp2 == NULL);
    bool ok = true;
#ifdef VMASK
    word_t vmask1 = cp1->vmask;
    word_t vmask2 = cp2->vmask;
    size_t i;
    /* OK for chunks to have different lengths, as long as mask bits cover same words */
    size_t len = cp1->length < cp2->length ? cp1->length : cp2->length;
    for (i = 0; ok && i < len; i++) {
	int check1 = vmask1 & 0x1;
	int check2 = vmask2 & 0x1;
	if (check1 != check2) {
	    ok = false;
	}
	if (ok && check1 && cp1->words[i] != cp2->words[i]) {
	    ok = false;
	}
	vmask1 >>= 1;
	vmask2 >>= 1;
    }
    ok = ok && vmask1 == 0 && vmask2 == 0;
#else
    size_t i;
    size_t len = cp1->length;
    if (len != cp2->length)
	ok = false;
    for (i = 0; ok && i < len; i++) {
	if (cp1->words[i] != cp2->words[i])
	    ok = false;
    }
#endif /* VMASK */
    return ok;
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
