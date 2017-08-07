PARSE = ./sparse.pl
COLUMNS = ./columnize.pl

all: sizes.txt times.txt ops.txt mem.txt ops-detailed.txt sizes-okRCD.txt

sizes.txt:
	grep "Cudd size" q*/q*slow*.out | $(PARSE) -l okC:okRC:okD:okRCD:okO:ok | $(COLUMNS) > sizes.txt

sizes-okRCD.txt: sizes.txt
	grep okRCD < sizes.txt > sizes-okRCD.txt

times.txt:
	grep "Delta" q*/q*fast*.out | $(PARSE) | $(COLUMNS) > times.txt

ops.txt:
	grep "insertions" q*/q*fast*.out | $(PARSE) | $(COLUMNS) > ops.txt

ops-detailed.txt:
	grep "insertions" q*/q*slow*.out | $(PARSE) -l okC:okRC:okD:okRCD:okO:ok | $(COLUMNS) > ops-detailed.txt


mem.txt:
	grep "Memory" q*/q*fast*.out | $(PARSE) | $(COLUMNS) > mem.txt

move:
	-mv -f q*08*.out q08
	-mv -f q*09*.out q09
	-mv -f q*10*.out q10
	-mv -f q*11*.out q11
	-mv -f q*12*.out q12
	-mv -f q*13*.out q13
	-mv -f q*14*.out q14
	-mv -f q*15*.out q15
	-mv -f q*16*.out q16

clean:
	rm -f *~ *.txt

flush:
	rm -f *.out */*.out