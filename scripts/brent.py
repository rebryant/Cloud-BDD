# Encoding matrix multiplication problems
import functools
import re
import random
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
    prefixOrder = {'gamma' : 0, 'alpha' : 1, 'beta' : 2 }


    def __init__(self, prefix = None, row = None, column = None, level = None):
        if prefix is not None:
            self.prefix = prefix
            self.symbol = self.symbolizer[prefix]
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

    def __hash__(self):
        return str(self).__hash__()

    def __eq__(self, other):
        return str(self) == str(other)

    def __cmp__(self, other):
        c = cmp(self.level, other.level)
        if c != 0:
            return c
        if self.prefix != other.prefix:
            if self.prefix in self.prefixOrder and other.prefix in other.prefixOrder:
                return cmp(self.prefixOrder[self.prefix], other.prefixOrder[other.prefix])
            return cmp(self.prefix, other.prefix)
        c = cmp(self.row, other.row)
        if c != 0:
            return c
        c = cmp(self.column, other.column)
        return c


# A literal is either a variable (phase = 1) or its complement (phase = 0)
class Literal:

    variable = None
    # Should be 0 or 1
    phase = 1

    def __init__(self, variable, phase):
        self.variable = variable
        self.phase = phase

    def __str__(self):
        prefix = '+' if self.phase == 1 else '-'
        return prefix + str(self.variable)

    def __eq__(self, other):
        return self.variable == other.variable and self.phase == other.phase

    def __cmp__(self, other):
        c = cmp(self.variable, other.variable)
        if c != 0:
            return c
        return cmp(self.phase, other.phase)

    def assign(self, ckt):
        node = circuit.Node(self.variable)
        ckt.assignConstant(node, self.phase)



