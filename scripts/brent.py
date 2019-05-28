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

# Helper function for permuation generation
def permutationBuilder(n):
    if n == 1:
        return [[0]]
    plist = permutationBuilder(n-1)
    nplist = []
    for p in plist:
        for pos in range(n):
            np = p[:pos] + [n-1] + p[pos:]
            nplist.append(np)
    return nplist
    
# A permuter is a dictionary mapping a set of elements to itself
# Generate set of all permuters of ls
def allPermuters(vals):
    listForm = permutationBuilder(len(vals))
    listForm.sort()
    dictForm = []
    for nls in listForm:
        d = { v : vals[i] for v, i in zip(vals, nls) }
        dictForm.append(d)
    return dictForm

# Replace permuter elements
def convertPermuter(p, rvals):
    return { rvals[k] : rvals[p[k]] for k in p.keys()}

# Invert permuter
def invertPermuter(p):
    return { p[k] : k for k in p.keys()}

# Create string representation of permutation
def showPerm(p):
    slist = ["%s --> %s" % (k, p[k]) for k in sorted(p.keys())]
    return ", ".join(slist)

# Create a compressed representation of a permutation
def signPerm(p):
    slist = [str(p[k]) for k in sorted(p.keys())]
    return "".join(slist)

# Brent variables
class BrentVariable:
    symbol = 'a'
    prefix = 'alpha'
    row = 1
    column = 1
    level = 1
    fieldSplitter = re.compile('[-\.]')
    namer = {'a':'alpha', 'b':'beta', 'c':'gamma'}
    symbolizer = {'alpha':'a', 'beta':'b', 'gamma':'c'}
    prefixOrder = {'gamma' : 0, 'alpha' : 1, 'beta' : 2 }
    # Which reorderings require a subscript swap
    # Those that require odd number of swaps to generate
    flip = { 'abc' : False, 'bca' : False, 'cab' : False, 'cba': True, 'bac': True, 'acb' : True }

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
        var = "%s-r-%d.c-%d.l-%.2d" % (self.prefix, self.row, self.column, self.level)
        return var

    # For use in displaying scheme
    def generateTerm(self, permuteC = False):
        sym = self.symbol
        s1 = str(self.row)
        s2 = str(self.column)
        if permuteC:
            s1, s2, = s2, s1
        return sym + s1 + s2

    def shouldFlip(self, variablePermuter):
        klist = [self.symbolizer[variablePermuter[p]] for p in ['alpha', 'beta', 'gamma']]
        key = "".join(klist)
        return self.flip[key]
                       

    def permute(self, variablePermuter = None, indexPermuter = None, levelPermuter = None):
        prefix = self.prefix
        row = self.row
        column = self.column
        level = self.level
        if variablePermuter is not None:
            if prefix == 'gamma':
                left, right = column, row
            else:
                left, right = row, column
            if self.shouldFlip(variablePermuter):
                left, right = right, left
            prefix = variablePermuter[prefix]
            if prefix == 'gamma':
                row, column = right, left
            else:
                row, column = left, right
        if indexPermuter is not None:
            row = indexPermuter[row]
            column = indexPermuter[column]
        if levelPermuter is not None:
            level = levelPermuter[level]
        return BrentVariable(prefix, row, column, level)

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
        prefix = '' if self.phase == 1 else '!'
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

    def __contains__(self, key):
        return key in self.asst

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

    # Generate assignment where indices and levels are permuted according to permutation maps
    def permute(self, variablePermuter = None, indexPermuter = None, levelPermuter = None):
        nliterals = [Literal(lit.variable.permute(variablePermuter, indexPermuter, levelPermuter), lit.phase) for lit in self.literals()]
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

