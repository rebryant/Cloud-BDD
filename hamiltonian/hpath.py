#!/usr/bin/python

# Generate formula encoding all Hamiltonian paths in a graph

import sys
import circuit
import getopt


def usage(name):
    print("Usage: %s [-h] [-M] [-P] [-S] [-Z] [-R] [-r ROWS] [-c COLS] [-s R:C] [-t R:C] [-o OUT]" % name)
    print("  -h      Print this information")
    print("  -M      Use Mesh graph, rather than Knight's move graph")
    print("  -P      Use position-major ordering of variables")
    print("  -S      Enumerate satisfying solutions")
    print("  -Z      Use ZDDs")
    print("  -R      Recursively divide space and time")
    print("  -r ROWS Specify number of rows (default = 8)")
    print("  -c COLS Specify number of cols (default = number of rows)")
    print("  -s R:C  Specify source node")
    print("  -t R:C  Specify sink node")
    print("  -o OUT  Output file")

def inclusiveRange(nmin, nmax):
    return range(nmin, nmax+1)

def unitRange(n):
    return inclusiveRange(1,n)


class Vertex:
    id = 0
    name = ""

    def __init__(self, id, name = None):
        self.id = id
        if name is None:
            self.id = 'V' + str(id)
        else:
            self.name = name
            
    def __str__(self):
        return self.name

    def __eq__(self, other):
        return self.id == other.id

    def __hash__(self):
        return hash(self.id)

class Edge:
    source = None
    destination = None

    def __init__(self, source, dest):
        self.source = source
        self.destination = dest

    def __str__(self):
        return "(%s,%s)" % (str(self.source), str(self.destination))

class GraphException(Exception):

    def __init__(self, value):
        self.value = value

    def __str__(self):
        return "Hamiltonian Path Exception: " + str(self.value)

