#!/usr/bin/python

import string
import sys

infile = "words.list"

def outfile(n):
    return "all-words-%.2d.list" % n

sets = {}

try:
    inf = open(infile, 'r')
except:
    print "Couldn't open %s" % infile
    sys.exit(1)

def fixstring(s):
    s = string.lower(s)
    t = ""
    for c in s:
        x = ord(c)
        if x >= ord('a') and x <= ord('z'):
            t += c
    return t

for line in inf:
    s = fixstring(line)
    n = len(s)
    if n in sets:
        sets[n] |= { s }
    else:
        sets[n] = { s }

for n in sets.keys():
    fname = outfile(n)
    try:
        outf = open(fname, 'w')
    except:
        "Couldn't open outfile file %s" % outf
        sys.exit(1)
    for s in sets[n]:
        outf.write(s + '\n')
    print "Length = %d.  Count = %d" % (n, len(sets[n]))
    outf.close()




    
        
