#!/usr/bin/python
import sys
import getopt

# Enumerate invertible n X n matrices in Z2

def usage(name):
    print("Usage: %s [-h] [-n N]" % name)
    print("   -h    Print this information")
    print("   -n N  Specify matrix dimension")

# Matrix in Z2
class Matrix:
    n = 3
    elements = [0] * 3 * 3 # Matrix elements in row-major order

    # Signature is bit vector of length n * n
    def __init__(self, n, elements = None, signature = None):
        self.n = n
        if elements is not None:
            if len(elements) != n * n:
                print("Matrix must have %d elements.  Only %d provided" % (n * n, len(elements)))
                return
            self.elements = elements
        else:
            self.elements = [0] * n * n
            if signature is not None:
                for i in range(n*n):
                    b = (signature >> i) & 0x1
                    self.elements[i] = b

    def index(self, r, c):
        return r*self.n + c

    def row(self, idx):
        return idx // self.n

    def col(self, idx):
        return idx % self.n

    def val(self, r, c):
        return self.elements[self.index(r, c)]

    def rowString(self, r):
        line = "|"
        for c in range(self.n):
            line += " %d" % self.val(r,c)
        line += " |"
        return line

    def show(self, outf = sys.stdout):
        for r in range(self.n):
            outf.write(self.rowString(r) + '\n')

    def isIdentity(self):
        ok = True
        for r in range(self.n):
            for c in range(self.n):
                v = self.val(r, c)
                if r == c:
                    ok = ok and v == 1
                else:
                    ok = ok and v == 0
        return ok

    def isPermutation(self):
        for r in range(self.n):
            rcount = 0
            for c in range(self.n):
                v = self.val(r, c)
                rcount += v
            if rcount != 1:
                return False
        return True

    def mult(self, other):
        ielements = [0] * self.n * self.n
        for i in range(self.n):
            for j in range(self.n):
                a = self.val(i, j)
                for k in range(self.n):
                    b = other.val(j, k)
                    ielements[self.index(i,k)] += a * b
        elements = [val % 2 for val in ielements]
        return Matrix(self.n, elements)

    def compress(self):
        val = 0
        for i in range(len(self.elements)):
            b = self.elements[i]
            val += (b << i)
        return val

    # Sort matrices so that diagonal matrix is preferable
    def sortKey(self):
        s = ""
        for idx in range(self.n):
            for r in range(self.n):
                c  = (r + idx) % self.n
                b = self.val(r,c)
                if r == c:
                    b = 1-b
                s += str(b)
        return s
    
def generate(n, sig):
    return Matrix(n, signature=sig)

def pairTest(m1, m2):
    m = m1.mult(m2)
    return m.isIdentity()

def showPair(m1, m2, outf = sys.stdout):
    for r in range(m1.n):
        s1 = m1.rowString(r)
        s2 = m2.rowString(r)
        sep = " * " if r == m1.n//2 else "   "
        outf.write("#   " + s1 + sep + s2 + '\n')
                
def showPairs(n, plist, outf = sys.stdout):
    outf.write("# Unique pairs of %d x %d matrices A, A^{-1}\n" % (n, n))
    outf.write("# Represented in compressed form\n")
    outf.write("# as integer with bits expanding to form matrix elements\n")
    outf.write("matrixList = [\n")
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
            m1p1 = m1.mult(p1)
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
    plist = uniquePairs(n)
    showPairs(n, plist)
        

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
        
