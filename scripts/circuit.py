# Library of routines for generating Boolean expression evaluations scripts
# Assume constants one & zero are named "one" and "zero"

import sys


# For generating unique names
class Uniq:
    nextId = 0
    root = "n_t"
    
    def __init(self, root = "n_t"):
        self.root = root
        self.nextId = 0

    def new(self):
        result = "%s%d" % (self.root, self.nextId)
        self.nextId += 1
        return result

# Representation of single nodes
class Node:
    name = ""
    refCnt = 0

    def __init__(self, name):
        self.name = name
        self.refCnt = 1

    def addRef(self):
        self.refCnt += 1

    def decRef(self):
        self.refCnt -= 1

    def dead(self):
        return self.refCnt <= 0

    def __str__(self):
        return self.name


# Represent signal as vector of names.
# Should keep these immutable
class Vec:
    # Represent as array of strings.
    nodes = []
    refCnt = 0
    
    def __init__(self, nodeList = []):
        self.nodes = nodeList
        self.refCnt = 1
        

    def addRef(self):
        self.refCnt += 1

    def decRef(self):
        self.refCnt -= 1

    def dead(self):
        return self.refCnt <= 0

    def __str__(self):
        return " ".join(self.nodes)

    def length(self):
        return len(self.nodes)

    # Various ways of constructing new nodes

    # Create list of names of form root.i
    # Use little-Endian ordering
    def nameVec(self, root, n):
        ls = [root + '.' + str(i) for i in range(n)]
        self.nodes = ls

    def reverse(self):
        n = len(self.nodes)
        return Vec([self.nodes[n-i-1] for i in range(n)])

    def dup(self):
        return Vec([nd for nd in self.nodes])

    def concatenate(self, other):
        ls = [nd for nd in self.nodes]
        ls.extend(other.nodes)
        return Vec(ls)

    # Extend vector with repeated element
    # If necessary, truncate so that final size is n
    def extend(self, n, ele = "zero"):
        if n < len(self.nodes):
            ls = [self.nodes[i] for i in range(n)]
        else:
            ls = [nd for nd in self.nodes]
            other = [ele for i in range(n-len(ls))]
            ls.extend(other)
        return Vec(ls)

    def shiftLeft(self, n):
        ls = ["zero" for i in range(n)]
        ls.extend(self.nodes)
        return Vec(ls)

    # Create interleaved vector
    def interleave(self, other):
        if self.length() < other.length():
            short, long = self.nodes, other.nodes
            nshort, nlong = self.length(), other.length()
        else:
            short, long = other.nodes, self.nodes
            nshort, nlong = other.length(), self.length()
        step = nlong / nshort
        ls = []
        for ele in short:
            ls.append(ele)
            ls.extend(long[:step])
            long = long[step:]
        ls.extend(long)
        return Vec(ls)
        
