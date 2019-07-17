#!/usr/bin/python

# Scan through existing solutions
# Convert to canonical form and generate signature
# See how many unique signatures there are, and analyze their properties
# For each unique signature, store copy of one solution with this signature.

import sys
import os
import os.path
import glob

import circuit
import brent


# Map from signature to list of file paths
signatureDict = {}
# Map from signature to its hash 
hashDict = {}
schemeDict = {}
solutionCount = 0
signatureCount = 0

dim = (3,3,3)
auxCount = 23

ckt = circuit.Circuit()

subDirectory = "mm-solutions"

candidatePath = subDirectory + "/heule-candidates.txt"
reportPath = subDirectory + "/signatures.txt"
signatureDirectory = subDirectory + "/signatures"
solutionDirectory =  subDirectory + "/unique-signatures"
sourceDirectory = subDirectory + "/heule-online"

generatedSourceDirectory = subDirectory + "/generated"
generatedSolutionCount = 0
generatedSignatureCount = 0
generatedSignatureDict = {}

def checkSolution(subPath):
    global signatureDict, solutionCount, signatureCount, schemeDict, hashDict
    path = sourceDirectory + '/' + subPath
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(path)
    except Exception as ex:
        print("ERROR: Could not extract solution from file '%s' (%s)" % (path, str(ex)))
        return
    solutionCount += 1
    sc = s.canonize()
    sig = sc.kernelTerms.signature()
    list = signatureDict[sig] if sig in signatureDict else []
    if len(list) == 0:
        print("File %s has new signature '%s'" % (subPath, sig))
        signatureCount += 1
        hashDict[sig] = sc.kernelTerms.sign()
        schemeDict[sig] = sc
    else:
        print("File %s has same signature as %d other downloaded files" % (subPath, len(list)))        
    list.append(path)
    signatureDict[sig] = list

def checkGeneratedSolution(subPath):
    global signatureDict, solutionCount, signatureCount, hashDict, schemeDict
    global generatedSignatureDict, generatedSolutionCount, generatedSignatureCount

    path = generatedSourceDirectory + '/' + subPath
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(path)
    except Exception as ex:
        print("ERROR: Could not extract solution from file '%s' (%s)" % (path, str(ex)))
        return
    generatedSolutionCount += 1
    sc = s.canonize()
    sig = sc.kernelTerms.signature()
    list = generatedSignatureDict[sig] if sig in generatedSignatureDict else []
    if len(list) == 0:
        generatedSignatureCount += 1
        if sig in signatureDict:
            print("File %s has new signature among generated solutions '%s'" % (subPath, sig))
        else:
            print("File %s has new signature '%s'" % (subPath, sig))
            signatureCount += 1
            hashDict[sig] = sc.kernelTerms.sign()
            signatureDict[sig] = [path]
            schemeDict[sig] = sc
    else:
        print("File %s has same signature as %d other generated files" % (subPath, len(list)))        
    list.append(path)
    generatedSignatureDict[sig] = list


def processHeule():
    try:
        cfile = open(candidatePath, 'r')
    except:
        print("Cannot open file '%s'" % candidatePath)
        return

    for line in cfile:
        line = brent.trim(line)
        checkSolution(line)

    cfile.close()

def processGenerated():
    template = generatedSourceDirectory + "/*.exp"
    fpaths = glob.glob(template)
    for fpath in fpaths:
        path = fpath[len(generatedSourceDirectory) + 1:]
        checkGeneratedSolution(path)


def saveScheme(id, scheme):
    sname = solutionDirectory + '/solution-%.2d.exp' % id
    try:
        sfile = open(sname, 'w')
    except:
        print("Couldn't open file '%s'" % sname)
        return
    scheme.printPolynomial(sfile)
    sfile.close()


def process():

    processHeule()
    
    heuleCountDict = { k : len(signatureDict[k]) for k in signatureDict.keys() }

    processGenerated()

    try:
        ofile = open(reportPath, 'w')
    except:
        print("Cannot open file '%s'" % reportPath)
        return

    
    totalCount = solutionCount + generatedSolutionCount
    print("%d solutions (%d downloaded, %d generated), %d unique signatures" % (totalCount, solutionCount, generatedSolutionCount, signatureCount))


    fields = ["Sig #", "Hash", "Downloaded", "Generated", "Signature"]
    ofile.write("\t".join(fields) + '\n')

    keys = sorted(signatureDict.keys())
    for idx in range(signatureCount):
        k = keys[idx]
        hcount = heuleCountDict[k] if k in heuleCountDict else 0
        gcount = len(generatedSignatureDict[k]) if k in generatedSignatureDict else 0
        fields = ["%.2d" % (idx+1), hashDict[k], str(hcount), str(gcount), k]
        ofile.write("\t".join(fields) + '\n')
        saveScheme(idx+1, schemeDict[k])
        sname = signatureDirectory + ('/sig-%.2d.txt' % (idx + 1))
        try:
            sfile = open(sname, 'w')
        except:
            print("Couldn't open file '%s'" % sname)
            continue
        plist = sorted(signatureDict[k])
        for p in plist:
            sfile.write(p + '\n')
        sfile.close()
    ofile.close()
    
if __name__ == "__main__":
    process()
