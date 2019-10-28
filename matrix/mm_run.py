#!/usr/bin/python

# Execute runbdd on multiple command files

import os.path
import sys
import glob
import getopt
import subprocess

import brent

def usage(name):
    print("Usage %s [-h] [-r] [-R] [-C] [-I] [-s SUFFIX] [-t TIME] [-v VERB]")
    print(" -h               Print this message")
    print(" -r               Redo runs that didn't complete")
    print(" -R               Redo all runs")
    print(" -C               Disable chaining")
    print(" -I IDIR          Directory containing command files")
    print(" -s SUFFIX        Specify suffix for log file root name")
    print(" -t TIME          Set runtime limit (in seconds)")
    print(" -v VERB          Set verbosity level")
    sys.exit(0)

# Set to home directory for program, split into tokens
homePathFields = ['.']

runbddFields = ["..", "runbdd"]

softRedo = False
hardRedo = False

chain = True

timeLimit = None
verbLevel = None

cmdPrefix = "cmd>"


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
        cmd = ["/".join(homePathFields + runbddFields), '-c']
        if not chain:
            cmd += ['-C', 'n']
        cmd += ['-f', cmdPath]
        cmd += ['-L', logPath]
        if timeLimit is not None:
            cmd += ['-t', str(timeLimit)]
        if verbLevel is not None:
            cmd += ['-v', str(verbLevel)]
        cmdLine = " ".join(cmd)
        print("Running %s" % cmdPath)
        p = subprocess.Popen(cmd)
        p.wait()
        if p.returncode != 0:
            print("Running command '%s' failed.  Return code = %d" % (cmdLine, p.returncode))


def run(name, args):
    global softRedo, hardRedo, chain, timeLimit, verbLevel
    nameList = []
    suffix = None
    optlist, args = getopt.getopt(args, 'hrRCI:s:t:v:')
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
        elif opt == '-I':
            template = "%s/*.cmd" % val
            nameList = sorted(glob.glob(template))
        elif opt == '-s':
            suffix = val
        elif opt == '-t':
            timeLimit = int(val)
        elif opt == '-v':
            verbLevel = int(val)
    for name in nameList:
        process(name, suffix)

if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])

    
