#!/usr/bin/python

# Given set of literals, compress into set with one less level
# This should be run within the directory matrix/mm-data/compress-levels

import sys
import circuit
import brent

root = "smirnov"
infile = "smirnov-family.lit"


dim = (3,3,3)
sourceAuxCount = 23

ckt = circuit.Circuit()

def outName(root, fromLevel, toLevel):
    return "%s-%.2d-%.2d.lit" % (root, fromLevel, toLevel)

def generate(scheme, fromLevel, toLevel, root):
    snew = scheme.mergeLevels(fromLevel, toLevel)
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
    
def run():
    try:
        scheme = brent.MScheme(dim, sourceAuxCount, ckt).parseLiteralsFromFile(infile)
    except Exception as ex:
        print("Couldn't get initial literals from %s (%s)" % (infile, str(ex)))
        return
    for toLevel in brent.unitRange(sourceAuxCount):
        for fromLevel in range(toLevel+1, sourceAuxCount+1):
            generate(scheme, fromLevel, toLevel, root)
        
if __name__ == "__main__":
    run()

