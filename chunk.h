/******************************************************************************
Data structure for representing data as a sequence of 64-bit words
******************************************************************************/

/* What kind of correctness checks should be applied by package.  */
/* Possible levels:
   0: Nothing.
   1: Allocation/deallocation.
   2: Null pointer detection & bounds checking.
      Make sure don't exceed maximum length constraints
   3: Overlap checking.
   Invalid to insert data into chunk position that is already filled.
 */
unsigned chunk_check_level;


/*
  Chunk represents data as a sequence of words.
*/

typedef struct {
    /* Number of data words in chunk.  Maximum value = 64 */
    size_t length;
#ifdef VMASK
    /* Bit vector indicating which words of this chunk are valid */
    word_t vmask;    
#endif
    word_t words[1]; /* First data word. */
} chunk_t, *chunk_ptr;

/* Set parameter limiting maximum allowable chunk length. */
#ifdef VMASK
/* When chunk includes valid mask, limited by mask length */
#define CHUNK_MAX_LENGTH WORD_BITS
#else
/* Otherwise, limit is arbitrary */
#define CHUNK_MAX_LENGTH 1024
#endif

/* Total number of bytes in longest possible chunk */
#define CHUNK_MAX_SIZE  \
 (sizeof(chunk_t) + sizeof(word_t) * ((CHUNK_MAX_LENGTH) - 1))

/* Create a new chunk */
chunk_ptr chunk_new(size_t len);

/* Free a chunk */
void chunk_free(chunk_ptr cp);

/* Replicate a chunk */
chunk_ptr chunk_clone(chunk_ptr cp);

/* Test whether all words of chunk are valid */
bool chunk_filled(chunk_ptr cp);

/* Insert word into chunk */
void chunk_insert_word(chunk_ptr cp, word_t wd, size_t offset);

/* Replace word into chunk */
void chunk_replace_word(chunk_ptr cp, word_t wd, size_t offset);

/* Get word from chunk */
word_t chunk_get_word(chunk_ptr cp, size_t offset);

/* Insert words from source chunk into destination chunk with designated offset */
void chunk_insert_chunk(chunk_ptr cdestp, chunk_ptr csrcp, size_t offset);

/* Extract subchunk */
chunk_ptr chunk_get_chunk(chunk_ptr cp, size_t offset, size_t length);

/* File I/O based on low-level Unix file descriptors.
   These can be files or network connections */
/* Read chunk from file.  Return null pointer if fail.
   Set flag if EOF encountered.
   This is legacy code - no buffering is used AT ALL.
   Do not intersperse with the chunk_read functions below!
 */

chunk_ptr chunk_read_legacy(int fd, bool *eofp);

/* Write chunk to file */
/* Return true if successful, false if failed */
bool chunk_write(int fd, chunk_ptr cp);

/* Convert a string into a chunk */
chunk_ptr str2chunk(char *s);

/* Get string that has been stored as chunk */
char * chunk2str(chunk_ptr cp);

/***** Key/value tables for chunks *****/

/* Hash function suitable for use in keyvalue table */
size_t chunk_hash(word_t vcp);

/* Equality function suitable for use in keyvalue table */
bool chunk_equal(word_t vcp1, word_t vcp2);

/* Create a keyvalue table mapping chunks to words */
keyvalue_table_ptr chunk_table_new();

#ifdef QUEUE

/*****  FIFO Queue for Chunks *****/
/* This queue is fully synchronized for use by multiple threads */
typedef struct {
    /* How many elements are in the queue? */
    size_t length;
    /* How large is the circular buffer */
    size_t alloc_length;
    /* Index of last element inserted */
    size_t tail;
    /* Index of next element to be removed */
    size_t head;
    /* Array of chunk pointers, managed as circular buffer */
    chunk_ptr *elements;
    /* Synchronization variables */
    pthread_mutex_t mutex;
    pthread_cond_t cvar;
} chunk_queue, *chunk_queue_ptr;

chunk_queue_ptr chunk_queue_new();

void chunk_queue_free();

void chunk_queue_insert(chunk_queue_ptr cq, chunk_ptr item);

size_t chunk_queue_length(chunk_queue_ptr cq);

/* Remove & return oldest element.  Blocks until queue is nonempty */
chunk_ptr chunk_queue_get(chunk_queue_ptr cq);

/* Flush queue and free buffer storage */
void chunk_queue_flush(chunk_queue_ptr cq);
#endif /* QUEUE */

/***** More specialized capabilities ****/
typedef void (*err_fun)(void);

/* Set error function to be called when error detected */
void chunk_at_error(err_fun f);

/* internal buffer implementation for buffered reading. */

typedef struct buffer_node {
  char* buf;
  int location;
  int length;
  int fd;
  struct buffer_node* next;
} buf_node;

extern buf_node* buf_list_head;


/*** DO NOT INTERSPERSE THESE FUNCTIONS WITH THE STANDARD select()
     AND chunk_read_legacy()!
***/

/* A version of select that takes into account whether there are chunks waiting
   in one or more buffers, and blocks only when needed */
int buf_select(int nfds, fd_set *readfds, fd_set *writefds,
	       fd_set *exceptfds, struct timeval *timeout);

/* default read operation. sets up a buffer for the fd if necessary,
   fills it if possible, then reads a chunk from the buffer,
   refilling it as necessary */
chunk_ptr chunk_read(int fd, bool *eofp);

/* read operation that does not fill the buffer at random.
   This provides PSEUDO-UNBUFFERED reading if you are worried about calling
   read without necessarily knowing if the socket has input for you
   i.e., if you're reading without calling buf_select().
*/

chunk_ptr chunk_read_unbuffered(int fd, bool *eofp);

/* Frees all buffers associated with file descriptors. Call when exiting.
 */
void chunk_deinit();
