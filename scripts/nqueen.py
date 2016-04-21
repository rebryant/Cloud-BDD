#!/usr/bin/python

import sys
import getopt
import circuit

def usage(name):
    print "Usage %s [-h] [-a] [-z] [-n N] [-b] [-c] [-v] [-p r|c|d]" % name
    print " -h       Print this message"
    print " -a       Generate all combinations"
    print " -z       Use ZDDs"
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
    preconstrain = circuit.PC.none
    genall = False
    zdd = False
    optlist, args = getopt.getopt(args, 'hazn:bcvp:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        if opt == '-a':
            genall = True
        elif opt == '-z':
            zdd = True
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
                preconstrain = circuit.PC.row
            elif m == 'c':
                preconstrain = circuit.PC.column
            elif m == 'd':
                preconstrain = circuit.PC.diagonal
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
                    for p in [circuit.PC.none, circuit.PC.row, circuit.PC.column]:
                        circuit.qgen(n, b, c, c, p, False)
                    for p in [circuit.PC.none, circuit.PC.row]:
                        circuit.qgen(n, b, c, c, p, True)
    else:
        circuit.qgen(n, binary, careful, info, preconstrain, zdd)

run(sys.argv[0], sys.argv[1:])
