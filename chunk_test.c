/* Test code for chunk */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdbool.h>

#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "dtype.h"
#include "table.h"
#include "chunk.h"
#include "report.h"

static void efun(void) {
    err(true, "Error encountered.  Exiting\n");
}


/* Generate random string up to some maximum length */
char *rstring(size_t maxlen) {
    size_t len = random() % (maxlen + 1);
    char *s = malloc_or_fail(len+1, "rstring");
    size_t i;
    for (i = 0; i < len; i++) {
	char c = 'a' + random() % 26;
	s[i] = c;
    }
    s[i] = '\0';
    return s;
}


/* Test ability to convert string into chunk and back again.
*/
static void to_from_test(char *s, chunk_ptr cp) {
    char *t = chunk2str(cp);
    if (strcmp(s, t) != 0) {
	err(false, "to_from mismatch. '%s' --> '%s'", s, t);
    } else {
	report(2, "to_from success. '%s' --> '%s'", s, t);
    }
    free_string(t);
}

/* Test ability to clone chunks */
static void clone_test(char *s, chunk_ptr cp) {
    chunk_ptr ccp = chunk_clone(cp);
    char *t = chunk2str(ccp);
    if (strcmp(s, t) != 0) {
	err(false, "clone mismatch. '%s' --> '%s'", s, t);
    } else {
	report(2, "clone success. '%s' --> '%s'", s, t);
    }
    chunk_free(ccp);
    free_string(t);
}

/* Test ability to reconstruct chunk from single & double word parts */
static void reconstruct_test(char *s, chunk_ptr cp) {
    size_t len = cp->length;
    chunk_ptr ncp = chunk_new(len);
    size_t i = 0;
    while (i < len) {
	if (i < len-1) {
	    dword_t w = chunk_get_dword(cp, i);
	    chunk_insert_dword(ncp, w, i);
	    i++;
	} else {
	    word_t w = chunk_get_word(cp, i);
	    chunk_insert_word(ncp, w, i);
	    i++;
	}
    }
    char *t = chunk2str(ncp);
    if (strcmp(s, t) != 0) {
	err(false, "reconstruct mismatch. '%s' --> '%s'", s, t);
    } else {
	report(2, "reconstruct success. '%s' --> '%s'", s, t);
    }
    chunk_free(ncp);
    free_string(t);
}

/* Test ability to construct chunk from subchunks */
static void reassemble_test(char *s, chunk_ptr cp) {
    size_t *split_pos = calloc_or_fail(cp->length+1, sizeof(size_t), "reassemble_test");
    size_t sidx;
    size_t cnt = 0;
    for (sidx = 0; sidx < cp->length; sidx += 1 + random() % (cp->length - sidx)) {
	split_pos[cnt++] = sidx;
    }
    split_pos[cnt] = cp->length;
    chunk_ptr *scp = calloc_or_fail(cnt, sizeof(chunk_ptr), "reassemble_test");
    size_t idx;
    /* Break into cnt chunks */
    for (idx = 0; idx < cnt; idx++) {
	size_t len = split_pos[idx+1] - split_pos[idx];
	if (len > cp->length) {
	    err(true, "Impossible length %lu.  idx = %lu, split_pos[idx] = %lu, split_pos[idx+1] = %lu\n",
		len, idx, split_pos[idx], split_pos[idx+1]);
	}
	scp[idx] = chunk_get_chunk(cp, split_pos[idx], len);
	char *ss = chunk2str(scp[idx]);
	report(2, "Created:\tOffset %lu\tLength %lu\tString '%s'", split_pos[idx], len, ss);
	free_string(ss);
    }
    /* Reassemble */
    chunk_ptr ncp = chunk_new(cp->length);
    for (idx = 0; idx < cnt; idx++) {
	chunk_insert_chunk(ncp, scp[idx], split_pos[idx]);
    }
    char *t = chunk2str(ncp);
    if (strcmp(s, t) != 0) {
	report(1, "Split chunk into parts:");
	for (idx = 0; idx < cnt; idx++) {
	    char *ss = chunk2str(scp[idx]);
	    report(1, "\tOffset %lu\tLength %lu\tString '%s'", split_pos[idx], split_pos[idx+1] - split_pos[idx], ss);
	    free_string(ss);
	}
	err(false, "reassemble mismatch. '%s' --> '%s'", s, t);
    } else {
	report(2, "Split chunk into parts:");
	for (idx = 0; idx < cnt; idx++) {
	    char *ss = chunk2str(scp[idx]);
	    report(2, "\tOffset %lu\tLength %lu\tString '%s'", split_pos[idx], split_pos[idx+1] - split_pos[idx], ss);
	    free_string(ss);
	}
	report(2, "reassemble success. '%s' --> '%s'", s, t);
    }
    for (idx = 0; idx < cnt; idx++) {
	chunk_free(scp[idx]);
    }
    chunk_free(ncp);
    free_string(t);
    free_array(scp, cnt, sizeof(chunk_ptr));
    free_array(split_pos, cp->length+1, sizeof(size_t));
}

