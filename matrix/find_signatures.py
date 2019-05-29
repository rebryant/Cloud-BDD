#!/usr/bin/python

# Scan through existing solutions
# Convert to canonical form and generate signature
# See how many unique signatures there are, and analyze their properties
# For each unique signature, store copy of one solution with this signature.

import sys
import os
import os.path

import circuit
import brent


# Map from signature to list of file paths
signatureDict = {}
solutionCount = 0
signatureCount = 0

dim = (3,3,3)
auxCount = 23

ckt = circuit.Circuit()

subDirectory = "mm-solutions"

candidatePath = subDirectory + "/heule-candidates.txt"
reportPath = subDirectory + "/heule-signatures.txt"
signatureDirectory = subDirectory + "/heule-signatures"
solutionDirectory =  subDirectory + "/heule-unique-signatures"

def checkSolution(subPath):
    global signatureDict, solutionCount, signatureCount
    path = subDirectory + '/' + subPath
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(path)
    except Exception as ex:
        print "ERROR: Could not extract solution from file '%s' (%s)" % (path, str(ex))
        return
    solutionCount += 1
    sc = s.canonize()
    sig = sc.kernelTerms.signature()
    list = signatureDict[sig] if sig in signatureDict else []
    if len(list) == 0:
        print "File %s has new signature '%s'" % (subPath, sig)
        signatureCount += 1
        sname = solutionDirectory + '/solution-%.2d.exp' % signatureCount
        skip = False
        try:
            sfile = open(sname, 'w')
        except:
            print "Couldn't open file '%s'" % sname
            skip = True
        if not skip:
            sc.printPolynomial(sfile)
            sfile.close()
    else:
        print "File %s has same signature as %d other files" % (subPath, len(list))        
    list.append(path)
    signatureDict[sig] = list

def process():
    try:
        cfile = open(candidatePath, 'r')
    except:
        print "Cannot open file '%s'" % candidatePath
        return
    try:
        ofile = open(reportPath, 'w')
    except:
        print "Cannot open file '%s'" % reportPath
        return

    for line in cfile:
        line = brent.trim(line)
        checkSolution(line)

    cfile.close()

    print "%d solutions, %d unique signatures" % (solutionCount, signatureCount)
    fields = ["Sig #", "Signature", "Count"]
    ofile.write("\t".join(fields) + '\n')

    keys = sorted(signatureDict.keys())
    for idx in range(signatureCount):
        k = keys[idx]
        fields = ["%.2d" % (idx+1), k, str(len(signatureDict[k]))]
        ofile.write("\t".join(fields) + '\n')

        sname = signatureDirectory + ('/sig-%.2d.txt' % (idx + 1))
        try:
            sfile = open(sname, 'w')
        except:
            print "Couldn't open file '%s'" % sname
            continue
        plist = sorted(signatureDict[k])
        for p in plist:
            sfile.write(p + '\n')
        sfile.close()
    ofile.close()
    
if __name__ == "__main__":
    process()
