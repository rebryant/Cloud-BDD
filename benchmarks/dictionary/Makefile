ENCODE = ./dencode.py
RUN = ../../runbdd -c
PARSE = ./sparse.pl


all: runbdd words pwords awords apwords move tables

mini: runbdd words-mini awords-mini move-mini tables

runbdd:
	(cd ../.. ; make runbdd)

words: all-words-obn.out all-words-oba.out \
	all-words-ozn.out all-words-oza.out \
	all-words-oan.out all-words-oaa.out \
	all-words-bbn.out all-words-bba.out \
	all-words-bzn.out all-words-bza.out \
	all-words-ban.out all-words-baa.out

pwords: passwords-obn.out passwords-oba.out \
	passwords-ozn.out passwords-oza.out \
	passwords-oan.out passwords-oaa.out \
	passwords-bbn.out passwords-bba.out \
	passwords-bzn.out passwords-bza.out \
	passwords-ban.out passwords-baa.out 

awords: all-words_ascii-obn.out all-words_ascii-oba.out \
	all-words_ascii-ozn.out all-words_ascii-oza.out \
	all-words_ascii-oan.out all-words_ascii-oaa.out \
	all-words_ascii-bbn.out all-words_ascii-bba.out \
	all-words_ascii-bzn.out all-words_ascii-bza.out \
	all-words_ascii-ban.out all-words_ascii-baa.out

apwords: passwords_ascii-obn.out passwords_ascii-oba.out \
	passwords_ascii-ozn.out passwords_ascii-oza.out \
	passwords_ascii-oan.out passwords_ascii-oaa.out \
	passwords_ascii-bbn.out passwords_ascii-bba.out \
	passwords_ascii-bzn.out passwords_ascii-bza.out \
	passwords_ascii-ban.out passwords_ascii-baa.out 

words-mini: all-words-mini-obn.out all-words-mini-oba.out \
	all-words-mini-ozn.out all-words-mini-oza.out \
	all-words-mini-oan.out all-words-mini-oaa.out \
	all-words-mini-bbn.out all-words-mini-bba.out \
	all-words-mini-bzn.out all-words-mini-bza.out \
	all-words-mini-ban.out all-words-mini-baa.out

awords-mini: all-words-mini_ascii-obn.out all-words-mini_ascii-oba.out \
	all-words-mini_ascii-ozn.out all-words-mini_ascii-oza.out \
	all-words-mini_ascii-oan.out all-words-mini_ascii-oaa.out \
	all-words-mini_ascii-bbn.out all-words-mini_ascii-bba.out \
	all-words-mini_ascii-bzn.out all-words-mini_ascii-bza.out \
	all-words-mini_ascii-ban.out all-words-mini_ascii-baa.out



tables: final-sizes.txt max-sizes.txt times.txt ops.txt mem.txt

final-sizes.txt:
	grep "Cudd size" */*.out | $(PARSE) > final-sizes.txt

max-sizes.txt:
	grep "Peak number of nodes" */*.out | $(PARSE) > max-sizes.txt

times.txt:
	grep "Delta" */*.out | $(PARSE) > times.txt

ops.txt:
	grep "insertions" */*.out | $(PARSE) > ops.txt

