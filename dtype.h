/* Common declarations across all parts of the code*/

/* Fundamental data storage unit */
typedef uint64_t word_t;

/* Block of two words.  Used for operation and operand headers */
typedef struct {
    word_t w0;
    word_t w1;
} dword_t;


#define WORD_BYTES (sizeof(word_t))

#define WORD_BITS (8*WORD_BYTES)


