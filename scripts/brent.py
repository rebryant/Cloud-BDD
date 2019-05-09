# Encoding matrix multiplication problems
import re
import sys

import circuit

class MatrixException(Exception):

    def __init__(self, value):
        self.value = value

    def __str__(self):
        return "Matrix Multiply Exception: " + str(self.value)
        

# Sequence of digits starting at one
def unitRange(n):
    return range(1, n+1)

# Trim string
def trim(s):
    while len(s) > 0 and s[-1] in '\n\r':
        s = s[:-1]
    return s

# Brent variables
class BrentVariable:
    symbol = 'a'
    prefix = 'alpha'
    row = 1
    column = 1
    level = 1
    fieldSplitter = None
    namer = {'a':'alpha', 'b':'beta', 'c':'gamma'}
    symbolizer = {'alpha':'a', 'beta':'b', 'gamma':'c'}


    def __init__(self, prefix = None, row = None, column = None, level = None):
        if prefix is not None:
            self.prefix = prefix
        if row is not None:
            self.row = row
        if column is not None:
            self.column = column
        if level is not None:
            self.level = level
        self.fieldSplitter = re.compile('[-\.]')

    def fromName(self, name):
        fields = self.fieldSplitter.split(name)
        self.prefix = fields[0]
        self.symbol = self.symbolizer[self.prefix]
        srow = fields[2]
        self.row = int(srow)
        scol = fields[4]
        self.column = int(scol)
        slevel = fields[-1]
        self.level = int(slevel)
        return self

    def fromTerm(self, sym, permuteC = False):
        self.symbol = sym[0]
        self.prefix = self.namer[self.symbol]
        self.row = int(sym[1])
        self.column = int(sym[2])
        if permuteC and self.prefix == 'gamma':
            self.row,self.column = self.column,self.row
        return self

    # For use in formulas
    def generateName(self):
        var = "%s-r-%d.c-%d.l-%d" % (self.prefix, self.row, self.column, self.level)
        return var

    # For use in displaying scheme
    def generateTerm(self, permuteC = False):
        sym = self.symbol
        s1 = str(self.row)
        s2 = str(self.column)
        if permuteC:
            s1, s2, = s2, s1
        return sym + s1 + s2

    def __str__(self):
        return self.generateName()

# Ways to refer to individual Brent equations
# as well as aggregations of them
# Indicate aggregation with character '*'    
class BrentTerm:

    prefix = 'brent'
    indices = ['*'] * 6
    suffix = ''

    def __init__(self, indices = None, prefix = None, level = None):
        self.indices = ['*'] * 6
        if indices is not None:
            n = len(indices)
            if n > 6:
                raise MatrixException('Cannot create Brent term with %d indices' % n)
            for i in range(n):
                self.indices[i] = str(indices[i])
        self.prefix = 'brent' if prefix is None else prefix
        self.suffix = '' if level is None else ('-' + str(level))
        
    def __str__(self):
        return self.prefix + '-' + '.'.join(self.indices) + self.suffix

