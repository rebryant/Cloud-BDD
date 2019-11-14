#!/usr/bin/python

# Given set of literals, compress into set with one less level
# for all p(p-1)/2 possible pairs of levels

import sys
import circuit
import brent
import getopt

def usage(name):
    print("Usage: %s [-h] [-r ROOT] [-L LFILE] [-m MODE] [-p AUX]" % name)
    print("    -h       Print this message")
    print("    -r ROOT  Set root name of literal file")
    print("    -L LFILE Specify name of source literal file")
    print("    -m MODE  Specify level merging mode")
    print("    -p AUX   Set number of auxilliary variables in source")


root = "smirnov"
infile = "smirnov-family.lit"
mode = 2
dim = (3,3,3)
sourceAuxCount = 23

ckt = circuit.Circuit()

def outName(root, fromLevel, toLevel):
    return "%s-m%.2d+%.2d-mode%d.lit" % (root, fromLevel, toLevel, mode)

def generate(scheme, fromLevel, toLevel):
    snew = scheme.mergeLevels(fromLevel, toLevel, mode)
    fname = outName(root, fromLevel, toLevel)
    try:
        outf = open(fname, 'w')
    except Exception as ex:
        print("Couldn't open output file %s" % fname)
        return
    lits = snew.assignment.literals()
    for lit in lits:
        outf.write(str(lit) + '\n')
    outf.close()
    print("Wrote %d literals to %s" % (len(lits), fname))
    
def run(name, args):
    global root, infile, mode, sourceAuxCount
    optlist, args = getopt.getopt(args, 'hr:L:m:p:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-r':
            root = val
        elif opt == '-L':
            infile = val
        elif opt == '-m':
            mode = int(val)
        elif opt == '-p':
            sourceAuxCount = int(val)
    try:
        scheme = brent.MScheme(dim, sourceAuxCount, ckt).parseLiteralsFromFile(infile)
    except Exception as ex:
        print("Couldn't get initial literals from %s (%s)" % (infile, str(ex)))
        return
    for toLevel in brent.unitRange(sourceAuxCount):
        for fromLevel in range(toLevel+1, sourceAuxCount+1):
            generate(scheme, fromLevel, toLevel)
        
if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])

