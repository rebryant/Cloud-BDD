#! Determine installed RAM size for machine

import sys
import subprocess

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
    process = subprocess.Popen(cmd, stdout = subprocess.PIPE, stderr = subprocess.PIPE)
    code = process.wait()
    if code != 0:
        show(verbose, "Running '%s' returned code %d" % code)
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
        fields = line.split()
        if lineCount == 1 and (len(fields) < 2 or fields[1] != 'total'):
            show(verbose, "Bad header '%s'" % line)
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


        
    
    
    
        
    
    
    
