#!/usr/bin/python

# Scan through existing solutions
# See many additions each requires
# Tabulate

import sys
import os
import os.path

import circuit
import brent


# Map from add count to list of file paths
addcountDict = {}
solutionCount = 0

dim = (3,3,3)
auxCount = 23

ckt = circuit.Circuit()

subDirectory = "mm-solutions"

candidatePath = subDirectory + "/heule-candidates.txt"
reportPath = subDirectory + "/heule-addcounts.txt"
minimumPath = subDirectory + "/heule-minadditions.txt"
sourceDirectory = subDirectory + "/heule-online"

def checkSolution(subPath):
    global addcountDict, solutionCount
    path = sourceDirectory + '/' + subPath
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(path)
    except Exception as ex:
        print "ERROR: Could not extract solution from file '%s' (%s)" % (path, str(ex))
        return
    solutionCount += 1
    count = s.addCount()
    list = addcountDict[count] if count in addcountDict else []
    if len(list) == 0:
        print "File %s has new addition count '%d'" % (subPath, count)
    list.append(subPath)
    addcountDict[count] = list

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

    print "%d solutions" % (solutionCount)
    fields = ["Additions", "Count"]
    ofile.write("\t".join(fields) + '\n')

    keys = sorted(addcountDict.keys())
    for k in keys:
        fields = [str(k), str(len(addcountDict[k]))]
        ofile.write("\t".join(fields) + '\n')
    ofile.close()

    try:
        sfile = open(minimumPath, 'w')
    except:
        print "Couldn't open file '%s'" % minimumPath
        return
    plist = sorted(addcountDict[keys[0]])
    for p in plist:
        sfile.write(p + '\n')
    sfile.close()

    
if __name__ == "__main__":
    process()
