#!/usr/bin/python

import sys
import getopt
import circuit

def usage(name):
    print "Usage %s [-h] [-a] | [[-Z] [-n N] [-b] [-c] [-v] [-i]]" % name
    print " -h       Print this message"
    print " -a       Generate all combinations"
    print "NOTE: Rest apply only for single benchmark"
    print " -Z       Do entirely with ZDDs"
    print " -A       Do entirely with ADDs"
    print " -n N     Encode N x N chessboard"
    print " -b       Use binary encoding"
    print " -c       Careful management of garbage collections"
    print " -v       Verbose information about functions"
    print " -i       Use interleaved variable ordering"

def run(name, args):
    n = 8
    binary = False
    careful = False
    info = False
    genall = False
    zdd = circuit.Z.none
    interleave = False
    optlist, args = getopt.getopt(args, 'haZAn:bcvi')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        if opt == '-a':
            genall = True
        elif opt == '-Z':
            zdd = circuit.Z.vars
        elif opt == '-A':
            zdd = circuit.Z.avars
        elif opt == '-n':
            n = int(val)
        elif opt == '-b':
            binary = True
        elif opt == '-c':
            careful = True
        elif opt == '-v':
            info = True
        elif opt == '-i':
            interleave = True
        else:
            print "Unknown option '%s'" % opt
            return

    if genall:
        for n in [4, 8, 9, 10, 11, 12, 13, 14, 15, 16]:
            for b in [True, False]:
                for c in [True, False]:
                    for z in [circuit.Z.none, circuit.Z.vars, circuit.Z.avars]:
                        for i in [False, True]:
                            circuit.lqgen(n, b, c, c, z, i)
    else:
        circuit.lqgen(n, binary, careful, info, zdd, interleave)

run(sys.argv[0], sys.argv[1:])
