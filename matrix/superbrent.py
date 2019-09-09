import circuit
import brent
import sys

# Canonicalizations of multiplication schemes based on multiplications by nonsingular matrices

# Matrix in Z2
class Matrix:
    rows = 3
    cols = 3
    elements = [0] * 3 * 3 # Matrix elements in row-major order

    # Signature is bit vector of length n * n
    def __init__(self, rows = None, cols = None, elements = None, signature = None):
        self.rows = 3 if rows is None else rows
        self.cols = self.rows if cols is None else cols
        if elements is not None:
            if len(elements) != self.rows * self.cols:
                print("Matrix must have %d elements.  Only %d provided" % (self.rows * self.cols, len(elements)))
                return
            self.elements = elements
        else:
            self.elements = [0] * self.rows * self.cols
            if signature is not None:
                for i in range(self.rows * self.cols):
                    b = (signature >> i) & 0x1
                    self.elements[i] = b

    def index(self, r, c):
        return r*self.cols + c

    def row(self, idx):
        return idx // self.cols

    def col(self, idx):
        return idx % self.cols

    def val(self, r, c):
        return self.elements[self.index(r, c)]

    def rowString(self, r):
        line = "|"
        for c in range(self.cols):
            line += " %d" % self.val(r,c)
        line += " |"
        return line

    def show(self, outf = sys.stdout):
        for r in range(self.rows):
            outf.write(self.rowString(r) + '\n')

    def isIdentity(self):
        if self.rows != self.cols:
            return False
        ok = True
        for r in range(self.rows):
            for c in range(self.cols):
                v = self.val(r, c)
                if r == c:
                    ok = ok and v == 1
                else:
                    ok = ok and v == 0
        return ok

    def isPermutation(self):
        if self.rows != self.cols:
            return False
        for r in range(self.rows):
            rcount = 0
            for c in range(self.cols):
                v = self.val(r, c)
                rcount += v
            if rcount != 1:
                return False
        return True

    def isSymmetric(self):
        if self.rows != self.cols:
            return False
        for r in range(self.rows):
            for c in range(self.cols):
                v = self.val(r, c)
                tv = self.val(c, r)
                if v != tv:
                    return False
        return True

    def multiply(self, other):
        if other.rows != self.cols:
            print("Cannot multiply %dx%d matrix times %dx%d matrix" % (self.rows, self.cols, other.rows, other.cols))
            return None
        ielements = [0] * self.rows * other.cols
        for i in range(self.rows):
            for j in range(self.cols):
                a = self.val(i, j)
                for k in range(other.cols):
                    b = other.val(j, k)
                    ielements[other.index(i,k)] += a * b
        elements = [val % 2 for val in ielements]
        return Matrix(self.rows, other.cols, elements)

    def compress(self):
        val = 0
        for i in range(len(self.elements)):
            b = self.elements[i]
            val += (b << i)
        return val

    # Sort matrices so that diagonal matrix is preferable
    def sortKey(self):
        s = ""
        for idx in range(self.cols):
            for r in range(self.rows):
                c  = (r + idx) % self.cols
                b = self.val(r,c)
                if r == c:
                    b = 1-b
                s += str(b)
        return s

# Describe encoding of matrix multiplication
class SuperScheme(brent.MScheme):
    # Make these properties sticky
    uniqueUsage = None
    maxDouble = None
    singletonExclusion = None

    def __init__(self, dim, auxCount, ckt, assignment = None):
        brent.MScheme.__init__(self, dim, auxCount, ckt, assignment)
        self.uniqueUsage = None
        self.maxDouble = None
        self.singletonExclusion = None

    # Check and record properties of scheme
    def obeysUniqueUsage(self):
        if self.uniqueUsage is None:
            self.uniqueUsage = brent.MScheme.obeysUniqueUsage(self)
        return self.uniqueUsage

    def obeysMaxDouble(self):
        if self.maxDouble is None:
            self.maxDouble = brent.MScheme.obeysMaxDouble(self)
        return self.maxDouble

    def obeysSingletonExclusion(self):
        if self.singletonExclusion is None:
            self.singletonExclusion = brent.MScheme.obeysSingletonExclusion(self)
        return self.singletonExclusion

    # Represent as a set of matrices: auxCount triples, each triple encoding the a, b, and c values
    def generateMatrixTriple(self, level):
        matrices = {}
        for cat in ['alpha', 'beta', 'gamma']:
            matrix = Matrix(self.nrow(cat), self.ncol(cat))
            matrices[cat] = matrix
            for r in brent.unitRange(self.nrow(cat)):
                for c in brent.unitRange(self.ncol(cat)):
                    v = brent.BrentVariable(cat, r, c, level)
                    matrix.elements[matrix.index(r-1, c-1)] = self.assignment[v]
        return (matrices['alpha'], matrices['beta'], matrices['gamma'])

    def generateMatrices(self):
        tlist = []
        for level in brent.unitRange(self.auxCount):
            tlist.append(self.generateMatrixTriple(level))
        return tlist

    def loadMatrixTriple(self, level, triple):
        matrices = {'alpha':triple[0], 'beta':triple[1], 'gamma':triple[2]}
        for cat in ['alpha', 'beta', 'gamma']:
            matrix = matrices[cat]
            for r in brent.unitRange(self.nrow(cat)):
                for c in brent.unitRange(self.ncol(cat)):
                    v = brent.BrentVariable(cat, r, c, level)
                    self.assignment[v] = matrix.elements[matrix.index(r-1, c-1)]
    
    def loadMatrices(self, tlist):
        for level in brent.unitRange(self.auxCount):
            self.loadMatrixTriple(level, tlist[level-1])
        self.kernelTerms = self.findKernels()

    # Generate new scheme by multiplying A by nonsingular matrix on right
    # and B by its inverse on left
    def productTransform(self, pair):
        um = pair[0]
        umi = pair[1]
        scheme = SuperScheme(self.dim, self.auxCount, self.ckt)
        for level in brent.unitRange(self.auxCount):
            matrixA, matrixB, matrixC = self.generateMatrixTriple(level)
            newA = matrixA.multiply(um)
            newB = umi.multiply(matrixB)
            scheme.loadMatrixTriple(level, (newA, newB, matrixC))
        scheme.kernelTerms = scheme.findKernels()
        return scheme

        
