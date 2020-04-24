# Library of routines for generating Boolean expression evaluations scripts
# Assume constants one & zero are named "one" and "zero"

import random
import sys


# Class to define use of ZDDs
class Z:
    none, vars, convert, avars = list(range(4))
    names = ["none", "vars", "convert", "avars"]
    suffixes = ["b", "v", "z", "a"]

    def name(self, id):
        return self.names[id]

    def suffix(self, id):
        return self.suffixes[id]

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
        self.name = str(name)
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
        self.nodes = [str(n) for n in nodeList]
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
            short, long  = self.nodes, other.nodes
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
    zero = None
    one = None
    
    def __init__(self, outfile = sys.stdout):
        self.outfile = outfile
        self.nodes = set()
        self.vecs = set()
        self.uniq = Uniq()
        self.zero = Node('zero')
        self.one = Node('one')

    def addVec(self, v):
        self.vecs.add(v)
        return v

    def addNode(self, nd):
        self.nodes.add(nd)
        return nd

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

    def vec(self, nodeList):
        v = Vec(nodeList)
        return self.addVec(v)

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
            ls = list(map(str, obj))
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

    def satisfy(self, fv):
        self.cmdLine("satisfy", fv)

    def store(self, node, fname):
        self.cmdLine("store", [node, fname])

    def load(self, node, fname):
        self.cmdLine("load", [node, fname])

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

    def conjunctN(self, dest, argList, randomize = False, quantify = False):
        if quantify:
            ls = ['-q', dest]
        else:
            ls = [dest]
        if randomize:
            args = list(argList)
            random.shuffle(args)
        else:
            args = argList
        ls.extend(args)
        self.cmdLine("conjunct", ls)

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
        
    def equantN(self, dest, fun, argList):
        ls = [dest, fun]
        ls.extend(argList)
        self.cmdLine("equant", ls)

    def uquantN(self, dest, fun, argList):
        ls = [dest, fun]
        ls.extend(argList)
        self.cmdLine("uquant", ls)

    def assignConstant(self, dest, val):
        if val == 1:
            self.andN(dest, [])
        elif val == 0:
            self.orN(dest, [])
        else:
            self.comment("Couldn't assign value %s to node %s" % (val, dest)) 

    def checkConstant(self, dest, val):
        cnode = None
        if val == 1:
            cnode = self.one
        elif val == 0:
            cnode = self.zero
        if cnode is None:
            self.comment("Couldn't check that node %s = %d" % (dest, val))
            return
        self.cmdLine("equal", [dest, cnode])


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
    def count01(self, c0, c1, v, nv, omitLast = False):
        n = len(v.nodes)
        self.assignConstant(c0, 1)
        self.assignConstant(c1, 0)
        for i in range(n):
            name = v.nodes[i]
            nname = nv.nodes[i]
            self.iteN(c1, [name, c0, c1])
            if not (omitLast and i == n-1):
                self.andN(c0, [c0, nname])

    # Is at most one signal in vector equal to 1?
    def exactly1(self, dest, v, nv):
        c0 = self.tmpNode()
        self.count01(c0, dest, v, nv, omitLast = True)
        self.decRefs([c0])

    # Is at most one signal in vector equal to 1?
    def atMost1(self, dest, v, nv):
        c0 = self.tmpNode()
        c1 = self.tmpNode()
        self.count01(c0, c1, v, nv)
        self.orN(dest, [c0, c1])
        self.decRefs([c0, c1])

    # Create counting network for values from 0 up to k
    # Generates destV[l] encodes case where count = l
    # where k = len(destV)-1
    def countGeneratorVector(self, destV, v, nv):
        k = len(destV) - 1
        n = len(v.nodes)
        t = self.tmpVec(k+1)
        self.assignConstant(t[0], 1)
        for l in range(1,k+1):
            self.assignConstant(t[l], 0)
        for i in range(n-1):
            name = v.nodes[i]
            nname = nv.nodes[i]        
            for l in range(k,0,-1):
                self.iteN(t[l], [name, t[l-1], t[l]])
            self.andN(t[0], [t[0], nname])
        name = v.nodes[n-1]
        nname = nv.nodes[n-1]
        for l in range(k,0,-1):
            self.iteN(destV[l], [name, t[l-1], t[l]])
        self.andN(destV[0], [t[0], nname])
        self.decRefs([t])

    def atMostK(self, dest, v, nv, k):
        t = self.tmpVec(k+1)
        self.countGeneratorVector(t, v, nv)
        self.orN(dest, t)
        self.decRefs([t])

    # Count must match True case in True/False list
    def okList(self, dest, v, nv, list):
        n = len(list)
        t = self.tmpVec(n)
        self.countGeneratorVector(t, v, nv)
        tlist = [t.nodes[i] for i in range(n) if list[i]]
        st = self.vec(tlist)
        self.orN(dest, st)
        self.decRefs([st])

    # Create counting network for value k
    # Generates dest encodes case where count = k
    def exactlyK(self, dest, v, nv, k):
        n = len(v.nodes)
        t = self.tmpVec(k+1)
        self.assignConstant(t[0], 1)
        for l in range(1,k+1):
            self.assignConstant(t[l], 0)
        for i in range(n-1):
            # Some of these could be optimized out
            name = v.nodes[i]
            nname = nv.nodes[i]        
            for l in range(k,0,-1):
                self.iteN(t[l], [name, t[l-1], t[l]])
            self.andN(t[0], [t[0], nname])
        name = v.nodes[n-1]
        self.iteN(dest, [name, t[k-1], t[k]])
        self.decRefs([t])

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


    
