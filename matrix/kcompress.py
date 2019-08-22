#!/usr/bin/python

# Given existing solution, extract its kernel
# Then generate kernels that reduce number of products in matrix multiplication

import sys
import circuit
import brent

dim = (3,3,3)
sourceAuxCount = 23
targetAuxCount = 22

ckt = circuit.Circuit()
sourceKernel = None

testCount = 0
saveFiles = True

# Mapping from generated kernel signatures to unique kernels
kernelDict = {}

def load(path):
    global sourceKernel
    try:
        s = brent.MScheme(dim, sourceAuxCount, ckt).parseFromFile(path)
    except Exception as ex:
        print("ERROR: Could not extract solution from file '%s' (%s)" % (path, str(ex)))
        return
    sc = s.canonize()
    sourceKernel = sc.kernelTerms
    khash = sourceKernel.sign()
    print("Loaded kernel %s from scheme %s" % (khash, path))

# Given newly generated kdlist, convert to kernel
# Canonize it, and record if unique
def catalog(kdlist):
    global kernelDict, testCount
    testCount += 1
    k = brent.KernelSet(dim, targetAuxCount, kdlist)
    kc, dlist = k.listCanonize()
    sig = kc.signature()
    if sig not in kernelDict:
        kernelDict[sig] = kc

def mergeSingles():
    levelList = sourceKernel.levelize()
    # Split into levels with single kernels and ones with multiple kernels
    singleList = [ls for ls in levelList if len(ls) == 1]
    multiList =  [ls for ls in levelList if len(ls) > 1]
    # Enumerate pairs from single lists
    for idx1 in range(len(singleList)):
        kt1 = singleList[idx1][0]
        for idx2 in range(idx1+1, len(singleList)):
            kt2 = singleList[idx2][0]
            newSingleList = [ls for ls in singleList if ls[0] != kt1 and ls[0] != kt2]
            newList = [[kt1, kt2]]
            # Now generate list of kernel terms with new levels
            nextLevel = 1
            kdlist = []
            for ls in multiList + newList + newSingleList:
                for kt in ls:
                    nkt = kt.clone()
                    nkt.level = nextLevel
                    kdlist.append(nkt)
                nextLevel += 1
            catalog(kdlist)
    
def save(k):
    khash = k.sign()
    outName = khash + ".exp"
    try:
        outf = open(outName, 'w')
    except Exception as ex:
        print("Couldn't open output file '%s' (%s)" % (outName, str(ex)))
        return
    k.printPolynomial(outf)
    outf.close()
    print("Wrote to file %s" % outName)


def run(path):
    load(path)
    if sourceKernel is None:
        return
    mergeSingles()
    print("%d kernels tested.  %d unique kernels generated" % (testCount, len(kernelDict)))
    print("Original Signature:")
    print("  " + sourceKernel.shortString())
    print("New signatures:")
    klist = kernelDict.values()
    klist.sort(key = lambda k: k.shortString())
    for k in klist:
        print(k.shortString())
        if saveFiles:
            save(k)
        
run(sys.argv[1])        
