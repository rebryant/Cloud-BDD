#!/usr/bin/python
# Parse outputs generated for solution to Brent equations
# and solutions found at:
# http://www.algebra.uni-linz.ac.at/research/matrix-multiplication/

import os.path
import sys
import re
import getopt

import circuit
import brent
import glob


def usage(name):
    print "Usage: %s [-h] [-u] [-q] [-I IDIR] [-i IFILE] [-s PFILE] [-p AUX] [-n (N|N1:N2:N3)]" % name
    print " -h               Print this message"
    print " -u               Print number of nodes at each level, rather than solutions"
    print " -q               Quiet mode.  Only summarize results"
    print " -I IDIR          Run for all files with extension '.log' in directory IDIR"
    print " -i IFILE         Specify input file"
    print " -s PFILE         Read hard-coded values from polynomial in PFILE"
    print " -p AUX           Number of auxiliary variables"
    print " -n N or N1:N2:N3 Matrix dimension(s)"

# Quiet mode
quietMode = False

# Fields in solution database
fieldIndex = {'hash' : 0, 'additions' : 1, 'kernel hash' : 2, 'path' : 3}
databaseConverters = [str, int, str, str]
# Mapping from hash to list of fields
databaseDict = {}


# Set to home directory for program, split into tokens
homePathFields = ['.']

databaseDirectory = "mm-solutions"
databasePathFields = [[databaseDirectory, "heule-database.txt"]]


cmdPrefix = "cmd>"

# Mapping from canonized polynomial to solution name
solutionDict = {}
# How many of the solutions are not in main database?
uniqueCount = 0


def loadDatabase():
    global databaseDict
    for dbfields in databasePathFields:
        dbname = '/'.join(homePathFields + dbfields)
        try:
            dbfile = open(dbname, 'r')
        except Exception as ex:
            print "Couldn't open database file '%s' (%s)" % (dbname, str(ex))
            continue
        first = True
        for line in dbfile:
            if first:
                first = False
                continue
            line = brent.trim(line)
            fields = line.split('\t')
            if len(fields) != len(databaseConverters):
                print "Bad database format.  Expected %d fields, but found %d" % (len(databaseConverters), len(fields))
                break
            try:
                entry = [convert(field) for convert, field in zip(databaseConverters, fields)]
            except Exception as ex:
                print "Bad database format.  Couldn't convert entry (%s)" % str(ex)
                break
            databaseDict[entry[0]] = entry
        dbfile.close()
    if not quietMode:
        print "Database contains %d entries" % len(databaseDict)
        

# Extract the support information from file:
def getSupport(fname):
    matcher = re.compile("[\s]*Support")
    try:
        inf = open(fname, 'r')
    except:
        print "Couldn't open input file '%s'" % fname
        return []
    rline = "%sinfo %s" % (cmdPrefix, str(brent.BrentTerm()))
    ready = False
    for line in inf:
        line = brent.trim(line)
        if line == rline:
            ready = True
        if not ready or matcher.match(line) is None:
            continue
        fields = line.split()
        names = fields[1:]
        return names
    inf.close()
    return []
            
# Extract solutions from file:
def getSolutions(fname):
    slist = []
    matcher = re.compile("[01]+")
    try:
        inf = open(fname, 'r')
    except:
        print "Couldn't open input file '%s'" % fname
        return slist
    for line in inf:
        m = matcher.match(line)
        if m:
            s = m.group()
            if len(s) > 8:
                slist.append(s)
    inf.close()
    return slist

# Extract sizes of combined BDDs
def getSizes(fname):
    slist = []
    infoMatcher = re.compile(cmdPrefix + "info")
    sizeMatcher = re.compile("\s*Cudd size: ([\d]+) nodes")
    finalLine = "%sinfo %s" % (cmdPrefix, str(brent.BrentTerm()))
    try:
        inf = open(fname, 'r')
    except:
        print "Couldn't open input file '%s'" % fname
        return []
    ready = False
    gotFinal = False
    for line in inf:
        line = brent.trim(line)
        im = infoMatcher.match(line)
        if im:
            if line == finalLine and not gotFinal:
                ready = True
                gotFinal = True
            elif len(line.split()) > 2:
                ready = True
        if not ready:
            continue
        sm = sizeMatcher.match(line)
        if sm:
            ready = False
            try:
                snum = sm.group(1)
                size = int(snum)
                slist.append(size)
            except:
                print "Couldn't extract size from line '%s' % line"
                continue
    inf.close()
    return slist