# Solve matrix multiplication
class MProblem:
    # Matrix dimensions, given as (n1, n2, n3)
    dim = (0,0,0)
    # Number of auxilliary variables
    auxCount = 0
    # Circuit generator
    ckt = None

    def __init__(self, dim, auxCount, ckt = None):
        if type(dim) == type(2):
            self.dim = (dim, dim, dim)
        else:
            self.dim = dim
        self.auxCount = auxCount
        self.ckt = ckt

    def nrow(self, category):
        return self.dim[1] if category == 'beta' else self.dim[0]

    def ncol(self, category):
        return self.dim[1] if category == 'alpha' else self.dim[2]

    def fullRanges(self):
        return (self.nrow('alpha'), self.ncol('alpha'), self.nrow('beta'), self.ncol('beta'), self.nrow('gamma'), self.ncol('gamma'))

    # Generate Brent equation
    def generateBrent(self, indices, check = False):
        if len(indices) != 6:
            raise MatrixException("Cannot generate Brent equation for %d indices" % len(indices))
        i1, i2, j1, j2, k1, k2 = indices
        kd = i2 == j1 and i1 == k1 and j2 == k2
        self.ckt.comment("Brent equation for i1 = %d, i2 = %d, j1 = %d, j2 = %d, k1 = %d, k2 = %d (kron delta = %d)" % (i1, i2, j1, j2, k1, k2, 1 if kd else 0))
        av = self.ckt.addVec(circuit.Vec([BrentVariable('alpha', i1, i2, l) for l in unitRange(self.auxCount)]))
        bv = self.ckt.addVec(circuit.Vec([BrentVariable('beta', j1, j2, l) for l in unitRange(self.auxCount)]))
        gv = self.ckt.addVec(circuit.Vec([BrentVariable('gamma', k1, k2, l) for l in unitRange(self.auxCount)]))
        pv = self.ckt.addVec(circuit.Vec([BrentTerm(indices, 'bp', l) for l in unitRange(self.auxCount)]))
        self.ckt.andV(pv, [av, bv, gv])
        rv = pv.concatenate(circuit.Vec([self.ckt.one])) if not kd else pv.dup()
        bn = circuit.Node(BrentTerm(indices))
        self.ckt.xorN(bn, rv)
        self.ckt.decRefs([pv])
        if check:
            self.ckt.checkConstant(bn, 1)
        
    # Helper routines to build up formula encoding all Brent constraints

    # Given list of form [n1, n2, ..., nk],
    # generate list of all indices of the form [i1, i2, .., ik]
    # such that ij <= nj for all j
    def iexpand(self, rlist, sofar = [[]]):
        if len(rlist) == 0:
            return sofar
        n = rlist[-1]
        nsofar = []
        for idx in unitRange(n):
            for l in sofar:
                nsofar.append([idx] + l)
        return self.iexpand(rlist[:-1], nsofar)

    # Declare (subset of) variables
    def declareVariables(self, categories = ['alpha', 'beta', 'gamma']):
        for level in unitRange(self.auxCount):
            # Declare variables for each auxilliary variable level
            self.ckt.comment("Variables for auxilliary term %d" % level)
            for c in ['gamma', 'alpha', 'beta']:
                if c in categories:
                    nrow = self.nrow(c)
                    ncol = self.ncol(c)
                    v = circuit.Vec([BrentVariable(c, i/ncol+1, (i%ncol)+1, level) for i in range(nrow*ncol)])
                    self.ckt.declare(v)

    # Generate Brent equations
    def generateBrentConstraints(self, check = False):
        ranges = self.fullRanges()
        indices = self.iexpand(ranges)
        self.ckt.comment("Generate all Brent equations")
        first = True
        for idx in indices:
            self.generateBrent(idx, check)
            if first and not check:
                first = False
                name = circuit.Vec([BrentTerm(idx)])
                self.ckt.comment("Find size of typical Brent term")
                self.ckt.information(name)
        if not check:
            names = circuit.Vec([BrentTerm(idx) for idx in indices])
            self.ckt.comment("Find combined size of all Brent terms")
            self.ckt.information(names)
        for level in unitRange(6):
            self.ckt.comment("Combining terms at level %d" % level)
            gcount = ranges[-1]
            ranges = ranges[:-1]
            indices = self.iexpand(ranges)
            first = True
            for idx in indices:
                args = self.ckt.addVec(circuit.Vec([BrentTerm(idx + [x]) for x in unitRange(gcount)]))
                bn = BrentTerm(idx)
                self.ckt.andN(bn, args)
                self.ckt.decRefs([args])
                if check:
                    self.ckt.checkConstant(bn, 1)
                if first and not check:
                    first = False
                    name = circuit.Vec([BrentTerm(idx)])
                    self.ckt.comment("Find size of typical function at level %d" % level)
                    self.ckt.information(name)
            if not check:
                names = circuit.Vec([BrentTerm(idx) for idx in indices])
                self.ckt.comment("Find combined size for terms at level %d" % level)
                self.ckt.information(names)

