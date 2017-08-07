PARSE = ./sparse.pl
COLUMNS = ./columnize.pl
MAX = ./max.pl

all: final-sizes.txt max-sizes.txt times.txt ops.txt mem.txt

final-sizes.txt:
	grep "Cudd size" q*/lq*fast*.out | $(PARSE) | $(COLUMNS) > final-sizes.txt

max-sizes.txt:
	grep "Cudd size" q*/lq*slow*.out | $(MAX) | $(PARSE) | $(COLUMNS) > max-sizes.txt

times.txt:
	grep "Delta" q*/lq*fast*.out | $(PARSE) | $(COLUMNS) > times.txt

ops.txt:
	grep "insertions" q*/lq*fast*.out | $(PARSE) | $(COLUMNS) > ops.txt

mem.txt:
	grep "Memory" q*/lq*fast*.out | $(PARSE) | $(COLUMNS) > mem.txt

move:
	-mv -f lq*04*.out q04
	-mv -f lq*08*.out q08
	-mv -f lq*09*.out q09
	-mv -f lq*10*.out q10
	-mv -f lq*11*.out q11
	-mv -f lq*12*.out q12
	-mv -f lq*13*.out q13
	-mv -f lq*14*.out q14
	-mv -f lq*15*.out q15
	-mv -f lq*16*.out q16

clean:
	rm -f *~ *.txt

flush:
	rm -f *.out */*.out