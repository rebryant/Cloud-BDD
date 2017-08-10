#!/usr/bin/python

import string


infile = "words-all.txt"

def outfile(n):
    return "words-%.2d.txt" % n

sets = {}

try:
    inf = open(infile, 'r')
except:
    print "Couldn't open %s" % infile
    exit(1)

def fixstring(s):
    s = string.lower(s)
    while s[-1] in ' \n':
        s = s[:-1]
    return s

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
        exit(1)
    for s in sets[n]:
        outf.write(s + '\n')
    print "Length = %d.  Count = %d" % (n, len(sets[n]))
    outf.close()




    
        