# Describe encoding of matrix multiplication
class MScheme(MProblem):

    # Encoding of alpha variables
    alphaValues = []
    # Encoding of beta variables
    betaValues = []
    # Encoding of gamma variables
    gammaValues = []
    expressionSplitter = None

    def __init__(self, dim, auxCount, ckt):
        MProblem.__init__(self, dim, auxCount, ckt)
        self.alphaValues = [[]] * self.auxCount
        self.betaValues = [[]] * self.auxCount
        self.gammaValues = [[]] * self.auxCount
        self.expressionSplitter = re.compile('[-+]')

    # Get specified list
    def getList(self, category = 'alpha'):
        if category == 'alpha' or category == 'a':
            return self.alphaValues
        if category == 'beta' or category == 'b':
            return self.betaValues
        if category == 'gamma' or category == 'c':
            return self.gammaValues
        raise MultException("No category '%s'" % category)

    # Get all lists
    def getLists(self, category = 'alpha'):
        return [self.alphaValues, self.betaValues, self.gammaValues]

    # Parse the output generated by a solver
    def parseFromSolver(self, supportNames, bitString):
        supportVars = [BrentVariable().fromName(s) for s in supportNames]
        for level in unitRange(self.auxCount):
            lists = {'a':[], 'b':[], 'c':[]}
            for i in range(len(bitString)):
                var = supportVars[i]
                if bitString[i] == '1' and var.level == level:
                    lists[var.symbol].append(var)
            self.alphaValues[level-1] = sorted(lists['a'], key = str)
            self.betaValues[level-1] = sorted(lists['b'], key = str)
            self.gammaValues[level-1] = sorted(lists['c'], key = str)
        return self
                
    def showPolynomial(self, level):
        aterms = [v.generateTerm() for v in  self.alphaValues[level-1]]
        bterms = [v.generateTerm() for v in self.betaValues[level-1]]
        gterms = [v.generateTerm(permuteC=True) for v in self.gammaValues[level-1]]
        return "(%s)*(%s)*(%s)" % ("+".join(aterms), "+".join(bterms), "+".join(gterms))
            
    def printPolynomial(self, outfile = sys.stdout):
        for level in unitRange(self.auxCount):
            outfile.write(self.showPolynomial(level) + '\n')

    # Parse from polynomial representation
    def parsePolynomialLine(self, line, level):
        if level > self.auxCount:
            raise MatrixException('Out of range level: %d > %d' % (level, self.auxCount))
        parts = line.split('*')
        lists = [self.alphaValues, self.betaValues, self.gammaValues]
        if len(parts) != 3:
            raise MatrixException("Incomplete polynomial.  Only has %d parts" % len(parts))
        for p, l in zip(parts, lists):
            # Strip parentheses
            p = p[1:-1]
            # Split with + and -:
            terms = self.expressionSplitter.split(p)
            # Remove empty ones (due to unary -)
            terms = [t for t in terms if t != ""]
            # Create variables
            vars = sorted([BrentVariable(level = level).fromTerm(t, permuteC = True) for t in terms], key = str)
            l[level-1] = vars
        
    # Read polynomial from file
    def parseFromFile(self, fname):
        try:
            f = open(fname, 'r')
        except:
            raise MatrixException("Couldn't open file '%s'" % fname)
        level = 1
        for line in f:
            line = trim(line)
            if (len(line) > 0):
                self.parsePolynomialLine(line, level)
                level += 1
        f.close()
        if level != self.auxCount + 1:
            raise MatrixException("Expected %d equations in polynomial file, got %d" %  (self.auxCount, level))
        return self

    # Generate formula encoding (partial) solution
    def generateCategoryConstraints(self, category, level):
        vars = self.getList(category)[level-1]
        p = self.auxCount
        nrow = self.nrow(category)
        ncol = self.ncol(category)
        valueList = [[0 for c in range(ncol)] for r in range(nrow)]
        for v in vars:
            valueList[v.row-1][v.column-1] = 1
        self.ckt.comment("Assign values to %s terms for auxilliary term %d" % (category, level))
        for row in unitRange(nrow):
            for col in unitRange(ncol):
                v = BrentVariable(prefix = category, row = row, column = col, level = level)
                node = circuit.Node(v)
                self.ckt.assignConstant(node, valueList[row-1][col-1])
                    
    def generateFixedConstraints(self, categories = ['alpha', 'beta', 'gamma']):
        for l in unitRange(self.auxCount):
            for c in categories:
                self.generateCategoryConstraints(c, l)

    def subList(self, vlistA, vlistB):
        namesA = [str(v) for v in vlistA]
        namesB = [str(v) for v in vlistB]
        if len(namesB) > len(namesA):
            return False
        for n in namesA:
            if n not in namesB:
                return False
        return True

    def isSubScheme(self, other):
        if self.auxCount != other.auxCount:
            return False
        myLists = self.getLists()
        otherLists = other.getLists()

        for myL, otherL in zip(myLists, otherLists):
            for level in unitRange(self.auxCount):
                ml = myL[level-1]
                ol = otherL[level-1]
                if not self.subList(ml, l):
                    return False
        return True

    def generateProgram(self, fixedCategories = []):
        symbolicCategories = [c for c in ['alpha', 'beta', 'gamma'] if c not in fixedCategories]
        check = len(fixedCategories) == 3
        self.ckt.cmdLine("option", ["echo", 1])
        mode = "Checking" if check else "Solving"
        self.ckt.comment("%s Brent equations to derive matrix multiplication scheme" % mode)
        args = self.fullRanges() + (self.auxCount,)
        self.ckt.comment("Goal is to compute A (%d x %d) X B (%d x %d) = C (%d x %d) using %d multiplications" % args)
        if len(fixedCategories) > 0:
            self.ckt.comment("Variables %s hard coded" % ", ".join(fixedCategories))
        if len(symbolicCategories) > 0:
            self.ckt.comment("Variables %s symbolic" % ", ".join(symbolicCategories))
        if len(fixedCategories) > 0:
            self.generateFixedConstraints(fixedCategories)
        if len(symbolicCategories) > 0:
            self.declareVariables(symbolicCategories)
        self.generateBrentConstraints(check)
        bv = circuit.Vec([BrentTerm()])
        if not check:
            self.ckt.count(bv)
            self.ckt.status()
            self.ckt.satisfy(bv)
