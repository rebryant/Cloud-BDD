PARSE = ./sparse.pl
COLUMNS = ./columnize.pl
MAX = ./max.pl
LQRUN = ./lqrun.py

all: runbdd benchmarks run move report

mini: runbdd benchmarks mini-run move report

runbdd:
	(cd ../../ ; make runbdd)

benchmarks:
	(cd ../../scripts; make lq)

run: $(LQRUN)
	$(LQRUN) -N 15

mini-run: $(LQRUN)
	$(LQRUN) -N 10

report: final-sizes-lq.txt max-sizes-lq.txt times-lq.txt ops-lq.txt mem-lq.txt \
	final-sizes-iq.txt max-sizes-iq.txt times-iq.txt ops-iq.txt mem-iq.txt

## Regular variable ordering

final-sizes-lq.txt:
	grep "Cudd size" q*/lq*fast*.out | $(PARSE) | $(COLUMNS) > final-sizes-lq.txt

max-sizes-lq.txt:
	grep "Cudd size" q*/lq*slow*.out | $(MAX) | $(PARSE) | $(COLUMNS) > max-sizes-lq.txt

times-lq.txt:
	grep "Delta" q*/lq*fast*.out | $(PARSE) | $(COLUMNS) > times-lq.txt

ops-lq.txt:
	grep "insertions" q*/lq*fast*.out | $(PARSE) | $(COLUMNS) > ops-lq.txt

mem-lq.txt:
	grep "Memory" q*/lq*fast*.out | $(PARSE) | $(COLUMNS) > mem-lq.txt

## Interleaved variable ordering

final-sizes-iq.txt:
	grep "Cudd size" q*/iq*fast*.out | $(PARSE) | $(COLUMNS) > final-sizes-iq.txt

max-sizes-iq.txt:
	grep "Cudd size" q*/iq*slow*.out | $(MAX) | $(PARSE) | $(COLUMNS) > max-sizes-iq.txt

times-iq.txt:
	grep "Delta" q*/iq*fast*.out | $(PARSE) | $(COLUMNS) > times-iq.txt

ops-iq.txt:
	grep "insertions" q*/iq*fast*.out | $(PARSE) | $(COLUMNS) > ops-iq.txt

mem-iq.txt:
	grep "Memory" q*/iq*fast*.out | $(PARSE) | $(COLUMNS) > mem-iq.txt

move:
	-mkdir q04
	-mv -f *q*04*.out q04
	-mkdir q08
	-mv -f *q*08*.out q08
	-mkdir q09
	-mv -f *q*09*.out q09
	-mkdir q10
	-mv -f *q*10*.out q10
	-mkdir q11
	-mv -f *q*11*.out q11
	-mkdir q12
	-mv -f *q*12*.out q12
	-mkdir q13	
	-mv -f *q*13*.out q13
	-mkdir q14
	-mv -f *q*14*.out q14
	-mkdir q15
	-mv -f *q*15*.out q15
	-mkdir q16
	-mv -f *q*16*.out q16

clean:
	rm -f *~ *.txt

flush:
	rm -f *.out */*.out