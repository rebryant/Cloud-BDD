#!/usr/bin/python
# Parse outputs generated for solution to Brent equations
# and solutions found at:
# http://www.algebra.uni-linz.ac.at/research/matrix-multiplication/

import os.path
import os
import sys
import re
import getopt

import circuit
import brent
import glob


def usage(name):
    print("Usage: %s [-h] [-x] [-u] [-b] [-q] [-I IDIR] [-i IFILE] [-s PFILE] [-p AUX] [-n (N|N1:N2:N3)]" % name)
    print(" -h               Print this message")
    print(" -x               Preserve symmetry")
    print(" -u               Print number of nodes at each level, rather than solutions")
    print(" -q               Quiet mode.  Only summarize results")
    print(" -b               Commands generated via breadth-first traversal")
    print(" -I IDIR          Run for all files with extension '.log' in directory IDIR")
    print(" -i IFILE         Specify input file")
    print(" -s PFILE         Read hard-coded values from polynomial in PFILE")
    print(" -p AUX           Number of auxiliary variables")
    print(" -n N or N1:N2:N3 Matrix dimension(s)")

# Quiet mode
quietMode = False

# Fields in solution database
fieldIndex = {'hash' : 0, 'additions' : 1, 'kernel hash' : 2, 'path' : 3}
fieldTitles = ["Hash", "Adds", "K Hash", "Path"]
databaseConverters = [str, int, str, str]
# Mapping from hash to list of fields
heuleDatabaseDict = {}
generatedDatabaseDict = {}


# Set to home directory for program, split into tokens
homePathFields = ['.']
subdirectoryFields = ["mm-solutions"]
symmetricSubdirectoryFields = ["mm-solutions-symmetric"]

def heuleDatabasePathFields():
    sdf = symmetricSubdirectoryFields if doSymmetric else subDirectoryFields
    return homePathFields + sdf + ["heule-database.txt"]

def generatedDatabasePathFields():
    sdf = symmetricSubdirectoryFields if doSymmetric else subDirectoryFields
    return homePathFields + sdf + ["generated-database.txt"]

def generatedPathFields():
    sdf = symmetricSubdirectoryFields if doSymmetric else subDirectoryFields
    return homePathFields + sdf + ["generated"]

# Don't record in main database
localMode = False
# Work on symmetric cases
doSymmetric = False

cmdPrefix = "cmd>"

# Mapping from canonized polynomial to solution name
solutionDict = {}
# How many of the solutions are not in Heule database?
nonHeuleCount = 0
# How many of the solutions have never been encountered?
freshCount = 0

# Prefix for subdirectories in generated directory
directoryPrefix = 'D'

def loadDatabase(databaseDict, databasePathGenerator, quiet):
    pathFields = databasePathGenerator()
    dbpath = '/'.join(pathFields)
    dbname = pathFields[-1]
    if not os.path.exists(dbpath):
        try:
            dbfile = open(dbpath, 'w')
            fields = ["Hash", "Adds", "K Hash", "Path"]
            dbfile.write('\t'.join(fields) + '\n')
            dbfile.close()
        except Exception as ex:
            print("Couldn't create database file '%s' (%s)" % (dbpath, str(ex)))
            return
    try:
        dbfile = open(dbpath, 'r')
    except Exception as ex:
        print("Couldn't open database file '%s' (%s)" % (dbpath, str(ex)))
        return

    first = True
    lineNumber = 0
    dups = 0
    for line in dbfile:
        lineNumber += 1
        if first:
            first = False
            continue
        line = brent.trim(line)
        if len(line) == 0:
            continue
        fields = line.split('\t')
        if len(fields) != len(databaseConverters):
            print("Bad database format.  Expected %d fields, but found %d" % (len(databaseConverters), len(fields)))
            print("Line #%d '%s'" % (lineNumber, line))
            break
        try:
            entry = [convert(field) for convert, field in zip(databaseConverters, fields)]
        except Exception as ex:
            print("Bad database format.  Couldn't convert entry (%s)" % str(ex))
            break
        if entry[0] in databaseDict:
            dups += 1
        else:
            databaseDict[entry[0]] = entry
    dbfile.close()
    if not quiet:
        print("Database %s contains %d entries" % (dbname, len(databaseDict)))
    if dups > 0:
        print("WARNING.  Database %s contains %d duplicate entries" % (dbname, dups))

def subtractFromDatabase(db, dbr):
    count = 0
    for hash in dbr.keys():
        if hash in db:
            count += 1
            del db[hash]
    print("%d entries removed" % count)
    
        
