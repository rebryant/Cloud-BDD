#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>
#include "report.h"

#define MAX(a,b) ((a)<(b)?(b):(a))

FILE *errfile = NULL;
FILE *verbfile = NULL;

int verblevel = 0;
void init_files(FILE *efile, FILE *vfile)
{
    errfile = efile;
    verbfile = vfile;
}

void err(bool fatal, char *fmt, ...)
{
  va_list ap;
  if (!errfile)
    init_files(stdout, stdout);
  va_start(ap, fmt);
  fprintf(errfile, "Error: ");
  vfprintf(errfile, fmt, ap);
  fprintf(errfile, "\n");
  fflush(errfile);
  va_end(ap);
  if (fatal)
    exit(1);
}

void set_verblevel(int level)
{
    verblevel = level;
}


void report(int level, char *fmt, ...)
{
  va_list ap;
  if (!verbfile)
    init_files(stdout, stdout);
  if (level <= verblevel) {
    va_start(ap, fmt);
    vfprintf(verbfile, fmt, ap);
    fprintf(verbfile, "\n");
    fflush(verbfile);
    va_end(ap);
  }
}

void report_noreturn(int level, char *fmt, ...)
{
  va_list ap;
  if (!verbfile)
    init_files(stdout, stdout);
  if (level <= verblevel) {
    va_start(ap, fmt);
    vfprintf(verbfile, fmt, ap);
    fflush(verbfile);
    va_end(ap);
  }
}

/* Functions denoting failures */

/* General failure */
void fail_fun(char *format, char *msg) {
  err(false, format, msg);
}

/* Keeping track of memory allocation */
static size_t allocate_cnt = 0;
static size_t allocate_bytes = 0;
static size_t free_cnt = 0;
static size_t free_bytes = 0;
static size_t peak_bytes = 0;
static size_t current_bytes = 0;


/* Call malloc & exit if fails */
void * malloc_or_fail(size_t bytes, char *fun_name) {
  void *p = malloc(bytes);
  if (!p) {
    fail_fun("Malloc returned NULL in %s", fun_name);
    return NULL;
  }
  allocate_cnt++;
  allocate_bytes += bytes;
  current_bytes += bytes;
  peak_bytes = MAX(peak_bytes, current_bytes);
  return p;
}

/* Call calloc returns NULL & exit if fails */
void *calloc_or_fail(size_t cnt, size_t bytes, char *fun_name) {
  void *p = calloc(cnt, bytes);
  if (!p) {
    fail_fun("Calloc returned NULL in %s", fun_name);
    return NULL;
  }
  allocate_cnt++;
  allocate_bytes += cnt * bytes;
  current_bytes += cnt * bytes;
  peak_bytes = MAX(peak_bytes, current_bytes);

  return p;
}

/* Call realloc returns NULL & exit if fails.  Require explicit indication of current allocation */
void * realloc_or_fail(void *old, size_t old_bytes, size_t new_bytes, char *fun_name) {
  void *p = realloc(old, new_bytes);
  if (!p) {
    fail_fun("Realloc returned NULL in %s", fun_name);
    return NULL;
  }
  allocate_cnt++;
  allocate_bytes += new_bytes;
  current_bytes += (new_bytes-old_bytes);
  peak_bytes = MAX(peak_bytes, current_bytes);
  free_cnt++;
  free_bytes += old_bytes;
  return p;
}

char *strsave_or_fail(char *s, char *fun_name) {
  if (!s)
    return NULL;
  int len = strlen(s);
  char *ss = malloc(len+1);
  if (!ss) {
    fail_fun("strsave failed in %s", fun_name);
  }
  allocate_cnt++;
  allocate_bytes += len+1;
  current_bytes += len+1;
  peak_bytes = MAX(peak_bytes, current_bytes);

  return strcpy(ss, s);
}

/* Free block, as from malloc, realloc, or strsave */
void free_block(void *b, size_t bytes) {
    if (b == NULL) {
	err(0, "Attempting to free null block");
    }
    free(b);
    free_cnt++;
    free_bytes += bytes;
    current_bytes -= bytes;
}

/* Free array, as from calloc */
void free_array(void *b, size_t cnt, size_t bytes) {
    if (b == NULL) {
	err(0, "Attempting to free null block");
    }
    free(b);
    free_cnt++;
    free_bytes += cnt * bytes;
    current_bytes -= cnt * bytes;

}

/* Free string saved by strsave_or_fail */
void free_string(char *s) {
    if (s == NULL) {
	err(0, "Attempting to free null block");
    }
    free_block((void *) s, strlen(s)+1);
}


/* Report current allocation status */
void mem_status(FILE *fp) {
    fprintf(fp, "Allocated cnt/bytes: %lu/%lu.  Freed cnt/bytes: %lu/%lu.  Peak bytes %lu, Current bytes %ld\n",
	    (long unsigned) allocate_cnt, (long unsigned) allocate_bytes,
	    (long unsigned) free_cnt, (long unsigned) free_bytes,
	    (long unsigned) peak_bytes, (long) current_bytes);
}
