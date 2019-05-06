# Encoding solutions to matrix multiplication problems
import re
import circuit

# Sequence of digits starting at one
def unitRange(n):
    return range(1, n+1)


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

# Describe encoding of matrix multiplication
class MScheme:

    # Matrix dimensions, given as (n1, n2, n3)
    dim = (0,0,0)
    # Number of auxilliary variables
    auxCount = 0
    # Encoding of alpha variables
    alphaList = []
    # Encoding of beta variables
    betaList = []
    # Encoding of gamma variables
    gammaList = []
    expressionSplitter = None


    def __init__(self, dim = (0,0,0)):
        if type(dim) == type(2):
            self.dim = (dim, dim, dim)
        else:
            self.dim = dim
        self.alphaList = []
        self.betaList = []
        self.gammaList = []
        self.expressionSplitter = re.compile('[-+]')

    # Get specified list
    def getList(self, category = 'alpha'):
        if category == 'alpha' or category == 'a':
            return self.alphaList
        if category == 'beta' or category == 'b':
            return self.betaList
        if category == 'gamma' or category == 'c':
            return self.gammaList
        raise Exception("No category '%s'" % category)

    # Parse the output generated by a solver
    def parseFromSolver(self, supportNames, bitString):
        if self.auxCount > 0:
            self.__init__(self.dim)
        supportVars = [BrentVariable().fromName(s) for s in supportNames]
        self.auxCount = 0
        done = False
        level = 1
        while not done:
            lists = {'a':[], 'b':[], 'c':[]}
            lcount = 0
            for i in range(len(bitString)):
                name = supportVars[i]
                var = BrentVariable(name)
                if bitString[i] == '1' and var.level == level:
                    lists[var.symbol].append(var)
                    lcount += 1
            done = lcount == 0
            if not done:
                self.alphaList.append(lists['a'])
                self.betaList.append(lists['b'])
                self.gammaList.append(lists['c'])
                self.auxCount += 1
        return self
                
    # Parse from polynomial representation
    def parsePolynomialLine(self, line):
        level = self.auxCount+1
        parts = line.split('*')
        lists = [self.alphaList, self.betaList, self.gammaList]
        for p, l in zip(parts, lists):
            # Strip parentheses
            p = p[1:-1]
            # Split with + and -:
            terms = self.expressionSplitter.split(p)
            # Remove empty ones (due to unary -)
            terms = [t for t in terms if t != ""]
            # Create variables
            vars = [BrentVariable(level = level).fromTerm(t, permuteC = True) for t in terms]
            l.append(vars)
        self.auxCount += 1
        
    # Read polynomial from file
    def parseFromFile(self, fname):
        if self.auxCount > 0:
            self.__init__(self.dim)
        try:
            f = open(fname, 'r')
        except:
            print "Couldn't open file '%s'" % fname
            return
        for line in f:
            self.parsePolynomialLine(line)
        f.close()
        return self

    # Generate formula encoding (partial) solution
    def generateCategoryConstraints(self, ckt, category, level):
        vars = self.getList(category)
        p = self.auxCount
        nrow = self.dim[1] if category == 'beta' else self.dim[0]
        ncol = self.dim[1] if category == 'alpha' else self.dim[2]
        valueList = [0] * nrow * ncol
        for v in vars[level-1]:
            idx = (v.row-1)*ncol + (v.column-1)
            valueList[idx] = 1
        ckt.comment("Assign values to %s terms for auxilliary term %d" % (category, level))
        for row in unitRange(nrow):
            for col in unitRange(ncol):
                idx = (row-1)*ncol + (col-1)
                v = BrentVariable(prefix = category, row = row, column = col, level = level)
                node = circuit.Node(str(v))
                ckt.assignConstant(node, valueList[idx])
                    
    def generateConstraints(self, ckt, categories = ['alpha', 'beta', 'gamma']):
        for l in unitRange(self.auxCount):
            for c in categories:
                self.generateCategoryConstraints(ckt, c, l)