/* Test ability to write and read chunks as files */
static void write_read_test(char *s, chunk_ptr cp) {
    char *fname = "chunk_test.dat";
    int fd = open(fname, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
    if (fd < 0) {
	err(true, "Couldn't open file '%s' for write", fname);
    }
    if (!chunk_write(fd, cp)) {
	err(true, "Couldn't write chunk to file '%s'", fname);
    }
    if (close(fd) < 0) {
	err(true, "Couldn't close file '%s' after writing", fname);
    }
    fd = open(fname, O_RDONLY, 0);
    if (fd < 0) {
	err(true, "Couldn't open file '%s' for read", fname);
    }
    chunk_ptr rcp = chunk_read(fd, NULL);
    if (rcp == NULL) {
	err(true, "Couldn't read chunk from file '%s'", fname);
    }
    if (close(fd) < 0) {
	err(true, "Couldn't close file '%s' after reading", fname);
    }
    char *t = chunk2str(rcp);
    if (strcmp(s, t) != 0) {
	err(false, "write/read mismatch. '%s' --> '%s'", s, t);
    } else {
	report(2, "write/read success. '%s' --> '%s'", s, t);
    }
    chunk_free(rcp);
    free_string(t);
}



void test_string(int maxlen) {
    char *s = rstring(maxlen);
    chunk_ptr cp = str2chunk(s);
    size_t h = chunk_hash((word_t) cp);
    report(2, "Random string '%s'.  Hashes to 0x%lx", s, h);
    to_from_test(s, cp);
    reconstruct_test(s, cp);
    clone_test(s, cp);
    reassemble_test(s, cp);
    write_read_test(s, cp);
    chunk_free(cp);
    free_string(s);
}

int usage(char *cmd) {
    printf("Usage: %s [-h] [-n NTEST] [-m MLEN] [-v VERB]\n", cmd);
    printf("\t-h      \tSet number of tests\n");
    printf("\t-n NTEST\tSet number of tests\n");
    printf("\t-m MLEN \tSet maximum string length\n");
    printf("\t-v VERB \tSet verbosity level\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    chunk_at_error(efun);
    chunk_check_level = 3;
    verblevel = 3;
    int tcount = 10;
    int maxlen = 50;
    int c;
    while ((c = getopt(argc, argv, "hv:n:m:")) != -1) {
	switch(c) {
	case 'h':
	    usage(argv[0]);
	    break;
	case 'v':
	    verblevel = atoi(optarg);
	    break;
	case 'n':
	    tcount = atoi(optarg);
	    break;
	case 'm':
	    maxlen = atoi(optarg);
	    break;
	default:
	    printf("Unrecognized option '%c'\n", c);
	    usage(argv[0]);
	}
    }
    size_t i;
    for (i = 0; i < tcount; i++)
	test_string(maxlen);
    printf("Completed %d tests\n", tcount);
    mem_status(stdout);
    return 0;
}
