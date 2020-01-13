#!/usr/bin/python

# Execute runbdd on multiple command files

import os.path
import sys
import glob
import getopt
import subprocess

import brent
import find_memsize

def usage(name):
    print("Usage %s [-h] [-r] [-C] [-p] [-I] [-s SUFFIX] [-t TIME] [-v VERB] [-R RATIO] [-K LOOKUP]")
    print(" -h               Print this message")
    print(" -r               Redo runs that didn't complete")
    print(" -C               Use CUDD")
    print(" -p               Preprocess conjuncts with soft and")
    print(" -I IDIR          Directory containing command files")
    print(" -s SUFFIX        Specify suffix for log file root name")
    print(" -R RATIO         Set relative size of other argument for soft and")
    print(" -K LOOKUP        Limit cache lookups during conjunction (ratio wrt argument sizes")
    print(" -t TIME          Set runtime limit (in seconds)")
    print(" -v VERB          Set verbosity level")
    sys.exit(0)

# Set to home directory for program, split into tokens
homePathFields = ['.']

runbddFields = ["..", "runbdd"]
runbddCuddFields = ["..", "runbdd-cudd"]

softRedo = False
hardRedo = False

useCudd = False

timeLimit = None
verbLevel = None

softRatio = None

preprocessConjuncts = False

cmdPrefix = "cmd>"

# What fraction of total memory should be used
memoryFraction = 0.90

# How much memory should be used
# Default is 32 GB, discounted by memoryFraction
megabytes = int((1 << 15) * memoryFraction)

# What should be limit on cache lookups, as ratio to argument size(s)
lookupRatio  = None

# Check if should run.  Return either None (don't run) or name of log file
def shouldRun(cmdPath, suffix = None):
    fields = cmdPath.split('.')
    if fields[-1] != 'cmd':
        print("Path does not appear to be a command file '%s'" % cmdPath)
        return False
    if suffix is not None:
        fields[-2] += '-' + suffix
    fields[-1] = 'log'
    logPath = ".".join(fields)
    if hardRedo:
        return logPath
    if os.path.exists(logPath):
        if not softRedo:
            return None
        try:
            lfile = open(logPath, 'r')
        except Exception as ex:
            print("Could not open log file '%s' (%s)" % (logPath, str(ex)))
            return logPath
        quitLine = cmdPrefix + "quit"
        for line in lfile:
            line = brent.trim(line)
            if line == quitLine:
                lfile.close()
                return None
        lfile.close()
        # Soft redo, and didn't finish previous run
        return logPath
    else:
        return logPath
    
def process(cmdPath, suffix = None):
    logPath = shouldRun(cmdPath, suffix)
    if logPath is None:
        print("Skipping %s" % cmdPath)
    else:
        runFields = runbddCuddFields if useCudd else runbddFields
        cmd = ["/".join(homePathFields + runFields), '-c', '-M', str(megabytes)]
        if preprocessConjuncts:
            cmd += ['-p']
        cmd += ['-f', cmdPath]
        cmd += ['-L', logPath]
        if timeLimit is not None:
            cmd += ['-t', str(timeLimit)]
        if verbLevel is not None:
            cmd += ['-v', str(verbLevel)]
        if softRatio is not None:
            cmd += ['-R', str(softRatio)]
        if lookupRatio is not None:
            cmd += ['-K', str(lookupRatio)]
        cmdLine = " ".join(cmd)
        print("Running %s" % cmdLine)
        p = subprocess.Popen(cmd)
        p.wait()
        if p.returncode != 0:
            print("Running command '%s' failed.  Return code = %d" % (cmdLine, p.returncode))


def run(name, args):
    global softRedo, hardRedo, useCudd, timeLimit, verbLevel, megabytes, preprocessConjuncts, softRatio, lookupRatio
    mb = find_memsize.megabytes()
    if mb > 0:
        megabytes = int(mb * memoryFraction)
    nameList = []
    suffix = None
    optlist, args = getopt.getopt(args, 'hrCpI:s:t:v:R:K:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-r':
            softRedo = True
        elif opt == '-C':
            useCudd = True
        elif opt == '-p':
            preprocessConjuncts = True
        elif opt == '-I':
            template = "%s/*.cmd" % val
            nameList = sorted(glob.glob(template))
        elif opt == '-s':
            suffix = val
        elif opt == '-t':
            timeLimit = int(val)
        elif opt == '-v':
            verbLevel = int(val)
        elif opt == '-R':
            softRatio = int(val)
        elif opt == '-K':
            lookupRatio = int(val)
    for name in nameList:
        process(name, suffix)

if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])

    
