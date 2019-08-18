#!/usr/bin/python

# Test supercanonicalization:
# Read file for solution given as command-line argument
# Try each possible matrix transformation
# Keep track of unique cases, and their properties

import sys
import getopt
import glob

import circuit
import superbrent
import inversions
import inverse_pairs3


def usage(name):
    print("Usage: %s [-h] [-a|-p] [-b] [-v] [-f FILE] [-d DIR]" % name)
    print("   -h      Print this information")
    print("   -a      Apply all 168 possible matrices")
    print("   -p      Apply all 6 possible permutation matrices")
    print("   -b      Check adherence to Brent equations")
    print("   -v      Verbose mode")
    print("   -f FILE Read scheme from FILE")
    print("   -d DIR  Read all schemes in directory DIR")

verbose = False
checkBrent = False
pairList = inverse_pairs3.uniquePairList
uniqueHashes = {}
originalScheme = None

totalCount = 0
newCount = 0
brentCount = 0
uniqueCount = 0
doubleCount = 0
singleCount = 0


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
    ls = ['', 'Index', 'New', 'Brent', 'Unique', 'Double', 'Single', 'Adds', 'KHash', '', 'Hash']
    format(ls)

def process(idx):
    global totalCount, newCount
    global brentCount, uniqueCount, doubleCount, singleCount
    pair = [superbrent.Matrix(rows = dim, signature = pairList[idx][i]) for i in (0,1)]
    smult = mainScheme.productTransform(pair)
    s = smult.canonize()
    if verbose:
        print("")
        print("Original kernel: %s" % str(mainScheme.kernelTerms))
        print("Multiply by")
        inversions.showPair(pair[0], pair[1])
        print("Multiplied kernel: %s" % str(smult.kernelTerms))        
        print("Canonized kernel: %s" % str(s.kernelTerms))        
    hash = s.sign()
    khash = s.kernelTerms.sign()
    totalCount += 1
    new = not hash in uniqueHashes
    if new:
        uniqueHashes[hash] = s
        newCount += 1
    if checkBrent:
        obeysBrent = 'Y' if s.obeysBrent() else 'N'
        brentCount += 1
    else:
        obeysBrent = '-'
    obeysUnique = 'Y' if s.obeysUniqueUsage() else 'N'
    if obeysUnique == 'Y':
        uniqueCount += 1
    obeysDouble = 'Y' if s.obeysMaxDouble() else 'N'
    if obeysDouble == 'Y':
        doubleCount += 1
    obeysSingle = 'Y' if s.obeysSingletonExclusion() else 'N'
    if obeysSingle == 'Y':
        singleCount += 1
    adds = s.addCount()
    ls = ['', idx, new, obeysBrent, obeysUnique, obeysDouble, obeysSingle, adds, khash, hash]
    format(ls)
    
def run(name, args):
    global brentCheck, pairList, verbose
    pathList = []
    optlist, args = getopt.getopt(args, 'hbapf:d:v')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-a':
            pairList = inverse_pairs3.allPairList
        elif opt == '-p':
            pairList = inverse_pairs3.permutationPairList
        elif opt == '-b':
            brentCheck = True
        elif opt == '-v':
            verbose = True
        elif opt == '-f':
            pathList = [val]
        elif opt == '-d':
            pathList = glob.glob(val + '/*.exp')

    header()
    first = True
    for path in pathList:
        if not first:
            first = False
            print("")
        print("File: %s" % path)
        load(path)
        for idx in range(len(pairList)):
            process(idx)
    ls = ['TOTAL', totalCount, newCount, brentCount if checkBrent else '', uniqueCount, doubleCount, singleCount]
    format(ls)

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])



    
    
