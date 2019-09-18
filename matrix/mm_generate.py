#!/usr/bin/python
# Find or check solutions to Brent equations
# expressing different ways to multiply matrices with < n^3 multiplications


import functools
import sys
import getopt

import circuit
import brent

def usage(name):
    print "Usage %s [-h] [-k|-K] [-e|-E] [-2] [-x] [(-b|-B LLIST)] [-z] [-t SECS] [-S SEED] [-c APROB:BPROB:CPROB] [-s PFILE] [-p AUX] [-n (N|N1:N2:N3)] [-o OUTF]" % name
    print " -h               Print this message"
    print " -k               Use fixed values for Kronecker terms"
    print " -K KFILE         Read kernel structure from KFILE"
    print " -e               Generate streamline constraints based on singleton exclusion property"
    print " -E               Generate symbolic streamline formula constraining solution form"
    print " -2               Constrain nonkernel terms to have at most 2 positive instances"
    print " -x               Enforce symmetry constraints, if provided solution is symmetric"
    print " -b               Combine products in breadth-first order"
    print " -B LLIST         Use breadth-first order, combining at levels specified as comma-separated list"
    print " -z               Use a ZDD representation"
    print " -t SECS          Set runtime limit (in seconds)"
    print " -S SEED          Set random seed"
    print " -c APROB:BPROB:CPROB Assign probabilities (in percent) of fixing each variable class"
    print " -s PFILE         Read hard-coded values from polynomial in PFILE"
    print " -p AUX           Number of auxiliary variables"
    print " -n N or N1:N2:N3 Matrix dimension(s)"
    print " -o OUTF          Output file"
    sys.exit(0)

def run(name, args):
    n1, n2, n3 = 3, 3, 3
    auxCount = 23
    check = False
    outf = sys.stdout
    categoryProbabilities = {'alpha':0.0, 'beta':0.0, 'gamma':0.0}
    someFixed = False
    pname = None
    kname = None
    fixKV = False
    varKV = False
    excludeSingleton = False
    symbolicStreamline = False
    boundNonKernels = False
    checkSymmetry = False
    breadthFirst = False
    levelList = brent.unitRange(6)
    useZdd = False
    timeLimit = None
    seed = 0
    
    optlist, args = getopt.getopt(args, 'hkK:eE2xbB:zS:t:c:s:p:n:o:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-k':
            fixKV = True
        elif opt == '-K':
            kname = val
            fixKV = True
        elif opt == '-e':
            excludeSingleton = True
        elif opt == '-E':
            symbolicStreamline = True
        elif opt == '-2':
            boundNonKernels = True
        elif opt == '-x':
            checkSymmetry = True
        elif opt == '-b':
            breadthFirst = True
        elif opt == '-B':
            fields = val.split(':')
            levelList = tuple([int(f) for f in fields])
            if len(levelList) < 1 or len(levelList) > 6:
                print("Levels must be in range 1 .. 6")
                return
            if levelList[0] < 1:
                print("Levels must be in range 1 .. 6")
                return
            if levelList[-1] != 6:
                print("Final level must = 6")
                return
            breadthFirst = True
        elif opt == '-z':
            useZdd = True
        elif opt == '-S':
            seed = int(val)
        elif opt == '-t':
            timeLimit = int(val)
        elif opt == '-c':
            fields = val.split(":")
            if len(fields) == 1:
                # Single probability for all categories
                try:
                    pct = int(fields[0])
                except:
                    print("Cannot find percentage of fixed assignments from '%s'" % val)
                    usage(name)
                prob = pct / 100.0
                categoryProbabilities = {'alpha':prob, 'beta':prob, 'gamma':prob}
                someFixed = prob > 0.0
            elif len(fields) == 3:
                try:
                    plist = [int(f)/100.0 for f in fields]
                except:
                    print("Cannot find 3 percentages of fixed assignments from '%s'" % val)
                    usage(name)
                categoryProbabilities = {'alpha':plist[0], 'beta':plist[1], 'gamma':plist[2]}
                someFixed = functools.reduce(lambda x, y: x+y, plist) > 0.0
            else:
                print("Cannot find 3 percentages of fixed assignments from '%s'" % val)
                usage(name)
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
    if fixKV and varKV:
        print("Cannot have both fixed and variable kernel terms")
        usage(name)
        return
    ckt = circuit.Circuit(outf)
    s = brent.MScheme((n1, n2, n3), auxCount, ckt)
    if someFixed and pname is None:
        print "Need solution in order to assign fixed values"
        return
    if pname is not None:
        try:
            s.parseFromFile(pname)
        except brent.MatrixException as ex:
            print "Parse of file '%s' failed: %s" % (pname, str(ex))
            return
    if kname is not None:
        try:
            kt = brent.KernelSet((n1, n2, n3), auxCount).parseFromFile(kname)
        except brent.MatrixException as ex:
            print "Parse of kernel file '%s' failed: %s" % (kname, str(ex))
            return
        s.loadKernels(kt)
    if excludeSingleton and symbolicStreamline:
        print("ERROR.  Cannot enforce both fixed and symbolic streamline constraints")
    else:
        s.generateProgram(categoryProbabilities, seed = seed, timeLimit = timeLimit, fixKV = fixKV, 
                          excludeSingleton = excludeSingleton, 
                          breadthFirst = breadthFirst, levelList = levelList, useZdd = useZdd, symbolicStreamline = symbolicStreamline,
                          boundNonKernels = boundNonKernels, checkSymmetry = checkSymmetry
        )
    
    
            

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
