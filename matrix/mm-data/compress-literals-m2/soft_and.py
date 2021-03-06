# Analyze use of soft and in simplifying terms for conjunction

# Sample Lines:

# run-smirnov-m23+14-mode2-lim3500-file-v4.log:Elapsed time 1973.1.  Delta 0.0.  Soft_And.  conj_new2old.  cov = 0.952.  size = 16308.  Other size = 90533.  Skipping
# run-smirnov-m23+14-mode2-lim3500-file-v4.log:Elapsed time 3418.0.  Delta 113.9.  Soft_And.  conj_old2new.  cov = 0.974.  size = 790560.  Other size = 17298.  Lookups = 619774968.  Size --> 790560 (1.000X)
# run-smirnov-m23+14-mode2-lim3500-file-v4.log:Elapsed time 3472.6.  Delta 54.6.  Soft_And.  conj_old2new.  cov = 0.967.  size = 790560.  Other size = 59456.  Lookups = 145967915.  Requires more than 1581120 nodes
# run-smirnov-m14+05-mode2-lim7200-K200-v1.log:Elapsed time 114.9.  Delta 3.3.  Soft_And.  conj_old2new.  cov = 0.977.  size = 81275.  Other size = 66484.  Lookups = 16255001.  Too many cache lookups


import sys
import re

fileMatcher = re.compile("([^:]+):")
deltaMatcher = re.compile("Delta ([0-9]+\.[0-9]+)")
typeMatcher = re.compile("(conj_[a-z]+2[a-z]+)")
coverageMatcher = re.compile("cov = ([0-9]+\.[0-9]+)")
sizeMatcher = re.compile("size = ([0-9]+)\.")
otherSizeMatcher = re.compile("Other size = ([0-9]+)\.")
lookupMatcher = re.compile("Lookups = ([0-9]+)\.")
skipMatcher = re.compile("Skipping")
completedMatcher = re.compile("Size --> ([0-9]+)")
tooBigMatcher = re.compile("Requires more than ([0-9]+) nodes")
tooManyMatcher = re.compile("Too many cache lookups")

def trim(s):
    while len(s) > 0 and s[-1] == '\n':
        s = s[:-1]
    return s

def showList(ls, outFile = sys.stdout, label = None, formatString = None):
    if formatString is None:
        slist = [str(v) for v in ls]
    else:
        slist = [formatString % v for v in ls]
    if label is not None:
        slist = [label] + slist
    outFile.write("\t".join(slist) + '\n')


class Entry:
    fileName = None  # File name
    delta = None     # Delta seconds
    type = None      # Either "conj_new2old" or "conj_old2new"
    coverage = None  # Coverage metric
    size = None      # Size of argment DD
    otherSize = None # Size of other DD
    lookups = None   # Number of cache lookups
    outcome = None   # Either "skipped", "tooBig", "tooMany", or "completed"
    resultSize = None # Size of result, if completed
    tooBigSize = None # Threshold crossed for tooBig

    def parse(self, line):
        fm = fileMatcher.match(line)
        if fm is None:
            self.fileName = ""
        else:
            self.fileName = fm.group(1)

        dm = deltaMatcher.search(line)
        if dm is None:
            print("Failed to find delta in line '%s'" % line)
            return None
        self.delta = float(dm.group(1))

        tm = typeMatcher.search(line)
        if tm is None:
            print("Failed to find type in line '%s'" % line)
            return None
        self.type = tm.group(1)

        cm = coverageMatcher.search(line)
        if cm is None:
            print("Failed to find coverage in line '%s'" % line)
            return None
        self.coverage = float(cm.group(1))

        sm = sizeMatcher.search(line)
        if sm is None:
            print("Failed to find size in line '%s'" % line)
            return None
        self.size = int(sm.group(1))

        om = otherSizeMatcher.search(line)
        if om is None:
            print("Failed to find other size in line '%s'" % line)
            return None
        self.otherSize = int(om.group(1))


        sm = skipMatcher.search(line)
        if sm is not None:
            self.outcome = "skipped"
            self.lookups = 0
            return self

        lm = lookupMatcher.search(line)
        if lm is None:
            print("Failed to find lookups in line '%s'" % line)
            return None
        self.lookups = int(lm.group(1))

        cm = completedMatcher.search(line)
        if cm is not None:
            self.outcome = "completed"
            self.resultSize = int(cm.group(1))
            return self
        tm = tooBigMatcher.search(line)
        if tm is not None:
            self.outcome = "tooBig"
            self.tooBigSize = int(tm.group(1))
            return self
        tm = tooManyMatcher.search(line)
        if tm is not None:
            self.outcome = "tooMany"
            return self
        else:
            print("Failed to find matching type in line '%s'" % line)
            return None
        
    # How much reduction was obtained
    def reduction(self):
        if self.outcome != "completed":
            return 1.0
        return float(self.size)/float(self.resultSize)

    # Define success as having completed and obtained reduction
    def succeeded(self):
        return self.outcome == "completed" and self.reduction() > 1.0

    # How much work was performed per node in argument
    def lookupRatio(self):
        return float(self.lookups)/float(self.size)
    
    # How big was other argument relative to this one
    def otherRatio(self):
        return float(self.otherSize)/float(self.size)

    def lookupsPerMicrosecond(self):
        return float(self.lookups) / self.delta * 1e-6
    
    # If were to limit number of looks to argument size * ratio
    # Would this have completed
    def completeWithLookupRatio(self, ratio):
        if self.outcome != "completed":
            return False
        return self.lookupRatio() <= ratio
        
class EntrySet():
    entryList = []

    def __init__(self, list = []):
        self.entryList = list

    def fromFile(self, inFile):
        self.entryList = []
        lcount = 0
        for line in inFile:
            line = trim(line)
            lcount += 1
            e = Entry().parse(line)
            if e is not None:
                self.entryList.append(e)
        print("Read %d lines.  Got %d entries" % (lcount, len(self.entryList)))
        return self

    def subset(self, predicate):
        nlist = []
        for e in self.entryList:
            if predicate(e):
                nlist.append(e)
        return EntrySet(nlist)

    def withOutcome(self, outcome):
        return self.subset(lambda e: e.outcome == outcome)

    def successes(self):
        return self.subset(lambda e: e.succeeded())

    def totalTime(self):
        return sum([e.delta for e in self.entryList])

    # Generate data for scatter plot,
    # with functions specifying how to computer the X and Y data
    def scatterData(self, xFunction, yFunction, outFile = sys.stdout, xLabel = None, yLabel = None, formatString = "%.3f"):
        pairList = [(xFunction(e), yFunction(e)) for e in self.entryList]
        # Sort by X
        pairList.sort(key = lambda p: p[0])
        showList([p[0] for p in pairList], outFile = outFile, label = xLabel, formatString = formatString)
        showList([p[1] for p in pairList], outFile = outFile, label = yLabel, formatString = formatString)

    def sizeRange(self, minSize = None, maxSize = None):
        eset = self
        if minSize is not None:
            eset = eset.subset(lambda e : e.size >= minSize)
        if maxSize is not None:
            eset = eset.subset(lambda e : e.size < maxSize)
        return eset

    def __getitem__(self, idx):
        return self.entryList[idx]

    def __len__(self):
        return len(self.entryList)