# Framework for creating expressions
class Circuit:
    outfile = None
    # Set of (nonvector) nodes
    nodes = None
    # Set of vectors
    vecs = None
    uniq = None
    
    def __init__(self, outfile = sys.stdout):
        self.outfile = outfile
        self.nodes = set()
        self.vecs = set()
        self.uniq = Uniq()

    def addVec(self, v):
        self.vecs.add(v)

    def addNode(self, nd):
        self.nodes.add(nd)

    def flush(self):
        rnodes = []
        for nd in self.nodes:
            if nd.dead():
                self.delete(nd)
                rnodes.append(nd)
        for nd in rnodes:
            self.nodes.remove(nd)
        rvecs = []
        for v in self.vecs:
            if v.dead():
                self.delete(v)
                rvecs.append(v)
        for v in rvecs:
            self.vecs.remove(v)

    def node(self, name):
        nd = Node(name)
        self.addNode(nd)
        return nd

    def tmpNode(self):
        name = self.uniq.new()
        return self.node(name)

    def nameVec(self, root, n):
        v = Vec()
        v.nameVec(root, n)
        self.addVec(v)
        return v

    def tmpVec(self, n):
        root = self.uniq.new()
        return self.nameVec(root, n)

    # Decrement, and possibly delete, all nodes & vectors in ls
    def decRefs(self, ls):
        for ele in ls:
            ele.decRef()
        self.flush()


    # Write to file.  Adds EOL
    def write(self, line):
        self.outfile.write(line + "\n")

    def comment(self, line):
        self.write("# " + line)

    # Generate single line command
    # Obj can be single node or vector, or list of nodes/vectors
    def cmdLine(self, cmd, obj):
        if type(obj) in [type([]), type(())]:
            ls = map(str, obj)
            s = " ".join(ls)
        else:
            s = str(obj)
        self.write(cmd + " " + s)

    def declare(self, varv):
        self.cmdLine("var", varv)

    def delete(self, obj):
        self.cmdLine("delete", obj)

    def information(self, fv):
        self.cmdLine("info", fv)

    def collect(self):
        self.write("collect")

    def status(self):
        self.write("status")

    def count(self, fv):
        self.cmdLine("count", fv)

    # Generate sequence of commands
    # argList should be list of vectors
    def cmdSequence(self, cmd, argList):
        n = max([v.length() for v in argList])
        nargList = [v.extend(n) for v in argList]
        lists = [v.nodes for v in nargList]
        for i in range(n):
            args = [ele[i] for ele in lists]
            self.cmdLine(cmd, args)
        
    def andN(self, dest, argList):
        ls = [dest]
        ls.extend(argList)
        self.cmdLine("and", ls)

    def orN(self, dest, argList):
        ls = [dest]
        ls.extend(argList)
        self.cmdLine("or", ls)

    def xorN(self, dest, argList):
        ls = [dest]
        ls.extend(argList)
        self.cmdLine("xor", ls)

    def iteN(self, dest, argList):
        ls = [dest]
        ls.extend(argList)
        self.cmdLine("ite", ls)

    def notV(self, dest, v):
        self.cmdSequence("not", [dest, v])

    def andV(self, dest, argList):
        ls = [dest]
        ls.extend(argList)
        self.cmdSequence("and", ls)

    def orV(self, dest, argList):
        ls = [dest]
        ls.extend(argList)
        self.cmdSequence("or", ls)

    def xorV(self, dest, argList):
        ls = [dest]
        ls.extend(argList)
        self.cmdSequence("xor", ls)

    def maj3(self, dest, n1, n2, n3):
        p12 = self.tmpNode()
        p23 = self.tmpNode()
        p13 = self.tmpNode()
        self.cmdLine("and", [p12, n1, n2])
        self.cmdLine("and", [p23, n2, n3])
        self.cmdLine("and", [p13, n1, n3])
        self.cmdLine("or", [dest, p12, p23, p13])
        self.decRefs([p12, p23, p13])

    def majorityV3(self, dest, a1, a2, a3):
        n = dest.length()
        p12 = self.tmpVec(n)
        p23 = self.tmpVec(n)
        p13 = self.tmpVec(n)
        self.andV(p12, [a1, a2])
        self.andV(p23, [a2, a3])
        self.andV(p13, [a1, a3])
        self.orV(dest, [p12, p23, p13])
        self.decRefs([p12, p23, p13])

    def addV2(self, dest, a1, a2):
        n = dest.length()
        cv = self.tmpVec(n-1)
        carry = cv.shiftLeft(1)
        lsd = dest.nodes
        ls1 = a1.nodes
        ls2 = a2.nodes
        lsc = carry.nodes
        args = [ls1, ls2, lsc]
        for i in range(n):
            ls = [ele[i] for ele in args]
            self.xorN(lsd[i], ls) 
            if i < n-1:
                self.maj3(lsc[i+1], ls[0], ls[1], ls[2])
        self.decRefs([cv])

    def addV(self, dest, argList):
        n = dest.length()
        isTmp = [False for arg in argList]
        while len(argList) > 2:
            tmpV = self.tmpVec(n)
            self.addV2(tmpV, argList[0], argList[1])
            if isTmp[0]:
                self.decRefs([argList[0]])
            ls = [tmpV]
            ls.extend(argList[2:])
            argList = ls
            ls = [True]
            ls.extend(isTmp[2:])
            isTmp = ls
        self.addV2(dest, argList[0], argList[1])
        if isTmp[0]:
            self.decRefs([argList[0]])
    
    def multV2(self, dest, p1, p2):
        n = dest.length()
        np1 = p1.extend(n)
        np2 = p2.extend(n)
        pp = []
        tlist = []
        for i in range(n):
           a = Vec().extend(n, np2.nodes[i])
           t = self.tmpVec(n)
           tlist.append(t)
           self.andV(t, [a, np1])
           pp.append(t.shiftLeft(i).extend(n))
        self.addV(dest, pp)
        self.decRefs(tlist)

    def multV(self, dest, argList):
        n = dest.length()
        isTmp = [False for arg in argList]
        while len(argList) > 2:
            tmpV = self.tmpVec(n)
            self.multV2(tmpV, argList[0], argList[1])
            if isTmp[0]:
                self.decRefs([argList[0]])
            ls = [tmpV]
            ls.extend(argList[2:])
            argList = ls
            ls = [True]
            ls.extend(isTmp[2:])
            isTmp = ls
        self.multV2(dest, argList[0], argList[1])
        if isTmp[0]:
            self.decRefs([argList[0]])

    # Generate signals indicating whether have 0 or 1 elements of vector set.
    # nv is negation of v
    def count01(self, c0, c1, v, nv):
        n = len(v.nodes)
        self.andN(c0, [])
        self.orN(c1, [])
        for i in range(n):
            name = v.nodes[i]
            nname = nv.nodes[i]
            self.iteN(c1, [name, c0, c1])
            self.andN(c0, [c0, nname])

    # Is at most one signal in vector equal to 1?
    def exactly1(self, dest, v, nv):
        c0 = self.tmpNode()
        self.count01(c0, dest, v, nv)
        self.decRefs([c0])

    # Is at most one signal in vector equal to 1?
    def atMost1(self, dest, v, nv):
        c0 = self.tmpNode()
        c1 = self.tmpNode()
        self.count01(c0, c1, v, nv)
        self.orN(dest, [c0, c1])
        self.decRefs([c0, c1])

    def getBit(self, v, i):
        return ((v>>i) &1)

    # Create product term to match specified value
    def matchVal(self, v, vec, out):
        names = [(("!%s" if self.getBit(v, i) == 0 else "%s") % vec.nodes[i]) for i in range(len(vec.nodes))]
        lits = Vec(names)
        self.andN(out, lits.nodes)