# Graph for Hamiltonian path computations
class Graph:
    ckt = None
    vertices = []
    edges = []
    outEdges = {}
    inEdges = {}
    # Mapping from (v,t) to variable
    variableDict = {}
    # Used to generate unique Ids for clusters
    nextId = 1
    sourceVertex = None
    sinkVertex = None
    graphDescription = None

    def __init__(self, ckt=None):
        if ckt is None:
            self.ckt = circuit.Circuit()
        else:
            self.ckt = ckt
        self.vertices = []
        self.edges = []
        self.outEdges = {}
        self.inEdges = {}
        self.nextId = 1

    def addVertex(self, v):
        self.vertices.append(v)
        self.outEdges[v] = []
        self.inEdges[v] = []

    # Assume source and destination are already in self.vertices
    def addEdge(self, e):
        self.edges.append(e)
        self.outEdges[e.source].append(e.destination)
        self.inEdges[e.destination].append(e.source)

    def declareVariables(self, sourceVertex, sinkVertex, isZdd = False, positionMajor = False):
        if self.graphDescription is not None:
            self.ckt.comment("Graph %s" % self.graphDescription)
        self.ckt.comment("Source %s" % (str(sourceVertex)))
        self.ckt.comment("Sink %s" % (str(sinkVertex)))
        N = len(self.vertices)
        self.sourceVertex = sourceVertex
        self.sinkVertex = sinkVertex
        for v in self.vertices:
            self.variableDict[(v,1)] = "one" if v == sourceVertex else "zero"
            self.variableDict[(v,N)] = "one" if v == sinkVertex else "zero"
        abnames = []
        aznames = []
        if positionMajor:
            self.ckt.comment("Declaring variables in position-major order")
            for v in self.vertices:
                if v == sourceVertex or v == sinkVertex:
                    self.variableDict[(v,t)] = "zero"
                    continue
                bnames = []
                znames = []
                for t in inclusiveRange(2, N-1):
                    root = "%s_S%.2d" % (v.name, t)
                    if isZdd:
                        b = "B" + root
                        z = "V" + root
                        bnames.append(b)
                        znames.append(z)
                        self.variableDict[(v,t)] = z
                    else:
                        b = "V" + root
                        bnames.append(b)
                        self.variableDict[(v,t)] = b
                self.ckt.declare(bnames)
                if isZdd:
                    abnames += bnames
                    aznames += znames
        else:
            self.ckt.comment("Declaring variables in step-major order")
            for t in inclusiveRange(2, N-1):
                bnames = []
                znames = []
                for v in self.vertices:
                    if v == sourceVertex or v == sinkVertex:
                        self.variableDict[(v,t)] = "zero"
                        continue
                    root = "%s-S%.2d" % (v.name, t)
                    if isZdd:
                        b = "B" + root
                        z = "V" + root
                        bnames.append(b)
                        znames.append(z)
                        self.variableDict[(v,t)] = z
                    else:
                        b = "V" + root
                        bnames.append(b)
                        self.variableDict[(v,t)] = b
                self.ckt.declare(bnames)
                if isZdd:
                    abnames += bnames
                    aznames += znames
        if isZdd:
            zvec = self.ckt.vec(aznames)
            bvec = self.ckt.vec(abnames)
            self.ckt.zcV(zvec, bvec)

    def splitList(self, ls):
        n = len(ls)
        if len(ls) == 1:
            return ls
        m = n//2
        left = self.splitList(ls[:m])
        right = self.splitList(ls[m:])
        return [left, right]

    # Reorder nodes into binary tree
    def splittingTree(self):
        return self.splitList(self.vertices)
        
    def leafCount(self, tree):
        if len(tree) == 1:
            return 1
        count = 0
        for subtree in tree:
            count += self.leafCount(subtree)
        return count

    def assignId(self):
        id = self.nextId
        self.nextId += 1
        return id

    # Generate formula by splitting both time and space
    def generateFormulaTS(self, tree = None, stepMin = None, stepMax = None):
        if tree is None:
            tree = self.splittingTree()
            self.nextId = 1
        if stepMin is None:
            stepMin = 1
        if stepMax is None:
            stepMax = len(self.vertices)
        vcount = self.leafCount(tree)
        scount = stepMax-stepMin+1
        if vcount >= scount:
            if vcount == 1:
                vertex = tree[0]
                isSource = vertex == self.sourceVertex
                isSink = vertex == self.sinkVertex
                cluster = Cluster(self, self.assignId())
                cluster.unitCluster(vertex, stepMin)
                return cluster
            else:
                # Spatial split
                c1 = self.generateFormulaTS(tree[0], stepMin, stepMax)
                c2 = self.generateFormulaTS(tree[1], stepMin, stepMax)
                nc = c1.join(c2, self.assignId())
                c1.flush()
                c2.flush()
                return nc
        else:
            stepMid = (stepMin + stepMax) // 2
            c1 = self.generateFormulaTS(tree, stepMin, stepMid)
            c2 = self.generateFormulaTS(tree, stepMid+1, stepMax)
            nc = c1.join(c2, self.assignId())
            c1.flush()
            c2.flush()
            return nc

    # Generate formula stepwise
    def generateFormulaT(self):
        tree = self.splittingTree()
        self.nextId = 1
        cluster = self.generateFormulaTS(tree, 1, 1)

        for step in inclusiveRange(2, len(self.vertices)):
            sc = self.generateFormulaTS(tree, step, step)
            self.ckt.collect()
            nc = cluster.join(sc, self.assignId())
            cluster.flush()
            sc.flush()
            self.ckt.collect()
            cluster = nc

        return cluster
        
    # Final step.  Variable cluster is top-level cluster
    def wrapup(self, cluster, showSolutions = False):
        vec = [cluster.okFormula]
        self.ckt.status()
        self.ckt.information(vec)
        self.ckt.count(vec)
        if showSolutions:
            self.ckt.satisfy(vec)

