ANA = ../compress-literals-m2/analyze.py

all: combine-ghc-G32-S1800.txt combine-ghc-G32-S3500.txt combine-bridges-G64-S1800.txt combine-bridges-G120-S3500.txt \
 products-ghc-G32-S1800.txt products-ghc-G32-S3500.txt products-bridges-G64-S1800.txt products-bridges-G120-S3500.txt

combine-ghc-G32-S1800.txt:
	$(ANA) -r c -u run-smirnov-*-lim1800.log > combine-ghc-G32-S1800.txt

combine-ghc-G32-S3500.txt:
	$(ANA) -r c -u run-smirnov-*-lim1800.log > combine-ghc-G32-S3500.txt

combine-bridges-G64-S1800.txt:
	$(ANA) -r c -u run-smirnov-*-G64.log > combine-bridges-G64-S1800.txt

combine-bridges-G120-S3500.txt:
	$(ANA) -r c -u run-smirnov-*-G64.log > combine-bridges-G120-S3500.txt

products-ghc-G32-S1800.txt:
	$(ANA) -r p -u run-smirnov-*-lim1800.log > products-ghc-G32-S1800.txt

products-ghc-G32-S3500.txt:
	$(ANA) -r p -u run-smirnov-*-lim1800.log > products-ghc-G32-S3500.txt

products-bridges-G64-S1800.txt:
	$(ANA) -r p -u run-smirnov-*-G64.log > products-bridges-G64-S1800.txt

products-bridges-G120-S3500.txt:
	$(ANA) -r p -u run-smirnov-*-G64.log > products-bridges-G120-S3500.txt



clean:
	rm -f *~ *.txt