## Some benchmarks:
# Show that addition is associative
def addAssociative(n, f = sys.stdout):
    ckt = Circuit(f)
    # Generate vectors
    a, b, c, s, t, x = [ckt.nameVec(r, n) for r in ["a", "b", "c", "s", "t", "x"]]
    e = ckt.node("e")
    da, db, dc = [ckt.nameVec(r, n) for r in ["a", "b", "c"]]
    vars = da.interleave(db).interleave(dc).reverse()
    ckt.write("time")
    ckt.declare(vars)

    # Add them together
    ckt.addV(s, [a, b, c])
    ckt.addV(t, [c, b, a])
    ckt.information(t)
    ckt.decRefs([a, b, c])

    # Generate comparator
    ckt.xorV(x, [s, t])
    ckt.decRefs([s, t])
    ckt.orN(e, [ckt.node(name) for name in x.nodes])
    ckt.decRefs([x])
    ckt.write("equal zero e")
    ckt.write("time")
    ckt.status()

# Show that multiplication is associative
def multAssociative(n, f = sys.stdout):
    ckt = Circuit(f)
    # Generate vectors
    a, b, c, s, t, x = [ckt.nameVec(r, n) for r in ["a", "b", "c", "s", "t", "x"]]
    e = ckt.node("e")
    da, db, dc = [ckt.nameVec(r, n) for r in ["a", "b", "c"]]
    vars = da.interleave(db).interleave(dc).reverse()
    ckt.write("time")
    ckt.declare(vars)

    # Mult them together
    ckt.multV(s, [a, b, c])

    ckt.multV(t, [c, b, a])
    ckt.information(t)
    ckt.decRefs([a, b, c])

    # Generate comparator
    ckt.xorV(x, [s, t])
    ckt.decRefs([s, t])
    ckt.orN(e, [ckt.node(name) for name in x.nodes])
    ckt.decRefs([x])
    ckt.write("equal zero e")
    ckt.write("time")
    ckt.status()