# (Partial) assignment to a set of variables
class Assignment:

    # Dictionary mapping variables to phases
    asst = None

    def __init__(self, literals = []):
        self.asst = {}
        for lit in literals:
            self.asst[lit.variable] = lit.phase
        
    def variables(self):
        return sorted(self.asst.keys())

    def literals(self):
        vars = self.variables()
        lits = [Literal(v, self.asst[v]) for v in vars]
        return lits

    def __str__(self):
        slist = [str(lit) for lit in self.literals()]
        return " ".join(slist)
        
    def __getitem__(self, key):
        return self.asst[key]

    def __setitem__(self, key, value):
        self.asst[key] = value

    def __len__(self):
        return len(self.asst)

    def assign(self, ckt):
        lits = self.literals()
        for lit in lits:
            lit.assign(ckt)

    # Generate assignment consisting of randomly chosen subset
    def randomSample(self, prob = 0.5):
        tsize = int(prob * len(self.asst))
        sample = random.sample(self.literals(), tsize)
        return Assignment(sample)

    def subset(self, variableFilter = None):
        if variableFilter is None:
            nliterals = self.literals()
        else:
            nliterals = [lit for lit in self.literals() if variableFilter(lit.variable)]
        return Assignment(nliterals)

    # Update assignment with contents of another one
    def overWrite(self, other):
        for lit in other.literals():
            self.asst[lit.variable] = lit.phase

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
    def declareVariables(self, fixedList = []):
        for level in unitRange(self.auxCount):
            generatedComment = False
            for cat in ['gamma', 'alpha', 'beta']:
                nrow = self.nrow(cat)
                ncol = self.ncol(cat)
                allVars = [BrentVariable(cat, i/ncol+1, (i%ncol)+1, level) for i in range(nrow*ncol)]
                vars = [v for v in allVars if v not in fixedList]
                if len(vars) > 0:
                    if not generatedComment:
                        # Declare variables for auxilliary variable level
                        self.ckt.comment("Variables for auxilliary term %d" % level)
                        generatedComment = True
                    vec = circuit.Vec(vars)
                    self.ckt.declare(vec)

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

    # Assignment
    assignment = None

    expressionSplitter = None

    def __init__(self, dim, auxCount, ckt):
        MProblem.__init__(self, dim, auxCount, ckt)
        self.expressionSplitter = re.compile('\s*[-+]\s*')
        self.generateZeroAssignment()

    def generateZeroAssignment(self):
        self.assignment = Assignment()
        for level in unitRange(self.auxCount):
            for cat in ['alpha', 'beta', 'gamma']:
                for r in unitRange(self.nrow(cat)):
                    for c in unitRange(self.ncol(cat)):
                        v = BrentVariable(cat, r, c, level)
                        self.assignment[v] = 0

    def duplicate(self):
        nscheme = MScheme(self.dim, self.auxCount, self.ckt)
        nscheme.assignment = self.assignment.subset()
        return nscheme

    # Parse the output generated by a solver
    def parseFromSolver(self, supportNames, bitString):
        if len(supportNames) != len(bitString):
            raise MatrixException("Mismatch: %d variables in support, but only %d values in bit string" % (len(supportNames), len(bitString)))
        supportVars = [BrentVariable().fromName(s) for s in supportNames]
        for v, c in zip(supportVars, bitString):
            if c == '1':
                self.assignment[v] = 1
            elif c == '0':
                self.assignment[v] = 0
            else:
                raise MatrixException("Can't set variable %s to %s" % (str(v), c))
        return self
                
    def showPolynomial(self, level):
        slist = []
        for cat in ['alpha', 'beta', 'gamma']:
            cslist = []
            for r in unitRange(self.nrow(cat)):
                for c in unitRange(self.ncol(cat)):
                    v = BrentVariable(cat, r, c, level)
                    if self.assignment[v] == 1:
                        cslist.append(v.generateTerm(permuteC = cat == 'gamma'))
            s = '(' + "+".join(cslist) + ')'
            slist.append(s)
        return '*'.join(slist)
            
    def printPolynomial(self, outfile = sys.stdout):
        for level in unitRange(self.auxCount):
            outfile.write(self.showPolynomial(level) + '\n')

    # Parse from polynomial representation
    def parsePolynomialLine(self, line, level):
        if level > self.auxCount:
            raise MatrixException('Out of range level: %d > %d' % (level, self.auxCount))
        parts = line.split('*')
        if len(parts) != 3:
            raise MatrixException("Incomplete polynomial.  Only has %d parts" % len(parts))
        for p in parts:
            # Strip parentheses
            p = p[1:-1]
            # Split with + and -:
            terms = self.expressionSplitter.split(p)
            # Remove empty ones (due to unary -)
            terms = [t for t in terms if t != ""]
            # Create variables
            vars = [BrentVariable(level = level).fromTerm(t, permuteC = True) for t in terms]
            for v in vars:
                self.assignment[v] = 1
        
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


    def generateMixedConstraints(self, categoryProbabilities = {'alpha':1.0, 'beta':1.0, 'gamma':1.0}):
        fixedAssignment = Assignment()
        for cat in categoryProbabilities.keys():
            prob = categoryProbabilities[cat]
            ca = self.assignment.subset(lambda(v): v.prefix == cat).randomSample(prob)
            fixedAssignment.overWrite(ca)
        if len(fixedAssignment) > 0:
            self.ckt.comment("Fixed assignments")
            fixedAssignment.assign(self.ckt)
        fixedVariables = [v for v in fixedAssignment.variables()]
        self.declareVariables(fixedVariables)

    def generateProgram(self, categoryProbabilities = {'alpha':1.0, 'beta':1.0, 'gamma':1.0}):
        plist = categoryProbabilities.values()
        isFixed = functools.reduce(lambda x, y: x*y, plist) == 1.0
        self.ckt.cmdLine("option", ["echo", 1])
        mode = "Checking" if isFixed else "Solving"
        self.ckt.comment("%s Brent equations to derive matrix multiplication scheme" % mode)
        args = self.fullRanges() + (self.auxCount,)
        self.ckt.comment("Goal is to compute A (%d x %d) X B (%d x %d) = C (%d x %d) using %d multiplications" % args)
        for k in categoryProbabilities.keys():
            prob = categoryProbabilities[k]
            self.ckt.comment("Category %s has %.1f%% of its variables fixed" % (k, prob * 100.0))
        self.generateMixedConstraints(categoryProbabilities)
        self.generateBrentConstraints(isFixed)
        bv = circuit.Vec([BrentTerm()])
        if not isFixed:
            self.ckt.count(bv)
            self.ckt.status()
            self.ckt.satisfy(bv)
        self.ckt.write("time")

