#!/usr/bin/python
# Find solutions to Brent equations
# expressing different ways to multiply matrices with < n^3 multiplications

import sys
import getopt
import circuit

def usage(name):
    print "Usage %s [-h] [-p AUX] [-n (N|N1:N2:N3)] [-o OUTF]" % name
    print " -h               Print this message"
    print " -p AUX           Number of auxiliary variables"
    print " -n N or N1:N2:N3 Matrix dimension(s)"
    print " -o OUTF          Output file"

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
    name = "%s-%s.%s.%s.%s.%s.%s" % (prefix, i1, i2, j1, j2, k1, k2)
    if l is not None:
        name += ".l-%d" % l
    return name

def brentName(indices):
    t = ['*'] * 6
    for i in range(len(indices)):
        t[i] = str(indices[i])
    return bterm('brent', t[0], t[1], t[2], t[3], t[4], t[5])

def generateBrent(ckt, p, indices):
    i1, i2, j1, j2, k1, k2 = indices
    kd = i2 == j1 and i1 == k1 and j2 == k2
    ckt.comment("Brent equation for i1 = %d, i2 = %d, j1 = %d, j2 = %d, k1 = %d, k2 = %d (kron delta = %d)" % (i1, i2, j1, j2, k1, k2, 1 if kd else 0))
    av = ckt.addVec(circuit.Vec([alpha(i1, i2, l) for l in unitRange(p)]))
    bv = ckt.addVec(circuit.Vec([beta(j1, j2, l) for l in unitRange(p)]))
    gv = ckt.addVec(circuit.Vec([beta(k1, k2, l) for l in unitRange(p)]))
    pv = ckt.addVec(circuit.Vec([bterm('bp', i1, i2, j1, j2, k1, k2, l) for l in unitRange(p)]))
    ckt.andV(pv, [av, bv, gv])
    rv = pv.concatenate(circuit.Vec([ckt.one])) if not kd else pv.dup()
    bn = brentName([i1, i2, j1, j2, k1, k2])
    ckt.xorN(bn, rv)
    ckt.decRefs([pv])


def iexpand(rlist, sofar = [[]]):
    if len(rlist) == 0:
        return sofar
    n = rlist[-1]
    nsofar = []
    for idx in unitRange(n):
        for l in sofar:
            nsofar.append([idx] + l)
    return iexpand(rlist[:-1], nsofar)


def mencode(ckt, p, n1, n2, n3):
    ranges = [n1, n2, n2, n3, n1, n2]
    indices = iexpand(ranges)
    ckt.comment("Generate all Brent terms")
    for i in indices:
        generateBrent(ckt, p, i)
    names = circuit.Vec([brentName(i) for i in indices])
    ckt.comment("Find size of typical Brent term")
    ckt.information(circuit.Vec([names[0]]))
    ckt.comment("Find combined size of all Brent terms")
    ckt.information(names)
    for level in unitRange(6):
        ckt.comment("Combining terms at level %d" % level)
        gcount = ranges[-1]
        ranges = ranges[:-1]
        indices = iexpand(ranges)
        for i in indices:
            args = ckt.addVec(circuit.Vec([brentName(i + [x]) for x in unitRange(gcount)]))
            bn = brentName(i)
            ckt.andN(bn, args)
            ckt.decRefs([args])
        names = circuit.Vec([brentName(i) for i in indices])
        if len(names) > 1:
            ckt.comment("Find size of typical function at level %d" % level)
            ckt.information(circuit.Vec([names[0]]))
        ckt.comment("Find combined size for terms at level %d" % level)
        ckt.information(names)
        
def solveMatrix(p, n1, n2, n3, f = sys.stdout, zdd = circuit.Z.none):
    ckt = circuit.Circuit(f)
    ckt.cmdLine("option", ["echo", 1])
    ckt.comment("Solving Brent equations to derive matrix multiplication scheme")
    ckt.comment("Goal is to compute A (%d X %d) . B (%d X %d) = C (%d X %d) using %d multiplications" % (n1, n2, n2, n3, n1, n3, p))
    ckt.comment("ZDD mode = %s" % circuit.Z().name(zdd))
    for l in unitRange(p):
        # Declare variables for each auxilliary variable l
        ckt.comment("Variables for auxilliary variable %d" % l)
        nrow = n1
        ncol = n3
        gv = circuit.Vec([gamma(i/ncol+1, (i%ncol)+1, l) for i in range(nrow*ncol)])
        ckt.declare(gv)
        nrow = n1
        ncol = n2
        av = circuit.Vec([alpha(i/ncol+1, (i%ncol)+1, l) for i in range(nrow*ncol)])
        ckt.declare(av)
        nrow = n2
        ncol = n3
        bv = circuit.Vec([beta(i/ncol+1, (i%ncol)+1, l) for i in range(nrow*ncol)])
        ckt.declare(bv)
    mencode(ckt, p, n1, n2, n3)
    bv = circuit.Vec([brentName([])])
    ckt.count(bv)
    ckt.status()

def run(name, args):
    # Default is Strassens
    n1, n2, n3 = 2, 2, 2
    p = 7
    outf = sys.stdout
    optlist, args = getopt.getopt(args, 'hp:n:o:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-p':
            p = int(val)
        elif opt == '-n':
            fields = split(val, ':')
            if len(fields) == 1:
                n1 = n2 = n3 = int(fields[0])
            elif len(fields) == 3:
                n1, n2, n3 = int(fields[0]), int(fields[1]), int(fields[2])
            else:
                print "Invalid matrix dimension '%s'" % val
                usage(name)
                return
        elif opt == '-o':
            try:
                outf = open(val, 'w')
            except:
                print "Couldn't open output file '%s'" % val
                return
        else:
            print "Unknown option '%s'" % opt
            usage(name)
            return
    solveMatrix(p, n1, n2, n3, outf)

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
