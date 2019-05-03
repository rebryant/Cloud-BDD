#!/usr/bin/python
# Find solutions to Brent equations
# expressing different ways to multiply matrices with < n^3 multiplications

import sys
import getopt
import circuit

def usage(name):
    print "Usage %s [-h] [-c] [-p AUX] [-n (N|N1:N2:N3)] [-o OUTF]" % name
    print " -h               Print this message"
    print " -c               Check whether known solution satisfies equations"
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

def generateBrent(ckt, p, indices, check):
    i1, i2, j1, j2, k1, k2 = indices
    kd = i2 == j1 and i1 == k1 and j2 == k2
    ckt.comment("Brent equation for i1 = %d, i2 = %d, j1 = %d, j2 = %d, k1 = %d, k2 = %d (kron delta = %d)" % (i1, i2, j1, j2, k1, k2, 1 if kd else 0))
    av = ckt.addVec(circuit.Vec([alpha(i1, i2, l) for l in unitRange(p)]))
    bv = ckt.addVec(circuit.Vec([beta(j1, j2, l) for l in unitRange(p)]))
    gv = ckt.addVec(circuit.Vec([gamma(k1, k2, l) for l in unitRange(p)]))
    pv = ckt.addVec(circuit.Vec([bterm('bp', i1, i2, j1, j2, k1, k2, l) for l in unitRange(p)]))
    ckt.andV(pv, [av, bv, gv])
    rv = pv.concatenate(circuit.Vec([ckt.one])) if not kd else pv.dup()
    bn = brentName([i1, i2, j1, j2, k1, k2])
    ckt.xorN(bn, rv)
    ckt.decRefs([pv])
    if check:
        ckt.checkConstant(bn, 1)


def iexpand(rlist, sofar = [[]]):
    if len(rlist) == 0:
        return sofar
    n = rlist[-1]
    nsofar = []
    for idx in unitRange(n):
        for l in sofar:
            nsofar.append([idx] + l)
    return iexpand(rlist[:-1], nsofar)


def mencode(ckt, p, n1, n2, n3, check = False):
    ranges = [n1, n2, n2, n3, n1, n2]
    indices = iexpand(ranges)
    ckt.comment("Generate all Brent terms")
    first = True
    for i in indices:
        generateBrent(ckt, p, i, check = check)
        if first and not check:
            first = False
            name = circuit.Vec([brentName(i)])
            ckt.comment("Find size of typical Brent term")
            ckt.information(name)

    if not check:
        names = circuit.Vec([brentName(i) for i in indices])
        ckt.comment("Find combined size of all Brent terms")
        ckt.information(names)

    for level in unitRange(6):
        ckt.comment("Combining terms at level %d" % level)
        gcount = ranges[-1]
        ranges = ranges[:-1]
        indices = iexpand(ranges)
        first = True
        for i in indices:
            args = ckt.addVec(circuit.Vec([brentName(i + [x]) for x in unitRange(gcount)]))
            bn = brentName(i)
            ckt.andN(bn, args)
            ckt.decRefs([args])
            if check:
                ckt.checkConstant(bn, 1)
            if first and not check:
                first = False
                name = circuit.Vec([brentName(i)])
                ckt.comment("Find size of typical function at level %d" % level)
                ckt.information(name)
        if not check:
            names = circuit.Vec([brentName(i) for i in indices])
            ckt.comment("Find combined size for terms at level %d" % level)
            ckt.information(names)
        


# Check that equations are correct by evaluating on known solutions
strassen = {
    'alpha':
        [[(1,1), (2,2)],
         [(2,1), (2,2)],
         [(1,1)],
         [(2,2)],
         [(1,1), (1,2)],
         [(2,1), (1,1)],
         [(1,2), (2,2)]
         ],
    'beta':
        [[(1,1), (2,2)],
         [(1,1)],
         [(1,2), (2,2)],
         [(2,1), (1,1)],
         [(2,2)],
         [(1,1), (1,2)],
         [(2,1), (2,2)]
         ],
    'gamma':
    [[(1,1), (2,2)],
     [(2,1), (2,2)],
     [(1,2), (2,2)],
     [(1,1), (2,1)],
     [(1,1), (1,2)],
     [(2,2)],
     [(1,1)]
     ]
    }


