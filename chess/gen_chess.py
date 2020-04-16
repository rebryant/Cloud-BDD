#!/usr/bin/python

# Generate variations on mutilated chessboard

import sys
import getopt
import random

import circuit

def usage(name):
    print("Usage: %s [-h] [-z] [-c CTYPE] -N N [-r C1:C2:..:Ck] [-e ECNT] [-o OUT]")
    print("   -h        Print this message")
    print("   -z        Use ZDDs")
    print("   -c CTYPE  Specify conjunction method: n (none), r (rows), c (row/col), s (squares)")
    print("   -N N      Set board size")
    print("   -r CORNER Remove specified corner(s).  Subset of UL, UR, LL, LR")
    print("   -e ECNT   Enumerate ECNT variables")
    print("   -o OUT    Specify output files")

# Class to define conjunction method
class Conjunct:
    none, row, rowcol, square, dAndC = list(range(5))
    names = ["none", "rows", "row/col", "squares", "div/conq"]
    suffixes = ["n", "r", "c", "s", "d"]

    def name(self, id):
        return self.names[id]

    def suffix(self, id):
        return self.suffixes[id]

    def lookup(self, key):
        for id in range(5):
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

    def rowOK(self, r):
        name = "rowOK-%.2d" % (r)
        return self.ckt.node(name)

    def constrainRow(self, dest, r, conjunct = Conjunct.none):
        self.ckt.comment("Constraints for row %d" % (r))
        dest = self.rowOK(r)
        args = [self.constrainSquare(r, c) for c in range(self.N)]
        if conjunct in [Conjunct.row, Conjunct.rowcol]:
            self.ckt.conjunctN(dest, args, self.randomizeArgs)
        else:
            self.ckt.andN(dest, args)
            self.ckt.decRefs(args)
        self.ckt.information(circuit.Vec([dest]))
        return dest

    def allOK(self, columnStart = None, columnCount = None):
        if columnCount is None:
            columnCount = self.N
        if columnStart is None:
            columnStart = 0
        name = "ok"
        if columnCount < self.N:
            name += "-%.2d.%.2d" % (columnStart, columnStart + columnCount -1)
        return self.ckt.node(name)

    def constrainAll(self, conjunct = Conjunct.none, columnStart = None, columnCount = None):
        if columnStart is None:
            columnStart = 0
        if columnCount is None:
            columnCount = self.N - columnStart
        if columnCount < self.N:
            self.ckt.comment("Top-level Constraint for columns %d-%d" % (columnStart, columnStart+columnCount-1))
        else:
            self.ckt.comment("Top-level constraint")
        dest = self.allOK(columnStart, columnCount)
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

    def conjunctAllSquares(self, columnStart = None, columnCount = None):
        if columnStart is None:
            columnStart = 0
        if columnCount is None:
            columnCount = self.N - columnStart
        if columnCount < self.N:
            self.ckt.comment("Square Constraints for columns %d-%d" % (columnStart, columnStart+columnCount-1))
        else:
            self.ckt.comment("Square Constraints")
        dest = self.allOK(columnStart, columnCount)
        args = []
        for r in range(self.N):
            args += [self.constrainSquare(r, c) for c in range(columnStart, columnStart + columnCount)]
        self.ckt.comment("Form conjunction of all square constraints")
        if self.randomizeArgs:
            random.shuffle(args)
        self.ckt.conjunctN(dest, args)
        return dest

    def combineAll(self, conjunct = Conjunct.none, enumerateCount = None):
        if enumerateCount is None:
            okNode = self.conjunctAllSquares() if conjunct == Conjunct.square else self.constrainAll(conjunct)
        else:
            lcount = self.N//2
            rcount = self.N - lcount
            fname = "save-%.4x.bdd" % random.randint(0, (1<<16)-1)
            if conjunct == Conjunct.square:
                okLeft = self.conjunctAllSquares(0, lcount)
            else:
                okLeft = self.constrainAll(conjunct, 0, lcount)
            self.ckt.store(okLeft, fname)
            self.ckt.decRefs([okLeft])
            if conjunct == Conjunct.square:            
                okRight = self.conjunctAllSquares(lcount, rcount)
            else:
                okRight = self.constrainAll(conjunct, lcount, rcount)
            self.ckt.load(okLeft, fname)
            okNode = self.allOK()
            self.ckt.andN(okNode, [okLeft, okRight])
            self.ckt.decRefs([okLeft, okRight])
        return okNode

    def generate(self, zdd = circuit.Z.none, conjunct = False, enumerateCount = None):
        if len(self.removeList) == 0:
            self.ckt.comment("%d X %d chessboard with no corners removed" % (self.N, self.N))
        else:
            cstring = ", ".join(self.removeList)
            self.ckt.comment("%d X %d chessboard with the following corners removed: %s" % (self.N, self.N, cstring))
        self.declare(zdd)
        okNode = self.combineAll(conjunct, enumerateCount)
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
    enumerateCount = None
    outfile = sys.stdout
    optlist, args = getopt.getopt(args, 'hN:zc:r:o:e:')
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
            if conjunct < 0:
                print("Invalid conjunction mode '%s'" % val)
                usage(name)
                return
        elif opt == '-r':
            removeList = val.split(':')
        elif opt == '-e':
            enumerateCount = int(val)
        elif opt == '-o':
            try:
                outfile = open(val, 'w')
            except:
                print("Couldn't open output file '%s'" % val)
                return
    ckt = circuit.Circuit(outfile)
    b = Board(ckt, N, removeList)
    b.generate(zdd, conjunct, enumerateCount)
    if outfile != sys.stdout:
        outfile.close()

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
        