def restoreDatabase(databaseDict, databasePathGenerator, rehash = False, checkCanonical = False, dim = (3,3,3), auxCount = 23):
    pathFields = databasePathGenerator()
    dbpath = '/'.join(pathFields)
    dbname = pathFields[-1]
    try:
        dbfile = open(dbpath, 'w')
    except Exception as ex:
        print("Couldn't open database file '%s' (%s)" % (dbpath, str(ex)))
        return
    dbfile.write('\t'.join(fieldTitles) + '\n')
    count = 0
    if rehash:
        ckt = circuit.Circuit()
    for hash in databaseDict.keys():
        entry = databaseDict[hash]
        if rehash or checkCanonical:
            path = entry[fieldIndex['path']]
            try:
                scheme = brent.MScheme(dim, auxCount, ckt).parseFromFile(path)
            except Exception as ex:
                print("Couldn't recover scheme from file %s (%s)" % (path, str(ex)))
                continue
            if rehash:
                entry[fieldIndex['hash']] = scheme.sign()
            if checkCanonical and not scheme.isCanonical(symmetric = doSymmetric):
                sc = scheme.symmetricCanonize() if doSymmetric else s.canonize()
                print("WARNING: Scheme in file %s is not canonical.  Has signature %s.  Should have signature %s.  Ignoring" % (path, scheme.sign(), sc.sign()))
        fields = [str(e) for e in entry]
        dbfile.write('\t'.join(fields) + '\n')
        count += 1
    dbfile.close()
    print("Wrote %d entries to database %s" % (count, dbname))



# Extract the support information from file:
def getSupport(fname):
    matcher = re.compile("[\s]*Support")
    try:
        inf = open(fname, 'r')
    except:
        print("Couldn't open input file '%s'" % fname)
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
            
def getDependencies(fname):
    dependencyList = []
    matcher = re.compile(cmdPrefix + "# Symmetry dependency")
    try:
        inf = open(fname, 'r')
    except:
        print("Couldn't open input file '%s'" % fname)
        return []
    lineCount = 0
    for line in inf:
        line = brent.trim(line)
        if matcher.match(line):
            fields = line.split()
            dname = fields[-2]
            sname = fields[-1]
            dependencyList.append((dname, sname))
        lineCount += 1
    inf.close()
    return None if len(dependencyList) == 0 else dependencyList

def getPeakNodes(fname):
    matcher = re.compile("Peak number of live nodes: ([0-9]+)")
    try:
        inf = open(fname, 'r')
    except:
        print("Couldn't open input file '%s'" % fname)
        return []
    value = None
    for line in inf:
        line = brent.trim(line)
        sm = matcher.match(line)
        if sm:
            try:
                snum = sm.group(1)
                value = int(snum)
            except:
                print("Couldn't extract peak nodes from line '%s' % line")
                continue
    inf.close()
    return value
    

# Extract solutions from file:
def getBitSolutions(fname):
    slist = []
    matcher = re.compile("[01]+")
    try:
        inf = open(fname, 'r')
    except:
        print("Couldn't open input file '%s'" % fname)
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
        print("Couldn't open input file '%s'" % fname)
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
                print("Couldn't extract size from line '%s' % line")
                continue
    inf.close()
    return slist

def generateSignature(scheme):
    sigList = scheme.canonize().generatePolynomial()
    signature = "\n".join(sigList)
    return signature
    
# Pick off characters of file name to use as directory name
def directoryName(fname):
    first = 1
    length = 3
    if len(fname) < first+length:
        print("Cannot get directory name from '%s'" % fname)
        return ""
    return directoryPrefix + fname[first:length+first]

def recordSolution(scheme, metadata = []):
    global homePathFields, subdirectoryFields
    fname = scheme.sign() + '.exp'
    if localMode:
        homePathFields = ['.']
        subdirectoryFields = []
        dirFields = []
    else:
        dirName = directoryName(fname)
        dirFields = [dirName]

    dirPathFields = generatedPathFields() + dirFields

    dirPath = '/'.join(dirPathFields)
    dpath =  '/'.join(pathFields)
    fpath = '/'.join(dirPathFields + [fname])

    if not os.path.exists(dpath):
        try:
            os.mkdir(dpath)
        except Exception as ex:
            print("Could not create directory '%s' (%s)" % (dpath, str(ex)))
            return
    try:
        outf = open(fpath, 'w')
    except Exception as ex:
        print("Can't open output file '%s' (%s)" % (fpath, str(ex)))
        return
    scheme.printPolynomial(outf, metadata = metadata)
    outf.close()
    dbEntryFields = [scheme.sign(), str(scheme.addCount()), scheme.kernelTerms.sign(), fpath]
    dbpath = '/'.join(generatedDatabasePathFields())
    try:
        dbfile = open(dbpath, 'a')
    except Exception as ex:
        print("Can't open database file '%s' (%s)" % (dbpath, str(ex)))
        return
    dbfile.write("\t".join(dbEntryFields) + '\n')
    dbfile.close()
    return path
    
