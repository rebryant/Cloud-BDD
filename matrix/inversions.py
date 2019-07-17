
import sys

# Enumerate invertible k X k matrices in Z2


# Matrix in Z2
class Matrix:

    k = 3
    elements = [0] * 3 * 3

    def __init__(self, k, elements = None, signature = None):
        self.k = k
        if elements is not None:
            if len(elements) != k * k:
                print("Matrix must have %d elements.  Only %d provided" % (k * k, len(elements)))
                return
            self.elements = elements
        else:
            self.elements = [0] * k * k
            if signature is not None:
                for i in range(k*k):
                    b = (signature >> i) & 0x1
                    self.elements[i] = b

    def index(self, r, c):
        return r*self.k + c

    def row(self, idx):
        return idx // self.k

    def col(self, idx):
        return idx % self.k

    def val(self, r, c):
        return self.elements[self.index(r, c)]

    def rowString(self, r):
        line = "|"
        for c in range(self.k):
            line += " %d" % self.val(r,c)
        line += " |"
        return line

    def show(self, outf = sys.stdout):
        for r in range(self.k):
            outf.write(self.rowString(r) + '\n')

    def isIdentity(self):
        ok = True
        for r in range(self.k):
            for c in range(self.k):
                v = self.val(r, c)
                if r == c:
                    ok = ok and v == 1
                else:
                    ok = ok and v == 0
        return ok

    def isPermutation(self):
        for r in range(self.k):
            rcount = 0
            for c in range(self.k):
                v = self.val(r, c)
                rcount += v
            if rcount != 1:
                return False
        return True

    def mult(self, other):
        ielements = [0] * self.k * self.k
        for i in range(self.k):
            for j in range(self.k):
                a = self.val(i, j)
                for k in range(self.k):
                    b = other.val(j, k)
                    ielements[self.index(i,k)] += a * b
        elements = [val % 2 for val in ielements]
        return Matrix(self.k, elements)

    def compress(self):
        val = 0
        for i in range(len(self.elements)):
            b = self.elements[i]
            val = val | (b << i)
        return val
    
def generate(k, sig):
    return Matrix(k, signature=sig)

def pairTest(m1, m2):
    m = m1.mult(m2)
    return m.isIdentity()

def showPair(m1, m2, outf = sys.stdout):
    for r in range(m1.k):
        s1 = m1.rowString(r)
        s2 = m2.rowString(r)
        sep = " * " if r == m1.k//2 else "   "
        outf.write(s1 + sep + s2 + '\n')

def signatureRange(k):
    return (1 << (k*k))
                
def showPairs(plist, outf = sys.stdout):
    for (m1,m2) in plist:
        showPair(m1, m2, outf)
        outf.write("\n")
    

def allPairs(k):
    plist = []
    n = signatureRange(k)
    for sig1 in range(n):
        m1 = generate(k, sig1)
        for sig2 in range(n):
            m2 = generate(k, sig2)
            if pairTest(m1, m2):
                plist.append((m1,m2))
    return plist
            
def allPermutations(k):
    plist = []
    n = signatureRange(k)
    for sig1 in range(n):
        m1 = generate(k, sig1)
        if not m1.isPermutation():
            continue
        for sig2 in range(n):
            m2 = generate(k, sig2)
            if m2.isPermutation() and pairTest(m1, m2):
                plist.append((m1,m2))
    return plist

def uniquePairs(k):
    pairList = allPairs(k)
    permList = allPermutations(k)
    ulist = []
    sigList = []
    for (m1, m2) in pairList:
        sig1 = m1.compress()
        sig2 = m2.compress()
        found = False
        for (p1, p2) in permList:
            m1p1 = m1.mult(p1)
            sigmp = m1p1.compress()
            if sigmp in sigList:
                found = True
                break
        if not found:
            ulist.append((m1,m2))
            sigList.append(sigmp)
    return ulist
            
            
        
        
