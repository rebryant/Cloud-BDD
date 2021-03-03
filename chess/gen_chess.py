#!/usr/bin/python

# Generate variations on mutilated chessboard

import sys
import getopt
import random

import circuit

def usage(name):
    print("Usage: %s [-h] [-q] [-t W:H] -N N [-r C1:C2:..:Ck] [-o OUT]")
    print("   -h        Print this message")
    print("   -q        Allow existential quantification")
    print("   -l        Perform conjunctions in linear order")
    print("   -t W:H    Generate hierarchically with tiles of size W x H")
    print("   -N N      Set board height")
    print("   -M M      Set board width (default = N)")
    print("   -r CORNER Remove specified corner(s).  Subset of UL, UR, LL, LR")
    print("   -o OUT    Specify output files")

class Board:

    N = 8
    M = None
    removeList = []
    ckt = None
    randomizeArgs = True

    def __init__(self, ckt, N, M = None, removeList = []):
        self.ckt = ckt
        self.N = N
        if M is None:
            self.M = N
        else:
            self.M = M
        self.removeList = removeList

    def vertical(self, r, c, prefix = "", negate = False):
        name = "%sV-%.2d.%.2d" % (prefix, r, c)
        bang = '!' if negate else ''
        return bang + name

    def horizontal(self, r, c, prefix = "", negate = False):
        name = "%sH-%.2d.%.2d" % (prefix, r, c)
        bang = '!' if negate else ''
        return bang + name

    def vrow(self, r, prefix = ""):
        return circuit.Vec([self.vertical(r, c, prefix) for c in range(1, self.M)])

    def hrow(self, r, prefix = ""):
        return circuit.Vec([self.horizontal(r, c, prefix) for c in range(self.M)])

    def vcol(self, c, prefix = ""):
        return circuit.Vec([self.vertical(r, c, prefix) for r in range(self.N)])

    def hcol(self, c, prefix = ""):
        return circuit.Vec([self.horizontal(r, c, prefix) for r in range(1, self.N)])

    def squareVars(self, r, c, negate = False):
        slist = []
        if r > 0:
            slist.append(self.horizontal(r,   c,   negate = negate))
        if r < self.N-1:
            slist.append(self.horizontal(r+1, c,   negate = negate))
        if c > 0:
            slist.append(self.vertical(r,     c,   negate = negate))
        if c < self.M-1:
            slist.append(self.vertical(r,     c+1, negate = negate))
        return slist

    def removed(self, r, c):
        if r == 0 and c == 0:
            return "UL" in self.removeList
        if r == 0 and c == self.M-1:
            return "UR" in self.removeList
        if r == self.N-1 and c == 0:
            return "LL" in self.removeList
        if r == self.N-1 and c == self.M-1:
            return "LR" in self.removeList
        return False

    def declare(self):
        self.ckt.comment("Declare Boolean variables")
        for r in range(self.N):
            if r > 0:
                hr = self.hrow(r)
                self.ckt.declare(hr)
            vr = self.vrow(r)
            self.ckt.declare(vr)
                        
    def squareOK(self, r, c):
        name = "squareOK-%.2d.%.2d" % (r, c)
        return self.ckt.node(name)

    def constrainSquare(self, r, c):
        self.ckt.comment("Constraint for square %d, %d" % (r, c))
        dest = self.squareOK(r, c)
        if self.removed(r, c):
            nargs = self.squareVars(r, c, negate = True)
            self.ckt.andN(dest, nargs)
        else:
            vs = circuit.Vec(self.squareVars(r, c, negate = False))
            nvs = circuit.Vec(self.squareVars(r, c, negate = True))
            self.ckt.exactly1(dest, vs, nvs)        
        return dest

    def tileOK(self, rowStart, rowCount, columnStart, columnCount):
        rmax = rowStart + rowCount - 1
        cmax = columnStart + columnCount - 1
        name = "tileOK-%d..%d.%d..%d" % (rowStart, rmax, columnStart, cmax)
        return self.ckt.node(name)

    def allOK(self):
        name = "ok"
        return self.ckt.node(name)

    def generateTile(self, rowStart, rowCount, columnStart, columnCount):
        dest = self.tileOK(rowStart, rowCount, columnStart, columnCount)
        self.ckt.comment("Generating constraints for tile %s" % dest)
        if rowStart + rowCount > self.N:
            rowCount = self.N - rowStart
        if columnStart + columnCount > self.M:
            columnCount = self.M - columnStart
        if rowCount == 1 and columnCount == 1:
            return self.constrainSquare(rowStart, columnStart)
        args = []
        for r in range(rowStart, rowStart+rowCount):
            args += [self.constrainSquare(r, c) for c in range(columnStart, columnStart + columnCount)]
        self.ckt.conjunctN(dest, args, randomize = self.randomizeArgs)
        return dest

    def constrainAll(self, tileWidth, tileHeight, quantify):
        dest = self.allOK()
        ccount = self.M // tileWidth
        csize = (self.M + ccount - 1) // ccount
        rcount = self.N // tileHeight
        rsize = (self.N + rcount - 1) // rcount
        args = []
        for cidx in range(ccount):
            columnStart = cidx * csize
            for ridx in range(rcount):
                rowStart = ridx * rsize
                args.append(self.generateTile(rowStart, rsize, columnStart, csize))
        self.ckt.conjunctN(dest, args, randomize = self.randomizeArgs, quantify = quantify)
        return dest

    def generate(self, tileWidth = 1, tileHeight = 1, quantify = False):
        if len(self.removeList) == 0:
            self.ckt.comment("%d X %d chessboard with no corners removed" % (self.N, self.M))
        else:
            cstring = ", ".join(self.removeList)
            self.ckt.comment("%d X %d chessboard with the following corners removed: %s" % (self.N, self.M, cstring))
        self.declare()
        okNode = self.constrainAll(tileWidth, tileHeight, quantify = quantify)
        ok = circuit.Vec([okNode])
        self.ckt.information(ok)
#        self.ckt.count(ok)
        self.ckt.checkConstant(ok, 0)
#        self.ckt.satisfy(ok)
        self.ckt.write("status")
        self.ckt.write("time")
    
def run(name, args):
    N = 8
    M = None
    quantify = False
    removeList = []
    tileWidth = 1
    tileHeight = 1
    outfile = sys.stdout
    optlist, args = getopt.getopt(args, 'hqN:M:t:r:o:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-N':
            N = int(val)
        elif opt == '-M':
            M = int(val)
        elif opt == '-r':
            removeList = val.split(':')
        elif opt == '-q':
            quantify = True
        elif opt == '-t':
            fields = val.split(':')
            tileWidth = int(fields[0])
            tileHeight = int(fields[1]) if len(fields) > 1 else tileWidth
        elif opt == '-o':
            try:
                outfile = open(val, 'w')
            except:
                print("Couldn't open output file '%s'" % val)
                return
    ckt = circuit.Circuit(outfile)
    b = Board(ckt, N, M, removeList)
    b.generate(tileWidth, tileHeight, quantify = quantify)
    if outfile != sys.stdout:
        outfile.close()

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
        
