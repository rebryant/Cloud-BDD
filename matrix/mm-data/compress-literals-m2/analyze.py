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
import getopt

def usage(name):
    print("%s: [-h] [-u] [-r a|m|c] f1.log f2.log ..." % name)
    print(" -h      Print this message")
    print(" -u      Uniform time format (for charting with Excel)")
    print(" -r RPT  Specify reporting parameter; a (all combined), m (max), c (computed)")

totalConjuncts = 81
# Choices are 'combined', 'max', or 'computed'
reportField = 'combined'
resolution = 10.0
floatFormat = "%.0f"
# In uniformTime format, create uniform axis for all time points
# Otherwise, track each time for each series
uniformTime = False

# Are we in a pass to collect all time points?
recordTime = True
globalMaxTime = 0
timePoints = []

# Should we continue display of line to timeout?
showTimeout = False

# Expressions
initialMatcher = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Initial simplification.  Total ([0-9]+) .*  Max ([0-9]+)")
partialMatcher = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Partial result with ([0-9]+) values.  Max size = ([0-9]+).  Combined size = ([0-9]+).  Computed size = ([0-9]+)")
zeroMatcher = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Conjunction of 81 elements.  Encountered zero-valued conjunct with ([0-9]+)")
timeoutMatcher = re.compile("Error: Timeout after ([0-9]+)")
completedMatcher = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Conjunction result ([0-9]+)")
rootMatcher = re.compile("-(m[0-9]+\+[0-9]+)-")

def resolve(x):
    if not uniformTime:
        return x
    return resolution * int(x/resolution)

def getInitial(line):
    m = initialMatcher.match(line)
    if m:
        conjunctCount = totalConjuncts
        elapsed = resolve(float(m.group(1)))
        combinedSize = int(m.group(2))
        maxSize = int(m.group(3))
        computedSize = maxSize
        return {"type" : "initial", "elapsed" : elapsed, "conjuncts" : conjunctCount, "max" : maxSize, "combined" : combinedSize, "computed" : computedSize }

def getPartial(line):
    m = partialMatcher.match(line)
    if m:
        elapsed = resolve(float(m.group(1)))
        conjunctCount = int(m.group(2))
        maxSize = int(m.group(3))
        combinedSize = int(m.group(4))
        computedSize = int(m.group(5))
        return {"type" : "partial", "elapsed" : elapsed, "conjuncts" : conjunctCount, "max" : maxSize, "combined" : combinedSize, "computed" : computedSize }
    return None

def getZero(line):
    m = zeroMatcher.match(line)
    if m:
        elapsed = resolve(float(m.group(1)))
        conjunctCount = int(m.group(2))
        combinedSize = 1
        computedSize = 1
        maxSize = 1
        return {"type" : "zero", "elapsed" : elapsed, "conjuncts" : conjunctCount, "max" : maxSize, "combined" : combinedSize, "computed" : computedSize }
    return None

def getTimeout(line):
    m = timeoutMatcher.match(line)
    if m:
        time = resolve(int(m.group(1)))
        return {"type" : "timeout", "elapsed" : time }
    return None
        
def getCompleted(line):
    m = completedMatcher.match(line)
    if m:
        elapsed = resolve(float(m.group(1)))
        resultSize = int(m.group(2))
        maxSize = resultSize
        combinedSize = resultSize
        return {"elapsed" : elapsed, "type": "completed",  "max" : maxSize, "combined" : combinedSize, "computed" : resultSize }
    return None

def processLine(line):
    global timePoints, globalMaxTime
    d = getInitial(line)
    if d is None:
        d = getPartial(line)
    if d is None:
        d = getZero(line)
    if d is None:
        d = getTimeout(line)
    if d is None:
        d = getCompleted(line)
    if d is not None:
        if 'elapsed' in d:
            elapsed = d['elapsed']
            if recordTime and elapsed not in timePoints:
                if showTimeout or d['type'] != 'timeout':
                    timePoints.append(elapsed)
                    globalMaxTime = max(globalMaxTime, elapsed)
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
# maxTime

