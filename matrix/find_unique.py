#!/usr/bin/python

# Scan through existing solutions
# Convert to canonical form and see whether unique
# Store canonical representations

import sys
import os
import os.path

import circuit
import brent


# Map from canonical polynomial to list of file paths
signatureDict = {}
solutionCount = 0
signatureCount = 0

dim = (3,3,3)
auxCount = 23

ckt = circuit.Circuit()

subDirectory = "mm-solutions"

candidatePath = subDirectory + "/heule-candidates.txt"
sourceDirectory = subDirectory + "/heule-online"
canonicalDirectory = subDirectory + "/heule-canonical"

databasePath = subDirectory + "/heule-database.txt"
databaseFile = None


def newCanonical(scheme, subPath):
    fields = subPath.split('/')
    path = canonicalDirectory
    for dir in fields[:-1]:
        path = path + '/' + dir
        if not os.path.exists(path):
            try:
                os.mkdir(path)
            except:
                print "Couldn't create subdirectory '%s'" % path
                return
    path = path + '/' + fields[-1]
    try:
        ofile = open(path, 'w')
    except Exception as ex:
        print "Couldn't open file '%s' (%s)" % (path, str(ex))
        return
    scheme.printPolynomial(ofile)
    ofile.close()
    if databaseFile is not None:
        fields = [scheme.sign(), str(scheme.addCount()), scheme.kernelTerms.sign(), path]
        databaseFile.write("\t".join(fields) + '\n')

def checkSolution(subPath):
    global signatureDict, solutionCount, signatureCount
    path = sourceDirectory + '/' + subPath
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(path)
    except Exception as ex:
        print "ERROR: Could not extract solution from file '%s' (%s)" % (path, str(ex))
        return
    solutionCount += 1
    sc = s.canonize()
    sig = sc.signature()
    list = signatureDict[sig] if sig in signatureDict else []
    if len(list) == 0:
        print "File %s has unique solution" % (subPath)
        signatureCount += 1
        newCanonical(sc, subPath)
    else:
        print "File %s has same solution as %d other files" % (subPath, len(list))
    list.append(path)
    signatureDict[sig] = list

def process():
    global databaseFile
    try:
        databaseFile = open(databasePath, 'w')
    except:
        print "Couldn't open database file '%s' (%s)" % (databasePath, str(ex))
        databaseFile = None

    fields = ["Hash", "Adds", "K Hash", "Path"]
    databaseFile.write("\t".join(fields) + '\n')


    try:
        cfile = open(candidatePath, 'r')
    except:
        print "Cannot open file '%s'" % candidatePath
        return
    for line in cfile:
        line = brent.trim(line)
        checkSolution(line)
    cfile.close()
    databaseFile.close()
    print "%d solutions, %d unique" % (solutionCount, signatureCount)
    


if __name__ == "__main__":
    process()