# Representation of a triple a_i,j * b_j,k --> c_i,k
class KernelTerm:

    i = 1
    j = 1
    k = 1
    level = 1

    def __init__(self, i, j, k, level):
        self.i = i
        self.j = j
        self.k = k
        self.level = level

    def alpha(self):
        return BrentVariable('alpha', self.i, self.j, self.level)

    def beta(self):
        return BrentVariable('beta', self.j, self.k, self.level)

    def gamma(self):
        return BrentVariable('gamma', self.i, self.k, self.level)

    def variables(self):
        return [self.alpha(), self.beta(), self.gamma()]

    def inAssignment(self, asst):
        avar = self.alpha()
        if avar not in asst or asst[avar] == 0:
            return False
        bvar = self.beta()
        if bvar not in asst or asst[bvar] == 0:
            return False
        gvar = self.gamma()
        if gvar not in asst or asst[gvar] == 0:
            return False
        return True

    def permute(self, ijkPermuter = None, indexPermuter = None, levelPermuter = None):
        i = self.i
        j = self.j
        k = self.k
        level = self.level
        if ijkPermuter is not None:
            vals = [i, j, k]
            i, j, k = [vals[ijkPermuter[idx]] for idx in range(3)]
        if indexPermuter is not None:
            i = indexPermuter[i]
            j = indexPermuter[j]
            k = indexPermuter[k]
        if levelPermuter is not None:
            level = levelPermuter[level]
        return KernelTerm(i, j, k, level)

    def __cmp__(self, other):
        c = cmp(self.level, other.level)
        if c != 0:
            return c
        c = cmp(self.i, other.i)
        if c != 0:
            return c
        c = cmp(self.j, other.j)
        if c != 0:
            return c
        return cmp(self.k, other.k)

    def generateString(self, showLevel = True):
        astring = self.alpha().generateTerm()
        bstring = self.beta().generateTerm()
        cstring = self.gamma().generateTerm(True)
        lstring = "[%.2d]" % self.level if showLevel else ""
        return "%s*%s*%s%s" % (astring, bstring, cstring, lstring)

    def __str__(self):
        return self.generateString()

# Function to translate from permutation of ijk elements in kernel term to variables in Brent term
def ijk2var(ijkp):
    matchDict = { '012' : 'abc', '021' : 'cba', '102' : 'acb', '120' : 'cab', '201' : 'bca', '210' : 'bac' }
    key = signPerm(ijkp)
    sig = matchDict[key]
    vals = [BrentVariable.namer[c] for c in sig]
    vp = { v1 : v2 for v1, v2 in zip(['alpha', 'beta', 'gamma'], vals) }
    return vp

# Representation of set of Kernel terms
class KernelSet:

    dim = (2, 2, 2)
    auxCount = 7
    kdlist = []

    def __init__(self, dim, auxCount, kdlist = []):
        self.dim = dim
        self.auxCount = auxCount
        self.kdlist = kdlist

    def addTerm(self, kt):
        self.kdlist.append(kt)
    
    def variables(self):
        vlist = []
        for kt in self.kdlist:
            vlist += kt.variables()
        return vlist

    def generateString(self, showLevel = True):
        tstrings = [kt.generateString(showLevel) for kt in self.kdlist]
        return " ".join(tstrings)

    def __str__(self):
        return self.generateString()

    def __len__(self):
        return len(self.kdlist)

    def levelCanonize(self):
        # Canonical form lists kernels ordered in levels, with the levels containing
        # the most terms first.  Within level, order kernel lexicographically
        self.kdlist.sort()
        # kdlist sorted by terms
        # Only problem is that the levels should be sorted inversely by length
        # and secondarily by indices of first element

        levelList = [[] for l in range(self.auxCount)]
        for kt in self.kdlist:
            levelList[kt.level-1].append(kt)

        levelList.sort(key = lambda(ls) : "%d+%s" % (999-len(ls), ls[0].generateString(False)))

        # Map from old level to new level
        levelPermuter = {}
        nlevel = 1
        for llist in levelList:
            olevel = llist[0].level
            levelPermuter[olevel] = nlevel
            nlevel += 1
        return (self.permute(levelPermuter = levelPermuter), levelPermuter)

    # Find form that is unique among following transformations:
    #  Permutation of matrices A, B, and C
    #  Permutations of row and column indices
    #  Permutation of product terms (levels)
    def canonize(self):
        if self.dim[0] != self.dim[1] or self.dim[1] != self.dim[2]:
            return self.levelCanonize()
        indexPermuterList = allPermuters(unitRange(self.dim[0]))
        ijkPermuterList = allPermuters(range(3))
        bestSet = None
        bestIjkPermuter = None
        bestIndexPermuter = None
        bestLevelPermuter = None
        bestSignature = None
        for ijkPermuter in ijkPermuterList:
            for indexPermuter in indexPermuterList:
                kset = self.permute(ijkPermuter = ijkPermuter, indexPermuter = indexPermuter)
                nkset, levelPermuter = kset.levelCanonize()
                signature = nkset.generateString(False)
                if bestSignature is None or signature < bestSignature:
                    bestSet = nkset
                    bestIjkPermuter = ijkPermuter
                    bestIndexPermuter = indexPermuter
                    bestLevelPermuter = levelPermuter
                    bestSignature = signature
        variablePermuter = ijk2var(bestIjkPermuter)
        return (bestSet, variablePermuter, bestIndexPermuter, bestLevelPermuter)

    # See if there is a matching Kernel term and return its level
    # Return -1 if none found
    def findLevel(self, i, j, k):
        for kt in self.kdlist:
            if kt.i == i and kt.j == j and kt.k == k:
                return kt.level
        return -1

    # Find subset of kernels belonging to groups of specified size
    def groupings(self, minCount = 1, maxCount = 1):
        auxCount = max([kt.level for kt in self.kdlist])
        levelList = [[] for l in range(auxCount)]
        for kt in self.kdlist:
            levelList[kt.level-1].append(kt)
        nkdlist = []
        for llist in levelList:
            if len(llist) >= minCount and len(llist) <= maxCount:
                nkdlist += llist
        return KernelSet(self.dim, self.auxCount, nkdlist)

    # Permutation
    def permute(self, ijkPermuter = None, indexPermuter = None, levelPermuter = None):
        nkdlist = [kt.permute(ijkPermuter, indexPermuter, levelPermuter) for kt in self.kdlist]
        return KernelSet(self.dim, self.auxCount, nkdlist)

