#!/usr/bin/python

import sys
import getopt
import circuit

def usage(name):
    print "Usage %s [-h] [-n N] [-b|Z|A] [-r] [-i] [-c]"
    print "  -h    Print this message"
    print "  -n N  Generate N x N multiplier"
    print "  -b    Run entirely with BDDs"
    print "  -Z    Run entirely with ZDDs"
    print "  -A    Run entirely with ADDs"
    print "  -r    Order both args MSB to LSB"
    print "  -R    Order one MSB to LSB, the other LSB to MSB"
    print "  -i    Interleave input words"
    print "  -c    Add correctness checking tests"
    
def fname(n, zdd = circuit.Z.none, reverseA = False, reverseB = False, interleave = False, check = False):
    nstring = "%.2d" % n
    z = circuit.Z()
    zstring = z.suffix(zdd)
    rastring = "r" if reverseA else "f"
    rbstring = "r" if reverseB else "f"
    istring = "i" if interleave else "s"
    ostring = zstring + rastring + rbstring + istring
    cstring = "-check" if check else ""
    return "mult-%s-%s%s.cmd" % (nstring, ostring, cstring)

def run(name, args):
    n = 16
    zlist = [circuit.Z.none, circuit.Z.vars, circuit.Z.avars]
    reverseA = False
    reverseB = False
    interleave = False
    check = False
    optlist, optargs = getopt.getopt(args, 'hn:bZArRic')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-n':
            n = int(val)
        elif opt == '-b':
            zlist = [circuit.Z.none]
        elif opt == '-Z':
            zlist = [circuit.Z.vars]
        elif opt == '-A':
            zlist = [circuit.Z.avars]
        elif opt == '-r':
            reverseA = True
            reverseB = True
        elif opt == '-R':
            reverseA = False
            reverseB = True
        elif opt == '-i':
            interleave = True
        elif opt == '-c':
            check = True
        else:
            print "Unknown option '%s'" % val
            usage(name)
    for zdd in zlist:
        name = fname(n, zdd, reverseA, reverseB, interleave, check)
        try:
            f = open(name, 'w')
        except:
            print "Couldn't open output file '%s'" % name
            sys.exit(1)
        circuit.Multiplier(n, f, zdd, reverseA, reverseB, interleave, check)
        f.close()
            

run(sys.argv[0], sys.argv[1:])
