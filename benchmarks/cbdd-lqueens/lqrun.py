#!/usr/bin/python
# Benchmarking of CUDD with chaining on n-queens benchmarks

import subprocess
import sys
import getopt

rdir = "../.."
sdir = rdir + "/scripts"

rbdd = "%s/runbdd" % rdir
rbddo = "%s/runbddo" % rdir

uformats = [
            "bin-fast-q",
            "bin-slow-v",
            "onh-fast-q",
            "onh-slow-v"
]

# Class to define use of ZDDs
class Z:
    none, vars, convert = range(3)
    names = ["none", "vars", "convert"]
    suffixes = ["b", "v", "z"]

    def name(self, id):
        return self.names[id]

    def suffix(self, id):
        return self.suffixes[id]

def benchrun(nstart = 8, nend = 14, fspec = "onh-fast-q", ctype = 'n', zdd = Z.none):
    aformats = uformats
    flist = aformats if fspec == "" else [fspec]
    prefix = Z().suffix(zdd)
    for n in range(nstart, nend+1):
        for format in flist:
            root = "lq%s%.2d-%s" % (prefix, n, format)
            fname = "%s/%s.cmd" % (sdir, root)
            clist = list(ctype)
            for ct in clist:
                lname = "%s-c%s.out" % (root, ct)
                old = ct == 'o'
                try:
                    logfile = open(lname, 'w')
                except:
                    print "Couldn't open log file '%s'" % (lname)
                    return
                prog = [rbddo] if old else [rbdd]
                flags = ["-c", "-v", "1", "-f", fname]
                if not old:
                    flags = flags + ["-C", ct]
                clist = prog + flags
                print "Running %s" % clist
                rcode = subprocess.call(clist, stdout = logfile, stderr = logfile)

    
def usage(name):
    print "Usage %s [-h] [-n Ns] [-N Ne [-f FORMAT] [-Z] [-c (o|n|c|a)]" % name
    print "  -h     Print this message"
    print "  -n Ns  Specify starting size"
    print "  -N Ne  Specify ending size"
    print "  -f FMT Specify benchmark format string (Default: all)"
    print "  -Z     Compute using ZDDs"
    print "  -c     Specify chaining mode(s).  (Default: all but c and o)"
    print "         'o' run unmodified CUDD"
    print "         'n' none"
    print "         'c' constant"
    print "         'a' all"
   
def run(name, args):
    nstart = 8
    nend = 8
    newn = False
    format = ""
    ctype = "na"
    zdd = Z.none
    optlist, args = getopt.getopt(args, 'hn:N:f:c:Z')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-Z':
            zdd = Z.vars
        elif opt == '-n':
            nstart = int(val)
        elif opt == '-N':
            nend = int(val)
            newn = True
        elif opt == '-f':
            format = val
        elif opt == '-c':
            ctype = val
        else:
            print "Unknown option '%s'" % opt
            return
    if not newn:
        nend = nstart
    benchrun(nstart, nend, format, ctype, zdd = zdd)


run(sys.argv[0], sys.argv[1:])
