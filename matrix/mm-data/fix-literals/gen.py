#!/usr/bin/python

import subprocess

program = "../../mm_generate.py"

time = 7200
lcount = 336
lname = 'smirnov-family.lit'

def fname(suffix, blevel):
    return "run-fix%d-b%d-%s.cmd" % (lcount, blevel, suffix)

def cmd(singleton = True, blevel = 2):
    suffix = 'se' if singleton else 'nse'
    brange = '6' if blevel == 0 else '%d:6' % blevel
    ls = [program, '-k', '-B', brange,  '-t', str(time), '-L', lname, '-o', fname(suffix, blevel)]
    if singleton:
        ls.append('-e')
    return ls

singleton = True
for blevel in range(3):
    command = cmd(singleton, blevel)
    print "Running: %s" % " ".join(command)
    process = subprocess.Popen(command)
    process.wait()
    