# Process a generated solution
def processSolution(scheme, sname, metadata = [], recordFunction = recordSolution):
    global solutionDict, nonHeuleCount, freshCount
    signature = scheme.signature()
    found = signature in solutionDict
    if found:
        if not quietMode:
            osname = solutionDict[signature]
            print("Solution %s.  %d additions.  Isomorphic to solution %s" % (sname, scheme.addCount(), osname))
        return False
    if not quietMode:
        print("Solution %s.  %d additions" % (sname, scheme.addCount()))
    solutionDict[signature] = sname
    hash = scheme.sign()
    if hash in heuleDatabaseDict:
        if not quietMode:
            print("Probably isomorphic to solution in '%s'" % heuleDatabaseDict[hash][fieldIndex['path']])
    else:
        nonHeuleCount += 1
        if hash in generatedDatabaseDict:
            if not quietMode:
                print("Probably isomorphic to solution in '%s'" % generatedDatabaseDict[hash][fieldIndex['path']])
        else:
            freshCount += 1
            if not quietMode:
                print("Completely new Solution")
            recordFunction(scheme, metadata = metadata)
    return True

def generateSolutions(iname, fileScheme, recordFunction = recordSolution):
    global nonHeuleCount, freshCount
    supportNames = getSupport(iname)
    dependencyList = getDependencies(iname)
    slist = getBitSolutions(iname)
    index = 1
    newCount = 0
    nonHeuleCount = 0
    freshCount = 0
    metadata = ["Derived from scheme with signature %s" % fileScheme.sign()]
    for s in slist:
        try:
            ss = fileScheme.duplicate().parseFromSolver(supportNames, s)
        except Exception as ex:
            print("Couldn't process solution: %s" % str(ex))
            continue
        try:
            if dependencyList is not None:
                ss = ss.parseDependencies(dependencyList)
        except Exception as ex:
            print("Couldn't process symmetry dependencies: %s" % str(ex))
            continue
        sname = "%s #%d" % (iname, index)
        if not ss.obeysBrent():
            print("Oops.  Generated solution does not satisfy Brent constraints")
            ss.printPolynomial(metadata = metadata)
            continue
        sc = ss.symmetricCanonize() if doSymmetric else ss.canonize()
        if processSolution(sc, sname, metadata, recordFunction):
            newCount += 1
        index += 1
        if not quietMode:
            sc.printPolynomial(metadata = metadata)
    if quietMode:
        fields = [iname, str(len(slist)), str(newCount), str(nonHeuleCount), str(freshCount)]
        print("\t".join(fields))
    return len(slist)
        
def run(name, args):
    global solutionDict, quietMode
    global localMode
    global doSymmetric
    n1, n2, n3 = 3, 3, 3
    auxCount = 23
    solve = True
    pname = None
    inameList = []
    breadthFirst = False
    optlist, args = getopt.getopt(args, 'huqbxI:i:s:p:n:o:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-u':
            solve = False
        elif opt == '-q':
            quietMode = True
        elif opt == '-b':
            breadthFirst = True
        elif opt == '-x':
            doSymmetric = True
        elif opt == '-I':
            idir = val
            template = "%s/*.log" % idir
            inameList = sorted(glob.glob(template))
        elif opt == '-i':
            inameList = [val]
        elif opt == '-s':
            pname = val
        elif opt == '-p':
            localMode = True
            auxCount = int(val)
        elif opt == '-n':
            localMode = True
            fields = val.split(':')
            if len(fields) == 1:
                n1 = n2 = n3 = int(fields[0])
            elif len(fields) == 3:
                n1, n2, n3 = int(fields[0]), int(fields[1]), int(fields[2])
            else:
                print("Invalid matrix dimension '%s'" % val)
                usage(name)
                return
        elif opt == '-o':
            try:
                outf = open(val, 'w')
            except:
                print("Couldn't open output file '%s'" % val)
                return
        else:
            print("Unknown option '%s'" % opt)
            usage(name)
            return
    if len(inameList) == 0:
        print("Error. Require input file name")
        return
    if not solve:
        fields = ['File', 'Peak', 'Brent']
        if breadthFirst:
            fields += ["Level %d" % l for l in brent.unitRange(6)]
        else:
            fields += ["Level 6"]
        print("\t".join(fields))

        for iname in inameList:
            peak = getPeakNodes(iname)
            speak = '' if peak is None else str(peak)
            fields = [iname, speak]
            szlist = getSizes(iname)
            if len(szlist) > 0:
                fields += [str(n) for n in szlist]
                print("\t".join(fields))
        return
    if pname is None:
        print("Error. Require solution file name")

    if quietMode:
        fields = ['File', 'Solutions', 'Local New', 'Non Heule', 'DB New']
        print("\t".join(fields))

    ckt = circuit.Circuit()
    fileScheme = brent.MScheme((n1, n2, n3), auxCount, ckt)
    try:
        fileScheme.parseFromFile(pname)
    except brent.MatrixException as ex:
        print("Parse of file '%s' failed: %s" % (pname, str(ex)))
        return
    if not localMode:
        loadDatabase(heuleDatabaseDict, heuleDatabasePathFields, quietMode)
        loadDatabase(generatedDatabaseDict, generatedDatabasePathFields, quietMode)


    signature = generateSignature(fileScheme)
    # Preload with provided solution
    solutionDict[signature] = pname
    for iname in inameList:
        generateSolutions(iname, fileScheme, recordSolution)
            

    
if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])
