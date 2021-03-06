#!/usr/bin/python

# Find all unique kernels from set of schemes
# Print them in polynomial form

import os.path
import sys
import glob
import getopt

import circuit
import brent

pathList = []
outDir = '.'

prog = sys.argv[0]
args = sys.argv[1:]

auxCount = 23
dim = (3, 3, 3)

kdict = {}
ckt = circuit.Circuit()    

def usage(prog):
    print("Usage: %s [-h] [-d INDIR] [-D OUTDIR]")
    print("  -h        Print this message")
    print("  -d DLIST  Extract schemes from directory in colon-separated DLIST")
    print("  -D OUTDIR Write kernels to directory OUTDIR")
    

def followPath(p, extension, sofar = []):
    if os.path.isfile(p):
        fields = p.split('/')
        if len(fields) > 0:
            name = fields[-1]
            parts = name.split('.')
            if len(parts) > 1 and parts[-1] == extension:
                sofar.append(p)
    elif os.path.isdir(p):
        ls = glob.glob(p + '/*')
        for np in ls:
            sofar = followPath(np, extension, sofar)
    return sofar
    
def getFiles(dlist):
    flist = []
    dirs = dlist.split(':')
    for dir in dirs:
        flist = followPath(dir, 'exp', flist)
    return flist

optlist, args = getopt.getopt(args, 'hd:D:')
for (opt, val) in optlist:
    if opt == '-h':
        usage(prog)
        sys.exit(0)
    elif opt == '-d':
        pathList = getFiles(val)
        if len(pathList) == 0:
            print("No schemes found in directory %s" % val)
            sys.exit(0)
    elif opt == '-D':
        outDir = val

for p in pathList:
    fields = p.split('/')
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(p)
    except Exception as ex:
        print("Couldn't parse file %s (%s)" % (p, str(ex)))
        continue
    k = s.kernelTerms
    sig = k.sign()
    if sig in kdict:
        kdict[sig] += 1
    else:
        kdict[sig] = 1
        oname = outDir + '/' + k.sign() + '.exp'
        try:
            outf = open(oname, 'w')
        except Exception as ex:
            print("Couldn't open output file %s (%s)" % (oname, str(ex)))
            continue
        k.printPolynomial(outf)
        outf.close()
    
klist = kdict.keys()
klist.sort()

print("%d solutions.  %d distinct kernels:" % (len(pathList), len(klist)))

print "Signature\tCount"
for k in klist:
    print(k + '\t' + str(kdict[k]))


    

