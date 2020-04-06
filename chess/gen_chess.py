#!/usr/bin/python

# Generate variations on mutilated chessboard

import sys
import getopt
import random

import circuit

def usage(name):
    print("Usage: %s [-h] [-z] [-c CTYPE] -N N [-r C1:C2:..:Ck] [-o OUT]")
    print("   -h        Print this message")
    print("   -z        Use ZDDs")
    print("   -c CTYPE  Specify conjunction method: n (none), r (rows), c (row/col), s (squares)")
    print("   -N N      Set board size")
    print("   -r CORNER Remove specified corner(s).  Subset of UL, UR, LL, LR")
    print("   -o OUT    Specify output files")

# Class to define conjunction method
class Conjunct:
    none, row, rowcol, square = list(range(4))
    names = ["none", "rows", "row/col", "squares"]
    suffixes = ["n", "r", "c", "s"]

    def name(self, id):
        return self.names[id]

    def suffix(self, id):
        return self.suffixes[id]

    def lookup(self, key):
        for id in range(4):
            if self.suffix(id) == key:
                return id
        return -1

class Board:

    N = 8
    removeList = []
    ckt = None
    randomizeArgs = True

    def __init__(self, ckt, N, removeList = []):
        self.ckt = ckt
        self.N = N
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
        return circuit.Vec([self.vertical(r, c, prefix) for c in range(1, self.N)])

    def hrow(self, r, prefix = ""):
        return circuit.Vec([self.horizontal(r, c, prefix) for c in range(self.N)])

    def squareVars(self, r, c, negate = False):
        slist = []
        if r > 0:
            slist.append(self.horizontal(r,   c,   negate = negate))
        if r < self.N-1:
            slist.append(self.horizontal(r+1, c,   negate = negate))
        if c > 0:
            slist.append(self.vertical(r,     c,   negate = negate))
        if c < self.N-1:
            slist.append(self.vertical(r,     c+1, negate = negate))
        return slist

    def removed(self, r, c):
        if r == 0 and c == 0:
            return "UL" in self.removeList
        if r == 0 and c == self.N-1:
            return "UR" in self.removeList
        if r == self.N-1 and c == 0:
            return "LL" in self.removeList
        if r == self.N-1 and c == self.N-1:
            return "LR" in self.removeList
        return False

    def declare(self, zdd = circuit.Z.none):
        self.ckt.comment("Declare Boolean variables")
        if zdd != circuit.Z.none:
            tname = "ADD" if zdd == circuit.Z.avars else "ZDD"
            self.ckt.comment("  and convert to %s variables" % tname)
        prefix = "" if zdd == circuit.Z.none else "b"
        for r in range(self.N):
            if r > 0:
                hr = self.hrow(r, prefix)
                self.ckt.declare(hr)
            vr = self.vrow(r, prefix)
            self.ckt.declare(vr)
        if zdd == circuit.Z.none:
            return
        # Convert to ZDD or ADD
        for r in range(self.N):
            if r > 0:
                hr = self.hrow(r, prefix)
                chr = self.hrow(r)
                if zdd == circuit.Z.convert:
                    self.ckt.zcV(chr, hr)
                elif zdd == circuit.Z.avars:
                    self.ckt.acV(chr, hr)
            vr = self.vrow(r, prefix)
            cvr = self.vrow(r)
            if zdd == circuit.Z.convert:
                self.ckt.zcV(cvr, vr)
            elif zdd == circuit.Z.avars:
                self.ckt.acV(cvr, vr)
                        
    def squareOK(self, r, c):
        name = "squareOK-%.2d.%.2d" % (r, c)
        return circuit.Node(name)

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

    def rowOK(self, r):
        name = "rowOK-%.2d" % (r)
        return circuit.Node(name)

    def constrainRow(self, dest, r, conjunct = Conjunct.none):
        self.ckt.comment("Constraints for row %d" % (r))
        dest = self.rowOK(r)
        args = [self.constrainSquare(r, c) for c in range(self.N)]
        if conjunct in [Conjunct.row, Conjunct.rowcol]:
            if self.randomizeArgs:
                random.shuffle(args)
            self.ckt.conjunctN(dest, args)
        else:
            self.ckt.andN(dest, args)
            self.ckt.decRefs(args)
        self.ckt.information(circuit.Vec([dest]))
        return dest

    def allOK(self):
        name = "ok"
        return circuit.Node(name)


    def constrainAll(self, conjunct = Conjunct.none):
        self.ckt.comment("Top-level constraint")
        dest = self.allOK()
        # Construct from bottom to top
        args = [self.constrainRow(dest, r, conjunct) for r in range(self.N-1, -1, -1)]
        if conjunct in [Conjunct.rowcol]:
            if self.randomizeArgs:
                random.shuffle(args)
            self.ckt.conjunctN(dest, args)
        else:
            self.ckt.andN(dest, args)
            self.ckt.decRefs(args)
        return dest

    def conjunctAllSquares(self, rowStart = None, rowCount = None):
        if rowCount is None:
            rowCount = self.N
        if rowStart is None:
            rowStart = 0
        if rowCount < self.N:
            self.ckt.comment("Square Constraints for all squares")
        else:
            self.ckt.comment("Square Constraints for rows %d to %d" % (rowStart, rowStart + rowCount - 1))            
        dest = self.allOK()
        args = []
        for r in range(rowStart, rowStart + rowCount):
            args += [self.constrainSquare(r, c) for c in range(self.N)]
        self.ckt.comment("Form conjunction of %d square constraints" % (len(args)))
        if self.randomizeArgs:
            random.shuffle(args)
        self.ckt.conjunctN(dest, args)
        return dest

    def generate(self, zdd = circuit.Z.none, conjunct = False):
        if len(self.removeList) == 0:
            self.ckt.comment("%d X %d chessboard with no corners removed" % (self.N, self.N))
        else:
            cstring = ", ".join(self.removeList)
            self.ckt.comment("%d X %d chessboard with the following corners removed: %s" % (self.N, self.N, cstring))
#        self.ckt.write("option echo 1")
        self.declare(zdd)
        okNode = self.conjunctAllSquares() if conjunct == Conjunct.square else self.constrainAll(conjunct)
        ok = circuit.Vec([okNode])
        self.ckt.information(ok)
        self.ckt.count(ok)
#        self.ckt.satisfy(ok)
        self.ckt.write("status")
        self.ckt.write("time")
    
def run(name, args):
    N = 8
    zdd = circuit.Z.none
    conjunct = Conjunct.none
    removeList = []
    outfile = sys.stdout
    optlist, args = getopt.getopt(args, 'hN:zc:r:o:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-N':
            N = int(val)
        elif opt == '-z':
            zdd = circuit.Z.convert
        elif opt == '-c':
            conjunct = Conjunct().lookup(val)
            if conjunct < 1:
                print("Invalid conjunction mode '%s'" % val)
                usage(name)
                return
        elif opt == '-r':
            removeList = val.split(':')
        elif opt == '-o':
            try:
                outfile = open(val, 'w')
            except:
                print("Couldn't open output file '%s'" % val)
                return
    ckt = circuit.Circuit(outfile)
    b = Board(ckt, N, removeList)
    b.generate(zdd, conjunct)
    if outfile != sys.stdout:
        outfile.close()

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
        
