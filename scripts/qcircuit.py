# Extend to include various n-queens solutions
# Library of routines for generating Boolean expression evaluations scripts
# Assume constants one & zero are named "one" and "zero"

import sys
import circuit

# Functions related to n-queens
# Literal representing value at position r,c
def sq(r, c, negate = False):
    return "!r-%d.c-%d" % (r, c) if negate else "r-%d.c-%d" % (r, c)

def row(n, r, negate = False):
    return circuit.Vec([sq(r, c, negate) for c in range(n)])

def col(n, c, negate = False):
    return circuit.Vec([sq(r, c, negate) for r in range(n)])

# Principal diagonals numbered from -(n-1) to n-1
def diag(n, i, negate = False):
    if (i < 0):
        # elements numbered from -i,0 to n-1,n+i-1
        return circuit.Vec([sq(j-i, j, negate) for j in range(n+i)])
    else:
        # elements numbered from 0,i to n-i-1,n-1
        return circuit.Vec([sq(j, j+i, negate) for j in range(n-i)])

# Off diagonals numbered from -(n-1) to n-1
def offDiag(n, i, negate = False):
    if (i < 0):
        # elements numbered from n-1+i,0 to 0,n-1+i
        return circuit.Vec([sq(n-1+i-j, j, negate) for j in range(n+i)])
    else:
        # elements numbered from n-i,i to i,n-1
        return circuit.Vec([sq(n-1-j, i+j, negate) for j in range(n-i)])

# Test of subfunctions
def tAtMost1(n, f = sys.stdout):
    ckt = circuit.Circuit(f)
    v = ckt.nameVec("a", n)
    ckt.declare(v)
    nv = ckt.nameVec("!a", n)
    ok = ckt.node("ok")
    ckt.atMost1(ok, v, nv)
    
# Test of subfunctions
def tExactly1(n, f = sys.stdout):
    ckt = circuit.Circuit(f)
    v = ckt.nameVec("a", n)
    ckt.declare(v)
    nv = ckt.nameVec("!a", n)
#    ckt.notV(nv, v)
    ok = ckt.node("ok")
    ckt.exactly1(ok, v, nv)

# Class to define point at which to add preconstraints
class PC:
    none, row, column, diagonal, offdiagonal = range(5)
    names = ["None", "Row", "Column", "Diagonal", "Off-Diagonal"]
    snames = ["uncon", "rwcon", "clcon", "dicon", "odcon"]

    def name(self, id):
        return self.names[id]

    def sname(self, id):
        return self.snames[id]

def bigLog2(x):
    val = 0
    while ((1<<val) < x):
        val+=1
    return val

# Generate constraints for n-queens problem
def nQueens(n, f = sys.stdout, binary = False, careful = False, info = False, preconstrain = PC.none, zdd = circuit.Z.none):
    encoding = "binary" if binary else "one-hot"
    ckt = circuit.Circuit(f)
    ckt.comment("N-queens with %s encoding.  N = %d" % (encoding, n))
    ckt.comment("Preconstrain method: %s" % PC().name(preconstrain))
    ckt.comment("ZDD mode = %s" % circuit.Z().name(zdd))
