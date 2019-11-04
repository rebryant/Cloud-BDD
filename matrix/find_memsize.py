#!/usr/bin/python

# Determine installed RAM size for machine (in megabytes)

import getopt
import sys
import subprocess

def usage(name):
    print("Usage: %s [-h] [-v] [-M] [-L]" % name)
    print(" -h               Print this message")
    print(" -v               Verbose Mode")
    print(" -M               Mac only")
    print(" -L               Linux only")

def show(verbose, str):
    if verbose:
        sys.stderr.write(str + '\n')

#For Mac.  Return gigabytes
def runMac(verbose = False):
    key = 'hw.memsize'
    cmd = ['sysctl', key]
    cmdline = " ".join(cmd)
    try:
        process = subprocess.Popen(cmd, stdout = subprocess.PIPE, stderr = subprocess.PIPE)
    except Exception as ex:
        show(verbose, "Cannot run command '%s' (%s)" % (cmdline, str(ex)))
        return 0
    code = process.wait()
    if code != 0:
        show(verbose, "Running '%s' returned code %d" % (cmdline, code))
        return 0
    (stdout, stderr) = process.communicate()
    lines = stdout.split('\n')
    if len(lines) < 1:
        show(verbose, "Runnning command '%s' produced no result" % cmdline)
        return 0
    fields = lines[0].split()
    if fields[0] != key + ':':
        show(verbose, "Got unexpected keyword '%s'" % fields[0])
        return 0
    try:
        bytes = int(fields[1])
    except Exception as ex:
        show(verbose, "Couldn't find byte count in response '%s'" % stdout)
        return 0
    return bytes/2**20

def runLinux(verbose = False):
    cmd = ['free', '-m']
    cmdline = " ".join(cmd)
    try:
        process = subprocess.Popen(cmd, stdout = subprocess.PIPE, stderr = subprocess.PIPE)
    except Exception as ex:
        show(verbose, "Cannot run command '%s' (%s)" % (cmdline, str(ex)))
        return 0
    code = process.wait()
    if code != 0:
        show(verbose, "Running '%s' returned code %d" % code)
        return 0
    (stdout, stderr) = process.communicate()
    col = 0
    lines = stdout.split('\n')
    if len(lines) < 2:
        show(verbose, "Running '%s' produced only %d lines" % len(lines))
    headings = lines[0].split()
    if len(headings) < 2 or headings[0] != 'total':
        show(verbose, "Bad header line '%s'.  headings[1] = %s" % (lines[0], headings[0]))
        return 0
    data = lines[1].split()
    if len(data) < 2 or data[0] != 'Mem:':
        show(verbose, "Bad data line '%s'" % lines[1])
        return 0
    try:
        mb = int(data[1])
        return mb
    except:
        show(verbose, "Cannot extract gigabyte count from line '%s'" % data)
        return 0

def run(name, args):
    verbose = False
    tryMac = True
    tryLinux = True
    mb = 0
    optlist, args = getopt.getopt(args, 'hvLM')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-v':
            verbose = True
        elif opt == '-M':
            tryMac = True
            tryLinux = False
        elif opt == '-L':
            tryLinux = True
            tryMac = False
    if tryMac:
        show(verbose, "Trying Mac")
        mb = runMac(verbose)
    if mb == 0 and tryLinux:
        show(verbose, "Trying Linux")
        mb = runLinux(verbose)
    if mb == 0:
        show(verbose, "Couldn't find memory size")
    print(str(mb))

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
    
    
        
    
    
    
