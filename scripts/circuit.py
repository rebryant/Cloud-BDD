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

    def __len__(self):
        return len(self.nodes)

    def __getitem__(self, key):
        return self.nodes[key]

    def __setitem__(self, key, value):
       self.nodes[key] = value

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
        if len(self) < len(other):
            short, long = self.nodes, other.nodes
            nshort, nlong = len(self), len(other)
        else:
            short, long = other.nodes, self.nodes
            nshort, nlong = len(other), len(self)
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
        n = max([len(v) for v in argList])
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

    def norN(self, dest, argList):
        nargs = [(s[1:] if s[0] == "!" else "!"+s) for s in argList]
        self.andN(dest, nargs)

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

    def zc(self, dest, n):
        self.cmdLine("zconvert", [dest, n])

    def ac(self, dest, n):
        self.cmdLine("aconvert", [dest, n])

    def zcV(self, dest, v):
        self.cmdSequence("zconvert", [dest, v])

    def acV(self, dest, v):
        self.cmdSequence("aconvert", [dest, v])

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
        n = len(dest)
        p12 = self.tmpVec(n)
        p23 = self.tmpVec(n)
        p13 = self.tmpVec(n)
        self.andV(p12, [a1, a2])
        self.andV(p23, [a2, a3])
        self.andV(p13, [a1, a3])
        self.orV(dest, [p12, p23, p13])
        self.decRefs([p12, p23, p13])

    def addV2(self, dest, a1, a2):
        n = len(dest)
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
        n = len(dest)
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
        n = len(dest)
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
        n = len(dest)
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
    # nvec is negation of vec
    def matchVal(self, v, vec, out):
        names = [(("!%s" if self.getBit(v, i) == 0 else "%s") % vec.nodes[i]) for i in range(len(vec.nodes))]
        lits = Vec(names)
        self.andN(out, lits.nodes)

        
    # Components to build up C6288-style multipler
    # Single partial product: a_{i,j}
    def pprod(self, dest, avec, bvec, i, j):
        self.andN(dest, [avec.nodes[j], bvec.nodes[i]])

    # Vector of partial products for single level j
    def pprodV(self, dv, avec, bvec, j):
        m = len(dv)
        for i in range(m):
            self.pprod(dv.nodes[i], avec, bvec, i, j)

    # Half adder
    def hadd(self, carry, sum, x, z):
        self.andN(carry, [x, z])
        self.xorN(sum, [x, z])

    # Vector of half adders
    def haddV(self, cv, sv, xv, yv):
        for i in range(len(cv)):
            self.hadd(cv.nodes[i], sv.nodes[i], xv.nodes[i], yv.nodes[i])

    # Full adder
    def fadd(self, carry, sum, x, y, z):
        self.xorN(sum, [x, y, z])
        self.maj3(carry, x, y, z)

    # Vector of full adders
    def faddV(self, cv, sv, xv, yv, zv):
        for i in range(len(cv)):
            self.fadd(cv.nodes[i], sv.nodes[i], xv.nodes[i], yv.nodes[i], zv.nodes[i])
        
    # Top layer of multipler
    # out: Generates bits 0 and 1 of product
    # cvec: carry outputs (m-1)
    # svec: sum outputs (m-2)
    def tlayer(self, out, cvec, svec, avec, bvec):
        self.comment("Building top level of multiplier")
        m = len(avec)
        n = len(bvec)
        v0 = self.tmpVec(m-1)
        pv0 = Vec([out.nodes[0]] + v0.nodes)
        self.pprodV(pv0, avec, bvec, 0)
        v1 = self.tmpVec(m-1)
        self.pprodV(v1, avec, bvec, 1)
        sveclong = Vec([out.nodes[1]] + svec.nodes)
        self.haddV(cvec, sveclong, v0, v1)
        self.decRefs([v0, v1])

    # Middle layers of multiplier
    # out: Generates bit j of product
    # cvec: carry outputs (m-1)
    # svec: sum outputs (m-2)
    # cinvec: input carries (m-1)
    # sinvec: input sums (m-2)
    def mlayer(self, out, cvec, svec, cinvec, sinvec, avec, bvec, j):
        self.comment("Building level %d of multiplier" % j)
        m = len(avec)
        n = len(bvec)
        v = self.tmpVec(m-1)
        self.pprodV(v, avec, bvec, j)
        pp = self.tmpNode()
        self.pprod(pp, avec, bvec, m-1, j-1)
        sinveclong = Vec(sinvec.nodes + [pp])
        sveclong = Vec([out.nodes[j]] + svec.nodes)
        self.faddV(cvec, sveclong, sinveclong, cinvec, v)
        self.decRefs([v, pp])

    # Bottom level of multiplier
    # out: Generates bits n .. m+n-1 of product
    # cinvec: input carries (m-1)
    # sinvec: input sums (m-2)
    def blayer(self, out, cinvec, sinvec, avec, bvec):
        self.comment("Building bottom level of multiplier")
        m = len(avec)
        n = len(bvec)
        pp = self.tmpNode()
        self.pprod(pp, avec, bvec, m-1, n-1)
        cv = self.tmpVec(m-2)
        self.hadd(cv.nodes[0], out.nodes[n], sinvec.nodes[0], cinvec.nodes[0])
        for i in range(m-3):
            self.fadd(cv.nodes[i+1], out.nodes[n+1+i], sinvec.nodes[i+1], cinvec.nodes[i+1], cv.nodes[i])
        self.fadd(out.nodes[n+m-1], out.nodes[n+m-2], pp, cinvec.nodes[m-2], cv.nodes[m-3])
        self.decRefs([cv, pp])

    # m X n bit multiplier
    def multiplier(self, out, avec, bvec, verbose=False):
        # Require m >= 3, n >= 1
        m = len(avec)
        n = len(bvec)
        svec = self.tmpVec(m-2)
        cvec = self.tmpVec(m-1)
        self.tlayer(out, cvec, svec, avec, bvec)
        if verbose:
            self.information(svec.nodes + cvec.nodes)
        for j in range(2,n):
            sinvec = svec
            cinvec = cvec
            svec = self.tmpVec(m-2)
            cvec = self.tmpVec(m-1)
            self.mlayer(out, cvec, svec, cinvec, sinvec, avec, bvec, j)
            if verbose:
                self.information(svec.nodes + cvec.nodes)
            self.decRefs([sinvec, cinvec])
        self.blayer(out, cvec, svec, avec, bvec)
        self.decRefs([cvec, svec])
        
    # Construct function based on enumeration of function
    # User provides function mapping list of integers to integer
    def blast(self, out, inlist, ifun):
        # Initialize nodes to 0
        self.orV(out, [])
        xval = [0 for input in inlist]
        allnodes = []
        for input in inlist:
            allnodes = allnodes + input.nodes
        allinputs = Vec(allnodes)
        for idx in range(1 << len(allinputs)):
            v = idx
            for i in range(len(inlist)):
                xval[i] = 0
                for j in range(len(inlist[i])):
                    xval[i] += ((v & 0x1) << j)
                    v = v >> 1
            y = ifun(xval)
            select = None
            for j in range(len(out)):
                if y & (1 << j) != 0:
                    if select == None:
                        select = self.tmpNode()
                        self.matchVal(idx, allinputs, select)
                    self.orN(out[j], [out[j], select])
            if select != None:
                self.decRefs([select])
    
    def multfun(self, xlist):
        v = 1
        for x in xlist:
            v *= x
        return v

    def multblast(self, out, avec, bvec):
        self.blast(out, [avec, bvec], self.multfun)
        

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

