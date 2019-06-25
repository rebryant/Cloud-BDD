#!/usr/bin/python

# Lines of form: run-ab010-c100-s06-BNON.log:Conjunction BNON Result 170 Max_BDD 237862 Max_combined 270346 Max_sum 270426

import sys

sizeTag = "Max_combined"

# Mapping from mode to dict
# Each dict maps seed to size
modeDict = {}

ab = '000'
c = '000'

seedList = []
modeList = []
# Those modes that aren't dominated by any others
goodModeList = []

# What's max size relative to best for seed to be considered acceptable?
maxRatio = 2.0

# What's the smallest size for each seed?
minBySeed = {}

# For set of modes, find how they would perform as an ensemble
# Return (count, avg), where count is the number of cases at least one of them is below the maxRatio
# and avg is the average for those cases
def score(modeSubList):
    count = 0   # Number of seeds below minRatio
    sum = 0
    worst = 0
    for seed in seedList:
        minSize = None
        minRatio = 1000.0
        for mode in modeSubList:
            if seed not in modeDict[mode]:
                continue
            size = modeDict[mode][seed]
            ratio = float(size) / minBySeed[seed]
            if minSize is None or size < minSize:
                minSize = size
                minRatio = ratio
        sum += minRatio
        worst = max(worst, minRatio)
        if minRatio <= maxRatio:
            count += 1

    return (count, sum/len(seedList), worst)

def trim(s):
    while len(s) > 0 and s[-1] in '\r\n':
        s = s[:-1]
    return s

# Is m1 at least as good as m2 for every seed?
def dominates(m1, m2):
    same = True
    if m1 == m2:
        return False
    d1 = modeDict[m1]
    d2 = modeDict[m2]
    
    for seed in seedList:
        size1 = d1[seed] if seed in d1 else None
        size2 = d2[seed] if seed in d2 else None    
        if size1 is not None and size2 is not None:
            if size1 > size2:
                return False
            if size1 != size2:
                same = False
        if size1 is None:
            return False
    return not same

def dominatorList(m):
    return [om for om in modeList if dominates(om, m)]

def pextract(fname):
    root = fname.split(".")[0]
    fields = root.split("-")
    ab = fields[1][2:]
    c = fields[2][1:]
    seed = fields[3][1:]
    mode = fields[4]
    return {'ab' : ab, 'c' : c, 'seed' : seed, 'mode' : mode}

def process(line):
    global ab, c, modeDict, modeList, seedList
    fields = line.split()
    fname = fields[0].split(':')[0]
    params = pextract(fname)
    ab = params['ab']
    c = params['c']
    mode = params['mode']
    seed = params['seed']
    if mode not in modeList:
        modeList.append(mode)
    if seed not in seedList:
        seedList.append(seed)
    size = 0
    for idx in range(len(fields)-1):
        if fields[idx] == sizeTag:
            size = int(fields[idx+1])
            break
    if mode not in modeDict:
        modeDict[mode] = {}
    modeDict[mode][seed] = size

def showFields(fields):
    sfields = [str(f) for f in fields]
    print "\t".join(sfields)

# Header
showFields(["ab",  ab])
showFields(["c", c])
showFields(["Tag", sizeTag])


for line in sys.stdin:
    line = trim(line)
    if len(line) > 0:
        process(line)

seedList.sort()
modeList.sort()

hfields = [""] + [seed for seed in seedList]
showFields(hfields + ["Min"])

for mode in modeList:
    d = modeDict[mode]
    sizes = []
    for seed in d.keys():
        size = d[seed]
        sizes.append(size)
        if seed not in minBySeed or size < minBySeed[seed]:
            minBySeed[seed] = size
    minSize = min(sizes)
    fields = [mode] + [d[seed] if seed in d else "?" for seed in seedList] + [minSize]
    showFields(fields)

fields = ["Min"] + [minBySeed[seed] for seed in seedList]

showFields(fields)

showFields([])
showFields(["Ratios"])
showFields(["Cutoff", "%.2f" % maxRatio])

showFields(hfields + ["Count"])
for mode in modeList:
    d = modeDict[mode]
    count = 0
    for seed in d.keys():
        size = d[seed]
        msize = minBySeed[seed]
        if float(size)/msize <= maxRatio:
            count += 1
    ratios = ["%.2f" % (float(d[seed])/minBySeed[seed]) if seed in d else "?" for seed in seedList]
    showFields([mode] + ratios + [count])

# Find undominated modes

showFields([])
showFields(["Mode", "Dominators"])

for m in modeList:
    ls = dominatorList(m)
    if len(ls) == 0:
        goodModeList.append(m)
        ls = ["None"]
    showFields([m] + ls)

showFields([])
showFields(["Useful modes", len(goodModeList)])
showFields(goodModeList)

# See what would give best combinations:
bestCombo = []
bestCount = 0
bestAverage = 0
bestMax = 0


showFields([])

hfields = ["Category", "Count", "Average", "Worst", "Mode(s)"]
showFields(hfields)

# Singles
for m in goodModeList:
    mlist = [m]
    (count, avg, worst) = score(mlist)
    if count == bestCount:
        if avg < bestAverage:
            bestAverage = avg
            bestMax = worst
            bestCombo = mlist
    if count > bestCount:
        bestCount = count
        bestAverage = avg
        bestMax = worst
        bestCombo = mlist


fields = ["Single", bestCount, "%.2f" % bestAverage, "%.2f" % bestMax] + bestCombo
showFields(fields)

# Doubles
for m1 in goodModeList:
    for m2 in goodModeList:
        mlist = [m1, m2]
        if m2 in mlist[:1]:
            continue
        (count, avg, worst) = score(mlist)
        if count == bestCount:
            if avg < bestAverage:
                bestAverage = avg
                bestMax = worst
                bestCombo = mlist
        if count > bestCount:
            bestCount = count
            bestAverage = avg
            bestMax = worst
            bestCombo = mlist

fields = ["Double", bestCount, "%.2f" % bestAverage, "%.2f" % bestMax] + bestCombo
showFields(fields)

# Triples
for m1 in goodModeList:
    for m2 in goodModeList:
        for m3 in goodModeList:
            mlist = [m1, m2, m3]
            if m2 in mlist[:1]  or m3 in mlist[:2]:
                continue
            (count, avg, worst) = score(mlist)
            if count == bestCount:
                if avg < bestAverage:
                    bestAverage = avg
                    bestMax = worst
                    bestCombo = mlist
            if count > bestCount:
                bestCount = count
                bestAverage = avg
                bestMax = worst
                bestCombo = mlist

fields = ["Triple", bestCount, "%.2f" % bestAverage, "%.2f" % bestMax] + bestCombo
showFields(fields)

# Quads
for m1 in goodModeList:
    for m2 in goodModeList:
        for m3 in goodModeList:
            for m4 in goodModeList:
                mlist = [m1, m2, m3, m4]
                if m2 in mlist[:1] or m3 in mlist[:2] or m4 in mlist[:3]:
                    continue
                (count, avg, worst) = score(mlist)
                if count == bestCount:
                    if avg < bestAverage:
                        bestAverage = avg
                        bestMax = worst
                        bestCombo = mlist
                if count > bestCount:
                    bestCount = count
                    bestAverage = avg
                    bestMax = worst
                    bestCombo = mlist

fields = ["Quad", bestCount, "%.2f" % bestAverage, "%.2f" % bestMax] + bestCombo
showFields(fields)
