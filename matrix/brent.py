# Encoding matrix multiplication problems
import functools
import re
import random
import sys
import hashlib
from functools import total_ordering

import circuit

# How many hex digits should be in a hash signature?
schemeHashLength = 20
kernelHashLength = 10

class MatrixException(Exception):

    def __init__(self, value):
        self.value = value

    def __str__(self):
        return "Matrix Multiply Exception: " + str(self.value)
        

# Sequence of digits starting at one
def unitRange(n):
    return list(range(1, n+1))

# Trim string
def trim(s):
    while len(s) > 0 and s[-1] in '\n\r':
        s = s[:-1]
    return s

# Given list of form [n1, n2, ..., nk],
# generate list of all indices of the form [i1, i2, .., ik]
# such that ij <= nj for all j
def indexExpand(rlist, sofar = [[]]):
    if len(rlist) == 0:
        return sofar
    n = rlist[-1]
    nsofar = []
    for idx in unitRange(n):
        for l in sofar:
            nsofar.append([idx] + l)
    return indexExpand(rlist[:-1], nsofar)

# Helper function for permutation generation
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

# Given: List of permutation keys, and list of permutation lists
# Generate all permutation sets
def allPermuterSets(keyList, permList):
    if len(keyList) == 0:
        return [{}]
    k = keyList[0]
    rkList = keyList[1:]
    pList = permList[0]
    rpList = permList[1:]
    rset = allPermuterSets(rkList, rpList)
    result = [ ]
    for p in pList:
        for d in rset:
            dnew = { key : val for key,val in d.items() }
            dnew[k] = p
            result.append(dnew)
    return result

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
@total_ordering
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
               

    # Apply dictionary of permutations
    # Possible dictionary keys: 'i', 'j', 'k', 'variable', 'level'
    def permute(self, permutationSet):
        prefix = self.prefix
        row = self.row
        col = self.column
        level = self.level
        if 'variable' in permutationSet:
            perm = permutationSet['variable']
            if prefix == 'gamma':
                left, right = col, row
            else:
                left, right = row, col
            if self.shouldFlip(perm):
                left, right = right, left
            prefix = perm[prefix]
            if prefix == 'gamma':
                row, col = right, left
            else:
                row, col = left, right
        if 'i' in permutationSet:
            perm = permutationSet['i']
            if prefix in ['alpha', 'gamma']:
                row = perm[row]
        if 'j' in permutationSet:
            perm = permutationSet['j']
            if prefix == 'alpha':
                col = perm[col]
            elif prefix == 'beta':
                row = perm[row]
        if 'k' in permutationSet:
            perm = permutationSet['k']
            if prefix in ['beta', 'gamma']:
                col = perm[col]
        if 'level' in permutationSet:
            perm = permutationSet['level']
            level = perm[level]
        return BrentVariable(prefix, row, col, level)

    def __str__(self):
        return self.generateName()

    def __hash__(self):
        return str(self).__hash__()

    def __eq__(self, other):
        return str(self) == str(other)

    def __lt__(self, other):
        if self.level < other.level:
            return True
        if self.level > other.level:
            return False
        if self.prefix in self.prefixOrder and other.prefix in other.prefixOrder:
            if self.prefixOrder[self.prefix] < other.prefixOrder[other.prefix]:
                return True
            if self.prefixOrder[self.prefix] > other.prefixOrder[other.prefix]:
                return False
        else:
            if self.prefix < other.prefix:
                return True
            if self.prefix > other.prefix:
                return False
        if self.row < other.row:
            return True
        if self.row > other.row:
            return False
        return self.column < other.column


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
    def randomSample(self, prob = 0.5, seed = None):
        if seed is not None:
            random.seed(seed)
        tsize = int(prob * len(self.asst))
        sample = random.sample(self.literals(), tsize)
        return Assignment(sample)

    def subset(self, variableFilter = None):
        if variableFilter is None:
            nliterals = self.literals()
        else:
            nliterals = [lit for lit in self.literals() if variableFilter(lit.variable)]
        return Assignment(nliterals)

    # Apply dictionary of permutations
    # Possible dictionary keys: 'i', 'j', 'k', 'variable', 'level'
    def permute(self, permutationSet):
        nliterals = [Literal(lit.variable.permute(permutationSet), lit.phase) for lit in self.literals()]
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