def processFile(fname):
    maxTime = 0
    resultDict = { "root" : getRoot(fname), "partials" : {}, "type" : "unknown",  }
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
            e = { k : d[k] for k in ['elapsed', 'max', 'combined', 'computed', 'conjuncts'] }
            elapsed = d['elapsed']
            resultDict['partials'][elapsed] = e
            lastE = e
            maxTime = max(maxTime, elapsed)
        elif t in [ 'timeout' ]:
            resultDict['type'] = t
            # Create terminal event
            elapsed = d['elapsed']
            conjunctCount -= 1
            e = { 'elapsed' : elapsed, 'conjunct' : conjunctCount }
            if lastE is not None:
                for k in ['max', 'combined', 'computed']:
                    e[k] = lastE[k]
            resultDict['partials'][elapsed] = e
            if showTimeout:
                maxTime = max(maxTime, elapsed)
            break
        elif t in [ 'zero', 'completed' ]:
            resultDict['type'] = t
            # Create terminal event
            e = { k : d[k] for k in ['elapsed', 'max', 'combined', 'computed'] }
            elapsed = d['elapsed']
            conjunctCount -= 1
            e['conjuncts'] = conjunctCount
            resultDict['partials'][elapsed] = e
            maxTime = max(maxTime, elapsed)
            break
        else:
            print("Unknown line type '%s'" % t)
    f.close()
    resultDict['maxTime'] = maxTime
    return resultDict

def generateUniformTimes():
    global timePoints
    # Create new set of time points
    ntimes = int(globalMaxTime/resolution) + 1
    timePoints = [resolution * i for i in range(ntimes)]
    ifields = [""]
    ifields += [floatFormat % t for t in timePoints]
    print("\t".join(ifields))    

def generateUniformOutput(fileDict):
    partials = fileDict['partials']
    maxTime = fileDict['maxTime']
    tfields = ["" for t in timePoints]
    for i in range(len(timePoints)):
        t = timePoints[i]
        if t in partials:
            e = partials[t]
            tfields[i] = str(e[reportField])
        elif i > 0 and t <= maxTime:
            tfields[i] = tfields[i-1]
    label = fileDict['root'] + "-" + fileDict['type'][0]
    vfields = [label] + tfields
    print("\t".join(vfields))

def generateNonuniformOutput(fileDict):
    partials = fileDict['partials']
    timePoints = sorted(list(partials.keys()))
    tfields = [""] + ["%.1f" % t for t in timePoints]
    print("\t".join(tfields))
    label = fileDict['root'] + "-" + fileDict['type'][0]
    vfields = [label] + [str(partials[t][reportField]) for t in timePoints]
    print("\t".join(vfields))

def uniformRun(flist):
    global recordTime
    recordTime = True
    # Pass one:  Gather all time points
    for fname in flist:
        dict = processFile(fname)
    generateUniformTimes()
    recordTime = False
    for fname in flist:
        dict = processFile(fname)
        if dict is not None and dict['type'] != 'unknown':
            generateUniformOutput(dict)

def nonuniformRun(flist):
    global recordTime
    recordTime = False
    for fname in flist:
        dict = processFile(fname)
        if dict is not None and dict['type'] != 'unknown':
            generateNonuniformOutput(dict)



def run(name, args):
    global reportField, uniformTime
    optlist, nargs = getopt.getopt(args, 'hur:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-u':
            uniformTime = True
        elif opt == '-r':
            if val == 'a':
                reportField = 'combined'
            elif val == 'm':
                reportField = 'max'
            elif val == 'c':
                reportField = 'computed'
            else:
                print("Reporting field must be a, m, or c")
                return
        else:
            print("Unknown option '%s'" % opt)
            return
    if uniformTime:
        uniformRun(nargs)
    else:
        nonuniformRun(nargs)

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
    