# Class to define point at which to add preconstraints
class PC:
    none, row, column, diagonal, offdiagonal = range(5)
    names = ["None", "Row", "Column", "Diagonal", "Off-Diagonal"]
    snames = ["uncon", "rwcon", "clcon", "dicon", "odcon"]

    def name(self, id):
        return self.names[id]

    def sname(self, id):
        return self.snames[id]

# Class to define use of ZDDs
class Z:
    none, vars, convert, avars = range(4)
    names = ["none", "vars", "convert", "avars"]
    suffixes = ["b", "v", "z", "a"]

    def name(self, id):
        return self.names[id]

    def suffix(self, id):
        return self.suffixes[id]

def bigLog2(x):
    val = 0
    while ((1<<val) < x):
        val+=1
    return val

# Generate constraints for n-queens problem
def nQueens(n, f = sys.stdout, binary = False, careful = False, info = False, preconstrain = PC.none, zdd = Z.none):
    encoding = "binary" if binary else "one-hot"
    ckt = Circuit(f)
    ckt.comment("N-queens with %s encoding.  N = %d" % (encoding, n))
    ckt.comment("Preconstrain method: %s" % PC().name(preconstrain))
    ckt.comment("ZDD mode = %s" % Z().name(zdd))
#    ckt.write("time")
    pc = ckt.node("preconstrain")
    ckt.andN(pc, [])
    okR = ckt.node("okR")
    if zdd == Z.convert:
        zokR = ckt.node("zokR")
    if binary:
        m = bigLog2(n)
        rows = [ckt.nameVec("v-%d" % r, m) for r in range(n)]
        # Do variables for each row in succession, MSB first
        vars = Vec(["v-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
        if zdd == Z.vars:
            zvars = Vec(["bv-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
            ckt.declare(zvars)
            ckt.zcV(vars, zvars)
        elif zdd == Z.avars:
            avars = Vec(["bv-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
            ckt.declare(avars)
            ckt.acV(vars, avars)
        else:
            ckt.declare(vars)
        ckt.comment("Individual square functions")
        for r in range(n):
            for c in range(n):
                ckt.matchVal(c, rows[r], sq(r,c))
        ckt.andN(okR, [])
    else:
        # Generate variables for each square
        snames = [sq(i/n, i%n) for i in range(n*n)]
        sv = Vec(snames)
        if zdd == Z.vars:
            znames = ["b" + s for s in snames]
            zv = Vec(znames)
            ckt.declare(zv)
            ckt.zcV(sv, zv)
        elif zdd == Z.avars:
            anames = ["b" + s for s in snames]
            av = Vec(anames)
            ckt.declare(av)
            ckt.acV(sv, av)
        else:
            ckt.declare(sv)
        # Row constraints
        ckt.comment("Row Constraints")
        okr = ckt.nameVec("okr", n)
        for r in range(n):
            ckt.comment("Row %d" % r)
            ckt.exactly1(okr.nodes[r], row(n, r), row(n, r, True))
        ckt.andN(okR, okr.nodes)
        ckt.decRefs([okr])
        if preconstrain >= PC.row:
            ckt.andN(pc, [okR])
        if zdd == Z.convert:
            ckt.zc(zokR, okR)
            ckt.decRefs([okR])

    okc = ckt.nameVec("okc", n)
    okC = ckt.node("okC")
    okRC = ckt.node("okRC")
    okd = ckt.nameVec("okd", n+n-1)
    okD = ckt.node("okD")
    okRCD = ckt.node("okRCD")
    oko = ckt.nameVec("oko", n+n-1)
    okO = ckt.node("okO")
    ok = ckt.node("ok")

    if zdd == Z.convert:
        zokC = ckt.node("zokC")
        zokD = ckt.node("zokD")
        zokO = ckt.node("zokO")
        zokRC = ckt.node("zokRC")
        zokRCD = ckt.node("zokRCD")
        zok = ckt.node("zok")

    # Column constraints
    ckt.comment("Column Constraints")
    for c in range(n):
        ckt.comment("Column %d" % c)
        ckt.exactly1(okc.nodes[c], col(n, c), col(n, c, True))
        ckt.andN(okc.nodes[c], [okc.nodes[c], pc])
    ckt.andN(okC, okc.nodes)
    ckt.decRefs([okc])
    if preconstrain >= PC.row:
        ckt.decRefs([okR])
    if zdd == Z.convert:
        ckt.zc(zokC, okC)
        ckt.decRefs([okC])
        ils = [zokC]
    else:
        ils = [okC]
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information(ils)

    if zdd == Z.convert:
        if binary:
            ckt.comment("Row constraint implicit")
            ckt.andN(zokRC, [zokC])
            ils = [zokRC]
            dls = [zokC]
        else:
            ckt.comment("Combine row and column")
            ckt.andN(zokRC, [zokR, zokC])
            ils = [zokRC]
            dls = [zokR, zokC]
    elif preconstrain < PC.row:
        ckt.comment("Combine row and column")
        ckt.andN(okRC, [okR, okC])
        ils = [okRC]
        dls = [okR, okC]
    else:
        ckt.comment("Row constraints already incorporated into column constraints")
        ckt.andN(okRC, [okC])
        ils = [okRC]
        dls = [okC]
    if zdd != Z.convert and preconstrain >= PC.column:
        ckt.andN(pc, [okRC])
    ckt.decRefs(dls)
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information(ils)

    # Diagonal constraints:
    ckt.comment("Diagonal Constraints")
    for i in range(-n+1,n):
        ckt.comment("Diagonal %d" % i)
        out = okd.nodes[i+n-1]
        ckt.atMost1(out, diag(n,i), diag(n, i, True))
        ckt.andN(out, [out, pc])
    ckt.andN(okD, okd.nodes)
    ckt.decRefs([okd])
    if zdd == Z.convert:
        ckt.zc(zokD, okD)
        ckt.decRefs([okD])
        ils = [zokD]
    else:
        ils = [okD]

    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information(ils)

    if zdd == Z.convert or preconstrain < PC.column:
        ckt.comment("Add diagonal to row & column")
        if zdd == Z.convert:
            ckt.andN(zokRCD, [zokRC, zokD])
            ils = [zokRCD]
        else:
            ckt.andN(okRCD, [okRC, okD])
            ils = [okRCD]
    else:
        ckt.comment("Row & column constraints already incorporated into diagonal constraints")
        ckt.andN(okRCD, [okD])
    if zdd != Z.convert and preconstrain >= PC.diagonal:
        ckt.andN(pc, [okRCD])
    if zdd == Z.convert:
        ckt.decRefs([zokRC, zokD])
    else:
        ckt.decRefs([okRC, okD])
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information(ils)

    # Off diagonal constraints
    ckt.comment("Off-diagonal Constraints")
    for i in range(-n+1,n):
        ckt.comment("Off-diagonal %d" % i)
        out = oko.nodes[i+n-1]
        ckt.atMost1(out, offDiag(n,i), offDiag(n, i, True))
        ckt.andN(out, [out, pc])
    ckt.andN(okO, oko.nodes)
    ckt.decRefs([oko])
    if zdd == Z.convert:
        ckt.zc(zokO, okO)
        ckt.decRefs([okO])
        ils = [zokO]
    else:
        ils = [okO]
    if careful:
        ckt.comment("Forced GC")
        ckt.collect()
        ckt.status()
    if info:
        ckt.information(ils)

    if zdd == Z.convert or preconstrain < PC.diagonal:
        ckt.comment("Add off diagonal to row, column, and diagonal")
        if zdd == Z.convert:
            ckt.andN(zok, [zokRCD, zokO])
            ils = [zok]
        else:
            ckt.andN(ok, [okRCD, okO])
            ils = [ok]
    else:
        ckt.comment("Row, column, & diagonal constraints already incorporated into off-diagonal constraints")
        ckt.andN(ok, [okO])
    if zdd == Z.convert:
        ckt.decRefs([zokRCD, zokO])
    else:
        ckt.decRefs([okRCD, okO])
    ckt.decRefs([pc])
    ckt.comment("BDD generation completed")
    ckt.write("time")
    ckt.information(ils)
    ckt.comment("Model counting")
    ckt.count(ils)
    ckt.status()
    ckt.comment("Flush state")
    ckt.write("flush")
    ckt.comment("Exit")
    ckt.write("quit")

def qname(n, binary = False, careful = False, info = False, preconstrain = PC.none, zdd = Z.none):
    scnt = "%.2d" % n
    sencode = "bin" if binary else "onh"
    scare = "slow" if careful else "fast"
    sinfo = "v" if info else "q"
    sname = PC().sname(preconstrain)
    szdd = Z().suffix(zdd)
    return "q%s%s-%s-%s-%s-%s" % (szdd, scnt, sencode, scare, sinfo, sname)

def qgen(n, binary = False, careful = False, info = False, preconstrain = PC.none, zdd = Z.none):
    fname = qname(n, binary, careful, info, preconstrain, zdd) + ".cmd"
    try:
        f = open(fname, "w")
    except:
        print "Couldn't open file %d" % fname
        sys.exit(1)
    nQueens(n, f, binary, careful, info, preconstrain, zdd)

# Interleave the rows of a set of declared variables
def interleaveRows(vars, n):
    nvars = [""] * len(vars)
    m = len(vars) / n
    # Currently organized as rows 0 .. n-1, each consisting of n variables
    oddn = n % 2 == 1
    for vr in range(n):
        midn = (n-1)/2 if oddn else n/2
        bigvr = vr >= midn
        if oddn:
            if vr == midn:
                r = 0
            elif bigvr:
                r = (vr - midn - 1) * 2 + 1
            else:
                r = (midn - vr) * 2
        else:
            if bigvr:
                r = (vr - midn) * 2 + 1
            else:
                r = (midn - vr - 1) * 2
        for c in range(m):
            oidx = vr * m + c
            nidx = r * m + c
            nvars[nidx] = vars[oidx]
    return nvars

# Generate constraints for n-queens problem using layered construction
def lQueens(n, f = sys.stdout, binary = False, careful = False, info = False, zdd = Z.none, interleave = False):
    encoding = "binary" if binary else "one-hot"
    ckt = Circuit(f)
    ckt.comment("Layered construction of N-queens with %s encoding.  N = %d" % (encoding, n))
    ckt.comment("ZDD mode = %s" % Z().name(zdd))
    ckt.comment("%s of variables" % ("Interleaving" if interleave else "No interleaving"))
    okr = ckt.nameVec("okr", n)
    if binary:
        m = bigLog2(n)
        rows = [ckt.nameVec("v-%d" % r, m) for r in range(n)]
        # Do variables for each row in succession, MSB first
        vars = Vec(["v-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
        if zdd == Z.vars:
            zvars = Vec(["bv-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
            dvars = Vec(interleaveRows(zvars.nodes, n)) if interleave else zvars
            ckt.declare(dvars)
            ckt.zcV(vars, zvars)
        elif zdd == Z.avars:
            avars = Vec(["bv-%d.%d" % (i /  m, m-1- (i % m)) for i in range(m*n)])
            dvars = Vec(interleaveRows(avars.nodes, n)) if interleave else avars
            ckt.declare(dvars)
            ckt.acV(vars, avars)
        else:
            dvars = Vec(interleaveRows(vars.nodes, n)) if interleave else vars
            ckt.declare(dvars)
        ckt.comment("Individual square functions")
        for r in range(n):
            for c in range(n):
                ckt.matchVal(c, rows[r], sq(r,c))
    else:
        # Generate variables for each square
        snames = [sq(i/n, i%n) for i in range(n*n)]
        sv = Vec(snames)
        if zdd == Z.vars:
            znames = ["b" + s for s in snames]
            zv = Vec(znames)
            dvars = Vec(interleaveRows(znames, n)) if interleave else zv
            ckt.declare(dvars)
            ckt.zcV(sv, zv)
        if zdd == Z.avars:
            anames = ["b" + s for s in snames]
            av = Vec(anames)
            dvars = Vec(interleaveRows(anames, n)) if interleave else av
            ckt.declare(dvars)
            ckt.acV(sv, av)
        else:
            dvars = Vec(interleaveRows(snames, n)) if interleave else sv
            ckt.declare(dvars)
    # Row constraints
    ckt.comment("Row Constraints")
    for r in range(n):
        ckt.comment("Row %d" % r)
        ckt.exactly1(okr.nodes[r], row(n, r), row(n, r, True))

    ckt.comment("Create constraints from fictional row %d" % n)
    # Column free 0 .. n-1
    prevcfree = ckt.nameVec("cfree-%d" % n, n)
    for i in range(0, n):
        ckt.andN(prevcfree.nodes[i], [])
    # Diagonal free 1 .. n-1
    prevdfree = ckt.nameVec("dfree-%d" % n, n)
    for i in range(1, n):
        ckt.andN(prevdfree.nodes[i], [])
    # Off diagonal free 0 .. n-2
    prevofree = ckt.nameVec("ofree-%d" % n, n)
    for i in range(0, n-1):
        ckt.andN(prevofree.nodes[i], [])
    vok = ckt.nameVec("ok", n+1)
    ckt.andN(vok.nodes[n], [])
    for r in range(n-1,-1,-1):
        nrow = row(n, r, True)
        # Check conditions at this layer
        ckt.comment("Determine correctness for rows %d and above" % r)
        clear = ckt.nameVec("clear-%d" % r, n)
        ckt.andN(clear.nodes[0], [prevcfree.nodes[0], prevdfree.nodes[1]])
        for c in range(1, n-1):
            ckt.andN(clear.nodes[c], [prevofree.nodes[c-1], prevcfree.nodes[c], prevdfree.nodes[c+1]])
        ckt.andN(clear.nodes[n-1], [prevofree.nodes[n-2], prevcfree.nodes[n-1]])
        legal = ckt.nameVec("legal-%d" % r, n)
        ckt.orV(legal, [nrow, clear])
        alllegal = ckt.node("Legal-%d" % r)
        ckt.andN(alllegal, legal.nodes)
        ckt.andN(vok.nodes[r], [vok.nodes[r+1], okr.nodes[r], alllegal])

        if r > 0:
            # New constraints
            ckt.comment("Generate constraints from rows %d and above" % r)
            cfree = ckt.nameVec("cfree-%d" % r, n)
            dfree = ckt.nameVec("dfree-%d" % r, n)
            ofree = ckt.nameVec("ofree-%d" % r, n)
            for i in range(0, n):
                ckt.andN(cfree.nodes[i], [nrow.nodes[i], prevcfree.nodes[i]])
            for i in range(1, n-1):
                ckt.andN(dfree.nodes[i], [nrow.nodes[i], prevdfree.nodes[i+1]])
            ckt.andN(dfree.nodes[n-1], [nrow.nodes[n-1]])
            ckt.andN(ofree.nodes[0], [nrow.nodes[0]])
            for i in range(1, n-1):
                ckt.andN(ofree.nodes[i], [nrow.nodes[i], prevofree.nodes[i-1]])

        prevdfree.nodes = prevdfree.nodes[1:]
        prevofree.nodes = prevofree.nodes[:n-1]
        ckt.decRefs([prevcfree, prevdfree, prevofree, clear, legal, alllegal,
                     ckt.node(okr.nodes[r]), ckt.node(vok.nodes[r+1])])

        if careful:
            ckt.comment("Forced GC")
            ckt.collect()
            ckt.status()

        if r > 0:
            prevcfree = cfree
            prevdfree = dfree
            prevofree = ofree
            if info:
                ckt.information([vok.nodes[r]] + cfree.nodes + dfree.nodes[1:] + ofree.nodes[:n-1])

    ok = ckt.node("ok")
    ckt.andN(ok, [vok.nodes[0]])
    ckt.decRefs([ckt.node(vok.nodes[0])])
    ckt.comment("%s generation completed" % ("ADD" if zdd == Z.avars else "BDD" if zdd == Z.none else "ZDD"))
    ckt.write("time")
    ckt.information(["ok"])
    ckt.comment("Model counting")
    ckt.count(["ok"])
    ckt.status()
    ckt.comment("Flush state")
    ckt.write("flush")
    ckt.comment("Exit")
    ckt.write("quit")


def lqname(n, binary = False, careful = False, info = False, zdd = Z.none, interleave = False):
    scnt = "%.2d" % n
    sencode = "bin" if binary else "onh"
    scare = "slow" if careful else "fast"
    sinfo = "v" if info else "q"
    szdd = Z().suffix(zdd)
    iinfo = "i" if interleave else "l"
    return "%sq%s%s-%s-%s-%s" % (iinfo, szdd, scnt, sencode, scare, sinfo)


def lqgen(n, binary = False, careful = False, info = False, zdd = Z.none, interleave = False):
    fname = lqname(n, binary, careful, info, zdd, interleave) + ".cmd"
    try:
        f = open(fname, "w")
    except:
        print "Couldn't open file %d" % fname
        sys.exit(1)
    lQueens(n, f, binary, careful, info, zdd, interleave)

def Multiplier(n, f = sys.stdout, zdd = Z.none, reverseA = False, reverseB = False, interleave = False, check = False):
    ckt = Circuit(f)
    ckt.comment("Construction of %d x %d multiplier" % (n, n))
    ckt.comment("ZDD mode = %s" % Z().name(zdd))
    avec = ckt.nameVec("A", n)
    bvec = ckt.nameVec("B", n)
    if zdd == Z.none or zdd == Z.convert:
        davec = avec
        dbvec = bvec
    else:
        davec = ckt.nameVec("bA", n)
        dbvec = ckt.nameVec("bB", n)
    if reverseA:
        davec = davec.reverse()
    if reverseB:
        dbvec = dbvec.reverse()
    dvec = davec.interleave(dbvec) if interleave else Vec(davec.nodes + dbvec.nodes)
    ckt.declare(dvec)
    if zdd == Z.vars:
        ckt.zcV(avec, davec)
        ckt.zcV(bvec, dbvec)
    elif zdd == Z.avars:
        ckt.acV(avec, davec)
        ckt.acV(bvec, dbvec)
    outvec = ckt.nameVec("out", n+n)
    outcvec = ckt.nameVec("outc", n+n) if zdd == Z.convert else outvec
    ckt.multiplier(outcvec, avec, bvec)
    if zdd == Z.convert:
        ckt.zcV(outvec, outcvec)
        ckt.decRefs([outcvec])
    ckt.comment("%s generation completed" % ("ADD" if zdd == Z.avars else "BDD" if zdd == Z.none else "ZDD"))
    ckt.write("time")
    ckt.information(outvec.nodes)
    if check:
        checkvec = ckt.nameVec("cout", n+n)
        ckt.multblast(checkvec, avec, bvec)
        ckt.cmdSequence("equal", [outvec, checkvec])
    ckt.status()
    ckt.comment("Flush state")
    ckt.write("flush")
    ckt.comment("Exit")
    ckt.write("quit")


    