# Grid consisting of Array of nodes with mesh connections
class MeshGraph(Graph):
    rows = 8
    columns = 8
    # Dictionary mapping (r,c) to vertex
    vertexDict = {}

    def __init__(self, ckt = None, rows = 8, columns = None):
        Graph.__init__(self, ckt)
        self.vertexDict = {}
        self.rows = rows
        if columns is None:
            self.columns = rows
        else:
            self.columns = columns
        self.graphDescription = "Mesh %.2d X %.2d" % (rows, columns)
        # Generate vertices
        for r in unitRange(self.rows):
            for c in unitRange(self.columns):
                id = c + (r-1)*self.columns
                v = Vertex(id, "R%.2dC%.2d" % (r,c))
                self.addVertex(v)
                self.vertexDict[(r,c)] = v

        self.addEdges()
                        
    def getVertex(self, row, column):
        if (row,column) in self.vertexDict:
            return self.vertexDict[(row,column)]
        else:
            return None

    def addEdges(self):
        # Generate edges
        for r in unitRange(self.rows):
            for c in unitRange(self.columns):
                v = self.vertexDict[(r,c)]
                for dr in [-1,0,1]:
                    for dc in [-1,0,1]:
                        if abs(dr) + abs(dc) != 1:
                            continue
                        nr = r+dr
                        nc = c+dc
                        if nr < 1 or nr > self.rows:
                            continue
                        if nc < 1 or nc > self.columns:
                            continue
                        nv = self.vertexDict[(nr,nc)]
                        e = Edge(nv,v)
                        self.addEdge(e)


    def split(self, rmin, rmax, cmin, cmax):
        rcount = rmax-rmin+1
        ccount = cmax-cmin+1
        if rcount >= ccount:
            if rcount == 1:
                return [self.vertexDict[(rmin,cmin)]]
            else:
                rmid = (rmin+rmax)//2
                top =    self.split(rmin,   rmid, cmin, cmax)
                bottom = self.split(rmid+1, rmax, cmin, cmax)
                return [top, bottom]
        else:
            cmid = (cmin+cmax)//2
            left =  self.split(rmin, rmax, cmin,   cmid)
            right = self.split(rmin, rmax, cmid+1, cmax)
            return [left, right]
                
    def splittingTree(self):
        return self.split(1, self.rows, 1, self.columns)

# Knight's graph is like a mesh, except for edges
class KnightGraph(MeshGraph):

    def __init__(self, ckt = None, rows = 8, columns = None):
        MeshGraph.__init__(self, ckt, rows, columns)
        self.graphDescription = "Knight %.2d X %.2c" % (rows, columns)
                        
    def addEdges(self):
        # Generate edges
        for r in unitRange(self.rows):
            for c in unitRange(self.columns):
                v = self.vertexDict[(r,c)]
                for dr in [-2,-1,1,2]:
                    for dc in [-2,-1,1,2]:
                        if abs(dr) == abs(dc):
                            continue
                        nr = r+dr
                        nc = c+dc
                        if nr < 1 or nr > self.rows:
                            continue
                        if nc < 1 or nc > self.columns:
                            continue
                        nv = self.vertexDict[(nr,nc)]
                        e = Edge(nv,v)
                        self.addEdge(e)

    
    
