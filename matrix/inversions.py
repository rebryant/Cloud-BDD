#!/usr/bin/python
import sys
import getopt
import superbrent

# Enumerate invertible n X n matrices in Z2

def usage(name):
    print("Usage: %s [-h] [-n N]" % name)
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

def allPairs(n):
    plist = []
    n2 = signatureRange(n)
    for sig1 in range(n2):
        m1 = generate(n, sig1)
        for sig2 in range(n2):
            m2 = generate(n, sig2)
            if pairTest(m1, m2):
                plist.append((m1,m2))
    plist.sort(key = lambda p:p[0].sortKey())
    return plist
            
def allPermutations(n):
    plist = []
    n2 = signatureRange(n)
    for sig1 in range(n2):
        m1 = generate(n, sig1)
        if not m1.isPermutation():
            continue
        for sig2 in range(n2):
            m2 = generate(n, sig2)
            if m2.isPermutation() and pairTest(m1, m2):
                plist.append((m1,m2))
    plist.sort(key = lambda p:p[0].sortKey())
    return plist

def uniquePairs(n):
    pairList = allPairs(n)
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
    outf.write("# All pairs of %d x %d matrices A, A^{-1}\n" % (n, n))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = allPairs(n)
    showPairs("allPairList", plist)
    outf.write("\n")

    outf.write("# Unique (up to permutation) pairs of %d x %d matrices A, A^{-1}\n" % (n, n))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = uniquePairs(n)
    showPairs("uniquePairList", plist)
    outf.write("\n")
        
    outf.write("# All pairs of %d x %d permutation matrices A, A^{-1}\n" % (n, n))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    plist = allPermutations(n)
    showPairs("permutationPairList", plist)




if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
        
