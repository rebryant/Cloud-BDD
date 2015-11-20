/* Default reporting level.  Must recompile when change */
#ifndef RPT
#define RPT 2
#endif

/* Ways to report interesting behavior and errors */

/* Buffer sizes */
#define MAX_CHAR 512

void init_files(FILE *errfile, FILE *verbfile);

extern int verblevel;
void set_verblevel(int level);

/* Maximum number of megabytes that application can use (0 = unlimited) */
extern int mblimit;

/* Maximum number of seconds that application can use.  (0 = unlimited)  */
extern int timelimit;

/* Optional function to call when fatal error encountered */
extern void (*fatal_fun)();

/* Report Errors */
void err(bool fatal, char *fmt, ...);
/* Report useful information */
void report(int verblevel, char *fmt, ...);

/* Like report, but without return character */
void report_noreturn(int verblevel, char *fmt, ...);


/* Simple failure report */
void fail_fun(char *format, char *msg);

/* Attempt to call malloc.  Fail when returns NULL */
void *malloc_or_fail(size_t bytes, char *fun_name);

/* Attempt to call calloc.  Fail when returns NULL */
void *calloc_or_fail(size_t cnt, size_t bytes, char *fun_name);

/* Attempt to call realloc.  Fail when returns NULL */
void *realloc_or_fail(void *old, size_t old_bytes, size_t new_bytes,
		      char *fun_name);

/* Attempt to save string.  Fail when malloc returns NULL */
char *strsave_or_fail(char *s, char *fun_name);

/* Free block, as from malloc, realloc, or strsave */
void free_block(void *b, size_t len);

/* Free array, as from calloc */
void free_array(void *b, size_t cnt, size_t bytes);

/* Free string saved by strsave_or_fail */
void free_string(char *s);

/* Report current allocation status */
void mem_status(FILE *fp);

/** Counters giving peak memory usage **/

/* Never resets */
size_t peak_bytes;

/* Resettable */
size_t last_peak_bytes;

/* Reset last_peak_bytes */
void reset_peak_bytes();

/* Change value of timeout */
void change_timeout(int oldval);

