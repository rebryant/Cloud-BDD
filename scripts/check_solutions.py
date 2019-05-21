#!/usr/bin/python

# Check existing files to see
#  1) they all obey Brent equations
#  2) their other properties

import sys
import os
import os.path
import glob
import getopt

import circuit
import brent

def usage(name):
    print "Usage %s [-h] [-c] [-d DIR]"
    sys.exit(0)

# validCount
# uniqueUsageCount
# maxDoubleCount
# singletonExclusionCount = 0
totalCounts = [0, 0, 0, 0]
counts = [0, 0, 0, 0]

completedDirectories = []

dim = (3,3,3)
auxCount = 23

ckt = circuit.Circuit()

reportPath = "mm-solutions/report.txt"
archivePath = "mm-solutions/candidates.txt"

def checkSolution(directory, fname):
    global counts
    path = "%s/%s" % (directory, fname)
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(path)
    except Exception as ex:
        print "ERROR: Could not extract solution from file '%s' (%s)" % (path, str(ex))
        return
    if not s.obeysBrent():
        print "ERROR: Solution in file '%s' is not valid" % path
        return
    omit = False
    counts[0] += 1
    if s.obeysUniqueUsage():
        counts[1] += 1
    else:
        omit = True
    if s.obeysMaxDouble():
        counts[2] += 1
    else:
        omit = True
    if s.obeysSingletonExclusion():
        counts[3] += 1
    else:
        omit = True
    if not omit:
        try:
            afile = open(archivePath, 'a')
        except:
            return
        afile.write(path + '\n')
        afile.close()

def clearHistory():
    if os.path.exists(reportPath):
        try:
            os.remove(reportPath)
        except:
            print "Couldn't remove '%s'" % reportPath
    if os.path.exists(archivePath):
        try:
            os.remove(archivePath)
        except:
            print "Couldn't remove '%s'" % archivePath


def reset(total = False):
    global counts, totalCounts
    global completedDirectories
    counts = [0, 0, 0, 0]
    if total:
        totalCounts = [0, 0, 0, 0]
        completedDirectories = []

def load():
    global totalCounts
    global completedDirectories
    headings = ["Dir", "Valid", "Unique", "Doubles", "Singles"]
    fresh = False
    try:
        rfile = open(reportPath, 'r')
    except:
        try:
            rfile = open(reportPath, 'w')
            rfile.close()
            show(headings, archive = True)
            fresh = True
            rfile = open(reportPath, 'r')
        except Exception as ex:
            print "Couldn't open or create file '%s' (%s)" % (reportPath, str(ex))
            return
    reset(True)
    first = True
    for line in rfile:
        fields = line.split()        
        if first:
            first = False
            if not fresh:
                show(fields, archive = False)
            continue
        dir = fields[0]
        if dir == "TOTAL":
            continue
        show(fields, archive = False)
        counts = [int(f) for f in fields[1:]]
        totalCounts = [tc+c for tc,c in zip(totalCounts, counts)]
        completedDirectories.append(dir)
    rfile.close()
    
def show(list, archive = True):
    slist = [str(x) for x in list]
    s = "\t".join(slist)
    print s
    if archive:
        try:
            rfile = open(reportPath, 'a')
        except:
            return
        rfile.write(s + '\n')
        rfile.close()

def summarizeDirectory(directory):
    subDirectory = directory.split('/')[-1]
    slist = [subDirectory] + counts
    show(slist)

def runDirectory(directory):
    global totalCounts
    reset()
    template = directory + "/*.exp"
    names = sorted(glob.glob(template))
    for path in names:
        fname = path.split('/')[-1]
        checkSolution(directory, fname)
    summarizeDirectory(directory)
    totalCounts = [tc+c for tc,c in zip(totalCounts, counts)]
    
def runAll(mainDirectory):
    template = mainDirectory + "/*"
    load()
    directories = sorted(glob.glob(template))
    done = True
    for dir in directories:
        fields = dir.split('/')
        if fields[-1] in completedDirectories:
            continue
        fields = dir.split(".")
        if fields[-1] != "txt":
            runDirectory(dir)
            done = False
    slist = ["TOTAL"] + totalCounts
    show(slist, archive = not done)

def run(name, args):
    global reportPath
    global archivePath
    dir = 'mm-solutions'
    clear = False
    optlist, args = getopt.getopt(args, "hcd:")
    for opt, val in optlist:
        if opt == '-h':
            usage(name)
        elif opt == '-c':
            clear = True
        elif opt == '-d':
            dir = val
        else:
            print "Unknown option '%s'" % opt
            usage(name)
    reportPath = dir + "/report.txt"
    archivePath = dir + "/candidates.txt"
    if clear:
        clearHistory()
    runAll(dir)

    
if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