# Subformulas representing Hamiltonian path computation in portion of graph for portion of steps
class Cluster:
    id = ""
    graph = None
    stepMin = 0
    stepMax = 0
    vertexSet = {}
    okFormula = None
    vertexFormulaDict = {}
    stepFormulaDict = {}
    isUnit = False

    def __init__(self, graph, id, stepMin = None, stepMax = None, vertexSet = None):
        self.graph = graph
        self.id = "%.3d" % id if type(id) == type(1) else str(id)
        self.stepMin = 0 if stepMin is None else stepMin
        self.stepMax = 0 if stepMax is None else stepMax 
        self.vertexSet = {} if vertexSet is None else vertexSet 
        self.okFormula = None
        self.vertexFormulaDict = {}
        self.stepFormulaDict = {}

    def __str__(self):
        return 'C' + self.id

    def isUnit(self):
        return self.stepMin == self.stepMax and len(self.vertexSet) == 1

    def stepSpanning(self):
        return self.stepMin == 1 and self.stepMax == len(self.graph.vertices)

    def vertexSpanning(self):
        return len(self.vertexSet) == len(self.graph.vertices)


    def unitCluster(self, vertex, step):
        isSource = vertex == self.graph.sourceVertex
        isSink = vertex == self.graph.sinkVertex
        N = len(self.graph.vertices)
        self.graph.ckt.comment("Creating unit cluster %s.  Vertex = %s, Step = %.2d" % (str(self), str(vertex), step))
        self.stepMin = step
        self.stepMax = step
        self.vertexSet = {vertex}
        self.okFormula = "OK" if self.stepSpanning() and self.vertexSpanning() else "OK_" + str(self)
        localVariable = self.graph.variableDict[(vertex,step)]
        vformula = "Vertex_occupied_" + str(self) + "_S%.2d" % step
        self.vertexFormulaDict[step] = vformula
        sformula = "Step_occupied_" + str(self) + '_' + str(vertex)
        self.stepFormulaDict[vertex] = sformula
        if step == 1:
            self.graph.ckt.assignConstant(vformula, 1 if isSource else 0)
            self.graph.ckt.assignConstant(sformula, 1 if isSource else 0)
        elif step == N:
            self.graph.ckt.assignConstant(vformula, 1 if isSink else 0)
            self.graph.ckt.assignConstant(sformula, 1 if isSink else 0)
        elif isSource or isSink:
            self.graph.ckt.assignConstant(vformula, 0)
            self.graph.ckt.assignConstant(sformula, 0)
        else:
            self.graph.ckt.andN(vformula, [localVariable])
            self.graph.ckt.andN(sformula, [localVariable])
        if step == 1:
            self.graph.ckt.assignConstant(self.okFormula, 1)
        else:
            otherVariables = [self.graph.variableDict[(ov,step-1)] for ov in self.graph.inEdges[vertex]]
            vlist = ["!" + localVariable] + otherVariables
            self.graph.ckt.orN(self.okFormula, vlist)

    # Same vertices, consecutive time ranges
    def temporalJoin(self, other, newId):
        nstepMin = min(self.stepMin, other.stepMin)
        nstepMax = max(self.stepMax, other.stepMax)
        nvertexSet = set(self.vertexSet)
        ncluster = Cluster(self.graph, newId, nstepMin, nstepMax, nvertexSet)
        self.graph.ckt.comment("Cluster %s: temporal join of clusters %s and %s.  Steps [%.2d..%.2d]" % (str(ncluster), str(self), str(other), nstepMin, nstepMax))
        vlist = sorted([str(v) for v in nvertexSet])
        self.graph.ckt.comment("Cluster vertices: %s" % ", ".join(vlist))
        stepSpanning = ncluster.stepSpanning()
        ncluster.vertexFormulaDict = {}
        ncluster.stepFormulaDict = {}
        ilist = []


        if not self.vertexSpanning():
            for step in inclusiveRange(self.stepMin, self.stepMax):
                vformula = "Vertex_occupied_" + str(ncluster) + "_S%.2d" % step
                ncluster.vertexFormulaDict[step] = vformula
                self.graph.ckt.andN(vformula, [self.vertexFormulaDict[step]])
                ilist.append(vformula)
            for step in inclusiveRange(other.stepMin, other.stepMax):
                vformula = "Vertex_occupied_" + str(ncluster) + "_S%.2d" % step
                ncluster.vertexFormulaDict[step] = vformula
                self.graph.ckt.andN(vformula, [other.vertexFormulaDict[step]])
                ilist.append(vformula)

        vlist = []
        for vertex in nvertexSet:
            sformula = "Step_occupied_" + str(ncluster) + '_' + str(vertex)
            vlist.append(sformula)
            ncluster.stepFormulaDict[vertex] = sformula
            self.graph.ckt.orN(sformula, [self.stepFormulaDict[vertex], other.stepFormulaDict[vertex]])
            if not stepSpanning:
                ilist.append(sformula)

        ncluster.okFormula = "OK" if stepSpanning and ncluster.vertexSpanning() else "OK_" + str(ncluster)
        ilist.append(ncluster.okFormula)
        tvec = self.graph.ckt.tmpVec(len(ncluster.vertexSet))
        nvec = self.graph.ckt.tmpVec(len(ncluster.vertexSet))
        avec = self.graph.ckt.vec([self.stepFormulaDict[vertex] for vertex in ncluster.vertexSet])
        ovec = self.graph.ckt.vec([other.stepFormulaDict[vertex] for vertex in ncluster.vertexSet])
        self.graph.ckt.andV(tvec, [avec, ovec])
        self.graph.ckt.notV(nvec, tvec)
        clist = [self.okFormula, other.okFormula] + nvec.nodes
        if stepSpanning:
            clist += vlist
        rvec = self.graph.ckt.andN(ncluster.okFormula, clist)
        self.graph.ckt.decRefs([tvec, nvec])
        if stepSpanning:
            vec = self.graph.ckt.vec(vlist)
            self.graph.ckt.decRefs([vec])
            for vertex in nvertexSet:
                ncluster.stepFormulaDict[vertex] = None
        self.graph.ckt.information(ilist)
        return ncluster

    # Different vertices, Same time ranges
    def spatialJoin(self, other, newId):
        nstepMin = self.stepMin
        nstepMax = self.stepMax
        nvertexSet = self.vertexSet | other.vertexSet
        ncluster = Cluster(self.graph, newId, nstepMin, nstepMax, nvertexSet)
        self.graph.ckt.comment("Cluster %s: spatial join of clusters %s and %s.  Steps [%.2d..%.2d]" % (str(ncluster), str(self), str(other), nstepMin, nstepMax))
        vlist = sorted([str(v) for v in nvertexSet])
        self.graph.ckt.comment("Cluster vertices: %s" % ", ".join(vlist))
        vertexSpanning = ncluster.vertexSpanning()
        ncluster.vertexFormulaDict = {}
        ncluster.stepFormulaDict = {}

        ilist = []
        slist = []
        for step in inclusiveRange(nstepMin, nstepMax):
            vformula = "Vertex_occupied_" + str(ncluster) + "_S%.2d" % step
            slist.append(vformula)
            ncluster.vertexFormulaDict[step] = vformula
            self.graph.ckt.orN(vformula, [self.vertexFormulaDict[step], other.vertexFormulaDict[step]])
            if not vertexSpanning:
                ilist.append(vformula)
        if not self.stepSpanning():
            for vertex in self.vertexSet:
                sformula = "Step_occupied_" + str(ncluster) + '_' + str(vertex)
                ncluster.stepFormulaDict[vertex] = sformula
                self.graph.ckt.andN(sformula, [self.stepFormulaDict[vertex]])
                ilist.append(sformula)
            for vertex in other.vertexSet:
                sformula = "Step_occupied_" + str(ncluster) + '_' + str(vertex)
                ncluster.stepFormulaDict[vertex] = sformula
                self.graph.ckt.andN(sformula, [other.stepFormulaDict[vertex]])
                ilist.append(sformula)

        ncluster.okFormula = "OK" if ncluster.stepSpanning() and vertexSpanning else "OK_" + str(ncluster)
        ilist.append(ncluster.okFormula)
        tvec = self.graph.ckt.tmpVec(nstepMax-nstepMin+1)
        nvec = self.graph.ckt.tmpVec(nstepMax-nstepMin+1)
        avec = self.graph.ckt.vec([self.vertexFormulaDict[step] for step in inclusiveRange(nstepMin, nstepMax)])
        ovec = self.graph.ckt.vec([other.vertexFormulaDict[step] for step in inclusiveRange(nstepMin, nstepMax)])
        self.graph.ckt.andV(tvec, [avec, ovec])
        self.graph.ckt.notV(nvec, tvec)
        clist = [self.okFormula, other.okFormula] + nvec.nodes
        if vertexSpanning:
            clist += slist
        rvec = self.graph.ckt.andN(ncluster.okFormula, clist)
        self.graph.ckt.decRefs([tvec, nvec])
        if vertexSpanning:
            vec = self.graph.ckt.vec(slist)
            self.graph.ckt.decRefs([vec])
            for step in inclusiveRange(nstepMin, nstepMax):
                ncluster.vertexFormulaDict[step] = None
        self.graph.ckt.information(ilist)
        return ncluster
    
    def join(self, other, newId):
        if self.stepMin == other.stepMin and self.stepMax == other.stepMax:
            overlap = self.vertexSet & other.vertexSet
            if len(overlap) > 0:
                ostring = ", ".join(sorted([str(v) for v in overlap]))
                msg = "Invalid spatial join %s + %s.  Vertex sets overlap: {%s}" % (str(self), str(other), ostring)
                raise GraphException(msg)
            return self.spatialJoin(other, newId)
        elif self.vertexSet == other.vertexSet:
            if self.stepMin == other.stepMax + 1 or other.stepMin == self.stepMax + 1:
                return self.temporalJoin(other, newId)
            else:
                msg = "Invalid temporal join %s (Steps %.2d--%.2d) + %s (Steps %.2d--%.2d)" % (str(self), self.stepMin, self.stepMax, str(other), other.stepMin, other.stepMax)
                raise GraphException(msg)
        else:
            msg = "Invalid join %s + %s.  Does not qualify as temporal or spatial join" % (str(self), str(other))
            raise GraphException(msg)

    def flush(self):
        vlist = [self.okFormula] 
        vlist += [v for v in self.vertexFormulaDict.values()]
        vlist += [v for v in self.stepFormulaDict.values()]
        vlist = [v for v in vlist if v is not None]
        vec = self.graph.ckt.vec(vlist)
        self.graph.ckt.decRefs([vec])
                
        
