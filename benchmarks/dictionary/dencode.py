#!/usr/bin/python

# Read in collection of words.  Organize as TRIE.
# Generate encoding formula

import sys
import getopt
import string

# Functions related to formula generation

class Formula:

    depth = 0
    onh = True
    zdd = True
    radix = 26
    logradix = 5
    alphabet = ""
    c2i = {}
    # Escape character in naming
    echar = '@'
    # Self-printing characters.  All others are escaped in hex
    printChars = set(list("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"))
    template = "-%.2d.%.2d"
    outfile = None

    def __init__(self, depth, outfile = None, alphabet = "", onh = True, zdd = True):
        self.depth = depth
        self.outfile = sys.stdout if outfile == None else outfile
        self.alphabet = string.ascii_lowercase if alphabet == "" else alphabet
        self.radix = len(self.alphabet)
        self.logradix = self.p2log(self.radix + 1) # Include enough to encode null character
        self.c2i = {}
        # Create mapping from characters to encodings
        for i in range(self.radix):
            self.c2i[self.alphabet[i]] = i
        self.onh = onh
        self.zdd = zdd

    def p2log(self, x):
        l = 0
        v = 1
        while (v < x):
            l += 1
            v *= 2
        return l

    # Create names for characters.  Most are self-named, except for some special ones
    def cname(self, c):
        if c in self.printChars:
            return c
        else:
            return self.echar + hex(ord(c))

    def comment(self, str):
        self.outfile.write("# " + str + "\n")

    # Formula selecting char c at position i
    def selectorName(self, c, i):
        prefix = "term" if c == '' else self.cname(c)
        return "c-%s.%.2d" % (prefix, i)
    
    def declareVars(self, i):
        self.comment("Generate encoding variables for position %d" % i)
        prefix = 'b' if self.zdd else 'v'
        cnt = self.radix if self.onh else self.logradix
        vars = [prefix + self.template % (idx, i) for idx in range(cnt)]
        # Declare variables
        self.outfile.write("var " + " ".join(vars) + "\n")
        
    def zconvert(self, i):
        if not self.zdd:
            return
        self.comment("Convert to ZDDs")
        prefix = 'b'
        cnt = self.radix if self.onh else self.logradix
        vars = [prefix + self.template % (idx, i) for idx in range(cnt)]
        zvars = ['v' + self.template % (idx, i) for idx in range(cnt)]
        for idx in range(cnt):
            self.outfile.write("zconvert %s %s" % (zvars[idx], vars[idx]) + "\n")

    def genSelector(self, c, i):
        # Use '' to denote end marker for string
        idx = self.radix if c == '' else self.c2i[c]
        name = self.selectorName(c, i)
        cnt = self.radix if self.onh else self.logradix
        literals = []
        if self.onh:
            for jdx in range(cnt):
                prefix = "" if jdx == idx else "!"
                literals.append(prefix + 'v' + self.template % (jdx, i))
        else:
            for j in range(cnt):
                prefix = "!" if ((1 << j) & idx) == 0 else ""
                literals.append(prefix + 'v' + self.template % (j, i))
        self.outfile.write(("and %s " % name) + " ".join(literals) + "\n")

    def terminalName(self, i):
        return "term-%.2d" % i

    def genTerminal(self, i):
        self.comment("Generate encoding of string that terminates after %d characters" % i)
        args = [self.selectorName('', idx) for idx in range(i, self.depth)]
        self.outfile.write("and " + self.terminalName(i) + " " + " ".join(args) + "\n")

    def setup(self):
        for i in range(self.depth):
            self.declareVars(i)
        if self.zdd:
            for i in range(self.depth):
                self.zconvert(i)
        for i in range(self.depth):
            self.comment("Generate selector functions for level %d" % i)
            for c in self.alphabet:
                self.genSelector(c, i)
            self.genSelector('', i)
        for i in range(self.depth + 1):
            self.genTerminal(i)

    def finish(self):
        self.outfile.write("time\n")
        self.outfile.write("info dict\n")
        self.outfile.write("# Skipping: count dict\n")
        self.outfile.write("status\n")

        
    
