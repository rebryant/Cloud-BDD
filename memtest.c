#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "report.h"

/* 1/2 GB increments */
#define BSIZE 1UL<<29

size_t bcount = 0;

static void memstat() {
    printf("%lu buffers of size %lu each.  Total = %.3f GB\n",
	   bcount, BSIZE, gigabytes(bcount * BSIZE));
    size_t mem = resident_bytes();
    printf("%.3f GB resident\n", gigabytes(mem));
}

int main (int argc, char *argv[]) {
    size_t count = 32;
    size_t i;
    if (argc > 1)
	count = atoi(argv[1]);
    for (i = 0; i < count; i++) {
	void *region = malloc_or_fail(BSIZE, "main");
	if (!region) {
	    printf("Malloc returned NULL\n");
	    memstat();
	    exit(0);
	}
	memset(region, 0x55, BSIZE);
	bcount++;
	memstat();
	sleep(1);
    }
    printf("Completed\n");
    memstat();
    exit(0);
}
