#!/usr/bin/python

# Tabulate individual probabilities from Heule & generated solutions

import os
import sys

import brent
import circuit
import mm_parse

ckt = circuit.Circuit()


# How many solutions have been processed
solutionCount = 0
maxCount = 200000

# Mapping from variables to counts of number of times set to 1
countMap = {}
# List of all term names
termNames = []

dim = (3, 3, 3)
auxCount = 23

# Put Brent variable into simpler format
def pairForm(var):
    return (var.generateTerm(var.symbol == 'c'), var.level)

def processFile(path):
    global countMap, solutionCount, termNames
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(path)
    except Exception as ex:
        print("ERROR: Could not extract solution from file '%s' (%s)" % (path, str(ex)))
        return
    for lit in s.assignment.literals():
        pair = pairForm(lit.variable)
        if pair[0] not in termNames:
            termNames.append(pair[0])
        if pair in countMap:
            countMap[pair] += lit.phase
        else:
            countMap[pair] = lit.phase
    solutionCount += 1

def processDatabases():
    databaseDict = {}
    mm_parse.loadDatabase(databaseDict, mm_parse.heuleDatabasePathFields, True)
    mm_parse.loadDatabase(databaseDict, mm_parse.generatedDatabasePathFields, True)
    print ("Loaded database with %d entries" % len(databaseDict))
    for entry in databaseDict.values():
        path = entry[mm_parse.fieldIndex['path']]
        processFile(path)
        if solutionCount >= maxCount:
            break
    print ("Processed %d entries.  %d term names" % (solutionCount, len(termNames)))
    
def printList(ls):
    sls = [str(x) for x in ls]
    print("\t".join(sls))

def report():
    global termNames
    termNames.sort()
    # Mapping from (term, level) to [0.0, 1.0]
    printList([""] + termNames)
    for l in brent.unitRange(auxCount):
        probs = [float(countMap[(t, l)])/solutionCount for t in termNames]
        printList([l] + probs)
        
        

def run(name, args):
    processDatabases()
    report()



if __name__ == "__main__":
    current = os.path.realpath(__file__)
    mm_parse.homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])
