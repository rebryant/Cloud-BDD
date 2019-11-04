#!/usr/bin/python

# Determine installed RAM size for machine

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

def trim(s):
    while len(s) > 1 and s[-1] in '\r\n':
        s = s[:-1]
    return s

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
        show(verbose, "Running '%s' returned code %d" % (line, code))
        return 0
    (stdout, stderr) = process.communicate()
    stdout = trim(stdout)
    fields = stdout.split()
    if fields[0] != key + ':':
        show(verbose, "Got unexpected keyword '%s'" % fields[0])
        return 0
    try:
        bytes = int(fields[1])
    except Exception as ex:
        show(verbose, "Couldn't find byte count in response '%s'" % stdout)
        return 0
    return bytes/2**30

def runLinux(verbose = False):
    cmd = ['free', '-g']
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
    lineCount = 0
    for line in stdout:
        lineCount += 1
        line = trim(line)
        print("Line #%d:%s" % (lineCount, line))
        fields = line.split()
        if lineCount == 1 and (len(fields) < 2 or fields[1] != 'total'):
            show(verbose, "Bad header line '%s'" % line)
            return 0
        if lineCount == 2 and (len(fields) < 2 or fields[0] != 'Mem:'):
            show(verbose, "Bad infomation line '%s'" % line)
            return 0
        if lineCount == 2:
            try:
                gb = int(fields[1])
                return gb
            except:
                show(verbose, "Cannot extract gigabyte count from line '%s'" % line)
                return 0

def run(name, args):
    verbose = False
    tryMac = True
    tryLinux = True
    gb = 0
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
        gb = runMac(verbose)
    if gb == 0 and tryLinux:
        show(verbose, "Trying Linux")
        gb = runLinux(verbose)
    if gb == 0:
        show(verbose, "Couldn't find memory size")
    print(str(gb))

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
    
    
        
    
    
    