# Functions related to n-queens
# Literal representing value at position r,c
def sq(r, c, negate = False):
    return "!r-%d.c-%d" % (r, c) if negate else "r-%d.c-%d" % (r, c)

def row(n, r, negate = False):
    return Vec([sq(r, c, negate) for c in range(n)])

def col(n, c, negate = False):
    return Vec([sq(r, c, negate) for r in range(n)])

# Principal diagonals numbered from -(n-1) to n-1
def diag(n, i, negate = False):
    if (i < 0):
        # elements numbered from -i,0 to n-1,n+i-1
        return Vec([sq(j-i, j, negate) for j in range(n+i)])
    else:
        # elements numbered from 0,i to n-i-1,n-1
        return Vec([sq(j, j+i, negate) for j in range(n-i)])

# Off diagonals numbered from -(n-1) to n-1
def offDiag(n, i, negate = False):
    if (i < 0):
        # elements numbered from n-1+i,0 to 0,n-1+i
        return Vec([sq(n-1+i-j, j, negate) for j in range(n+i)])
    else:
        # elements numbered from n-i,i to i,n-1
        return Vec([sq(n-1-j, i+j, negate) for j in range(n-i)])

# Test of subfunctions
def tAtMost1(n, f = sys.stdout):
    ckt = Circuit(f)
    v = ckt.nameVec("a", n)
    ckt.declare(v)
    nv = ckt.nameVec("!a", n)
    ok = ckt.node("ok")
    ckt.atMost1(ok, v, nv)
    
# Test of subfunctions
def tExactly1(n, f = sys.stdout):
    ckt = Circuit(f)
    v = ckt.nameVec("a", n)
    ckt.declare(v)
    nv = ckt.nameVec("!a", n)
#    ckt.notV(nv, v)
    ok = ckt.node("ok")
    ckt.exactly1(ok, v, nv)

# Generate constraints for n-queens problem
def nQueens(n, f = sys.stdout):
    ckt = Circuit(f)
    # Generate variables for each square
    snames = [sq(i/n, i%n) for i in range(n*n)]
    nsnames = ["!%s" % name for name in snames]
    sv = Vec(snames)
    nsv = Vec(nsnames)
    ckt.write("time")
    ckt.declare(sv)
    okr = ckt.nameVec("okr", n)
    okR = ckt.node("okR")
    okc = ckt.nameVec("okc", n)
    okC = ckt.node("okC")
    okd = ckt.nameVec("okd", n+n-1)
    okD = ckt.node("okD")
    oko = ckt.nameVec("oko", n+n-1)
    okO = ckt.node("okO")
    ckt.comment("Row Constraints")
    # Row constraints
    for r in range(n):
        ckt.comment("Row %d" % r)
        ckt.exactly1(okr.nodes[r], row(n, r), row(n, r, True))
    ckt.andN(okR, okr.nodes)
    ckt.decRefs([okr])
    ckt.comment("Column Constraints")
    # Column constraints
    for c in range(n):
        ckt.comment("Column %d" % c)
        ckt.exactly1(okc.nodes[c], col(n, c), col(n, c, True))
    ckt.andN(okC, okc.nodes)
    ckt.decRefs([okc])
    # Diagonal constraints:
    ckt.comment("Diagonal Constraints")
    for i in range(-n+1,n):
        ckt.comment("Diagonal %d" % i)
        out = okd.nodes[i+n-1]
        ckt.atMost1(out, diag(n,i), diag(n, i, True))
    ckt.andN(okD, okd.nodes)
    ckt.decRefs([okd])
    # Off diagonal constraints
    ckt.comment("Off-diagonal Constraints")
    for i in range(-n+1,n):
        ckt.comment("Off-diagonal %d" % i)
        out = oko.nodes[i+n-1]
        ckt.atMost1(out, offDiag(n,i), offDiag(n, i, True))
    ckt.andN(okO, oko.nodes)
    ckt.decRefs([oko])
    ckt.comment("Combine Constraints")
    ok = ckt.node("ok")
    ckt.andN(ok, [okR, okC])
    ckt.decRefs([okR, okC])
    ckt.andN(ok, [ok, okD])
    ckt.decRefs([okD])
    ckt.andN(ok, [ok, okO])
    ckt.decRefs([okO])
    ckt.write("time")
    ckt.count([ok])
    
