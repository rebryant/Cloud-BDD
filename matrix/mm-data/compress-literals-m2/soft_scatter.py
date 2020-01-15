#!/usr/bin/python

# Generate scatter plot data for soft-and results

import sys
import getopt
import soft_and

def usage(name):
    print("Usage: %s [-h] [-x c|r] [-m MIN] [-M MAX] [-f INFILE] [-o OUTFILE] -v VERB" % usage)
    print(" -h      Print this message")
    print(" -x DAT  Choose X data (c = coverage, r = other:this size ratio)")
    print(" -m MIN  Minimum argument size")
    print(" -M MAX  Maximum argument size")
    print(" -f INF  Input file")
    print(" -f OUTF Output file")
    print(" -v VERB Verbosity level.  >= 1: also print general information")

xData = "ratio" # Can also be "coverage"

xDataMap = { 'r' : "ratio", 'c' : "coverage"}

minSize = None
maxSize = None

inFile = sys.stdin
outFile = sys.stdout

verbLevel = 1

def summarize(eset, category):
    count = len(eset)
    sizes = [e.size for e in eset.entryList]
    mins = min(sizes) if len(sizes) > 0 else 0
    maxs = max(sizes) if len(sizes) > 0 else 0
    total = eset.totalTime()
    average = 0.0 if count == 0 else total/count
    soft_and.showList([category, count, mins, maxs, total, average], outFile = outFile)


# Print general information about results
def generalReport(eset):
    soft_and.showList(["Cat", "Count", "msize", "Msize", "Time", "Avg"], outFile = outFile)
    summarize(eset, "Total")
    summarize(eset.withOutcome("skipped"), "Skip")
    summarize(eset.withOutcome("tooBig"), "TBig")
    summarize(eset.withOutcome("tooMany"), "TMany")
    summarize(eset.withOutcome("completed"), "Done")
    sset = eset.successes()
    summarize(sset, "Succ")

def process():
    eset = soft_and.EntrySet().fromFile(inFile)
    if len(eset) == 0:
        return
    eset = eset.sizeRange(minSize, maxSize)
    if verbLevel > 0:
        generalReport(eset)
        outFile.write('\n')
    sset = eset.successes()
    yFunction = lambda e: e.reduction()
    yLabel = 'red'
    xFunction = None
    yLabel = None
    if xData == 'ratio':
        xFunction = lambda e : e.otherRatio()
        xLabel = 'ratio'
    if xData == 'coverage':
        xFunction = lambda e : e.coverage
        xLabel = 'cov'
    if xFunction is None:
        print("Invalid x data category '%s'" % xData)
        return
    sset.scatterData(xFunction, yFunction, outFile, xLabel, yLabel)

def run(name, args):
    global xData, minSize, maxSize, inFile, outFile, verbLevel
    optList, nargs = getopt.getopt(args, 'hx:m:M:f:o:v:')
    for (opt, val) in optList:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-x':
            if val in xDataMap:
                xData = xDataMap[val]
            else:
                print("Unknown data option '%s'" % val)
                usage(name)
                return
        elif opt == '-M':
            maxSize = int(val)
        elif opt == '-m':
            minSize = int(val)
        elif opt == '-f':
            try:
                inFile = open(val, 'r')
            except Exception as ex:
                print("Couldn't open input file '%s' (%s)" % (val, str(ex)))
                return
        elif opt == '-o':
            try:
                outFile = open(val, 'w')
            except Exception as ex:
                print("Couldn't open output file '%s' (%s)" % (val, str(ex)))
                return
        elif opt == '-v':
            verbLevel = int(val)
    process()
    if inFile != sys.stdin:
        inFile.close()
    if outFile != sys.stdout:
        outFile.close()
    
if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
        
