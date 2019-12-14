#!/usr/bin/python3

# Extract data from partial execution
# Extraction cases either
#  Time out
#  Found Zero-valued conjunct 
#  Completed (successfully)

# Generated entries (two lines per file)
# Line 1:
#   2 Blanks + elapsed times for each conjunct
# Line 2:
#   Status (timeout, zero, success)
#   Combination levels
#   Combined size after each conjunct

import sys
import re

totalConjuncts = 81
reportField = 'combined'

# Expressions
initialMatcher = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Initial simplification.  Total ([0-9]+) .*  Max ([0-9]+)")
partialMatcher = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Partial result with ([0-9]+) values.  Max size = ([0-9]+).  Combined size = ([0-9]+).  Computed size = ([0-9]+)")
zeroMatcher = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Conjunction of 81 elements.  Encountered zero-valued conjunct with ([0-9]+)")
timeoutMatcher = re.compile("Error: Timeout after ([0-9]+)")
completedMatcher = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Conjunction result ([0-9]+)")
rootMatcher = re.compile("-(m[0-9]+\+[0-9]+)-")

def getInitial(line):
    m = initialMatcher.match(line)
    if m:
        conjunctCount = totalConjuncts
        elapsed = float(m.group(1))
        combinedSize = int(m.group(2))
        maxSize = int(m.group(3))
        computedSize = maxSize
        return {"type" : "partial", "elapsed" : elapsed, "conjuncts" : conjunctCount, "max" : maxSize, "combined" : combinedSize, "computed" : computedSize }

def getPartial(line):
    m = partialMatcher.match(line)
    if m:
        elapsed = float(m.group(1))
        conjunctCount = int(m.group(2))
        maxSize = int(m.group(3))
        combinedSize = int(m.group(4))
        computedSize = int(m.group(5))
        return {"type" : "partial", "elapsed" : elapsed, "conjuncts" : conjunctCount, "max" : maxSize, "combined" : combinedSize, "computed" : computedSize }
    return None

def getZero(line):
    m = zeroMatcher.match(line)
    if m:
        elapsed = float(m.group(1))
        conjunctCount = int(m.group(2))
        combinedSize = 1
        computedSize = 1
        maxSize = 1
        return {"type" : "zero", "elapsed" : elapsed, "conjuncts" : conjunctCount, "max" : maxSize, "combined" : combinedSize, "computed" : computedSize }
    return None

def getTimeout(line):
    m = timeoutMatcher.match(line)
    if m:
        time = int(m.group(1))
        return {"type" : "timeout", "elapsed" : time }
    return None
        
def getCompleted(line):
    m = completedMatcher.match(line)
    if m:
        elapsed = float(m.group(1))
        resultSize = int(m.group(2))
        maxSize = resultSize
        combinedSize = resultSize
        return {"elapsed" : elapsed, "type": "completed",  "max" : maxSize, "combined" : combinedSize, "computed" : resultSize }
    return None

def processLine(line):
    d = getInitial(line)
    if d is not None:
        return d
    d = getPartial(line)
    if d is not None:
        return d
    d = getZero(line)
    if d is not None:
        return d
    d = getTimeout(line)
    if d is not None:
        return d
    d = getCompleted(line)
    if d is not None:
        return d

def getRoot(fname):
    m = rootMatcher.search(fname)
    if m is None:
        return fname
    return m.group(1)


# Generate dictionary from file:
# root: root levels
# type: completed | zero | timeout
# partials: Dictionary indexed by conjunct count.
#             elapsed, combined, max, computed

def processFile(fname):
    resultDict = { "root" : getRoot(fname), "partials" : {}, "type" : "unknown" }
    try:
        f = open(fname, 'r')
    except Exception as ex:
        print("Couldn't open file %s (%s)" % (fname, str(ex)))
        return
    lastE = None
    conjunctCount = totalConjuncts
    for line in f:
        d = processLine(line)
        if d is None:
            continue
        t = d["type"]
        if t in ["initial", "partial"]:
            # Create event
            e = { k : d[k] for k in ['elapsed', 'max', 'combined', 'computed'] }
            conjunctCount = d['conjuncts']
            resultDict['partials'][conjunctCount] = e
            lastE = e
        elif t in [ 'timeout' ]:
            resultDict['type'] = t
            # Create terminal event
            conjunctCount -= 1
            e = { k : d[k] for k in ['elapsed'] }
            if lastE is not None:
                for k in ['max', 'combined', 'computed']:
                    e[k] = lastE[k]
            resultDict['partials'][conjunctCount] = e
            break
        elif t in [ 'zero', 'completed' ]:
            resultDict['type'] = t
            # Create terminal event
            e = { k : d[k] for k in ['elapsed', 'max', 'combined', 'computed'] }
            conjunctCount -= 1
            resultDict['partials'][conjunctCount] = e
            break
        else:
            print("Unknown line type '%s'" % t)
    f.close()
    return resultDict

def generateOutput(fileDict):
    partials = fileDict['partials']
    keys = list(partials.keys())
    keys.sort()
    keys.reverse()
    ifields = ["", ""]
    ifields += ["%.1f" % partials[k]['elapsed'] for k in keys]
    print("\t".join(ifields))
    vfields = [fileDict['type'], fileDict['root']]
    vfields += [str(partials[k][reportField]) for k in keys]
    print("\t".join(vfields))

def run(flist):
    for fname in flist:
        dict = processFile(fname)
        if dict is not None:
            generateOutput(dict)

if __name__ == "__main__":
    run(sys.argv[1:])

    
