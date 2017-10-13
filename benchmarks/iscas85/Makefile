PARSE = ./sparse.pl

tables: final-sizes.txt times.txt ops.txt mem.txt

final-sizes.txt:
	grep "Cudd size" */*.out | $(PARSE) > final-sizes.txt

times.txt:
	grep "Delta" */*.out | $(PARSE) > times.txt

ops.txt:
	grep "insertions" */*.out | $(PARSE) > ops.txt

mem.txt:
	grep "Memory" */*.out | $(PARSE) > mem.txt

move:
	-mv c17*.out out-c17
	-mv c432*.out out-c432
	-mv c499*.out out-c499
	-mv c880*.out out-c880
	-mv c1355*.out out-c1355
	-mv c1908*.out out-c1908
	-mv c2670*.out out-c2670
	-mv c3540*.out out-c3540
	-mv c5315*.out out-c5315
	-mv c6288*.out out-c6288
	-mv c7552*.out out-c7552