def run(name, args):
    rows = 8
    columns = None
    meshGraph = False
    positionMajor = False
    isZdd = False
    showSolutions = False
    outfile = sys.stdout
    sourceRC = (1,1)
    sinkRC = (1,1)
    recursive = False
    
    optlist, args = getopt.getopt(args, "hMPZSRr:c:s:t:o:")
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-M':
            meshGraph = True
        elif opt == '-R':
            recursive = True
        elif opt == '-P':
            positionMajor = True
        elif opt == '-Z':
            isZdd = True
        elif opt == '-S':
            showSolutions = True
        elif opt == '-r':
            rows = int(val)
        elif opt == '-c':
            columns = int(val)
        elif opt == '-s':
            fields = val.split(":")
            if len(fields) != 2:
                print("Invalid source specification '%s'" % val)
                usage(name)
                return
            try:
                sourceRC = tuple(int(s) for s in fields)
            except:
                print("Invalid source specification '%s'" % val)
                usage(name)
                return
        elif opt == '-t':
            fields = val.split(":")
            if len(fields) != 2:
                print("Invalid sink specification '%s'" % val)
                usage(name)
                return
            try:
                sinkRC = tuple(int(s) for s in fields)
            except:
                print("Invalid sink specification '%s'" % val)
                usage(name)
                return
        elif opt == '-o':
            try:
                outfile = open(val, 'w')
            except:
                print("Couldn't open output file '%s'" % val)
                return

    if columns is None:
        columns = rows

    ckt = circuit.Circuit(outfile = outfile)
    graph = MeshGraph(ckt, rows, columns) if meshGraph else KnightGraph(ckt, rows, columns)
    source = graph.getVertex(sourceRC[0], sourceRC[1])
    if source is None:
        print("Invalid source %s" % str(sourceRC))
        return
    sink = graph.getVertex(sinkRC[0], sinkRC[1])
    if sink is None:
        print("Invalid sink %s" % str(sinkRC))
        return
    graph.declareVariables(source, sink, isZdd = isZdd, positionMajor = positionMajor)
    cluster = graph.generateFormulaTS() if recursive else graph.generateFormulaT()
    graph.wrapup(cluster, showSolutions)

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
    


        
        
    
