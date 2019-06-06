#!/usr/bin/python

# Sweep through range of parameters in generating matrix command files

import os.path
import sys
import getopt
import subprocess

def usage(name):
    print "Usage %s [-h] [-k] [-e] [-b] [-z] [-t SECS] [-S SEED] [-l LTHRESH] [-u UTHRESH] [-s PFILE] [-p AUX] [-n (N|N1:N2:N3)]" % name
    print " -h               Print this message"
    print " -k               Use fixed values for Kronecker terms"
    print " -e               Generate streamline constraints based on singleton exclusion property"
    print " -b               Combine products in breadth-first order"
    print " -z               Use a ZDD representation"
    print " -t SECS          Set runtime limit (in seconds)"
    print " -S SEED          Set seed"
    print " -l LTHRESH       Set lower threshold of fix_ab + fix_c (100 = all)"
    print " -u UTHRESH       Set upper threshold of fix_ab + fix_c (100 = all)"
    print " -c CMIN          Set lower bound on fix_c (0-100)"
    print " -s PFILE         Read hard-coded values from polynomial in PFILE"
    print " -p AUX           Number of auxiliary variables"
    print " -n N or N1:N2:N3 Matrix dimension(s)"
    sys.exit(0)

# General parameters
fixKV = False
excludeSingleton = False
dim = (3,3,3)
auxCount = 23
solutionPath = None
timeLimit = None

deltaC = 5

# Set to home directory for program, split into tokens
homePathFields = ['.']
generatorFields = ['mm_generate.py']

def generate(seed, abprob, cprob, breadthFirst, useZdd):
    outName = "run"
    argList = []
    if fixKV:
        outName += "-k"
        argList += ['-k']
    if excludeSingleton:
        outName += "-e"
        argList += ['-e']
    if breadthFirst:
        argList += ['-b']
    if useZdd:
        argList += ['-z']
    if timeLimit is not None:
        argList += ['-t', str(timeLimit)]
    outName += "-ab%.3d-c%.3d" % (abprob, cprob)
    argList += ['-c', "%.3d:%.3d:%.3d" % (abprob, abprob, cprob)]
    if solutionPath is not None:
        argList += ['-s', solutionPath]
    outName += "-s%.2d" % seed
    argList += ['-S', str(seed)]
    argList += ['-p', str(auxCount)]
    argList += ['-n', '%d:%d:%d' % dim]
    outName += '.cmd'
    argList += ['-o', outName]
    program = "/".join(homePathFields + generatorFields)
    cmd = [program] + argList
    cmdLine = " ".join(cmd)
    print "Generating %s" % outName
    p = subprocess.Popen(cmd)
    p.wait()
    if p.returncode != 0:
        print "Something went wrong executing '%s'.  Exit code %d" % (cmdLine, p.returncode)
        return False
    return True
    
def sweeper(abcLimit, cmin, seed, breadthFirst, useZdd):
    startAB = max(0, abcLimit - 100)
    finishC = abcLimit - startAB
    startC = max(0, cmin, abcLimit - 100)
    for c in range(startC, finishC + deltaC, deltaC):
        ab = abcLimit - c
        generate(seed, ab, c, breadthFirst, useZdd)

def run(name, args):
    global fixKV, excludeSingleton, dim, auxCount, solutionPath, timeLimit
    lowThresh = 0
    highThresh = 200
    seed = 1
    cmin = 0
    breadthFirst = False
    useZdd = False
    optlist, args = getopt.getopt(args, 'hkebzS:t:l:u:c:s:p:n:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-k':
            fixKV = True
        elif opt == '-e':
            excludeSingleton = True
        elif opt == '-b':
            breadthFirst = True
        elif opt == '-z':
            useZdd = True
        elif opt == '-S':
            seed = int(val)
        elif opt == '-t':
            timeLimit = int(val)
        elif opt == '-l':
            lowThresh = int(val)
        elif opt == '-u':
            highThresh = int(val)
        elif opt == '-c':
            cmin = int(val)
        elif opt == '-s':
            solutionPath = val
        elif opt == '-p':
            auxCount = int(val)
        elif opt == '-n':
            fields = val.split(':')
            if len(fields) == 1:
                n = int(fields[0])
                dim = (n, n, n)
            elif len(fields) == 3:
                dim = (int(fields[0]), int(fields[1]), int(fields[2]))
            else:
                print "Invalid matrix dimension '%s'" % val
                usage(name)
                return
    for limit in range(lowThresh, highThresh+deltaC, deltaC):
        sweeper(limit, cmin, seed, breadthFirst, useZdd)

    

if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])