# Solve matrix multiplication
class MProblem:
    # Matrix dimensions, given as (n1, n2, n3)
    dim = (0,0,0)
    # Number of auxilliary variables
    auxCount = 0
    # Circuit generator
    ckt = None

    #### Performance parameters

    # At what level should streamline constraints be introduced?
    streamlineLevel = 2


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
    def generateBrent(self, indices, kset = None, check = False):
        if len(indices) != 6:
            raise MatrixException("Cannot generate Brent equation for %d indices" % len(indices))
        i1, i2, j1, j2, k1, k2 = indices
        kd = i2 == j1 and i1 == k1 and j2 == k2
        fixLevel = -1
        if kd and kset is not None:
            fixLevel = kset.findLevel(i1, j1, k2)
        av = circuit.Vec([BrentVariable('alpha', i1, i2, l) for l in unitRange(self.auxCount)])
        bv = circuit.Vec([BrentVariable('beta', j1, j2, l) for l in unitRange(self.auxCount)])
        gv = circuit.Vec([BrentVariable('gamma', k1, k2, l) for l in unitRange(self.auxCount)])
        pv = self.ckt.addVec(circuit.Vec([BrentTerm(indices, 'bp', l) for l in unitRange(self.auxCount)]))
        bn = circuit.Node(BrentTerm(indices))
        if fixLevel < 0:
            self.ckt.comment("Brent equation for i1 = %d, i2 = %d, j1 = %d, j2 = %d, k1 = %d, k2 = %d (kron delta = %d)" % (i1, i2, j1, j2, k1, k2, 1 if kd else 0))
            self.ckt.andV(pv, [av, bv, gv])
            rv = pv.concatenate(circuit.Vec([self.ckt.one])) if not kd else pv.dup()
            self.ckt.xorN(bn, rv)
        else:
            self.ckt.comment("Constrained Brent equation for i1 = %d, i2 = %d, j1 = %d, j2 = %d, k1 = %d, k2 = %d, level = %d" % (i1, i2, j1, j2, k1, k2, fixLevel))
            self.ckt.andV(pv, [av, bv, gv])
            lvec = [Literal(v, 0) for v in pv]
            lvec[fixLevel-1].phase = 1
            lv = circuit.Vec(lvec)
            self.ckt.andN(bn, lv)
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
    def generateBrentConstraints(self, kset = None, streamlineNode = None, check = False):
        ranges = self.fullRanges()
        indices = self.iexpand(ranges)
        self.ckt.comment("Generate all Brent equations")
        first = True
        for idx in indices:
            self.generateBrent(idx, kset, check)
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
                tlist = [BrentTerm(idx + [x]) for x in unitRange(gcount)]
                terms = self.ckt.addVec(circuit.Vec(tlist))
                args = terms
                if level == self.streamlineLevel and streamlineNode is not None:
                    tlist = [streamlineNode] + tlist
                    args = circuit.Vec(tlist)
                bn = BrentTerm(idx)
                self.ckt.andN(bn, args)
                self.ckt.decRefs([terms])
                if check:
                    self.ckt.checkConstant(bn, 1)
                if first and not check:
                    first = False
                    name = circuit.Vec([BrentTerm(idx)])
                    self.ckt.comment("Find size of typical function at level %d" % level)
                    self.ckt.information(name)
            if streamlineNode is not None and level == self.streamlineLevel:
                self.ckt.decRefs([streamlineNode])
            if not check:
                names = circuit.Vec([BrentTerm(idx) for idx in indices])
                self.ckt.comment("Find combined size for terms at level %d" % level)
                self.ckt.information(names)

