#!/usr/bin/python
# Benchmarking of CUDD with chaining on n-queens benchmarks

import subprocess
import sys
import getopt
import find_memsize

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

# What fraction of total memory should be used
memoryFraction = 0.95


# Class to define use of ZDDs
class Z:
    none, vars, avars, all = range(4)
    names = ["none", "vars", "avars", "all"]
    suffixes = ["b", "v", "a"]

    def name(self, id):
        return self.names[id]

    def suffix(self, id):
        return self.suffixes[id]

    def chars(self, id):
        return self.suffixes if id == self.all else self.suffixes[id]

# Class to define use of interleaving
class I:
    no, yes, all = range(3)
    interleave = [False, True]
    suffixes = ["l", "i"]

    def name(self, id):
        return self.names[id]

    def suffix(self, id):
        return self.suffixes[id]

    def chars(self, id):
        return self.suffixes if id == self.all else self.suffixes[id]


def benchrun(nstart = 8, nend = 14, fspec = "onh-fast-q", ctype = 'n', ztype = Z.all, itype = I.all):
    aformats = uformats
    flist = aformats if fspec == "" else [fspec]
    ztypes = Z().chars(ztype)
    itypes = I().chars(itype)
    for n in range(nstart, nend+1):
        for format in flist:
            for zprefix in ztypes:
                for iprefix in itypes:
                    root = "%sq%s%.2d-%s" % (iprefix, zprefix, n, format)
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
                        mb = find_memsize.megabytes()
                        if mb > 0:
                            megabytes = int(mb * memoryFraction)
                            flags += ['-M', str(megabytes)]
                        clist = prog + flags
                        print "Running '%s'" % " ".join(clist)
                        rcode = subprocess.call(clist, stdout = logfile, stderr = logfile)

    
def usage(name):
    print "Usage %s [-h] [-n Ns] [-N Ne [-f FORMAT] [-bZ] [-Ii] [-c (o|n|c|a)]" % name
    print "  -h     Print this message"
    print "  -n Ns  Specify starting size"
    print "  -N Ne  Specify ending size"
    print "  -f FMT Specify benchmark format string (Default: all)"
    print "  -b     Compute using BDDs (only)"
    print "  -Z     Compute using ZDDs (only)"
    print "  -A     Compute using ADDs (only)"
    print "  -I     Don't interleave rows (only)"
    print "  -i     Interleave rows (only)"
    print "  -c     Specify chaining mode(s).  (Default: all but c and o)"
    print "         'n' none"
    print "         'c' constant"
    print "         'a' all"
   
def run(name, args):
    nstart = 8
    nend = 8
    newn = False
    format = ""
    ctype = "na"
    ztype = Z.all
    itype = I.all
    optlist, args = getopt.getopt(args, 'hn:N:f:c:bZAiI')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-b':
            ztype = Z.none
        elif opt == '-Z':
            ztype = Z.vars
        elif opt == '-A':
            ztype = Z.avars
        elif opt == '-I':
            itype = I.no
        elif opt == '-i':
            itype = I.yes
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
    benchrun(nstart, nend, format, ctype, ztype = ztype, itype = itype)


run(sys.argv[0], sys.argv[1:])
