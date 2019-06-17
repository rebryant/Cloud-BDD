CUDDDIR = ./cudd-symlink
OCUDDDIR = ./cudd-symlink-original

CUDDINC= -I$(CUDDDIR)/include
CUDDLIBS = $(CUDDDIR)/lib/libcudd.a 

OCUDDINC= -I$(OCUDDDIR)/include
OCUDDLIBS = $(OCUDDDIR)/lib/libcudd.a

#CUDDFLAGS = -DSIZEOF_VOID_P=8 -DSIZEOF_LONG=8

VLEVEL=5

CC=gcc
#CFLAGS= -Wall -g -O0 -DRPT=$(VLEVEL)
CFLAGS = -Wall -g -O2 -DRPT=$(VLEVEL)

# Optionally test version with very small hash signatures to stress aliasing code
BDDFLAGS=
#BDDFLAGS=-DSMALL_HASH

PFILES = agent.h bdd.h chunk.h conjunct.h console.h dtype.h msg.h report.h shadow.h table.h \
	agent.c bdd.c bworker.c chunk.c conjunct.c console.c controller.c msg.c report.c \
	router.c runbdd.c shadow.c table.c 

default: runbdd

b: runbdd bworker controller router

dft: controller router tworker tclient

code.pdf: $(PFILES)
	enscript -E -2r $(PFILES) -o code.ps
	ps2pdf code.ps

tests: chunk_test set_test word_set_test chunktable_test shadow_test console_test 

chunk.o: chunk.c chunk.h dtype.h
	$(CC) $(CFLAGS) -c chunk.c

table.o: table.c table.h dtype.h
	$(CC) $(CFLAGS) -c table.c

report.o: report.c report.h
	$(CC) $(CFLAGS) -c report.c

bdd.o: bdd.c dtype.h bdd.h table.h chunk.h report.h msg.h console.h agent.h
	$(CC) $(CFLAGS) $(BDDFLAGS) -c bdd.c

shadow.o: shadow.c shadow.h bdd.h table.h chunk.h report.h console.h agent.h msg.h
	$(CC) $(CFLAGS) $(CUDDFLAGS) $(BDDFLAGS) $(CUDDINC)  -c shadow.c

shadow-nochain.o: shadow.c shadow.h bdd.h table.h chunk.h report.h console.h agent.h msg.h
	$(CC) $(CFLAGS) $(CUDDFLAGS) $(BDDFLAGS) $(CUDDINC) -DNO_CHAINING -c shadow.c -o shadow-nochain.o

conjunct.o: conjunct.c msg.h console.h agent.h bdd.h shadow.h report.h conjunct.h
	$(CC) $(CFLAGS) $(CUDDFLAGS) $(BDDFLAGS) $(CUDDINC)  -c conjunct.c

console.o: console.c console.h report.h
	$(CC) $(CFLAGS) -c console.c

agent.o: agent.c dtype.h table.h chunk.h report.h msg.h console.h agent.h
	$(CC) $(CFLAGS) -c agent.c

msg.o: msg.c table.h chunk.h report.h msg.h
	$(CC) $(CFLAGS) -c msg.c

test_df.o: test_df.c dtype.h table.h chunk.h report.h msg.h console.h agent.h test_df.h
	$(CC) $(CFLAGS) -c test_df.c

chunk_test: chunk_test.c dtype.h table.h chunk.h report.h chunk.o report.o table.o
	$(CC) $(CFLAGS) -o chunk_test chunk_test.c chunk.o report.o table.o

set_test: set_test.c dtype.h table.h report.h table.o report.o
	$(CC) $(CFLAGS) -o set_test set_test.c table.o report.o

word_set_test: word_set_test.c dtype.h table.h report.h table.o report.o
	$(CC) $(CFLAGS) -o word_set_test word_set_test.c table.o report.o

chunktable_test: chunktable_test.c dtype.h table.h chunk.h report.h chunk.o report.o table.o
	$(CC) $(CFLAGS) -o chunktable_test chunktable_test.c chunk.o report.o table.o

shadow_test: shadow_test.c console.o chunk.o table.o report.o bdd.o shadow.o msg.o agent.o
	$(CC) $(CFLAGS) $(BDDFLAGS) $(CUDDINC) -o shadow_test shadow_test.c console.o chunk.o table.o report.o bdd.o shadow.o msg.o agent.o $(CUDDLIBS)

console_test: console_test.c console.h report.h console.o report.o chunk.o table.o
	$(CC) $(CFLAGS) -o console_test console_test.c console.o report.o chunk.o table.o

runbdd: runbdd.c conjunct.o console.o chunk.o table.o report.o bdd.o shadow.o msg.o agent.o
	$(CC) $(CFLAGS) $(CUDDFLAGS) $(BDDFLAGS) $(CUDDINC) -o runbdd runbdd.c chunk.o conjunct.o console.o table.o report.o bdd.o shadow.o msg.o agent.o $(CUDDLIBS) -lm

bworker: bworker.c table.o chunk.o report.o msg.o console.o agent.o bdd.o
	$(CC) $(CFLAGS) -o bworker bworker.c table.o chunk.o report.o msg.o console.o agent.o bdd.o

router: router.c table.o chunk.o report.o msg.o dtype.h table.h chunk.h report.h msg.h
	$(CC) $(CFLAGS) -o router router.c  table.o chunk.o report.o msg.o

tclient: tclient.c table.o chunk.o report.o msg.o console.o agent.o test_df.o dtype.h table.h chunk.h report.h msg.h
	$(CC) $(CFLAGS) -o tclient tclient.c  table.o chunk.o report.o msg.o console.o agent.o test_df.o

tworker: tworker.c table.o chunk.o report.o msg.o console.o agent.o test_df.o dtype.h table.h chunk.h report.h msg.h
	$(CC) $(CFLAGS) -o tworker tworker.c  table.o chunk.o report.o msg.o console.o agent.o test_df.o


controller: controller.c table.o chunk.o report.o msg.o console.o dtype.h table.h chunk.h report.h msg.h console.h
	$(CC) $(CFLAGS) -o controller controller.c  table.o chunk.o report.o msg.o console.o

memtest: memtest.c report.o
	$(CC) $(CFLAGS) -o memtest memtest.c  report.o

clean:
	rm -f *.o *~ *.dat
	rm -rf *.dSYM
	rm -f chunk_test chunktable_test shadow_test console_test set_test
	rm -f runbdd tworker tclient bworker router controller