def bigLog2(x):
    val = 0
    while ((1<<val) < x):
        val+=1
    return val

# Version of nqueens using logarithmic encoding of column numbers
def lQueens(n, f = sys.stdout, careful = False, info = False):
    ckt = Circuit(f)
    m = bigLog2(n)
    rows = [ckt.nameVec("v-%d" % r, m) for r in range(n)]
    nrows = [ckt.nameVec("v-%d" % r, m) for r in range(n)]
# Interleave variables with MSB first
#    vars = Vec(["v-%d.%d" % (i %  n, m-1 - (i / n)) for i in range(m*n)])
# Interleave variables with LSB first
#    vars = Vec(["v-%d.%d" % (i %  n, (i / n)) for i in range(m*n)])
# Do variables for each row in succession, MSB first
    vars = Vec(["v-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
    ckt.write("time")
    ckt.declare(vars)
    snames = [sq(i/n, i%n)for i in range(n*n)]
    nsnames = ["!%s" % name for name in snames]
    sv = Vec(snames)
    nsv = Vec(nsnames)
    ckt.comment("Individual square functions")
    for r in range(n):
        for c in range(n):
            ckt.matchVal(c, rows[r], sq(r,c))
    okc = ckt.nameVec("okc", n)
    okC = ckt.node("okC")
    okd = ckt.nameVec("okd", n+n-1)
    okD = ckt.node("okD")
    oko = ckt.nameVec("oko", n+n-1)
    okO = ckt.node("okO")
    ckt.comment("Column Constraints")
    # Column constraints
    for c in range(n):
        ckt.comment("Column %d" % c)
        ckt.exactly1(okc.nodes[c], col(n, c), col(n, c, True))
    ckt.andN(okC, okc.nodes)
    ckt.decRefs([okc])
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information([okC])
        ckt.count([okC])
    # Diagonal constraints:
    ckt.comment("Diagonal Constraints")
    for i in range(-n+1,n):
        ckt.comment("Diagonal %d" % i)
        out = okd.nodes[i+n-1]
        ckt.atMost1(out, diag(n,i), diag(n, i, True))
    ckt.andN(okD, okd.nodes)
    ckt.decRefs([okd])
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information([okD])
        ckt.count([okD])
    ckt.comment("Combine Constraints: column + diagonal")
    okCD = ckt.node("okCD")
    ckt.andN(okCD, [okC, okD])
    ckt.decRefs([okC, okD])
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information([okCD])
        ckt.count([okCD])
    # Off diagonal constraints
    ckt.comment("Off-diagonal Constraints")
    for i in range(-n+1,n):
        ckt.comment("Off-diagonal %d" % i)
        out = oko.nodes[i+n-1]
        ckt.atMost1(out, offDiag(n,i), offDiag(n, i, True))
    ckt.andN(okO, oko.nodes)
    ckt.decRefs([oko])
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information([okO])
        ckt.count([okO])
    ckt.comment("Combine Constraints: Add off-diagonal")
    ok = ckt.node("ok")
    ckt.andN(ok, [okCD, okO])
    ckt.decRefs([okCD, okO])
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    ckt.comment("BDD generation completed")
    ckt.write("time")
    ckt.information([ok])
    ckt.comment("Model counting")
    ckt.count([ok])
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
    ckt.write("time")
    ckt.status()
    ckt.comment("Flush state")
    ckt.write("flush")
    ckt.comment("Exit")
    ckt.write("quit")


    
    
    
    