#    ckt.write("time")
    pc = ckt.node("preconstrain")
    ckt.andN(pc, [])
    okR = ckt.node("okR")
    if zdd == circuit.Z.convert:
        zokR = ckt.node("zokR")
    if binary:
        m = bigLog2(n)
        rows = [ckt.nameVec("v-%d" % r, m) for r in range(n)]
        # Do variables for each row in succession, MSB first
        vars = circuit.Vec(["v-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
        if zdd == circuit.Z.vars:
            zvars = circuit.Vec(["bv-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
            ckt.declare(zvars)
            ckt.zcV(vars, zvars)
        elif zdd == circuit.Z.avars:
            avars = circuit.Vec(["bv-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
            ckt.declare(avars)
            ckt.acV(vars, avars)
        else:
            ckt.declare(vars)
        ckt.comment("Individual square functions")
        for r in range(n):
            for c in range(n):
                ckt.matchVal(c, rows[r], sq(r,c))
        ckt.andN(okR, [])
    else:
        # Generate variables for each square
        snames = [sq(i/n, i%n) for i in range(n*n)]
        sv = circuit.Vec(snames)
        if zdd == circuit.Z.vars:
            znames = ["b" + s for s in snames]
            zv = circuit.Vec(znames)
            ckt.declare(zv)
            ckt.zcV(sv, zv)
        elif zdd == circuit.Z.avars:
            anames = ["b" + s for s in snames]
            av = circuit.Vec(anames)
            ckt.declare(av)
            ckt.acV(sv, av)
        else:
            ckt.declare(sv)
        # Row constraints
        ckt.comment("Row Constraints")
        okr = ckt.nameVec("okr", n)
        for r in range(n):
            ckt.comment("Row %d" % r)
            ckt.exactly1(okr.nodes[r], row(n, r), row(n, r, True))
        ckt.andN(okR, okr.nodes)
        ckt.decRefs([okr])
        if preconstrain >= PC.row:
            ckt.andN(pc, [okR])
        if zdd == circuit.Z.convert:
            ckt.zc(zokR, okR)
            ckt.decRefs([okR])

    okc = ckt.nameVec("okc", n)
    okC = ckt.node("okC")
    okRC = ckt.node("okRC")
    okd = ckt.nameVec("okd", n+n-1)
    okD = ckt.node("okD")
    okRCD = ckt.node("okRCD")
    oko = ckt.nameVec("oko", n+n-1)
    okO = ckt.node("okO")
    ok = ckt.node("ok")

    if zdd == circuit.Z.convert:
        zokC = ckt.node("zokC")
        zokD = ckt.node("zokD")
        zokO = ckt.node("zokO")
        zokRC = ckt.node("zokRC")
        zokRCD = ckt.node("zokRCD")
        zok = ckt.node("zok")

    # Column constraints
    ckt.comment("Column Constraints")
    for c in range(n):
        ckt.comment("Column %d" % c)
        ckt.exactly1(okc.nodes[c], col(n, c), col(n, c, True))
        ckt.andN(okc.nodes[c], [okc.nodes[c], pc])
    ckt.andN(okC, okc.nodes)
    ckt.decRefs([okc])
    if preconstrain >= PC.row:
        ckt.decRefs([okR])
    if zdd == circuit.Z.convert:
        ckt.zc(zokC, okC)
        ckt.decRefs([okC])
        ils = [zokC]
    else:
        ils = [okC]
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information(ils)

    if zdd == circuit.Z.convert:
        if binary:
            ckt.comment("Row constraint implicit")
            ckt.andN(zokRC, [zokC])
            ils = [zokRC]
            dls = [zokC]
        else:
            ckt.comment("Combine row and column")
            ckt.andN(zokRC, [zokR, zokC])
            ils = [zokRC]
            dls = [zokR, zokC]
    elif preconstrain < PC.row:
        ckt.comment("Combine row and column")
        ckt.andN(okRC, [okR, okC])
        ils = [okRC]
        dls = [okR, okC]
    else:
        ckt.comment("Row constraints already incorporated into column constraints")
        ckt.andN(okRC, [okC])
        ils = [okRC]
        dls = [okC]
    if zdd != circuit.Z.convert and preconstrain >= PC.column:
        ckt.andN(pc, [okRC])
    ckt.decRefs(dls)
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information(ils)

    # Diagonal constraints:
    ckt.comment("Diagonal Constraints")
    for i in range(-n+1,n):
        ckt.comment("Diagonal %d" % i)
        out = okd.nodes[i+n-1]
        ckt.atMost1(out, diag(n,i), diag(n, i, True))
        ckt.andN(out, [out, pc])
    ckt.andN(okD, okd.nodes)
    ckt.decRefs([okd])
    if zdd == circuit.Z.convert:
        ckt.zc(zokD, okD)
        ckt.decRefs([okD])
        ils = [zokD]
    else:
        ils = [okD]

    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information(ils)

    if zdd == circuit.Z.convert or preconstrain < PC.column:
        ckt.comment("Add diagonal to row & column")
        if zdd == circuit.Z.convert:
            ckt.andN(zokRCD, [zokRC, zokD])
            ils = [zokRCD]
        else:
            ckt.andN(okRCD, [okRC, okD])
            ils = [okRCD]
    else:
        ckt.comment("Row & column constraints already incorporated into diagonal constraints")
        ckt.andN(okRCD, [okD])
    if zdd != circuit.Z.convert and preconstrain >= PC.diagonal:
        ckt.andN(pc, [okRCD])
    if zdd == circuit.Z.convert:
        ckt.decRefs([zokRC, zokD])
    else:
        ckt.decRefs([okRC, okD])
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information(ils)

    # Off diagonal constraints
    ckt.comment("Off-diagonal Constraints")
    for i in range(-n+1,n):
        ckt.comment("Off-diagonal %d" % i)
        out = oko.nodes[i+n-1]
        ckt.atMost1(out, offDiag(n,i), offDiag(n, i, True))
        ckt.andN(out, [out, pc])
    ckt.andN(okO, oko.nodes)
    ckt.decRefs([oko])
    if zdd == circuit.Z.convert:
        ckt.zc(zokO, okO)
        ckt.decRefs([okO])
        ils = [zokO]
    else:
        ils = [okO]
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information(ils)

    if zdd == circuit.Z.convert or preconstrain < PC.diagonal:
        ckt.comment("Add off diagonal to row, column, and diagonal")
        if zdd == circuit.Z.convert:
            ckt.andN(zok, [zokRCD, zokO])
            ils = [zok]
        else:
            ckt.andN(ok, [okRCD, okO])
            ils = [ok]
    else:
        ckt.comment("Row, column, & diagonal constraints already incorporated into off-diagonal constraints")
        ckt.andN(ok, [okO])
    if zdd == circuit.Z.convert:
        ckt.decRefs([zokRCD, zokO])
    else:
        ckt.decRefs([okRCD, okO])
    ckt.decRefs([pc])
    ckt.comment("BDD generation completed")
    ckt.write("time")
    ckt.information(ils)
    ckt.comment("Model counting")
    ckt.count(ils)
    ckt.status()
    ckt.comment("Flush state")
    ckt.write("flush")
    ckt.comment("Exit")
    ckt.write("quit")

def qname(n, binary = False, careful = False, info = False, preconstrain = PC.none, zdd = circuit.Z.none):
    scnt = "%.2d" % n
    sencode = "bin" if binary else "onh"
    scare = "slow" if careful else "fast"
    sinfo = "v" if info else "q"
    sname = PC().sname(preconstrain)
    szdd = circuit.Z().suffix(zdd)
    return "q%s%s-%s-%s-%s-%s" % (szdd, scnt, sencode, scare, sinfo, sname)

def qgen(n, binary = False, careful = False, info = False, preconstrain = PC.none, zdd = circuit.Z.none):
    fname = qname(n, binary, careful, info, preconstrain, zdd) + ".cmd"
    try:
        f = open(fname, "w")
    except:
        print "Couldn't open file %d" % fname
        sys.exit(1)
    nQueens(n, f, binary, careful, info, preconstrain, zdd)

# Interleave the rows of a set of declared variables
def interleaveRows(vars, n):
    nvars = [""] * len(vars)
    m = len(vars) / n
    # Currently organized as rows 0 .. n-1, each consisting of n variables
    oddn = n % 2 == 1
    for vr in range(n):
        midn = (n-1)/2 if oddn else n/2
        bigvr = vr >= midn
        if oddn:
            if vr == midn:
                r = 0
            elif bigvr:
                r = (vr - midn - 1) * 2 + 1
            else:
                r = (midn - vr) * 2
        else:
            if bigvr:
                r = (vr - midn) * 2 + 1
            else:
                r = (midn - vr - 1) * 2
        for c in range(m):
            oidx = vr * m + c
            nidx = r * m + c
            nvars[nidx] = vars[oidx]
    return nvars

# Generate constraints for n-queens problem using layered construction
def lQueens(n, f = sys.stdout, binary = False, careful = False, info = False, zdd = circuit.Z.none, interleave = False):
    encoding = "binary" if binary else "one-hot"
    ckt = circuit.Circuit(f)
    ckt.comment("Layered construction of N-queens with %s encoding.  N = %d" % (encoding, n))
    ckt.comment("ZDD mode = %s" % circuit.Z().name(zdd))
    ckt.comment("%s of variables" % ("Interleaving" if interleave else "No interleaving"))
    okr = ckt.nameVec("okr", n)
    if binary:
        m = bigLog2(n)
        rows = [ckt.nameVec("v-%d" % r, m) for r in range(n)]
        # Do variables for each row in succession, MSB first
        vars = circuit.Vec(["v-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
        if zdd == circuit.Z.vars:
            zvars = circuit.Vec(["bv-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
            dvars = circuit.Vec(interleaveRows(zvars.nodes, n)) if interleave else zvars
            ckt.declare(dvars)
            ckt.zcV(vars, zvars)
        elif zdd == circuit.Z.avars:
            avars = circuit.Vec(["bv-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
            dvars = circuit.Vec(interleaveRows(avars.nodes, n)) if interleave else avars
            ckt.declare(dvars)
            ckt.acV(vars, avars)
        else:
            dvars = circuit.Vec(interleaveRows(vars.nodes, n)) if interleave else vars
            ckt.declare(dvars)
        ckt.comment("Individual square functions")
        for r in range(n):
            for c in range(n):
                ckt.matchVal(c, rows[r], sq(r,c))
    else:
        # Generate variables for each square
        snames = [sq(i/n, i%n) for i in range(n*n)]
        sv = circuit.Vec(snames)
        if zdd == circuit.Z.vars:
            znames = ["b" + s for s in snames]
            zv = circuit.Vec(znames)
            dvars = circuit.Vec(interleaveRows(znames, n)) if interleave else zv
            ckt.declare(dvars)
            ckt.zcV(sv, zv)
        elif zdd == circuit.Z.avars:
            anames = ["b" + s for s in snames]
            av = circuit.Vec(anames)
            dvars = circuit.Vec(interleaveRows(anames, n)) if interleave else av
            ckt.declare(dvars)
            ckt.acV(sv, av)
        else:
            dvars = circuit.Vec(interleaveRows(snames, n)) if interleave else sv
            ckt.declare(dvars)
    # Row constraints
    ckt.comment("Row Constraints")
    for r in range(n):
        ckt.comment("Row %d" % r)
        ckt.exactly1(okr.nodes[r], row(n, r), row(n, r, True))

    ckt.comment("Create constraints from fictional row %d" % n)
    # Column free 0 .. n-1
    prevcfree = ckt.nameVec("cfree-%d" % n, n)
    for i in range(0, n):
        ckt.andN(prevcfree.nodes[i], [])
    # Diagonal free 1 .. n-1
    prevdfree = ckt.nameVec("dfree-%d" % n, n)
    for i in range(1, n):
        ckt.andN(prevdfree.nodes[i], [])
    # Off diagonal free 0 .. n-2
    prevofree = ckt.nameVec("ofree-%d" % n, n)
    for i in range(0, n-1):
        ckt.andN(prevofree.nodes[i], [])
    vok = ckt.nameVec("ok", n+1)
    ckt.andN(vok.nodes[n], [])
    for r in range(n-1,-1,-1):
        nrow = row(n, r, True)
        # Check conditions at this layer
        ckt.comment("Determine correctness for rows %d and above" % r)
        clear = ckt.nameVec("clear-%d" % r, n)
        ckt.andN(clear.nodes[0], [prevcfree.nodes[0], prevdfree.nodes[1]])
        for c in range(1, n-1):
            ckt.andN(clear.nodes[c], [prevofree.nodes[c-1], prevcfree.nodes[c], prevdfree.nodes[c+1]])
        ckt.andN(clear.nodes[n-1], [prevofree.nodes[n-2], prevcfree.nodes[n-1]])
        legal = ckt.nameVec("legal-%d" % r, n)
        ckt.orV(legal, [nrow, clear])
        alllegal = ckt.node("Legal-%d" % r)
        ckt.andN(alllegal, legal.nodes)
        ckt.andN(vok.nodes[r], [vok.nodes[r+1], okr.nodes[r], alllegal])

        if r > 0:
            # New constraints
            ckt.comment("Generate constraints from rows %d and above" % r)
            cfree = ckt.nameVec("cfree-%d" % r, n)
            dfree = ckt.nameVec("dfree-%d" % r, n)
            ofree = ckt.nameVec("ofree-%d" % r, n)
            for i in range(0, n):
                ckt.andN(cfree.nodes[i], [nrow.nodes[i], prevcfree.nodes[i]])
            for i in range(1, n-1):
                ckt.andN(dfree.nodes[i], [nrow.nodes[i], prevdfree.nodes[i+1]])
            ckt.andN(dfree.nodes[n-1], [nrow.nodes[n-1]])
            ckt.andN(ofree.nodes[0], [nrow.nodes[0]])
            for i in range(1, n-1):
                ckt.andN(ofree.nodes[i], [nrow.nodes[i], prevofree.nodes[i-1]])

        prevdfree.nodes = prevdfree.nodes[1:]
        prevofree.nodes = prevofree.nodes[:n-1]
        ckt.decRefs([prevcfree, prevdfree, prevofree, clear, legal, alllegal,
                     ckt.node(okr.nodes[r]), ckt.node(vok.nodes[r+1])])

        if careful:
            ckt.comment("Forced GC")
            ckt.collect()
            ckt.status()

        if r > 0:
            prevcfree = cfree
            prevdfree = dfree
            prevofree = ofree
            if info:
                ckt.information([vok.nodes[r]] + cfree.nodes + dfree.nodes[1:] + ofree.nodes[:n-1])

    ok = ckt.node("ok")
    ckt.andN(ok, [vok.nodes[0]])
    ckt.decRefs([ckt.node(vok.nodes[0])])
    ckt.comment("%s generation completed" % ("ADD" if zdd == circuit.Z.avars else "BDD" if zdd == circuit.Z.none else "ZDD"))
    ckt.write("time")
    ckt.information(["ok"])
    ckt.comment("Model counting")
    ckt.count(["ok"])
    ckt.status()
    ckt.comment("Flush state")
    ckt.write("flush")
    ckt.comment("Exit")
    ckt.write("quit")


def lqname(n, binary = False, careful = False, info = False, zdd = circuit.Z.none, interleave = False):
    scnt = "%.2d" % n
    sencode = "bin" if binary else "onh"
    scare = "slow" if careful else "fast"
    sinfo = "v" if info else "q"
    szdd = circuit.Z().suffix(zdd)
    iinfo = "i" if interleave else "l"
    return "%sq%s%s-%s-%s-%s" % (iinfo, szdd, scnt, sencode, scare, sinfo)


def lqgen(n, binary = False, careful = False, info = False, zdd = circuit.Z.none, interleave = False):
    fname = lqname(n, binary, careful, info, zdd, interleave) + ".cmd"
    try:
        f = open(fname, "w")
    except:
        print "Couldn't open file %d" % fname
        sys.exit(1)
    lQueens(n, f, binary, careful, info, zdd, interleave)

def Multiplier(n, f = sys.stdout, zdd = circuit.Z.none, reverseA = False, reverseB = False, interleave = False, check = False):
    ckt = circuit.Circuit(f)
    ckt.comment("Construction of %d x %d multiplier" % (n, n))
    ckt.comment("ZDD mode = %s" % circuit.Z().name(zdd))
    avec = ckt.nameVec("A", n)
    bvec = ckt.nameVec("B", n)
    if zdd == circuit.Z.none or zdd == circuit.Z.convert:
        davec = avec
        dbvec = bvec
    else:
        davec = ckt.nameVec("bA", n)
        dbvec = ckt.nameVec("bB", n)
    if reverseA:
        davec = davec.reverse()
    if reverseB:
        dbvec = dbvec.reverse()
    dvec = davec.interleave(dbvec) if interleave else circuit.Vec(davec.nodes + dbvec.nodes)
    ckt.declare(dvec)
    if zdd == circuit.Z.vars:
        ckt.zcV(avec, davec)
        ckt.zcV(bvec, dbvec)
    elif zdd == circuit.Z.avars:
        ckt.acV(avec, davec)
        ckt.acV(bvec, dbvec)
    outvec = ckt.nameVec("out", n+n)
    outcvec = ckt.nameVec("outc", n+n) if zdd == circuit.Z.convert else outvec
    ckt.multiplier(outcvec, avec, bvec)
    if zdd == circuit.Z.convert:
        ckt.zcV(outvec, outcvec)
        ckt.decRefs([outcvec])
    ckt.comment("%s generation completed" % ("ADD" if zdd == circuit.Z.avars else "BDD" if zdd == circuit.Z.none else "ZDD"))
    ckt.write("time")
    ckt.information(outvec.nodes)
    if check:
        checkvec = ckt.nameVec("cout", n+n)
        ckt.multblast(checkvec, avec, bvec)
        ckt.cmdSequence("equal", [outvec, checkvec])
    ckt.status()
    ckt.comment("Flush state")
    ckt.write("flush")
    ckt.comment("Exit")
    ckt.write("quit")


    
