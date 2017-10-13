PART = ./partition.py
ENCODE = ./dencode.py
RUN = ../../runbdd -c
PARSE = ./sparse.pl

all: rwords pwords awords apwords

tables: final-sizes.txt max-sizes.txt times.txt ops.txt mem.txt

lwords: lowercase-words-obn.out lowercase-words-oba.out \
	lowercase-words-ozn.out lowercase-words-oza.out \
	lowercase-words-oan.out lowercase-words-oaa.out \
	lowercase-words-bbn.out lowercase-words-bba.out \
	lowercase-words-bzn.out lowercase-words-bza.out \
	lowercase-words-ban.out lowercase-words-baa.out
rwords: all-words-obn.out all-words-oba.out \
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

lowercase-words.list: words.list $(PART)
	$(PART)

# Words from dict/words converted to lower case

lowercase-words-ob.cmd: lowercase-words.list $(ENCODE)
	$(ENCODE) -i lowercase-words.list -o lowercase-words-ob.cmd

lowercase-words-oz.cmd: lowercase-words.list $(ENCODE)
	$(ENCODE) -i lowercase-words.list -o lowercase-words-oz.cmd -Z

lowercase-words-oa.cmd: lowercase-words.list $(ENCODE)
	$(ENCODE) -i lowercase-words.list -o lowercase-words-oa.cmd -A

lowercase-words-bb.cmd: lowercase-words.list $(ENCODE)
	$(ENCODE) -i lowercase-words.list -o lowercase-words-bb.cmd -b

lowercase-words-bz.cmd: lowercase-words.list $(ENCODE)
	$(ENCODE) -i lowercase-words.list -o lowercase-words-bz.cmd -b -Z

lowercase-words-ba.cmd: lowercase-words.list $(ENCODE)
	$(ENCODE) -i lowercase-words.list -o lowercase-words-ba.cmd -b -A

lowercase-words-obn.out: lowercase-words-ob.cmd
	$(RUN) -f lowercase-words-ob.cmd -C n > lowercase-words-obn.out

lowercase-words-oba.out: lowercase-words-ob.cmd
	$(RUN) -f lowercase-words-ob.cmd -C a > lowercase-words-oba.out

lowercase-words-ozn.out: lowercase-words-oz.cmd
	$(RUN) -f lowercase-words-oz.cmd -C n > lowercase-words-ozn.out

lowercase-words-oza.out: lowercase-words-oz.cmd
	$(RUN) -f lowercase-words-oz.cmd -C a > lowercase-words-oza.out

lowercase-words-bbn.out: lowercase-words-bb.cmd
	$(RUN) -f lowercase-words-bb.cmd -C n > lowercase-words-bbn.out

lowercase-words-bba.out: lowercase-words-bb.cmd
	$(RUN) -f lowercase-words-bb.cmd -C a > lowercase-words-bba.out

lowercase-words-bzn.out: lowercase-words-bz.cmd
	$(RUN) -f lowercase-words-bz.cmd -C n > lowercase-words-bzn.out

lowercase-words-bza.out: lowercase-words-bz.cmd
	$(RUN) -f lowercase-words-bz.cmd -C a > lowercase-words-bza.out


# Raw words in dict/words using natural character set

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


# Raw passwords using full natural character set

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

# Raw words in dict/words using full ASCII character set

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


# Raw passwords using full ASCII character set

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

move: # rwords pwords longwords longpwords
	cp -p passwords_ascii-*.out passwords_ascii
	cp -p all-words_ascii-*.out all-words_ascii
	cp -p passwords-*.out passwords
	cp -p all-words-*.out all-words

clean:
	rm -f *~ *.out *.cmd