class TrieNode:

    children = {}
    endWord = 'END'
    
    def __init__(self):
        self.children = {}
        

    def addWord(self, w):
        if len(w) == 0:
            newword = not self.endWord in self.children
            if newword:
                self.children[self.endWord] = None
            return newword
        c = w[0]
        rest = w[1:]
        if c in self.children:
            t = self.children[c]
        else:
            t = TrieNode()
            self.children[c] = t
        return t.addWord(rest)

    def enumerate(self, prefix = ''):
        ls = []
        for c in self.children.keys():
            if c == self.endWord:
                ls.append(prefix)
            else:
                t = self.children[c]
                nprefix = prefix + c
                ls += t.enumerate(nprefix)
        return ls

    def height(self):
        rheight = 0
        for t in self.children.values():
            if t != None:
                rheight = max(rheight, 1 + t.height())
        return rheight

    def count(self):
        cnt = 0
        for t in self.children.values():
            if t == None:
                cnt += 1
            else:
                cnt += t.count()
        return cnt

    def size(self):
        n = 1
        for t in self.children.values():
            if t != None:
                n += t.size()
        return n

    # Generate formula
    def encode(self, depth, prefix, form):
        subname = "d-" + prefix
        name = "dict" if depth == 0 else subname
        # First pass: Generate subformulas
        for c,t in self.children.items():
            if t != None:
                nprefix = prefix + form.cname(c)
                t.encode(depth+1, nprefix, form)
        # Second pass: Collect into formula
        form.outfile.write("or " + name + "\n")
        for c,t in self.children.items():
            if t == None:
                form.outfile.write("or %s %s %s" % (name, name, form.terminalName(depth)) + "\n")
            else:
                cname = subname+form.cname(c)
                sname = "s-" + cname
                form.outfile.write("and %s %s %s" % (sname, form.selectorName(c, depth), cname) + "\n")
                form.outfile.write("or %s %s %s" % (name, name, sname) + "\n")
                form.outfile.write("delete %s %s" % (cname, sname) + "\n")

class Trie:
    root = None
    form = None
    words = 0
    alphaset = set()
    
    def __init__(self, fullRadix = False):
        self.root = TrieNode()
        self.words = 0
        if fullRadix:
            self.alphaset = set(map(chr, range(128)))
        else:
            self.alphaset = set()
    
    def cleanup(self, s):
        while len(s) > 0 and s[0] in " ":
            s = s[1:]
        while len(s) > 0 and s[-1] in " \n":
            s = s[:-1]
        self.alphaset |= set(list(s))
        return s

    def addWords(self, f):
        for line in f:
            s = self.cleanup(line)
            if s != '':
                if self.root.addWord(s):
                    self.words += 1

    def startFormula(self, outfile = None, onh = True, zdd = True):
        depth = self.root.height()
        alphalist = list(self.alphaset)
        alphalist.sort()
        alphabet = "".join(alphalist)
        self.form = Formula(depth, outfile, alphabet, onh, zdd)
        self.form.comment("Dictionary with %d words" % self.words)
        if len(alphabet) == 128:
            self.form.comment("Radix = %d" % len(alphabet))
        else:
            self.form.comment("Alphabet = '%s' (radix = %d)" % (alphabet, len(alphabet)))
        size = self.root.size()
        self.form.comment("Encoding trie has %d nodes" % size)
        self.form.setup()

    def genFormula(self):
        self.root.encode(0, "", self.form)

    def finishFormula(self):
        self.form.finish()


def gen(infiles = [], outfile = None, onh = True, zdd = True, fullRadix = False):
    t = Trie(fullRadix)
    if (len(infiles) == 0):
        infiles = [sys.stdin]
    for f in infiles:
        t.addWords(f)
    t.startFormula(outfile, onh, zdd)
    t.genFormula()
    t.finishFormula()


def usage(name):
    print "Usage %s [-h] [-i f1:f2:..] [-b] [-a] [-z] -o [oufile]" % name
    print "  -h          Print this message"
    print "  -i f1:f2:.. Specify input word file(s)"
    print "  -b          Use binary encoding"
    print "  -a          Use full ASCII character set"
    print "  -z          Use ZDDs"
    print "  -o outfile  Specify output file"

def run(name, args):
    infiles = []
    outfile = None
    zdd = False
    onh = True
    fullRange = False
    optlist, args = getopt.getopt(args, 'hi:bazo:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-i':
            infilenames = val.split(":")
            for fname in infilenames:
                try:
                    f = open(fname, 'r')
                except:
                    print "Could not open input file '%s'" % fname
                    return
                infiles.append(f)
        elif opt == '-b':
            onh = False
        elif opt == '-a':
            fullRange = True
        elif opt == '-z':
            zdd = True
        elif opt == '-o':
            fname = val
            try:
                outfile = open(fname, 'w')
            except:
                print "Could not open output file '%s'" % fname
                return
    gen(infiles, outfile, onh, zdd, fullRange)
    for f in infiles:
        f.close()
    if outfile != None:
        outfile.close()

if  __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])


                
    
