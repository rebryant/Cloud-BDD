#!/usr/bin/python

# Test supercanonicalization:
# Read file for solution given as command-line argument
# Try each possible matrix transformation
# Keep track of unique cases, and their properties

import sys
import circuit
import superbrent
import inverse_pairs3

uniqueHashes = {}

originalScheme = None

dim = 3
auxCount = 23

ckt = circuit.Circuit()

def load(path):
    global mainScheme
    mainScheme = superbrent.SuperScheme(dim, auxCount, ckt).parseFromFile(path)
    

def format(ls):
    slist = [str(e) for e in ls]
    print '\t'.join(slist)

def header():
    ls = ['Index', 'New', 'Brent', 'Unique', 'Double', 'Single', 'Adds', 'KHash', '', 'Hash']
    format(ls)

def process(idx):
    pair = [superbrent.Matrix(rows = dim, signature = inverse_pairs3.matrixList[idx][i]) for i in (0,1)]
    s = mainScheme.productTransform(pair).canonize()
    hash = s.sign()
    khash = s.kernelTerms.sign()
    new = not hash in uniqueHashes
    if new:
        uniqueHashes[hash] = s
    obeysBrent = s.obeysBrent()
    obeysBrent = True
    obeysUnique = s.obeysUniqueUsage()
    obeysDouble = s.obeysMaxDouble()
    obeysSingle = s.obeysSingletonExclusion()
    adds = s.addCount()
    ls = [idx, new, obeysBrent, obeysUnique, obeysDouble, obeysSingle, adds, khash, hash]
    format(ls)
    
def run(path):
    load(path)
    header()
    for idx in range(len(inverse_pairs3.matrixList)):
        process(idx)

if __name__ == "__main__":
    run(sys.argv[1])



    
    