def brentCheck(ckt, n1, n2, n3, scheme):
    alist = scheme['alpha']
    blist = scheme['beta']
    glist = scheme['gamma']
    p = len(alist)
    aval = [0] * p * n1 * n2
    bval = [0] * p * n2 * n3
    gval = [0] * p * n1 * n3
    for l in unitRange(p):
        nrow = n1
        ncol = n2
        offset = nrow * ncol * (l-1)
        for pair in alist[l-1]:
            (i1,i2) = pair
            idx = offset + (i1-1)*ncol + (i2-1)
            aval[idx] = 1

        nrow = n2
        ncol = n3
        offset = nrow * ncol * (l-1)
        for pair in blist[l-1]:
            (j1,j2) = pair
            idx = offset + (j1-1)*ncol + (j2-1)
            bval[idx] = 1


        nrow = n1
        ncol = n3
        offset = nrow * ncol * (l-1)
        for pair in glist[l-1]:
            (k1,k2) = pair
            idx = offset + (k1-1)*ncol + (k2-1)
            gval[idx] = 1

    for l in unitRange(p):
        ckt.comment("Assign values for auxilliary term %d" % l)
        nrow = n1
        ncol = n2
        offset = nrow * ncol * (l-1)
        for i1 in unitRange(nrow):
            for i2 in unitRange(ncol):
                idx = offset + (i1-1)*ncol + (i2-1)
                node = circuit.Node(alpha(i1,i2,l))
                ckt.assignConstant(node, aval[idx])

        nrow = n2
        ncol = n3
        offset = nrow * ncol * (l-1)
        for j1 in unitRange(nrow):
            for j2 in unitRange(ncol):
                idx = offset + (j1-1)*ncol + (j2-1)
                node = circuit.Node(beta(j1,j2,l))
                ckt.assignConstant(node, bval[idx])

        nrow = n1
        ncol = n3
        offset = nrow * ncol * (l-1)
        for k1 in unitRange(nrow):
            for k2 in unitRange(ncol):
                idx = offset + (k1-1)*ncol + (k2-1)
                node = circuit.Node(gamma(k1,k2,l))
                ckt.assignConstant(node, gval[idx])

def brentVariables(ckt, p, n1, n2, n3):
    for l in unitRange(p):
        # Declare variables for each auxilliary variable l
        ckt.comment("Variables for auxilliary term %d" % l)
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

def solveMatrix(p, n1, n2, n3, f = sys.stdout, zdd = circuit.Z.none):
    ckt = circuit.Circuit(f)
    ckt.cmdLine("option", ["echo", 1])
    ckt.comment("Solving Brent equations to derive matrix multiplication scheme")
    ckt.comment("Goal is to compute A (%d X %d) . B (%d X %d) = C (%d X %d) using %d multiplications" % (n1, n2, n2, n3, n1, n3, p))
    ckt.comment("ZDD mode = %s" % circuit.Z().name(zdd))
    brentVariables(ckt, p, n1, n2, n3)
    mencode(ckt, p, n1, n2, n3)
    bv = circuit.Vec([brentName([])])
    ckt.count(bv)
    ckt.status()

def checkMatrix(p, n1, n2, n3, f = sys.stdout, zdd = circuit.Z.none):
    scheme = None
    if n1 == 2 and n2 == 2 and n3 == 2 and p == 7:
        scheme = strassen
    else:
        print "No checker for (%d, %d, %d) with %d terms" % (n1, n2, n3, p)
        return
    ckt = circuit.Circuit(f)
    ckt.cmdLine("option", ["echo", 1])
    ckt.comment("Checking Brent equations to derive matrix multiplication scheme")
    ckt.comment("Goal is to compute A (%d X %d) . B (%d X %d) = C (%d X %d) using %d multiplications" % (n1, n2, n2, n3, n1, n3, p))
    ckt.comment("ZDD mode = %s" % circuit.Z().name(zdd))
    brentCheck(ckt, n1, n2, n3, scheme)
    mencode(ckt, p, n1, n2, n3, check = True)

def run(name, args):
    # Default is Strassens
    n1, n2, n3 = 2, 2, 2
    p = 7
    check = False
    outf = sys.stdout
    optlist, args = getopt.getopt(args, 'hcp:n:o:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-c':
            check = True
        elif opt == '-p':
            p = int(val)
        elif opt == '-n':
            fields = val.split(':')
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
    if check:
        checkMatrix(p, n1, n2, n3, outf)
    else:
        solveMatrix(p, n1, n2, n3, outf)

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
