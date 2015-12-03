#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "report.h"

/* 1/2 GB increments */
#define BSIZE 1UL<<29

size_t bcount = 0;

double gb(size_t n) {
    return (double) n / (1UL << 30);
}

static void memstat() {
    struct rusage r;
    printf("%lu buffers of size %lu each.  Total = %.3f GB\n",
	   bcount, BSIZE, gb(bcount * BSIZE));
    int code = getrusage(RUSAGE_SELF, &r);
    if (code < 0) {
	printf("Call to getrusage failed\n");
    } else {
	size_t mem = r.ru_maxrss * 1024;
	printf("%.3f GB resident\n", gb(mem));
    }
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
}
