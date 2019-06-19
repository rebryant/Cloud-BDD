#!/usr/bin/python

# Execute runbdd on single command file, but with multiple configurations of conjunct parameters

import os.path
import sys
import getopt
import subprocess

import brent

def usage(name):
    print "Usage %s [-h] [-r] [-R] [-C] [-v VERB] [-q] [-m MODES] [-p PROCS] -f FILE"
    print " -h               Print this message"
    print " -r               Redo runs that didn't complete"
    print " -R               Redo all runs"
    print " -C               Disable chaining"
    print " -v VERB          Set verbosity level"
    print " -q               Don't print output results"
    print " -m M1:M2...      Specify reduction mode(s) T, B, L, P, D"
    print " -p P1:P2...      Specify simplification processing options NON, (U|S)(L|R)(Y|N)"
    print " -f FILE          Command file"
    sys.exit(0)

# Set to home directory for program, split into tokens
homePathFields = ['.']

runbddFields = ["..", "runbdd"]

softRedo = False
hardRedo = False

chain = True

verbLevel = None
quietMode = False

cmdPrefix = "cmd>"

reductionList = ['T', 'B', 'L', 'P', 'D']
processingList = ['NON', 'ULN', 'URN', 'SLN', 'SLY', 'SRN', 'SRY']

# Is a combination of reduction and processing modes compatible?
def compatibleModes(reduction, processing):
    if reduction not in reductionList:
        return False
    if processing not in processingList:
        return False
    if reduction == 'D' and processing[-1] == 'Y':
        return False
    return True;
    
def makeMode(reduction, processing):
    return reduction + processing

def logPath(cmdPath, mode):
    fields = cmdPath.split('.')
    fields[-2] += '-' + mode
    fields[-1] = 'log'
    return ".".join(fields)
        

# Check if should run.  Return either None (don't run) or name of log file
def shouldRun(cmdPath, mode):
    fields = cmdPath.split('.')
    if fields[-1] != 'cmd':
        print "Path does not appear to be a command file '%s'" % cmdPath
        return False
    lpath = logPath(cmdPath, mode)
    if hardRedo:
        return lpath
    if os.path.exists(lpath):
        if not softRedo:
            return None
        try:
            lfile = open(lpath, 'r')
        except Exception as ex:
            print "Could not open log file '%s' (%s)" % (lpath, str(ex))
            return lpath
        quitLine = cmdPrefix + "quit"
        for line in lfile:
            line = brent.trim(line)
            if line == quitLine:
                lfile.close()
                return None
        lfile.close()
        # Soft redo, and didn't finish previous run
        return lpath
    else:
        return lpath
    
def process(cmdPath, modeList):
    for mode in modeList:
        lpath = shouldRun(cmdPath, mode)
        if lpath is None:
            print "Skipping %s + %s" % (cmdPath, mode)
        else:
            cmd = ["/".join(homePathFields + runbddFields), '-c']
            if not chain:
                cmd += ['-C', 'n']
            cmd += ['-O', mode]
            cmd += ['-f', cmdPath]
            cmd += ['-L', lpath]
            if verbLevel is not None:
                cmd += ['-v', str(verbLevel)]
            cmdLine = " ".join(cmd)
            if not quietMode:
                print "Running '%s'" % cmdLine
            if quietMode:
                p = subprocess.Popen(cmd, stdout = subprocess.PIPE, stderr = subprocess.PIPE)
                (stdout, stderr) = p.communicate()
                lines = stdout.split('\n')
                for line in lines:
                    if "Conjunction" in line:
                        print line
            else:
                p = subprocess.Popen(cmd)
                p.wait()
            if p.returncode != 0:
                print "Running command '%s' failed.  Return code = %d" % (cmdLine, p.returncode)


def run(name, args):
    global softRedo, hardRedo, chain, verbLevel, quietMode
    reductions = reductionList
    processings = processingList
    cmdPath = None
    optlist, args = getopt.getopt(args, 'hrRCv:qf:m:p:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-r':
            softRedo = True
        elif opt == '-R':
            hardRedo = True
        elif opt == '-C':
            chain = False
        elif opt == '-f':
            cmdPath = val
        elif opt == '-v':
            verbLevel = int(val)
        elif opt == '-q':
            quietMode = True
        elif opt == '-m':
            reductions = val.split(':')
        elif opt == '-p':
            processings = val.split(':')
    if not cmdPath:
        print "Need command file"
        usage(name)
        return
    modeList = []
    for r in reductions:
        for p in processings:
            if compatibleModes(r,p):
                mode = makeMode(r,p)
                modeList.append(mode)
            else:
                print "Invalid mode: %s.  Skipping" % makeMode(r, p)
    process(cmdPath, modeList)

if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])

    
