#!/usr/bin/python

# Read in collection of words.  Organize as TRIE.
# Generate encoding formula

import sys
import getopt


# Functions related to formula generation

class Formula:

    depth = 0
    onh = True
    zdd = True
    radix = 26
    logradix = 5
    template = "-%.2d.%.2d"
    outfile = None

    def __init__(self, depth, outfile = None, onh = True, zdd = True):
        self.depth = depth
        self.outfile = sys.stdout if outfile == None else outfile
        self.onh = onh
        self.zdd = zdd

    def comment(self, str):
        self.outfile.write("# " + str + "\n")

    # Formula selecting char c at position i
    def selectorName(self, c, i):
        prefix = "term" if c == '' else c
        return "c-%s.%.2d" % (prefix, i)
    
    def declareVars(self, i):
        self.comment("Generate encoding variables")
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
        idx = self.radix if c == '' else ord(c) - ord('a') 
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
            for idx in range(self.radix):
                c = chr(idx + ord('a'))
                self.genSelector(c, i)
            self.genSelector('', i)
        for i in range(self.depth + 1):
            self.genTerminal(i)

    def finish(self):
        self.outfile.write("time\n")
        self.outfile.write("info dict\n")
        self.outfile.write("count dict\n")
#        self.outfile.write("status\n")

        
    
class TrieNode:

    children = {}
    endWord = '@'
    
    def __init__(self):
        self.children = {}
        

    def addWord(self, w):
        if len(w) == 0:
            self.children[self.endWord] = None
        else:
            c = w[0]
            rest = w[1:]
            if c in self.children:
                t = self.children[c]
            else:
                t = TrieNode()
                self.children[c] = t
            t.addWord(rest)

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

    # Generate formula
    def encode(self, prefix, form):
        subname = "d-" + prefix
        name = "dict" if prefix == "" else subname
        # First pass: Generate subformulas
        for c,t in self.children.items():
            if t != None:
                nprefix = prefix + c
                t.encode(nprefix, form)
        # Second pass: Collect into formula
        form.outfile.write("or " + name + "\n")
        for c,t in self.children.items():
            if t == None:
                form.outfile.write("or %s %s %s" % (name, name, form.terminalName(len(prefix))) + "\n")
            else:
                cname = subname+c
                sname = "s-" + cname
                form.outfile.write("and %s %s %s" % (sname, form.selectorName(c, len(prefix)), cname) + "\n")
                form.outfile.write("or %s %s %s" % (name, name, sname) + "\n")
                form.outfile.write("delete %s %s" % (cname, sname) + "\n")

class Trie:
    root = None
    form = None
    
    def __init__(self):
        self.root = TrieNode()
    
    def cleanup(self, s):
        while len(s) > 0 and s[0] in " ":
            s = s[1:]
        while len(s) > 0 and s[-1] in " \n":
            s = s[:-1]
        return s

    def addWords(self, f):
        for line in f:
            s = self.cleanup(line)
            if s != '':
                self.root.addWord(s)

    def startFormula(self, outfile = None, onh = True, zdd = True):
        depth = self.root.height()
        self.form = Formula(depth, outfile, onh, zdd)
        self.form.setup()

    def genFormula(self):
        self.root.encode("", self.form)

    def finishFormula(self):
        self.form.finish()


def gen(infiles = [], outfile = None, onh = True, zdd = True):
    t = Trie()
    if (len(infiles) == 0):
        infiles = [sys.stdin]
    for f in infiles:
        t.addWords(f)
    t.startFormula(outfile, onh, zdd)
    t.genFormula()
    t.finishFormula()


def usage(name):
    print "Usage %s [-h] [-i f1:f2:..] [-b] [-z] -o [oufile]" % name
    print "  -h          Print this message"
    print "  -i f1:f2:.. Specify input word file(s)"
    print "  -b          Use binary encoding"
    print "  -o outfile  Specify output file"

def run(name, args):
    infiles = []
    outfile = None
    zdd = False
    onh = True
    optlist, args = getopt.getopt(args, 'hi:bzo:')
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
        elif opt == '-z':
            zdd = True
        elif opt == '-o':
            fname = val
            try:
                outfile = open(fname, 'w')
            except:
                print "Could not open output file '%s'" % fname
                return
    gen(infiles, outfile, onh, zdd)
    for f in infiles:
        f.close()
    if outfile != None:
        outfile.close()

run(sys.argv[0], sys.argv[1:])


                
    