# Representation of triple of brent variables a_i1,i2 * b_j1,j2 --> c_k1,k2
class GeneralTerm:
    i1 = 1
    i2 = 1
    j1 = 1
    j2 = 1
    k1 = 1
    k2 = 1
    level = 1

    def __init__(self, i1, i2, j1, j2, k1, k2, level):
        self.i1 = i1
        self.i2 = i2
        self.j1 = j1
        self.j2 = j2
        self.k1 = k1
        self.k2 = k2
        self.level = level

    def alpha(self):
        return BrentVariable('alpha', self.i1, self.i2, self.level)

    def beta(self):
        return BrentVariable('beta', self.j1, self.j2, self.level)

    def gamma(self):
        return BrentVariable('gamma', self.k1, self.k2, self.level)

    def variables(self):
        return [self.alpha(), self.beta(), self.gamma()]

    def indices(self):
        return (self.i1, self.i2, self.j1, self.j2, self.k1, self.k2)

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
    
    def generateString(self, showLevel = True):
        astring = self.alpha().generateTerm()
        bstring = self.beta().generateTerm()
        cstring = self.gamma().generateTerm(True)
        lstring = "[%.2d]" % self.level if showLevel else ""
        return "%s*%s*%s%s" % (astring, bstring, cstring, lstring)

    # Name for use in symbolic constraint generation
    def symbol(self):
        return "term-%d%d%d%d%d%d-%.2d" % (self.i1, self.i2, self.j1, self.j2, self.k1, self.k2, self.level)

    def degree(self):
        cnt = 0
        if self.i1 == self.k1:
            cnt += 1
        if self.i2 == self.j1:
            cnt += 1
        if self.j2 == self.k2:
            cnt += 1
        return cnt

    def isKernel(self):
        return self.degree() == 3

    def __str__(self):
        return self.generateString()