# Describe encoding of matrix multiplication
class MScheme(MProblem):

    # Assignment
    assignment = None
    kernelTerms = None

    expressionSplitter = re.compile('\s*[-+]\s*')

    def __init__(self, dim, auxCount, ckt, assignment = None):
        MProblem.__init__(self, dim, auxCount, ckt)
        if assignment is None:
            self.generateZeroAssignment()
        else:
            self.assignment = assignment
        self.kernelTerms = self.findKernels()

    def generateZeroAssignment(self):
        self.assignment = Assignment()
        for level in unitRange(self.auxCount):
            for cat in ['alpha', 'beta', 'gamma']:
                for r in unitRange(self.nrow(cat)):
                    for c in unitRange(self.ncol(cat)):
                        v = BrentVariable(cat, r, c, level)
                        self.assignment[v] = 0

    def findKernels(self):
        kset = KernelSet(self.dim, self.auxCount, kdlist = [])
        for level in unitRange(self.auxCount):
            for i in unitRange(self.dim[0]):
                for j in unitRange(self.dim[1]):
                    for k in unitRange(self.dim[2]):
                        kt = KernelTerm(i, j, k, level)
                        if kt.inAssignment(self.assignment):
                            kset.addTerm(kt)
        return kset

    # How many additions would be required to do the multiplication?
    def addCount(self):
        tcount = functools.reduce(lambda x, y: x+y, [lit.phase for lit in self.assignment.literals()])
        # Additions only needed to reduce to single terms
        return tcount - (self.auxCount * 3)

    def duplicate(self):
        return MScheme(self.dim, self.auxCount, self.ckt, self.assignment.subset())

    # Check properties of scheme
    def obeysUniqueUsage(self):
        return len(self.kernelTerms) == self.dim[0] * self.dim[1] * self.dim[2]

    def obeysMaxDouble(self):
        k = self.kernelTerms.groupings(minCount = 3, maxCount = self.auxCount)
        return len(k) == 0

    def obeysSingletonExclusion(self):
        allLiterals = self.assignment.literals()
        sk = self.kernelTerms.groupings(minCount = 1, maxCount = 1)
        for kt in sk.kdlist:
            level = kt.level
            foundSingle = False
            for cat in ['alpha', 'beta', 'gamma']:
                catLits = [lit for lit in allLiterals if lit.variable.prefix == cat and lit.variable.level == level]
                count = functools.reduce(lambda x, y: x+y, [lit.phase for lit in catLits])
                foundSingle = foundSingle or count <= 1
            if not foundSingle:
                return False
        return True

    def brentCheck(self, i1, i2, j1, j2, k1, k2):
        val = 0
        for level in unitRange(self.auxCount):
            avar = BrentVariable('alpha', i1, i2, level)
            aphase = self.assignment[avar] if avar in self.assignment else 0
            bvar = BrentVariable('beta', j1, j2, level)
            bphase = self.assignment[bvar] if bvar in self.assignment else 0
            gvar = BrentVariable('gamma', k1, k2, level)
            gphase = self.assignment[gvar] if gvar in self.assignment else 0
            val = val + (aphase * bphase * gphase)
        kd = i2 == j1 and i1 == k1 and j2 == k2
        return (val % 2) == kd

    def obeysBrent(self):
        ok = True
        for i1 in unitRange(self.dim[0]):
            for i2 in unitRange(self.dim[1]):
                for j1 in unitRange(self.dim[1]):
                    for j2 in unitRange(self.dim[2]):
                        for k1 in unitRange(self.dim[0]):
                            for k2 in unitRange(self.dim[2]):
                                ok = ok and self.brentCheck(i1, i2, j1, j2, k1, k2)
        return ok

    # Return scheme with levels reordered to canonize kernels
    def canonize(self):
        (k, variablePermuter, indexPermuter, levelPermuter) = self.kernelTerms.canonize()
        return self.permute(variablePermuter, indexPermuter, levelPermuter)

    def levelCanonize(self):
        (k, levelPermuter) = self.kernelTerms.levelCanonize()
        return self.permute(levelPermuter = levelPermuter)


    # Apply permutations
    def permute(self, variablePermuter = None, indexPermuter = None, levelPermuter = None):
        nassignment = self.assignment.permute(variablePermuter, indexPermuter, levelPermuter)
        return MScheme(self.dim, self.auxCount, self.ckt, nassignment)

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
        self.kernelTerms = self.findKernels()
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
        self.kernelTerms = self.findKernels()
        return self


    # Generate streamline constraints based on singleton exclusion
    def generateStreamline(self):
        self.ckt.comment("Generate streamline conditions based on singleton exclusion property")
        snode = self.ckt.addNode(circuit.Node("streamline"))
        allLiterals = self.assignment.literals()
        sk = self.kernelTerms.groupings(minCount = 1, maxCount = 1)
        scount = len(sk)
        av = self.ckt.nameVec("single_alpha", scount)
        bv = self.ckt.nameVec("single_beta", scount)
        gv = self.ckt.nameVec("single_gamma", scount)
        tv = {'alpha': av , 'beta' : bv, 'gamma' : gv }
        pv = self.ckt.nameVec("exclude", scount)
        for j in range(scount):
            kt = sk.kdlist[j]
            level = kt.level
            ktAlpha = kt.alpha()
            ktBeta = kt.beta()
            ktGamma = kt.gamma()
            ktVars = { 'alpha' : ktAlpha, 'beta' : ktBeta, 'gamma' : ktGamma }
            for cat in ['gamma', 'alpha', 'beta']:
                nrow = self.nrow(cat)
                ncol = self.ncol(cat)
                allVars = [BrentVariable(cat, i/ncol+1, (i%ncol)+1, level) for i in range(nrow*ncol)]
                lits = [Literal(v, 1 if v == ktVars[cat] else 0) for v in allVars]
                lvec = circuit.Vec(lits)
                self.ckt.andN(tv[cat][j], lvec)
            ev = circuit.Vec([av[j], bv[j], gv[j]])
            self.ckt.orN(pv[j], ev)
        self.ckt.andN(snode, pv)
        self.ckt.decRefs([av, bv, gv, pv])
        return snode

    def generateMixedConstraints(self, categoryProbabilities = {'alpha':1.0, 'beta':1.0, 'gamma':1.0}, fixKV = False):
        fixedAssignment = Assignment()
        vlist = []
        if fixKV:
            vlist = self.kernelTerms.variables()
            ka = self.assignment.subset(lambda(v): v in vlist)
            fixedAssignment.overWrite(ka)
        for cat in categoryProbabilities.keys():
            prob = categoryProbabilities[cat]
            ca = self.assignment.subset(lambda(v): v.prefix == cat and v not in vlist).randomSample(prob)
            fixedAssignment.overWrite(ca)
        if len(fixedAssignment) > 0:
            self.ckt.comment("Fixed assignments")
            fixedAssignment.assign(self.ckt)
        fixedVariables = [v for v in fixedAssignment.variables()]
        self.declareVariables(fixedVariables)

    def generateProgram(self, categoryProbabilities = {'alpha':1.0, 'beta':1.0, 'gamma':1.0}, fixKV = False, excludeSingleton = False):
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
        self.generateMixedConstraints(categoryProbabilities, fixKV)
        streamlineNode = None
        if excludeSingleton:
            streamlineNode = self.generateStreamline()
        kset = self.kernelTerms if fixKV else None
        self.generateBrentConstraints(kset, streamlineNode = streamlineNode, check=isFixed)
        bv = circuit.Vec([BrentTerm()])
        if not isFixed:
            self.ckt.count(bv)
            self.ckt.status()
            self.ckt.satisfy(bv)
        self.ckt.write("time")

