#!/usr/bin/python

import sys
import getopt
import circuit
import qcircuit

def usage(name):
    print "Usage %s [-h] [-a] | [[-zZ] [-n N] [-b] [-c] [-v] [-p r|c|d]]" % name
    print " -h       Print this message"
    print " -a       Generate all combinations"
    print "NOTE: Rest apply only for single benchmark"
    print " -z       Convert to ZDDs part way through"
    print " -Z       Do entirely with ZDDs"
    print " -n N     Encode N x N chessboard"
    print " -b       Use binary encoding"
    print " -c       Careful management of garbage collections"
    print " -v       Verbose information about functions"
    print " -p       Employ preconstraints:"
    print "          r Row"
    print "          c Row and column"
    print "          d Row, column, and diagonal"
    

def run(name, args):
    n = 8
    binary = False
    careful = False
    info = False
    preconstrain = qcircuit.PC.none
    genall = False
    zdd = circuit.Z.none
    optlist, args = getopt.getopt(args, 'hazZn:bcvp:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        if opt == '-a':
            genall = True
        elif opt == '-z':
            zdd = circuit.Z.convert
        elif opt == '-Z':
            zdd = circuit.Z.vars
        elif opt == '-n':
            n = int(val)
        elif opt == '-b':
            binary = True
        elif opt == '-c':
            careful = True
        elif opt == '-v':
            info = True
        elif opt == '-p':
            m = val[0]
            if m == 'r':
                preconstrain = qcircuit.PC.row
            elif m == 'c':
                preconstrain = qcircuit.PC.column
            elif m == 'd':
                preconstrain = qcircuit.PC.diagonal
            else:
                print "Unknown preconstrain method '%s'" % m
                return
        else:
            print "Unknown option '%s'" % opt
            return

    if genall:
        for n in [4, 8, 9, 10, 11, 12, 13, 14, 15, 16]:
            for b in [True, False]:
                for c in [True, False]:
                    for p in [qcircuit.PC.none, qcircuit.PC.row, qcircuit.PC.column]:
                        for z in [circuit.Z.none, circuit.Z.vars, circuit.Z.convert]:
                            qcircuit.qgen(n, b, c, c, p, z)
    else:
        qcircuit.qgen(n, binary, careful, info, preconstrain, zdd)

run(sys.argv[0], sys.argv[1:])
