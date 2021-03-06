#!/usr/bin/python
# Generate histogram information about probabilities in variable-probabilities.txt

import getopt
import os
import sys

import circuit
import brent
import random

def usage(name):
    print("Usage: %s [-h] [-H] [-v N] [-k K] [-o OFILE]")
    print("  -h          Print this message")
    print("  -H          Generate histogram")
    print("  -n N        Select assignments for N variables")
    print("  -k K        Select exponent in weight computation")
    print("  -o OFILE    Write output to OFILE")


# set to home directory for program, split into tokens
homePathFields = ['.']
pathFields = ["analysis", "smirnov", "variable-probabilities.txt"]

def getPath():
    fields = homePathFields + pathFields
    return "/".join(fields)

# Histogram parameters
bucketCount = 102
buckets = {}
variableCount = 0

# Selection parameters
# Exponent used in computing weights
exponent = 5.0

# Brent variables that are always 1 or 0
highVariables = []
lowVariables = []
# Mapping from Brent variable to weight
weightedVariables = {}
# List of variable terms
termList = []

def assignBucket(v):
    if v == 0.0:
        return 0
    return 1 + int(v * (bucketCount-2))

def bucketRange(b):
    if b == 0:
        return "[0.000, 0.000]"
    if b == bucketCount-1:
        return "[1.000, 1.000]"
    lbrack = "(" if b == 1 else "["
    vmin = float(b-1)/(bucketCount-2)
    vmax = float(b)/(bucketCount-2)
    return "%s%.3f, %.3f)" % (lbrack, vmin, vmax)

def generateHistogram(path):
    global variableCount, buckets
    try:
        inf = open(path, 'r')
    except Exception as ex:
        print("Couldn't open file '%s' (%s)" % (path, str(ex)))
        return
    first = True
    count = 0
    for line in inf:
        if first:
            first = False
            continue
        fields = line.split('\t')[1:]
        for sv in fields:
            v = float(sv)
            b = assignBucket(v)
            if b in buckets:
                buckets[b] += 1
            else:
                buckets[b] = 1
            variableCount += 1
    inf.close()
        
def showHistogram():
    print ("Interval\tCount\tFrac\tCumm\Frac")
    cumm = 0
    for b in range(bucketCount):
        cnt = buckets[b] if b in buckets else 0
        fcnt = float(cnt)/variableCount
        cumm += cnt
        fcumm = float(cumm)/variableCount
        print ("%s\t%d\t%.3f\t%d\t%.3f" % (bucketRange(b), cnt, fcnt, cumm, fcumm))
    
def weight(p):
    return (1.0-p)**exponent

def generateVariables(path):
    global variableCount, highVariables, lowVariables, weightedVariables, termList
    try:
        inf = open(path, 'r')
    except Exception as ex:
        print("Couldn't open file '%s' (%s)" % (path, str(ex)))
        return
    first = True
    count = 0
    level = 1
    for line in inf:
        if first:
            termList = line.split('\t')[1:]
            first = False
            continue
        fields = line.split('\t')[1:]
        vars = [brent.BrentVariable(level = level).fromTerm(t, permuteC = True) for t in termList]
        weights = [float(sv) for sv in fields]
        for (v, w) in zip(vars, weights):
            if w == 0.0:
                lowVariables.append(v)
            elif w == 1.0:
                highVariables.append(v)
            else:
                weightedVariables[v] = weight(w)
            count += 1
        level += 1
    inf.close()
    print("Total: %d.  Hi: %d.  Lo: %d.  Weighted: %d" % (count, len(highVariables), len(lowVariables), len(weightedVariables)))
    

def select(count):
    lowLiterals = [brent.Literal(v, 0) for v in lowVariables]
    highLiterals = [brent.Literal(v, 1) for v in highVariables]
    forcedLiterals = lowLiterals + highLiterals
    if count == 0:
        count = len(forcedLiterals)
    forcedCount = min(count, len(forcedLiterals))
    literals = random.sample(forcedLiterals, forcedCount)
    count -= forcedCount
    if count == 0:
        print("Returning %d literals, all forced" % forcedCount)
        literals.sort()
        return literals
    print("Choosing %d unforced literals" % count)
    while count > 0:
        vlist = weightedVariables.keys()
        vlist.sort()
        wlist = [weightedVariables[v] for v in vlist]
        tweight = sum(wlist)
        x = random.uniform(0, tweight)
        sofar = 0
        for (v, w) in zip(vlist, wlist):
            sofar += w
            if sofar >= x:
                literals.append(brent.Literal(v, 0))
                count -= 1
                del weightedVariables[v]
                break
    literals.sort()
    return literals
    
def process(path, count, opath = None):
    generateVariables(path)
    literals = select(count)
    if opath is None:
        ofile = sys.stdout
    else:
        try:
            ofile = open(opath, 'w')
        except Exception as ex:
            print("Couldn't open output file '%s' (%s)" % (opath, str(ex)))
            return
    for lit in literals:
        ofile.write(str(lit) + '\n')

def run(name, args):
    global exponent
    path = getPath()
    count = 0
    opath = None
    optlist, args = getopt.getopt(args, 'hHk:n:o:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-H':
            generateHistogram(path)
            showHistogram()
            return
        elif opt == '-k':
            exponent = float(val)
        elif opt == '-n':
            count = int(val)
        elif opt == '-o':
            opath = val
    process(path, count, opath)


if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])
    