def generateSignature(scheme):
    sigList = scheme.canonize().generatePolynomial()
    signature = "\n".join(sigList)
    return signature
    

def generateSolutions(iname, fileScheme):
    global solutionDict, uniqueCount
    supportNames = getSupport(iname)
    slist = getSolutions(iname)
    index = 1
    newCount = 0
    newUniqueCount = 0
    for s in slist:
        try:
            ss = fileScheme.duplicate().parseFromSolver(supportNames, s)
        except Exception as ex:
            print "Couldn't process solution: %s" % str(ex)
            continue
        sname = "%s #%d" % (iname, index)
        sc = ss.canonize()
        signature = sc.signature()
        found = signature in solutionDict
        if found:
            osname = solutionDict[signature]
            if not quietMode:
                print "Solution %s.  %d additions.  Isomorphic to solution %s" % (sname, ss.addCount(), osname)
        else:
            newCount += 1
            if not quietMode:
                print "Solution %s.  %d additions" % (sname, ss.addCount())
            solutionDict[signature] = sname
        if not found:
            hash = sc.sign()
            if hash in databaseDict:
                if not quietMode:
                    print "Probably isomorphic to solution in '%s'" % databaseDict[hash][fieldIndex['path']]
            else:
                newUniqueCount += 1
                uniqueCount += 1
                if not quietMode:
                    print "Solution not yet in database"
        index += 1
        if not quietMode:
            ss.printPolynomial()
    if quietMode:
        fields = [iname, str(len(slist)), str(newCount), str(newUniqueCount)]
        print "\t".join(fields)
        

def run(name, args):
    global solutionDict, quietMode
    n1, n2, n3 = 3, 3, 3
    auxCount = 23
    solve = True
    pname = None
    inameList = []
    optlist, args = getopt.getopt(args, 'huqI:i:s:p:n:o:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-u':
            solve = False
        elif opt == '-q':
            quietMode = True
        elif opt == '-I':
            idir = val
            template = "%s/*.log" % idir
            inameList = sorted(glob.glob(template))
        elif opt == '-i':
            inameList = [val]
        elif opt == '-s':
            pname = val
        elif opt == '-p':
            auxCount = int(val)
        elif opt == '-n':
            fields = val.split(':')
            if len(fields) == 1:
                n1 = n2 = n3 = int(fields[0])
            elif len(fields) == 3:
                n1, n2, n3 = int(fields[0]), int(fields[1]), int(fields[2])
            else:
                print "Invalid matrix dimension '%s'" % val
                usage(name)
                return
        elif opt == '-o':
            try:
                outf = open(val, 'w')
            except:
                print "Couldn't open output file '%s'" % val
                return
        else:
            print "Unknown option '%s'" % opt
            usage(name)
            return
    if len(inameList) == 0:
        print "Error. Require input file name"
        return
    if not solve:
        fields = ['File', 'Brent']
        fields += ["Level %d" % l for l in brent.unitRange(6)]
        print "\t".join(fields)

        for iname in inameList:
            szlist = getSizes(iname)
            if len(szlist) > 0:
                slist = [str(n) for n in szlist]
                print iname + ":\t" + "\t".join(slist)
        return
    if pname is None:
        print "Error. Require solution file name"

    if quietMode:
        fields = ['File', 'Solutions', 'Local New', 'DB New']
        print "\t".join(fields)

    ckt = circuit.Circuit()
    fileScheme = brent.MScheme((n1, n2, n3), auxCount, ckt)
    try:
        fileScheme.parseFromFile(pname)
    except brent.MatrixException as ex:
        print "Parse of file '%s' failed: %s" % (pname, str(ex))
        return
    loadDatabase()

    sname = "%s" % (pname)
    signature = generateSignature(fileScheme)
    solutionDict[signature] = sname
    for iname in inameList:
        generateSolutions(iname, fileScheme)
            
    
if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])
