#!/usr/bin/python

import subprocess

program = "../../mm_generate.py"

time = 7200
lcount = 336
lname = 'smirnov-family.lit'

def fname(cat):
    return "run-fix%d-%s.cmd" % (lcount, cat)

def cmd(cat = 'se', singleton = True):
    ls = [program, '-k', '-B', '2:6',  '-t', str(time), '-L', lname, '-o', fname(cat)]
    if singleton:
        ls.append('-e')
    return ls

command = cmd()
print "Running: %s" % " ".join(command)
process = subprocess.Popen(command)
process.wait()
    


