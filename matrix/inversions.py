#!/usr/bin/python
import sys
import getopt
import superbrent

# Enumerate invertible n X n matrices in Z2

def usage(name):
    print("Usage: %s [-h] [-x] [-n N]" % name)
    print("   -h    Print this information")
    print("   -n N  Specify matrix dimension")

    
def generate(n, sig):
    return superbrent.Matrix(rows = n, signature=sig)

def pairTest(m1, m2):
    m = m1.multiply(m2)
    return m.isIdentity()

def showPair(m1, m2, outf = sys.stdout):
    for r in range(m1.rows):
        s1 = m1.rowString(r)
        s2 = m2.rowString(r)
        sep = " * " if r == m1.rows//2 else "   "
        outf.write("#   " + s1 + sep + s2 + '\n')
                
def showPairs(fname, plist, outf = sys.stdout):
    outf.write("%s = [\n" % fname)
    for (m1,m2) in plist:
        showPair(m1, m2, outf)
        outf.write("    (0x%.2x, 0x%.2x),\n" % (m1.compress(), m2.compress()))
    outf.write("]\n")

def signatureRange(n):
    return (1 << (n*n))

def allPairs(n, symmetricOnly = False):
    plist = []
    n2 = signatureRange(n)
    for sig1 in range(n2):
        m1 = generate(n, sig1)
        if symmetricOnly and not m1.isSymmetric():
            continue
        for sig2 in range(n2):
            m2 = generate(n, sig2)
            if symmetricOnly and not m2.isSymmetric():
                continue
            if pairTest(m1, m2):
                plist.append((m1,m2))
    plist.sort(key = lambda p:p[0].sortKey())
    return plist
            
def selfInverses(n, symmetricOnly = False):
    plist = []
    n2 = signatureRange(n)
    for sig in range(n2):
        m = generate(n, sig)
        if symmetricOnly and not m.isSymmetric():
            continue
        if pairTest(m, m):
            plist.append((m, m))
    plist.sort(key = lambda p:p[0].sortKey())
    return plist

def transposeInverses(n):
    plist = []
    n2 = signatureRange(n)
    for sig in range(n2):
        m = generate(n, sig)
        mt = m.transpose()
        if pairTest(m, mt):
            plist.append((m, mt))
    plist.sort(key = lambda p:p[0].sortKey())
    return plist

def allPermutations(n, symmetricOnly = False):
    plist = []
    n2 = signatureRange(n)
    for sig1 in range(n2):
        m1 = generate(n, sig1)
        if not m1.isPermutation():
            continue
        if symmetricOnly and not m1.isSymmetric():
            continue
        for sig2 in range(n2):
            m2 = generate(n, sig2)
            if symmetricOnly and not m2.isSymmetric():
                continue
            if m2.isPermutation() and pairTest(m1, m2):
                plist.append((m1,m2))
    plist.sort(key = lambda p:p[0].sortKey())
    return plist

def uniquePairs(n, symmetricOnly = False):
    pairList = allPairs(n, symmetricOnly = symmetricOnly)
    permList = allPermutations(n)
    ulist = []
    sigList = []
    for (m1, m2) in pairList:
        sig1 = m1.compress()
        found = False
        for (p1, p2) in permList:
            m1p1 = m1.multiply(p1)
            sigmp = m1p1.compress()
            if sigmp in sigList:
                found = True
                break
        if not found:
            ulist.append((m1,m2))
            sigList.append(sig1)
    return ulist
            
def run(name, args):
    n = 3
    outf = sys.stdout
    optlist, args = getopt.getopt(args, 'hn:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-n':
            n = int(val)
        else:
            print("Unknown option '%s'" % opt)
            usage(name)
            return


    symmetricOnly = False
    qstring = " symmetric" if symmetricOnly else ""

    outf.write("# All pairs of%s %d x %d matrices A, A^{-1}\n" % (qstring, n, n))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = allPairs(n, symmetricOnly)
    showPairs("allPairList", plist)
    outf.write("\n")

    outf.write("# All pairs of%s %d x %d matrices A, A^{-1} s.t. A^{-1} == A^{T}\n" % (qstring, n, n))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = transposeInverses(n)
    showPairs("transposePairList", plist)
    outf.write("\n")

    outf.write("# All%s %d x %d matrices A, such that, A = A^{-1}\n" % (qstring, n, n))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = selfInverses(n, symmetricOnly)
    showPairs("selfInverseList", plist)
    outf.write("\n")


    outf.write("# Unique (up to permutation) pairs of %d x %d%s matrices A, A^{-1}\n" % (n, n, qstring))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = uniquePairs(n, symmetricOnly)
    showPairs("uniquePairList", plist)
    outf.write("\n")
       
    outf.write("# All pairs of %d x %d%s permutation matrices A, A^{-1}\n" % (n, n, qstring))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = allPermutations(n, symmetricOnly)
    showPairs("permutationPairList", plist)

    symmetricOnly = True
    qstring = " symmetric" if symmetricOnly else ""


    outf.write("# All pairs of%s %d x %d matrices A, A^{-1}\n" % (qstring, n, n))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = allPairs(n, symmetricOnly)
    showPairs("allSymmetricPairList", plist)
    outf.write("\n")

    outf.write("# All%s %d x %d matrices A, such that, A = A^{-1}\n" % (qstring, n, n))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = selfInverses(n, symmetricOnly)
    showPairs("symmetricSelfInverseList", plist)
    outf.write("\n")


    outf.write("# Unique (up to permutation) pairs of %d x %d%s matrices A, A^{-1}\n" % (n, n, qstring))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = uniquePairs(n, symmetricOnly)
    showPairs("uniqueSymmetricPairList", plist)
    outf.write("\n")
        
    outf.write("# All pairs of %d x %d%s permutation matrices A, A^{-1}\n" % (n, n, qstring))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = allPermutations(n, symmetricOnly)
    showPairs("symmetricPermutationPairList", plist)




if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
        
