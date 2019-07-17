#!/usr/bin/python

# Execute runbdd on multiple command files

import os.path
import sys
import glob
import getopt
import subprocess

import brent

def usage(name):
    print("Usage %s [-h] [-r] [-R] [-C] [-I]")
    print(" -h               Print this message")
    print(" -r               Redo runs that didn't complete")
    print(" -R               Redo all runs")
    print(" -C               Disable chaining")
    print(" -I IDIR          Directory containing command files")
    sys.exit(0)

# Set to home directory for program, split into tokens
homePathFields = ['.']

runbddFields = ["..", "runbdd"]

softRedo = False
hardRedo = False

chain = True

cmdPrefix = "cmd>"


# Check if should run.  Return either None (don't run) or name of log file
def shouldRun(cmdPath):
    fields = cmdPath.split('.')
    if fields[-1] != 'cmd':
        print("Path does not appear to be a command file '%s'" % cmdPath)
        return False
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
    
def process(cmdPath):
    logPath = shouldRun(cmdPath)
    if logPath is None:
        print("Skipping %s" % cmdPath)
    else:
        cmd = ["/".join(homePathFields + runbddFields), '-c']
        if not chain:
            cmd += ['-C', 'n']
        cmd += ['-f', cmdPath]
        cmd += ['-L', logPath]
        cmdLine = " ".join(cmd)
        print("Running %s" % cmdPath)
        p = subprocess.Popen(cmd)
        p.wait()
        if p.returncode != 0:
            print("Running command '%s' failed.  Return code = %d" % (cmdLine, p.returncode))


def run(name, args):
    global softRedo, hardRedo, chain
    nameList = []
    optlist, args = getopt.getopt(args, 'hrRCI:')
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
    for name in nameList:
        process(name)

if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])

    