# Representation of a triple a_i,j * b_j,k --> c_i,k
# Special case of general term where:
# i1 = k1 = i
# i2 = j1 = j
# j2 = k2 = k
@total_ordering
class KernelTerm(GeneralTerm):

    i = 1
    j = 1
    k = 1
    level = 1

    def __init__(self, i, j, k, level):
        GeneralTerm.__init__(self, i, j, j, k, i, k, level)
        self.i = i
        self.j = j
        self.k = k

    def clone(self):
        return KernelTerm(self.i, self.j, self.k, self.level)

    # Apply dictionary of permutations
    # Possible dictionary keys: 'i', 'j', 'k', 'ijk', 'level'
    def permute(self, permutationSet):
        i = self.i
        j = self.j
        k = self.k
        level = self.level
        if 'ijk' in permutationSet:
            perm = permutationSet['ijk']
            vals = [i, j, k]
            i, j, k = [vals[perm[idx]] for idx in range(3)]
        if 'i' in permutationSet:
            i = permutationSet['i'][i]
        if 'j' in permutationSet:
            j = permutationSet['j'][j]
        if 'k' in permutationSet:
            k = permutationSet['k'][k]
        if 'level' in permutationSet:
            level = permutationSet['level'][level]
        return KernelTerm(i, j, k, level)

    def __lt__(self, other):
        if self.level < other.level:
            return True
        if self.level > other.level:
            return False
        if self.i < other.i:
            return True
        if self.i > other.i:
            return False
        if self.j < other.j:
            return True
        if self.j > other.j:
            return False
        return self.k < other.k

    def __eq__(self, other):
        return self.level == other.level and self.i == other.i and self.j == other.j and self.k == other.k

    # Name for use in symbolic constraint generation
    def symbol(self):
        return "kernel-i%dj%dk%d.l-%.2d" % (self.i, self.j, self.k, self.level)

    def shortString(self):
        return "K%d%d%d" % (self.i, self.j, self.k)

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

    def shortString(self):
        levelList = self.levelize()
        tstrings = []
        for ls in levelList:
            slist = [k.shortString() for k in ls]
            sform = '[' + ' '.join(slist) + ']' if len(ls) > 1 else slist[0]
            tstrings.append(sform)
        return " ".join(tstrings)

    def printPolynomial(self, outf = sys.stdout):
        levelList = self.levelize()
        for ls in levelList:
            tstrings = [kt.generateString(showLevel = False) for kt in ls]
            outf.write(" + ".join(tstrings) + '\n')

    def parsePolynomialLine(self, line, level):
        kdlist = []
        line = trim(line)
        fields = line.split(" + ")
        for poly in fields:
            terms = poly.split("*")
            i = int(terms[0][1])
            j = int(terms[0][2])
            k = int(terms[1][2])
            kdlist.append(KernelTerm(i, j, k, level))
        return kdlist

    def parseFromFile(self, fname):
        kdlist = []
        try:
            f = open(fname, 'r')
        except:
            raise MatrixException("Couldn't open file '%s'" % fname)
        level = 1
        for line in f:
            line = trim(line)
            if len(line) > 0 and line[0] != '#':
                kdlist += self.parsePolynomialLine(line, level)
                level += 1
        f.close()
        if level != self.auxCount + 1:
            raise MatrixException("Expected %d equations in polynomial file, got %d" %  (self.auxCount, level))
        self.kdlist = kdlist
        return self

    # Convert into lists, ordered by level
    def levelize(self):
        levelList = [[] for l in range(self.auxCount)]
        for kt in self.kdlist:
            levelList[kt.level-1].append(kt)
        return levelList

    # Create string that characterizes set in compressed form
    def signature(self):
        levelList = self.levelize()
        terms = []
        for llist in levelList:
            if len(llist) > 1:
                terms += llist
        terms.sort()
        return " ".join([str(t) for t in terms])

    def sign(self):
        sig = self.signature()
        return "K" + hashlib.sha1(sig.encode('ASCII')).hexdigest()[:kernelHashLength]

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
        levelList = self.levelize()
        levelList.sort(key = lambda ls : "%d+%s" % (999-len(ls), ls[0].generateString(False)))

        # Map from old level to new level
        levelPermuter = {}
        nlevel = 1
        for llist in levelList:
            olevel = llist[0].level
            levelPermuter[olevel] = nlevel
            nlevel += 1
        pkset = self.permute({'level': levelPermuter})
        return (pkset, levelPermuter)

    # Find form that is unique among following transformations:
    #  Permutation of matrices A, B, and C
    #  Permutations of i, j, and k indices
    #  Permutation of product terms (levels)
    #  Return resulting form + list of dictionaries, each encoding set of permutations
    def listCanonize(self):
        square = self.dim[0] == self.dim[1] and self.dim[1] and self.dim[2]
        keyList = []
        permList = []
        if square:
            keyList.append('ijk')
            permList.append(allPermuters(list(range(3))))
        else:
            # This is overly conservative.  Could get permutations when i == j or j == k or i == k
            pass
        keyList.append('i')
        permList.append(allPermuters(unitRange(self.dim[0])))
        keyList.append('j')
        permList.append(allPermuters(unitRange(self.dim[1])))
        keyList.append('k')
        permList.append(allPermuters(unitRange(self.dim[2])))
        bestSet = None
        bestSignature = None
        bestPermuterList = []
        for pset in allPermuterSets(keyList, permList):
            kset = self.permute(pset)
            nkset, levelPermuter = kset.levelCanonize()
            signature = nkset.signature()
            if bestSignature is None or signature <= bestSignature:
                npset = { idx : pset[idx] for idx in ['i', 'j', 'k']}
                npset['level'] = levelPermuter
                if square:
                    npset['variable'] = ijk2var(pset['ijk'])
                if bestSignature is None or signature < bestSignature:
                    bestSet = nkset
                    bestSignature = signature
                    # Start with fresh list
                    bestPermuterList = [npset]
                else:
                    bestPermuterList.append(npset)
        return (bestSet, bestPermuterList)

    def isSymmetric(self):
        pset = { 'ijk' : {'i' : 'k', 'j' : 'j', 'k' : 'i'}}
        pkset, lp = self.levelCanonize()        
        mpkset, plp = pkset.permute(pset).levelCanonize()
        return pkset.signature() == mpkset.signature()


    #  Find all permutations that yield symmetric kernel
    #  Return list of kernels, plus list of dictionary lists
    #  Each dictionary list records all permutations leading to corresponding kernel
    #  In event that no permutation is symmetric, return two empty lists
    def listCanonize(self):
        # Computed results
        kernelList = []
        permDictList = []
        # Map from kernel signature to position in list
        signatureDict = {}
        square = self.dim[0] == self.dim[1] and self.dim[1] and self.dim[2]
        if not square:
            # Note: This is overly conservative.  Could get symmetry if i == k
            return (kernelList, permDictList)
        # List of permutations to attempt
        permList = []
        keyList = []
        keyList.append('ijk')
        permList.append(allPermuters(list(range(3))))
        keyList.append('i')
        permList.append(allPermuters(unitRange(self.dim[0])))
        keyList.append('j')
        permList.append(allPermuters(unitRange(self.dim[1])))
        keyList.append('k')
        permList.append(allPermuters(unitRange(self.dim[2])))

        for pset in allPermuterSets(keyList, permList):
            kset = self.permute(pset)
            if not kset.isSymmetric():
                continue
            nkset, levelPermuter = kset.levelCanonize()
            npset = { idx : pset[idx] for idx in ['i', 'j', 'k', 'ijk']}
            npset['level'] = levelPermuter
            signature = nkset.signature()
            if signature in signatureDict:
                permDictList[signatureDict[signature]].append(npset)
            else:
                signatureDict[signature] = len(kernelList)
                kernelList.append(nkset)
                permDictList.append([npset])
        return (kernelList, permDictList)

    # See if there is a matching Kernel term and return its level
    # Return -1 if none found
    def findLevels(self, i, j, k):
        levels = []
        for kt in self.kdlist:
            if kt.i == i and kt.j == j and kt.k == k:
                levels.append(kt.level)
        return levels

    # Find subset of kernels belonging to groups of specified size
    def groupings(self, minCount = 1, maxCount = 1):
        auxCount = max([kt.level for kt in self.kdlist])
        levelList = self.levelize()
        nkdlist = []
        for llist in levelList:
            if len(llist) >= minCount and len(llist) <= maxCount:
                nkdlist += llist
        return KernelSet(self.dim, self.auxCount, nkdlist)

    # Apply dictionary of permutations
    # Possible dictionary keys: 'i', 'j', 'k', 'ijk', 'level'
    def permute(self, permutationSet):
        nkdlist = [kt.permute(permutationSet) for kt in self.kdlist]
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
    # At what levels should we conjunct, rather than and
    conjunctLevels = list(range(4,7))

    def __init__(self, dim, auxCount, ckt = None):
        if type(dim) == type(2):
            self.dim = (dim, dim, dim)
        else:
            self.dim = dim
        self.auxCount = auxCount
        self.ckt = circuit.Circuit() if ckt is None else ckt 
            
    def nrow(self, category):
        return self.dim[1] if category == 'beta' else self.dim[0]

    def ncol(self, category):
        return self.dim[1] if category == 'alpha' else self.dim[2]

    def fullRanges(self):
        return (self.nrow('alpha'), self.ncol('alpha'), self.nrow('beta'), self.ncol('beta'), self.nrow('gamma'), self.ncol('gamma'))

    # Generate Brent equation
    def generateBrent(self, indices, kset = None, check = False, useZdd = False, fixKV = False, boundNonKernels = False):
        if len(indices) != 6:
            raise MatrixException("Cannot generate Brent equation for %d indices" % len(indices))
        i1, i2, j1, j2, k1, k2 = indices
        kd = i2 == j1 and i1 == k1 and j2 == k2
        fixLevels = []
        if fixKV and kd and kset is not None:
            fixLevels = kset.findLevels(i1, j1, k2)
        av = circuit.Vec([BrentVariable('alpha', i1, i2, l) for l in unitRange(self.auxCount)])
        bv = circuit.Vec([BrentVariable('beta', j1, j2, l) for l in unitRange(self.auxCount)])
        gv = circuit.Vec([BrentVariable('gamma', k1, k2, l) for l in unitRange(self.auxCount)])
        pv = self.ckt.addVec(circuit.Vec([BrentTerm(indices, 'bp', l) for l in unitRange(self.auxCount)]))
        bn = circuit.Node(BrentTerm(indices))
        if len(fixLevels) == 0:
            if boundNonKernels and not kd:
                self.ckt.comment("Limited Brent equation for i1 = %d, i2 = %d, j1 = %d, j2 = %d, k1 = %d, k2 = %d (limit 0 or 2)" % (i1, i2, j1, j2, k1, k2))
            else:
                self.ckt.comment("Brent equation for i1 = %d, i2 = %d, j1 = %d, j2 = %d, k1 = %d, k2 = %d (kron delta = %d)" % (i1, i2, j1, j2, k1, k2, 1 if kd else 0))
            self.ckt.andV(pv, [av, bv, gv])
            if boundNonKernels and not kd:
                npv = circuit.Vec(['!' + p for p in pv.nodes])
                self.ckt.okList(bn, pv, npv, [True, False, True])
            else:
                rv = pv.concatenate(circuit.Vec([self.ckt.one])) if not kd else pv.dup()
                self.ckt.xorN(bn, rv)
        else:
            levelString = "level %d" % fixLevels[0]
            if len(fixLevels) > 1:
                levelString = 'levels [' + ', '.join(str(l) for l in fixLevels) + ']'
            self.ckt.comment("Constrained Brent equation for i1 = %d, i2 = %d, j1 = %d, j2 = %d, k1 = %d, k2 = %d, kernel at %s" % (i1, i2, j1, j2, k1, k2, levelString))
            self.ckt.andV(pv, [av, bv, gv])
            lvec = [Literal(v, 0) for v in pv]
            for l in fixLevels:
                lvec[l-1].phase = 1
            lv = circuit.Vec(lvec)
            self.ckt.andN(bn, lv)
        self.ckt.decRefs([pv])
        if useZdd:
            self.ckt.comment("Convert to ZDD")
            self.ckt.cmdLine("zconvert", circuit.Vec([bn, bn]))
        if check:
            self.ckt.checkConstant(bn, 1)

    # Helper routines to build up formula encoding all Brent constraints

    # Declare (subset of) variables
    def declareVariables(self, fixedList = []):
        for level in unitRange(self.auxCount):
            generatedComment = False
            for cat in ['gamma', 'alpha', 'beta']:
                nrow = self.nrow(cat)
                ncol = self.ncol(cat)
                allVars = [BrentVariable(cat, i//ncol+1, (i%ncol)+1, level) for i in range(nrow*ncol)]
                vars = [v for v in allVars if v not in fixedList]
                if len(vars) > 0:
                    if not generatedComment:
                        # Declare variables for auxilliary variable level
                        self.ckt.comment("Variables for auxilliary term %d" % level)
                        generatedComment = True
                    vec = circuit.Vec(vars)
                    self.ckt.declare(vec)


    def dfGenerator(self, streamlineNode = None, check = False, prefix = []):
        level = 6 - len(prefix)
        if level == 0:
            # We're done.  Reached the Brent term
            return
        ranges = self.fullRanges()
        gcount = ranges[len(prefix)]
        tlist = []
        for x in unitRange(gcount):
            nprefix = prefix + [x]
            # Recursively generate next level term
            self.dfGenerator(streamlineNode, check, prefix = nprefix)
            tlist.append(BrentTerm(nprefix))
        terms = self.ckt.addVec(circuit.Vec(tlist))
        args = terms
        if level == self.streamlineLevel and streamlineNode is not None:
            tlist = [streamlineNode] + tlist
            args = circuit.Vec(tlist)
        bn = BrentTerm(prefix)
        self.ckt.andN(bn, args)
        self.ckt.decRefs([terms])
        if check:
            self.ckt.checkConstant(bn, 1)
        if level == 6:
            # Top level cleanup
            if streamlineNode is not None:
                self.ckt.decRefs([streamlineNode])
            if not check:
                names = circuit.Vec([bn])
                self.ckt.comment("Find combined size for terms at level %d" % level)
                self.ckt.information(names)

    def oldBfGenerator(self, streamlineNode = None, check = False):
        ranges = self.fullRanges()
        for level in unitRange(6):
            self.ckt.comment("Combining terms at level %d" % level)
            gcount = ranges[-1]
            ranges = ranges[:-1]
            indices = indexExpand(ranges)
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

    def bfGenerator(self, streamlineNode = None, check = False, levelList = None):
        if levelList is None:
            levelList = unitRange(6)
        ranges = self.fullRanges()
        lastLevel = 0
        for level in levelList:
            self.ckt.comment("Combining terms at level %d" % level)
            gcounts = ranges[6-level:6-lastLevel]
            ranges = ranges[:6-level]
            indices = indexExpand(ranges)
            for idx in indices:
                tlist = [BrentTerm(idx + ls) for ls in indexExpand(gcounts)]
                terms = self.ckt.addVec(circuit.Vec(tlist))
                args = terms
                if level >= self.streamlineLevel and lastLevel < self.streamlineLevel and streamlineNode is not None:
                    tlist = [streamlineNode] + tlist
                    args = circuit.Vec(tlist)
                bn = BrentTerm(idx)
                if level in self.conjunctLevels:
                    self.ckt.conjunctN(bn, args)
                else:
                    self.ckt.andN(bn, args)
                self.ckt.decRefs([terms])
                if check:
                    self.ckt.checkConstant(bn, 1)
            if streamlineNode is not None and level == self.streamlineLevel:
                self.ckt.decRefs([streamlineNode])
            if not check:
                names = circuit.Vec([BrentTerm(idx) for idx in indices])
                self.ckt.comment("Find combined size for terms at level %d" % level)
                self.ckt.information(names)
            lastLevel = level

    # Generate Brent equations
    def generateBrentConstraints(self, kset = None, streamlineNode = None, check = False, breadthFirst = False, levelList = None, useZdd = False, fixKV = False, boundNonKernels = False):
        ranges = self.fullRanges()
        indices = indexExpand(ranges)
        self.ckt.comment("Generate all Brent equations")
        first = True
        for idx in indices:
            self.generateBrent(idx, kset = kset, check = check, useZdd = useZdd, fixKV = fixKV, boundNonKernels = boundNonKernels)
            if first and not check:
                first = False
                name = circuit.Vec([BrentTerm(idx)])
                self.ckt.comment("Find size of typical Brent term")
                self.ckt.information(name)
        if not check:
            names = circuit.Vec([BrentTerm(idx) for idx in indices])
            self.ckt.comment("Find combined size of all Brent terms")
            self.ckt.information(names)

        if breadthFirst:
            self.bfGenerator(streamlineNode, check, levelList)
        else:
            self.dfGenerator(streamlineNode, check)

    # Define kernel terms symbolically.
    # Return vector for later dereferencing
    def generateKernels(self):
        self.ckt.comment("Define kernel terms")
        klist = []
        ijkList = indexExpand(self.dim)
        for l in unitRange(self.auxCount):
            for (i,j,k) in ijkList:
                klist.append(KernelTerm(i, j, k, l))
        for k in klist:
            kname = k.symbol()
            aname = str(k.alpha())
            bname = str(k.beta())
            cname = str(k.gamma())
            self.ckt.andN(kname, [aname, bname, cname])
        kvec = self.ckt.vec([k.symbol() for k in klist])
        return kvec

    def generateUniqueUsage(self, dest):
        self.ckt.comment("Ensure that each kernel term appears in only one product")
        ijkList = indexExpand(self.dim)
        unodes = { (i,j,k) : "unique-i%d.j%d.k%d" % (i,j,k) for (i,j,k) in ijkList }
        uvec = self.ckt.vec([unodes[(i,j,k)] for (i,j,k) in ijkList])
        for (i,j,k) in ijkList:
            klist = [KernelTerm(i,j,k,l).symbol() for l in unitRange(self.auxCount)]
            nklist = ["!" + kt for kt in klist]
            self.ckt.exactly1(unodes[(i,j,k)], circuit.Vec(klist), circuit.Vec(nklist))
        self.ckt.andN(dest, uvec)
        self.ckt.decRefs([uvec])


    # Define terms that are NOT kernels symbolically.
    # Return list of terms
    def generateNonKernels(self):
        self.ckt.comment("Generate nonkernel terms")
        nklist = []
        indexList = indexExpand(self.fullRanges())
        for l in unitRange(self.auxCount):
            for i1, i2, j1, j2, k1, k2 in indexList:
                nkt = GeneralTerm(i1, i2, j1, j2, k1, k2, l)
                if not nkt.isKernel():
                    nklist.append(nkt)
        for nk in nklist:
            name = nk.symbol()
            aname = str(nk.alpha())
            bname = str(nk.beta())
            cname = str(nk.gamma())
            self.ckt.andN(name, [aname, bname, cname])
        return nklist

    # Limit usage of nonkernel terms to 0 or 2
    def generateBoundNonKernels(self, dest):
        nklist = self.generateNonKernels()
        slist = [nk.symbol() for nk in nklist]
        nkvec = self.ckt.vec(slist)
        self.ckt.comment("Ensure that each nonkernel term appears in either 0 or 2 products")
        indexList = [nk.indices() for nk in nklist if nk.level == 1]
        cnodes = { idx : "limit0or2-%d%d%d%d%d%d" % idx for idx in indexList }
        cvec = self.ckt.vec(cnodes.values())
        for idx in indexList:
            tlist = [nk for nk in nklist if nk.indices() == idx]
            tlist.sort(key = lambda nk : nk.level)
            slist = [nk.symbol() for nk in tlist]
            nslist = ['!' + s for s in slist]
            self.ckt.okList(cnodes[idx], circuit.Vec(slist), circuit.Vec(nslist), [True, False, True])
        self.ckt.decRefs([nkvec])
        # Aggregate into smaller groups
        range = self.fullRanges()
        subrange = range[:3]
        indices = [tuple(idx) for idx in indexExpand(subrange)]
        agnodes = { idx : "limit0or2-%d%d%d***" % idx for idx in indices }
        agvec = self.ckt.vec(agnodes.values())
        for idx in indices:
            subnodes = [cnodes[sidx] for sidx in indexList if sidx[:3] == idx]
            self.ckt.andN(agnodes[idx], circuit.Vec(subnodes))
        self.ckt.decRefs([cvec])
        # Conjunct across these nodes
        self.ckt.conjunctN(dest, agvec)
        self.ckt.decRefs([agvec])

    def generateMaxDouble(self, dest):
        dcount = self.dim[0] * self.dim[1] * self.dim[2] - self.auxCount
        self.ckt.comment("Ensure that first %d products have two kernel terms, and the remaining have one" % dcount)
        ijkList = indexExpand(self.dim)
        drange = unitRange(dcount)
        srange = [l+dcount+1 for l in range(self.auxCount - dcount)]
        dnodes = { l : "double-%.2d" % (l) for l in drange}
        dvec = [dnodes[l] for l in drange]
        snodes = { l : "single-%.2d" % l for l in srange }
        svec = [snodes[l] for l in srange ]
        uvec = self.ckt.vec(dvec + svec)
        for l in drange:
            klist = [KernelTerm(i, j, k, l).symbol() for (i,j,k) in ijkList]
            kvec = circuit.Vec(klist)
            nklist = ["!" + kt for kt in klist]
            nkvec = circuit.Vec(nklist)
            self.ckt.exactlyK(dnodes[l], kvec, nkvec, 2)
        for l in srange:
            klist = [KernelTerm(i, j, k, l).symbol() for (i,j,k) in ijkList]
            kvec = circuit.Vec(klist)
            nklist = ["!" + k for k in klist]
            nkvec = circuit.Vec(nklist)
            self.ckt.exactly1(snodes[l], kvec, nkvec)
        self.ckt.andN(dest, uvec)
        self.ckt.decRefs([uvec])

    def generateSingletonExclusion(self, dest):
        self.ckt.comment("Enforce singleton exclusion property")
        dcount = self.dim[0] * self.dim[1] * self.dim[2] - self.auxCount
        srange = [l+dcount+1 for l in range(self.auxCount - dcount)]
        ijkList = indexExpand(self.dim)
        xNodes = { l : "exclude-%.2d" % (l) for l in srange}
        xvec = self.ckt.vec([xNodes[l] for l in srange])
        for l in srange:
            xlNodes = { (i,j,k) : "exclude-%.2d.i-%d.j-%d.k-%d" % (l, i, j, k) for (i,j,k) in ijkList}
            xlaNodes = { (i,j,k) : "exclude-alpha-%.2d.i-%d.j-%d.k-%d" % (l, i, j, k) for (i,j,k) in ijkList}
            xlbNodes = { (i,j,k) : "exclude-beta-%.2d.i-%d.j-%d.k-%d" % (l, i, j, k) for (i,j,k) in ijkList}
            xlcNodes = { (i,j,k) : "exclude-gamma-%.2d.i-%d.j-%d.k-%d" % (l, i, j, k) for (i,j,k) in ijkList}
            xlVec = self.ckt.vec([xlNodes[(i,j,k)] for (i,j,k) in ijkList])
            xlaVec = self.ckt.vec([xlaNodes[(i,j,k)] for (i,j,k) in ijkList])
            xlbVec = self.ckt.vec([xlbNodes[(i,j,k)] for (i,j,k) in ijkList])
            xlcVec = self.ckt.vec([xlcNodes[(i,j,k)] for (i,j,k) in ijkList])
            for (i,j,k) in ijkList:
                kernel = KernelTerm(i, j, k, l)
                kname = kernel.symbol()
                xijlist = [(i1,i2) for (i1,i2) in indexExpand([self.dim[0], self.dim[1]])  if (i1 != i or i2 != j)]
                xjklist = [(j1,j2) for (j1,j2) in indexExpand([self.dim[1], self.dim[2]])  if (j1 != j or j2 != k)]
                xiklist = [(k1,k2) for (k1,k2) in indexExpand([self.dim[0], self.dim[2]])  if (k1 != i or k2 != k)]
                alist = [str(BrentVariable('alpha', i1, i2, l)) for (i1,i2) in xijlist]
                anode = circuit.Node(xlaNodes[(i,j,k)])
                self.ckt.orN(anode, alist)
                blist = [str(BrentVariable('beta', j1, j2, l)) for (j1,j2) in xjklist]                
                bnode = circuit.Node(xlbNodes[(i,j,k)])
                self.ckt.orN(bnode, blist)
                clist = [str(BrentVariable('gamma', k1, k2, l)) for (k1,k2) in xiklist]
                cnode = circuit.Node(xlcNodes[(i,j,k)])
                self.ckt.orN(cnode, clist)
                nargs = [kname, str(anode), str(bnode), str(cnode)]
                args = ['!' + arg for arg in nargs]
                self.ckt.orN(xlNodes[(i,j,k)], args)
                self.ckt.decRefs([anode, bnode, cnode])
            self.ckt.andN(xNodes[l], xlVec)
            self.ckt.decRefs([xlVec, xlaVec, xlbVec, xlcVec])
        self.ckt.andN(dest, xvec)
        self.ckt.decRefs([xvec])
                
    def symbolicStreamline(self, boundNonKernels = False):
        streamline = self.ckt.node("streamline")
        uniqueUsage = self.ckt.node("unique-usage")
        maxDouble = self.ckt.node("max-double")
        singleton = self.ckt.node("singleton-exclusion")
        self.ckt.comment("Generate symbolic streamline formula constraining form of solution")
        kvec = self.generateKernels()
        self.generateUniqueUsage(uniqueUsage)
        self.generateMaxDouble(maxDouble)
        self.generateSingletonExclusion(singleton)
        snodes = [uniqueUsage, maxDouble, singleton]
        if boundNonKernels:
            nonKernels = self.ckt.node("bound-nonkernels")
            self.ckt.comment("Complete symbolic streamline formula")
            self.generateBoundNonKernels(nonKernels)
            snodes.append(nonKernels)
        self.ckt.andN(streamline, snodes)
        self.ckt.decRefs(snodes)
        self.ckt.decRefs([kvec])
        return streamline

# Describe encoding of matrix multiplication
class MScheme(MProblem):

    # Assignment
    assignment = None
    kernelTerms = None
    hasBeenCanonized = False

    expressionSplitter = re.compile('\s*[-+]\s*')

    def __init__(self, dim, auxCount, ckt, assignment = None):
        MProblem.__init__(self, dim, auxCount, ckt)
        if assignment is None:
            self.generateZeroAssignment()
        else:
            self.assignment = assignment
        self.kernelTerms = self.findKernels()
        self.hasBeenCanonized = False

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

    def loadKernels(self, k):
        self.kernelTerms = k
        for kt in k.kdlist:
            vars = kt.variables()
            for v in vars:
                self.assignment[v] = 1
            

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

    # Put into canonical form by canonizing kernel and trying all compatible permutations
    def canonize(self):
        if self.hasBeenCanonized:
            return self
        (k, permuterList) = self.kernelTerms.listCanonize()
        sbest = None
        ssigbest = None
        for pset in permuterList:
            sp = self.permute(pset)
            ssig = sp.signature()
            if sbest is None or ssig < ssigbest:
                sbest = sp
                ssigbest = ssig
        sbest.hasBeenCanonized = True
        return sbest

    def levelCanonize(self):
        (k, levelPermuter) = self.kernelTerms.levelCanonize()
        return self.permute({'level' : levelPermuter})


    # Apply permutations
    def permute(self, permutationSet):
        nassignment = self.assignment.permute(permutationSet)
        return MScheme(self.dim, self.auxCount, self.ckt, nassignment)

    def isCanonical(self):
        if self.hasBeenCanonized:
            return True
        sc = self.canonize()
        return sc.signature() == self.signature()

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
            
    def generatePolynomial(self):
        lines = [self.showPolynomial(level) for level in unitRange(self.auxCount)]
        return lines
        

    def printPolynomial(self, outfile = sys.stdout, metadata = []):
        outfile.write("# Compute A (%d x %d) X B (%d x %d) = C (%d x %d)\n" % self.fullRanges())
        outfile.write("# Requires %d multiplications and %d additions\n" % (self.auxCount, self.addCount()))
        outfile.write("# Kernel signature %s\n" % self.kernelTerms.sign())
        outfile.write("# Own signature %s\n" % self.sign())
        if self.hasBeenCanonized:
            outfile.write("# This representation has been put into canonical form\n")

        for line in metadata:
            outfile.write("# %s\n" % line)

        for line in self.generatePolynomial():
            outfile.write(line + '\n')

    def signature(self):
        lines = self.generatePolynomial()
        return "|".join(lines)

    def sign(self):
        sig = self.signature()
        return "M" + hashlib.sha1(sig.encode('ASCII')).hexdigest()[:schemeHashLength]
        

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
        
    # Put into form suitable for network transmission
    def bundle(self):
        plist = self.generatePolynomial()
        return (self.dim, self.auxCount, plist)

    # Extract from bundled representation
    def unbundle(self, rep):
        dim, auxCount, plist = rep
        self.__init__(dim, auxCount, self.ckt)
        for level in unitRange(auxCount):
            self.parsePolynomialLine(plist[level-1], level)
        self.kernelTerms = self.findKernels()
        return self

    # Read polynomial from file
    def parseFromFile(self, fname):
        try:
            f = open(fname, 'r')
        except:
            raise MatrixException("Couldn't open file '%s'" % fname)
        level = 1
        for line in f:
            line = trim(line)
            if len(line) > 0 and line[0] != '#':
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
        snode = self.ckt.node("streamline")
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
                allVars = [BrentVariable(cat, i//ncol+1, (i%ncol)+1, level) for i in range(nrow*ncol)]
                lits = [Literal(v, 1 if v == ktVars[cat] else 0) for v in allVars]
                lvec = circuit.Vec(lits)
                self.ckt.andN(tv[cat][j], lvec)
            ev = circuit.Vec([av[j], bv[j], gv[j]])
            self.ckt.orN(pv[j], ev)
        self.ckt.andN(snode, pv)
        self.ckt.decRefs([av, bv, gv, pv])
        return snode

    def generateMixedConstraints(self, categoryProbabilities = {'alpha':1.0, 'beta':1.0, 'gamma':1.0}, seed = None, fixKV = False, varKV = False):
        fixedAssignment = Assignment()
        vlist = []
        if fixKV:
            vlist = self.kernelTerms.variables()
            ka = self.assignment.subset(lambda v: v in vlist)
            fixedAssignment.overWrite(ka)
        elif varKV:
            vlist = self.kernelTerms.variables()
        for cat in categoryProbabilities.keys():
            prob = categoryProbabilities[cat]
            ca = self.assignment.subset(lambda v: v.prefix == cat and v not in vlist).randomSample(prob, seed = seed)
            fixedAssignment.overWrite(ca)
        if len(fixedAssignment) > 0:
            self.ckt.comment("Fixed assignments, generated from scheme with signature %s" % self.sign())
            fixedAssignment.assign(self.ckt)
        fixedVariables = [v for v in fixedAssignment.variables()]
        self.declareVariables(fixedVariables)


    def generateProgram(self, categoryProbabilities = {'alpha':1.0, 'beta':1.0, 'gamma':1.0}, seed = None, timeLimit = None, fixKV = False, varKV = False, excludeSingleton = False, breadthFirst = False, levelList = None, useZdd = False, symbolicStreamline = False, boundNonKernels = False):
        plist = list(categoryProbabilities.values())
        isFixed = functools.reduce(lambda x, y: x*y, plist) == 1.0
        self.ckt.cmdLine("option", ["echo", 1])
        if timeLimit is not None:
            self.ckt.cmdLine("option", ["seconds", timeLimit])
        mode = "Checking" if isFixed else "Solving"
        self.ckt.comment("%s Brent equations to derive matrix multiplication scheme" % mode)
        args = self.fullRanges() + (self.auxCount,)
        self.ckt.comment("Goal is to compute A (%d x %d) X B (%d x %d) = C (%d x %d) using %d multiplications" % args)
        for k in categoryProbabilities.keys():
            prob = categoryProbabilities[k]
            self.ckt.comment("Category %s has %.1f%% of its variables fixed" % (k, prob * 100.0))
        if fixKV:
            khash = self.kernelTerms.sign()
            self.ckt.comment("Kernel terms fixed according to kernel %s" % khash)
        if varKV:
            khash = self.kernelTerms.sign()
            self.ckt.comment("Leaving kernel terms from kernel %s variable" % khash)
        if excludeSingleton:
            self.ckt.comment("Enforcing singleton exclusion")
        if symbolicStreamline:
            self.ckt.comment("Enforcing kernel constraints unique usage, max double, and singleton exclusion")            
        self.generateMixedConstraints(categoryProbabilities, seed, fixKV, varKV)
        streamlineNode = None
        if excludeSingleton:
            streamlineNode = self.generateStreamline()
        if symbolicStreamline:
            if excludeSingleton:
                print("WARNING: Can't enforce both fixed streamline and symbolic streamline")
            else:
                streamlineNode = self.symbolicStreamline()
        kset = self.kernelTerms if fixKV else None
        self.generateBrentConstraints(kset, streamlineNode = streamlineNode, check=isFixed, breadthFirst = breadthFirst, levelList = levelList, useZdd = useZdd, fixKV = fixKV, boundNonKernels = boundNonKernels)
        bv = circuit.Vec([BrentTerm()])
        if not isFixed:
            self.ckt.count(bv)
            self.ckt.status()
            self.ckt.satisfy(bv)
        self.ckt.write("time")
        self.ckt.write("quit")

