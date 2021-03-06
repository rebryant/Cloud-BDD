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

# 1/9/2019.  With file-based conjunction, changed what data is collected.
# Use sum of sizes rather than combined size of entire DD.
# But, in past found these two numbers were essentially identical.

import sys
import re
import getopt

def usage(name):
    print("%s: [-h] [-u] [-r p|a|m|c] [-R RES] [-T MAXT] f1.log f2.log ..." % name)
    print(" -h      Print this message")
    print(" -u      Uniform time format (for charting with Excel)")
    print(" -r RPT  Specify reporting parameter; p (products, default), a (all combined), m (max), c (computed)")
    print(" -R RES  Specify resolution (in seconds)")
    print(" -T MAXT Upper limit on operations to consider")

totalConjuncts = 81
# Choices are 'combined', 'max', 'computed', or 'products'
reportField = 'product'
# What is the resolution for reporting events over time
resolution = 10.0
# What is the upper limit of time to report
maxReportTime = 1000000000

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
# Two different matchers, due to shift in reported parameters
partialMatcher1 = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Partial result with ([0-9]+) values.  Max size = ([0-9]+).  Combined size = ([0-9]+).  Computed size = ([0-9]+)")
partialMatcher2 = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Partial result with ([0-9]+) values.  Max size = ([0-9]+).  Sum size = ([0-9]+).  Resident size = [0-9]+.  Computed size = ([0-9]+)")
zeroMatcher = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Conjunction of 81 elements.  Encountered zero-valued conjunct with ([0-9]+)")
timeoutMatcher = re.compile("Error: Timeout after ([0-9]+)")
completedMatcher = re.compile("Elapsed time ([0-9]+\.[0-9]+).  Conjunction result ([0-9]+)")
rootMatcher = re.compile("-(m[0-9]+\+[0-9]+)-")

def trim(s):
    while len(s) > 0 and s[-1] == '\n':
        s = s[:-1]
    return s

def resolve(x):
    if not uniformTime:
        return x
    return resolution * int(x/resolution)

def getInitial(line):
    rval = None
    m = initialMatcher.match(line)
    if m:
        productCount = 0
        elapsed = resolve(float(m.group(1)))
        combinedSize = int(m.group(2))
        maxSize = int(m.group(3))
        computedSize = maxSize
        rval =  {"type" : "initial", "elapsed" : elapsed, "products" : productCount, "max" : maxSize, "combined" : combinedSize, "computed" : computedSize }
    return rval

def getPartial(line):
    rval = None
    m = partialMatcher1.match(line)
    if not m:
        m = partialMatcher2.match(line)
    if m:
        elapsed = resolve(float(m.group(1)))
        productCount = totalConjuncts - int(m.group(2))
        maxSize = int(m.group(3))
        combinedSize = int(m.group(4))
        computedSize = int(m.group(5))
        rval =  {"type" : "partial", "elapsed" : elapsed, "products" : productCount, "max" : maxSize, "combined" : combinedSize, "computed" : computedSize }
#    if rval is not None:
#        print("Partials '%s' --> %s" % (line, str(rval)))
    return rval

def getZero(line):
    rval = None
    m = zeroMatcher.match(line)
    if m:
        elapsed = resolve(float(m.group(1)))
        productCount = totalConjuncts # - int(m.group(2))
        combinedSize = 1
        computedSize = 1
        maxSize = 1
        rval = {"type" : "zero", "elapsed" : elapsed, "products" : productCount, "max" : maxSize, "combined" : combinedSize, "computed" : computedSize }
    return rval

def getTimeout(line):
    rval = None
    m = timeoutMatcher.match(line)
    if m:
        time = resolve(int(m.group(1)))
        rval = {"type" : "timeout", "elapsed" : time }
    return rval
        
def getCompleted(line):
    rval = None
    m = completedMatcher.match(line)
    if m:
        elapsed = resolve(float(m.group(1)))
        resultSize = int(m.group(2))
        maxSize = resultSize
        combinedSize = resultSize
        rval = {"elapsed" : elapsed, "type": "completed",  "max" : maxSize, "combined" : combinedSize, "computed" : resultSize }
    return rval

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
# partials: Dictionary indexed by product elapsed.
#             elapsed, combined, max, computed, and products

def processFile(fname):
    maxTime = 0
    resultDict = { "root" : getRoot(fname), "partials" : {}, "type" : "unknown",  }
    try:
        f = open(fname, 'r')
    except Exception as ex:
        print("Couldn't open file %s (%s)" % (fname, str(ex)))
        return
    lastE = None
    productCount = 0
    for line in f:
        line = trim(line)
        d = processLine(line)
        if d is None:
            continue
        t = d["type"]
        if t in ["initial", "partial"]:
            # Create event
            e = { k : d[k] for k in ['elapsed', 'max', 'combined', 'computed', 'products'] }
            elapsed = d['elapsed']
            resultDict['partials'][elapsed] = e
            lastE = e
            maxTime = max(maxTime, elapsed)
        elif t in [ 'timeout' ]:
            resultDict['type'] = t
            # Create terminal event
            elapsed = d['elapsed']
            e = { 'elapsed' : elapsed, 'product' : productCount }
            if lastE is not None:
                for k in ['max', 'combined', 'computed', 'products']:
                    e[k] = lastE[k]
            resultDict['partials'][elapsed] = e
            if showTimeout:
                maxTime = max(maxTime, elapsed)
            break
        elif t in [ 'zero', 'completed' ]:
            resultDict['type'] = t
            # Create terminal event
            e = { k : d[k] for k in ['elapsed', 'max', 'combined', 'computed', 'products'] }
            elapsed = d['elapsed']
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
    reportTime = min(globalMaxTime, maxReportTime)
    ntimes = int(reportTime/resolution) + 1
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
    global reportField, uniformTime, resolution, maxReportTime
    optlist, nargs = getopt.getopt(args, 'hur:R:T:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-u':
            uniformTime = True
        elif opt == '-R':
            resolution = float(val)
        elif opt == '-T':
            maxReportTime = int(val)
        elif opt == '-r':
            if val == 'a':
                reportField = 'combined'
            elif val == 'm':
                reportField = 'max'
            elif val == 'c':
                reportField = 'computed'
            elif val == 'p':
                reportField = 'products'
            else:
                print("Reporting field must be a, m, c, or p")
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
    