mem.txt:
	grep "Memory" */*.out | $(PARSE) > mem.txt


# Words in dict/words using natural character set

all-words-ob.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words-ob.cmd

all-words-oz.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words-oz.cmd -Z

all-words-oa.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words-oa.cmd -A

all-words-bb.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words-bb.cmd -b

all-words-bz.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words-bz.cmd -b -Z

all-words-ba.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words-ba.cmd -b -A

all-words-obn.out: all-words-ob.cmd
	$(RUN) -f all-words-ob.cmd -C n > all-words-obn.out

all-words-oba.out: all-words-ob.cmd
	$(RUN) -f all-words-ob.cmd -C a > all-words-oba.out

all-words-ozn.out: all-words-oz.cmd
	$(RUN) -f all-words-oz.cmd -C n > all-words-ozn.out

all-words-oza.out: all-words-oz.cmd
	$(RUN) -f all-words-oz.cmd -C a > all-words-oza.out

all-words-oan.out: all-words-oa.cmd
	$(RUN) -f all-words-oa.cmd -C n > all-words-oan.out

all-words-oaa.out: all-words-oa.cmd
	$(RUN) -f all-words-oa.cmd -C a > all-words-oaa.out

all-words-bbn.out: all-words-bb.cmd
	$(RUN) -f all-words-bb.cmd -C n > all-words-bbn.out

all-words-bba.out: all-words-bb.cmd
	$(RUN) -f all-words-bb.cmd -C a > all-words-bba.out

all-words-bzn.out: all-words-bz.cmd
	$(RUN) -f all-words-bz.cmd -C n > all-words-bzn.out

all-words-bza.out: all-words-bz.cmd
	$(RUN) -f all-words-bz.cmd -C a > all-words-bza.out

all-words-ban.out: all-words-ba.cmd
	$(RUN) -f all-words-ba.cmd -C n > all-words-ban.out

all-words-baa.out: all-words-ba.cmd
	$(RUN) -f all-words-ba.cmd -C a > all-words-baa.out


# Passwords using full natural character set

passwords-ob.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords-ob.cmd

passwords-oz.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords-oz.cmd -Z

passwords-oa.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords-oa.cmd -A

passwords-bb.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords-bb.cmd -b

passwords-bz.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords-bz.cmd -b -Z

passwords-ba.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords-ba.cmd -b -A

passwords-obn.out: passwords-ob.cmd
	$(RUN) -f passwords-ob.cmd -C n > passwords-obn.out

passwords-oba.out: passwords-ob.cmd
	$(RUN) -f passwords-ob.cmd -C a > passwords-oba.out

passwords-ozn.out: passwords-oz.cmd
	$(RUN) -f passwords-oz.cmd -C n > passwords-ozn.out

passwords-oza.out: passwords-oz.cmd
	$(RUN) -f passwords-oz.cmd -C a > passwords-oza.out

passwords-oan.out: passwords-oa.cmd
	$(RUN) -f passwords-oa.cmd -C n > passwords-oan.out

passwords-oaa.out: passwords-oa.cmd
	$(RUN) -f passwords-oa.cmd -C a > passwords-oaa.out

passwords-bbn.out: passwords-bb.cmd
	$(RUN) -f passwords-bb.cmd -C n > passwords-bbn.out

passwords-bba.out: passwords-bb.cmd
	$(RUN) -f passwords-bb.cmd -C a > passwords-bba.out

passwords-bzn.out: passwords-bz.cmd
	$(RUN) -f passwords-bz.cmd -C n > passwords-bzn.out

passwords-bza.out: passwords-bz.cmd
	$(RUN) -f passwords-bz.cmd -C a > passwords-bza.out

passwords-ban.out: passwords-ba.cmd
	$(RUN) -f passwords-ba.cmd -C n > passwords-ban.out

passwords-baa.out: passwords-ba.cmd
	$(RUN) -f passwords-ba.cmd -C a > passwords-baa.out

# Words in dict/words using full ASCII character set

all-words_ascii-ob.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words_ascii-ob.cmd -a

all-words_ascii-oz.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words_ascii-oz.cmd -a -Z

all-words_ascii-oa.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words_ascii-oa.cmd -a -A

all-words_ascii-bb.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words_ascii-bb.cmd -a -b

all-words_ascii-bz.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words_ascii-bz.cmd -a -b -Z

all-words_ascii-ba.cmd: words.list $(ENCODE)
	$(ENCODE) -i words.list -o all-words_ascii-ba.cmd -a -b -A


all-words_ascii-obn.out: all-words_ascii-ob.cmd
	$(RUN) -f all-words_ascii-ob.cmd -C n > all-words_ascii-obn.out

all-words_ascii-oba.out: all-words_ascii-ob.cmd
	$(RUN) -f all-words_ascii-ob.cmd -C a > all-words_ascii-oba.out

all-words_ascii-ozn.out: all-words_ascii-oz.cmd
	$(RUN) -f all-words_ascii-oz.cmd -C n > all-words_ascii-ozn.out

all-words_ascii-oza.out: all-words_ascii-oz.cmd
	$(RUN) -f all-words_ascii-oz.cmd -C a > all-words_ascii-oza.out

all-words_ascii-oan.out: all-words_ascii-oa.cmd
	$(RUN) -f all-words_ascii-oa.cmd -C n > all-words_ascii-oan.out

all-words_ascii-oaa.out: all-words_ascii-oa.cmd
	$(RUN) -f all-words_ascii-oa.cmd -C a > all-words_ascii-oaa.out

all-words_ascii-bbn.out: all-words_ascii-bb.cmd
	$(RUN) -f all-words_ascii-bb.cmd -C n > all-words_ascii-bbn.out

all-words_ascii-bba.out: all-words_ascii-bb.cmd
	$(RUN) -f all-words_ascii-bb.cmd -C a > all-words_ascii-bba.out

all-words_ascii-bzn.out: all-words_ascii-bz.cmd
	$(RUN) -f all-words_ascii-bz.cmd -C n > all-words_ascii-bzn.out

all-words_ascii-bza.out: all-words_ascii-bz.cmd
	$(RUN) -f all-words_ascii-bz.cmd -C a > all-words_ascii-bza.out

all-words_ascii-ban.out: all-words_ascii-ba.cmd
	$(RUN) -f all-words_ascii-ba.cmd -C n > all-words_ascii-ban.out

all-words_ascii-baa.out: all-words_ascii-ba.cmd
	$(RUN) -f all-words_ascii-ba.cmd -C a > all-words_ascii-baa.out


# Passwords using full ASCII character set

passwords_ascii-ob.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords_ascii-ob.cmd -a

passwords_ascii-oz.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords_ascii-oz.cmd -a -Z

passwords_ascii-oa.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords_ascii-oa.cmd -a -A

passwords_ascii-bb.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords_ascii-bb.cmd -a -b

passwords_ascii-bz.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords_ascii-bz.cmd -a -b -Z

passwords_ascii-ba.cmd: passwords.list $(ENCODE)
	$(ENCODE) -i passwords.list -o passwords_ascii-ba.cmd -a -b -A

passwords_ascii-obn.out: passwords_ascii-ob.cmd
	$(RUN) -f passwords_ascii-ob.cmd -C n > passwords_ascii-obn.out

passwords_ascii-oba.out: passwords_ascii-ob.cmd
	$(RUN) -f passwords_ascii-ob.cmd -C a > passwords_ascii-oba.out

passwords_ascii-ozn.out: passwords_ascii-oz.cmd
	$(RUN) -f passwords_ascii-oz.cmd -C n > passwords_ascii-ozn.out

passwords_ascii-oza.out: passwords_ascii-oz.cmd
	$(RUN) -f passwords_ascii-oz.cmd -C a > passwords_ascii-oza.out

passwords_ascii-oan.out: passwords_ascii-oa.cmd
	$(RUN) -f passwords_ascii-oa.cmd -C n > passwords_ascii-oan.out

passwords_ascii-oaa.out: passwords_ascii-oa.cmd
	$(RUN) -f passwords_ascii-oa.cmd -C a > passwords_ascii-oaa.out

passwords_ascii-bbn.out: passwords_ascii-bb.cmd
	$(RUN) -f passwords_ascii-bb.cmd -C n > passwords_ascii-bbn.out

passwords_ascii-bba.out: passwords_ascii-bb.cmd
	$(RUN) -f passwords_ascii-bb.cmd -C a > passwords_ascii-bba.out

passwords_ascii-bzn.out: passwords_ascii-bz.cmd
	$(RUN) -f passwords_ascii-bz.cmd -C n > passwords_ascii-bzn.out

passwords_ascii-bza.out: passwords_ascii-bz.cmd
	$(RUN) -f passwords_ascii-bz.cmd -C a > passwords_ascii-bza.out

passwords_ascii-ban.out: passwords_ascii-ba.cmd
	$(RUN) -f passwords_ascii-ba.cmd -C n > passwords_ascii-ban.out

passwords_ascii-baa.out: passwords_ascii-ba.cmd
	$(RUN) -f passwords_ascii-ba.cmd -C a > passwords_ascii-baa.out

words-mini.list: words.list
	sed -n -e '1,10000p' words.list > words-mini.list

# Subset of words in dict/words using natural character set
all-words-mini-ob.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini-ob.cmd

all-words-mini-oz.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini-oz.cmd -Z

all-words-mini-oa.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini-oa.cmd -A

all-words-mini-bb.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini-bb.cmd -b

all-words-mini-bz.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini-bz.cmd -b -Z

all-words-mini-ba.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini-ba.cmd -b -A

all-words-mini-obn.out: all-words-mini-ob.cmd
	$(RUN) -f all-words-mini-ob.cmd -C n > all-words-mini-obn.out

all-words-mini-oba.out: all-words-mini-ob.cmd
	$(RUN) -f all-words-mini-ob.cmd -C a > all-words-mini-oba.out

all-words-mini-ozn.out: all-words-mini-oz.cmd
	$(RUN) -f all-words-mini-oz.cmd -C n > all-words-mini-ozn.out

all-words-mini-oza.out: all-words-mini-oz.cmd
	$(RUN) -f all-words-mini-oz.cmd -C a > all-words-mini-oza.out

all-words-mini-oan.out: all-words-mini-oa.cmd
	$(RUN) -f all-words-mini-oa.cmd -C n > all-words-mini-oan.out

all-words-mini-oaa.out: all-words-mini-oa.cmd
	$(RUN) -f all-words-mini-oa.cmd -C a > all-words-mini-oaa.out

all-words-mini-bbn.out: all-words-mini-bb.cmd
	$(RUN) -f all-words-mini-bb.cmd -C n > all-words-mini-bbn.out

all-words-mini-bba.out: all-words-mini-bb.cmd
	$(RUN) -f all-words-mini-bb.cmd -C a > all-words-mini-bba.out

all-words-mini-bzn.out: all-words-mini-bz.cmd
	$(RUN) -f all-words-mini-bz.cmd -C n > all-words-mini-bzn.out

all-words-mini-bza.out: all-words-mini-bz.cmd
	$(RUN) -f all-words-mini-bz.cmd -C a > all-words-mini-bza.out

all-words-mini-ban.out: all-words-mini-ba.cmd
	$(RUN) -f all-words-mini-ba.cmd -C n > all-words-mini-ban.out

all-words-mini-baa.out: all-words-mini-ba.cmd
	$(RUN) -f all-words-mini-ba.cmd -C a > all-words-mini-baa.out

# Subset of words-mini in dict/words-mini using full ASCII character set

all-words-mini_ascii-ob.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini_ascii-ob.cmd -a

all-words-mini_ascii-oz.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini_ascii-oz.cmd -a -Z

all-words-mini_ascii-oa.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini_ascii-oa.cmd -a -A

all-words-mini_ascii-bb.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini_ascii-bb.cmd -a -b

all-words-mini_ascii-bz.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini_ascii-bz.cmd -a -b -Z

all-words-mini_ascii-ba.cmd: words-mini.list $(ENCODE)
	$(ENCODE) -i words-mini.list -o all-words-mini_ascii-ba.cmd -a -b -A


all-words-mini_ascii-obn.out: all-words-mini_ascii-ob.cmd
	$(RUN) -f all-words-mini_ascii-ob.cmd -C n > all-words-mini_ascii-obn.out

all-words-mini_ascii-oba.out: all-words-mini_ascii-ob.cmd
	$(RUN) -f all-words-mini_ascii-ob.cmd -C a > all-words-mini_ascii-oba.out

all-words-mini_ascii-ozn.out: all-words-mini_ascii-oz.cmd
	$(RUN) -f all-words-mini_ascii-oz.cmd -C n > all-words-mini_ascii-ozn.out

all-words-mini_ascii-oza.out: all-words-mini_ascii-oz.cmd
	$(RUN) -f all-words-mini_ascii-oz.cmd -C a > all-words-mini_ascii-oza.out

all-words-mini_ascii-oan.out: all-words-mini_ascii-oa.cmd
	$(RUN) -f all-words-mini_ascii-oa.cmd -C n > all-words-mini_ascii-oan.out

all-words-mini_ascii-oaa.out: all-words-mini_ascii-oa.cmd
	$(RUN) -f all-words-mini_ascii-oa.cmd -C a > all-words-mini_ascii-oaa.out

all-words-mini_ascii-bbn.out: all-words-mini_ascii-bb.cmd
	$(RUN) -f all-words-mini_ascii-bb.cmd -C n > all-words-mini_ascii-bbn.out

all-words-mini_ascii-bba.out: all-words-mini_ascii-bb.cmd
	$(RUN) -f all-words-mini_ascii-bb.cmd -C a > all-words-mini_ascii-bba.out

all-words-mini_ascii-bzn.out: all-words-mini_ascii-bz.cmd
	$(RUN) -f all-words-mini_ascii-bz.cmd -C n > all-words-mini_ascii-bzn.out

all-words-mini_ascii-bza.out: all-words-mini_ascii-bz.cmd
	$(RUN) -f all-words-mini_ascii-bz.cmd -C a > all-words-mini_ascii-bza.out

all-words-mini_ascii-ban.out: all-words-mini_ascii-ba.cmd
	$(RUN) -f all-words-mini_ascii-ba.cmd -C n > all-words-mini_ascii-ban.out

all-words-mini_ascii-baa.out: all-words-mini_ascii-ba.cmd
	$(RUN) -f all-words-mini_ascii-ba.cmd -C a > all-words-mini_ascii-baa.out

move: # rwords pwords longwords longpwords
	-mkdir passwords_ascii
	-mv passwords_ascii-*.out passwords_ascii
	-mkdir all-words_ascii
	-mv all-words_ascii-*.out all-words_ascii
	-mkdir passwords
	-mv passwords-*.out passwords
	-mkdir all-words
	-mv all-words-*.out all-words

move-mini: # rwords pwords longwords longpwords
	-mkdir all-words-mini_ascii
	-mv all-words-mini_ascii-*.out all-words-mini_ascii
	-mkdir all-words-mini
	-mv all-words-mini-*.out all-words-mini

clean:
	rm -f *~ *.cmd *.pyc *.txt words-mini.list


