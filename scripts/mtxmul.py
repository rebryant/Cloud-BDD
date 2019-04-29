# Find solutions to Brent equations
# expressing different ways to multiply matrices with < n^3 multiplications

import sys
import circuit

# Sequence of digits starting at one
def unitRange(n):
    return range(1, n+1)

# Literal representing value prefix p, row r, column c, aux variable l
def mmterm(prefix, r, c, l, negate = False):
    var = "%s-r-%d.c-%d.l-%d" % (prefix, r, c, l)
    return '!' + var if negate else var

def alpha(r, c, l, negate=False):
    return mmterm('alpha', r, c, l, negate)

def beta(r, c, l, negate=False):
    return mmterm('beta', r, c, l, negate)

def gamma(r, c, l, negate=False):
    return mmterm('gamma', r, c, l, negate)

def bterm(prefix, i1, i2, j1, j2, k1, k2, l = None):
    name = "%s-%d.%d.%d.%d.%d.%d" % (prefix, i1, i2, j1, j2, k1, k2)
    if l is not None:
        name += ".l-%d" % l
    return name

def brentName(i1, i2, j1, j2, k1, k2):
    return bterm('brent', i1, i2, j1, j2, k1, k2)

def generateBrent(ckt, p, i1, i2, j1, j2, k1, k2):
    kd = i2 == j1 and i1 == k1 and j2 == k2
    ckt.comment("Brent equation for i1 = %d, i2 = %d, j1 = %d, j2 = %d, k1 = %d, k2 = %d (k delta = %d)" % (i1, i2, j1, j2, k1, k2, 1 if kd else 0))
    av = ckt.addVec(circuit.Vec([alpha(i1, i2, l) for l in unitRange(p)]))
    bv = ckt.addVec(circuit.Vec([beta(j1, j2, l) for l in unitRange(p)]))
    gv = ckt.addVec(circuit.Vec([beta(k1, k2, l) for l in unitRange(p)]))
    pv = ckt.addVec(circuit.Vec([bterm('bp', i1, i2, j1, j2, k1, k2, l) for l in unitRange(p)]))
    ckt.andV(pv, [av, bv, gv])
    rv = pv.concatenate(circuit.Vec([ckt.one])) if not kd else pv.dup()
    bn = brentName(i1, i2, j1, j2, k1, k2)
    ckt.xorN(bn, rv)
    ckt.decRefs([pv])

def generateAllBrents(ckt, p, n1, n2, n3):
    for i1 in unitRange(n1):
        for i2 in unitRange(n2):
            for j1 in unitRange(n2):
                for j2 in unitRange(n3):
                    for k1 in unitRange(n1):
                        for k2 in unitRange(n3):
                            generateBrent(ckt, p, i1, i2, j1, j2, k1, k2)

def brentReduce(ckt, n1, n2, n3, levels = [true, true, true, true, true, true]):
    pass

def solveMatrix(p, n1, n2, n3, f = sys.stdout, zdd = circuit.Z.none):
    ckt = circuit.Circuit(f)
    ckt.comment("Solving Brent equations to derive matrix multiplication scheme")
    ckt.comment("Goal is to compute A (%d X %d) . B (%d X %d) = C (%d X %d) using %d multiplications" % (n1, n2, n2, n3, n1, n3, p))
    ckt.comment("ZDD mode = %s" % circuit.Z().name(zdd))
    for l in unitRange(p):
        # Declare variables for each auxilliary variable l
        ckt.comment("Variables for auxilliary variable %d" % l)
        nrow = n1
        ncol = n2
        av = circuit.Vec([alpha(i/ncol+1, (i%ncol)+1, l) for i in range(nrow*ncol)])
        ckt.declare(av)
        nrow = n2
        ncol = n3
        bv = circuit.Vec([beta(i/ncol+1, (i%ncol)+1, l) for i in range(nrow*ncol)])
        ckt.declare(bv)
        nrow = n1
        ncol = n3
        gv = circuit.Vec([beta(i/ncol+1, (i%ncol)+1, l) for i in range(nrow*ncol)])
        ckt.declare(gv)
        generateAllBrents(ckt, p, n1, n2, n3)

    
