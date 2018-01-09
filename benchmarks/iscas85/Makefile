PARSE = ./sparse.pl

all: circuits runs tables

circuits: iscparse.py
	./iscparse.py -A

runs: iscrun.py
	./iscrun.py -i c17:c432:c499:c880:c1355:c1908:c2670:c3540:c5315 -r -b -z -a
	./iscrun.py -i c7552 -R -b -z -a
	./iscrun.py -i c6288 -b -z -a

tables: final-sizes.txt times.txt ops.txt mem.txt

final-sizes.txt:
	grep "Cudd size" *.out | $(PARSE) > final-sizes.txt

times.txt:
	grep "Delta" *.out | $(PARSE) > times.txt

ops.txt:
	grep "insertions" *.out | $(PARSE) > ops.txt

mem.txt:
	grep "Memory" *.out | $(PARSE) > mem.txt

clean:
	rm -f *.txt *~ *.pyc

