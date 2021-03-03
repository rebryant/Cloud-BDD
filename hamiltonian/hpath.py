# Generate formula encoding all Hamiltonian paths in a graph

import circuit

def unitRange(n):
    return range(1,n+1)

def inclusiveRange(nmin, nmax):
    return range(nmin, nmax+1)

class Vertex:
    id = 0
    name = ""

    def __init__(self, id, name = None):
        self.id = id
        if name is None:
            self.id = 'N' + str(id)
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

# Graph for Hamiltonian path computations
class Graph:
    ckt = None
    vertices = []
    edges = []
    outEdges = {}
    inEdges = {}
    # Mapping from (v,t) to variable
    variableDict = {}

    def __init__(self, ckt=None):
        if ckt is None:
            self.ckt = circuit.Circuit()
        else:
            self.ckt = ckt
        self.vertices = []
        self.edges = []
        self.outEdges = {}
        self.inEdges = {}

    def addVertex(self, v):
        self.vertices.append(v)
        self.outEdges[v] = []
        self.inEdges[v] = []

    # Assume source and destination are already in self.vertices
    def addEdge(self, e):
        self.edges.append(e)
        self.outEdges[e.source].append(e.destination)
        self.inEdges[e.destination].append(e.source)

    def declareVariables(self, isZdd = False, positionMajor = False):
        if positionMajor:
            for v in self.vertices:
                bnames = []
                znames = []
                for t in unitRange(len(self.vertices)):
                    root = "%s-%.2d" % (v.name, t)
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
                    zvec = self.ckt.vec(znames)
                    bvec = self.ckt.vec(bnames)
                    self.ckt.zcV(zvec, bvec)
        else:
            for t in unitRange(len(self.vertices)):
                bnames = []
                znames = []
                for v in self.vertices:
                    root = "%s-%.2d" % (v.name, t)
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
                    zvec = self.ckt.vec(znames)
                    bvec = self.ckt.vec(bnames)
                    self.ckt.zcV(zvec, bvec)

class KnightGraph(Graph):
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
        # Generate vertices
        for r in unitRange(self.rows):
            for c in unitRange(self.columns):
                id = c + (r-1)*self.columns
                v = Vertex(id, "%.2d-%.2d" % (r,c))
                self.addVertex(v)
                self.vertexDict[(r,c)] = v

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

    def __str__(self):
        return str(self.id)

    def isUnit(self):
        return self.stepMin == self.stepMax and len(self.vertexSet) == 1

    def __init__(self, graph, id, stepMin = None, stepMax = None, vertexSet = None):
        self.graph = graph
        self.id = str(id)
        self.stepMin = 0 if stepMin is None else stepMin
        self.stepMax = 0 if stepMax is None else stepMax 
        self.vertexSet = set([]) if vertexSet is None else vertexSet 
        self.okFormula = None
        self.vertexFormulaDict = {}
        self.stepFormulaDict = {}

    def unitCluster(self, vertex, step, isSource = False, isSink = False):
        self.stepMin = step
        self.stepMax = step
        self.vertexSet = {vertex}
        self.okFormula = "ok_" + self.id
        localVariable = self.graph.variableDict[(vertex,step)]
        if step == 1:
            self.graph.ckt.assignConstant(self.okFormula, 1)
        else:
            otherVariables = [self.graph.variableDict[(ov,step-1)] for ov in self.graph.inEdges[vertex]]
            vlist = ["!" + localVariable] + otherVariables
            self.graph.ckt.orN(self.okFormula, vlist)
        vformula = "vertex_occupied_%s_%.2d" % (self.id, step)
        self.vertexFormulaDict[step] = vformula
        sformula = "step_occupied_%s_%s" % (self.id, str(vertex))
        self.stepFormulaDict[vertex] = sformula
        if isSource and step == 1:
            self.graph.ckt.assignConstant(vformula, 1)
            self.graph.ckt.assignConstant(sformula, 1)
        elif isSink and step == len(self.graph.vertices):
            self.graph.ckt.assignConstant(vformula, 1)
            self.graph.ckt.assignConstant(sformula, 1)
        elif isSource or isSink:
            self.graph.ckt.assignConstant(vformula, 0)
            self.graph.ckt.assignConstant(sformula, 0)
        else:
            self.graph.ckt.andN(vformula, [localVariable])
            self.graph.ckt.andN(sformula, [localVariable])

    # Same vertices, consecutive time ranges
    def temporalJoin(self, other, newId):
        nstepMin = min(self.stepMin, other.stepMin)
        nstepMax = max(self.stepMax, other.stepMax)
        nvertexSet = set(self.vertexSet)
        ncluster = Cluster(self.graph, newId, nstepMin, nstepMax, nvertexSet)
        ncluster.vertexFormulaDict = {}
        ncluster.stepFormulaDict = {}
        ncluster.okFormula = "ok_" + self.id
        tvec = self.graph.ckt.tmpVec(len(ncluster.vertexSet))
        nvec = self.graph.ckt.tmpVec(len(ncluster.vertexSet))
        avec = self.graph.ckt.vec([self.stepFormulaDict[vertex] for vertex in ncluster.vertexSet])
        ovec = self.graph.ckt.vec([other.stepFormulaDict[vertex] for vertex in ncluster.vertexSet])
        self.graph.ckt.andV(tvec, [avec, ovec])
        self.graph.ckt.notV(nvec, tvec)
        rvec = self.graph.ckt.andN(ncluster.okFormula, [self.okFormula, other.okFormula] + nvec.nodes)
        self.graph.ckt.decRefs([tvec, nvec])

        for step in inclusiveRange(self.stepMin, self.stepMax):
            vformula = "vertex_occupied_%s_%.2d" % (ncluster.id, step)
            ncluster.vertexFormulaDict[step] = vformula
            self.graph.ckt.andN(vformula, [self.vertexFormulaDict[step]])
        for step in inclusiveRange(other.stepMin, other.stepMax):
            vformula = "vertex_occupied_%s_%.2d" % (ncluster.id, step)
            ncluster.vertexFormulaDict[step] = vformula
            self.graph.ckt.andN(vformula, [other.vertexFormulaDict[step]])

        for vertex in nvertexSet:
            sformula = "step_occupied_%s_%s" % (ncluster.id, str(vertex))
            ncluster.stepFormulaDict[vertex] = sformula
            self.graph.ckt.orN(sformula, [self.stepFormulaDict[vertex], other.stepFormulaDict[vertex]])

        return ncluster

    # Different vertices, Same time ranges
    def spatialJoin(self, other, newId):
        nstepMin = self.stepMin
        nstepMax = self.stepMax
        nvertexSet = self.vertexSet | other.vertexSet
        ncluster = Cluster(self.graph, newId, nstepMin, nstepMax, nvertexSet)
        ncluster.vertexFormulaDict = {}
        ncluster.stepFormulaDict = {}
        ncluster.okFormula = "ok_" + self.id
        tvec = self.graph.ckt.tmpVec(nstepMax-nstepMin+1)
        nvec = self.graph.ckt.tmpVec(nstepMax-nstepMin+1)
        avec = self.graph.ckt.vec([self.vertexFormulaDict[step] for step in inclusiveRange(nstepMin, nstepMax)])
        ovec = self.graph.ckt.vec([other.vertexFormulaDict[step] for step in inclusiveRange(nstepMin, nstepMax)])
        self.graph.ckt.andV(tvec, [avec, ovec])
        self.graph.ckt.notV(nvec, tvec)
        rvec = self.graph.ckt.andN(ncluster.okFormula, [self.okFormula, other.okFormula] + nvec.nodes)
        self.graph.ckt.decRefs([tvec, nvec])

        for step in inclusiveRange(nstepMin, nstepMax):
            vformula = "vertex_occupied_%s_%.2d" % (ncluster.id, step)
            ncluster.vertexFormulaDict[step] = vformula
            self.graph.ckt.orN(vformula, [self.vertexFormulaDict[step], other.vertexFormulaDict[step]])

        for vertex in self.vertexSet:
            sformula = "step_occupied_%s_%s" % (ncluster.id, str(vertex))
            ncluster.stepFormulaDict[vertex] = sformula
            self.graph.ckt.andN(sformula, [self.stepFormulaDict[vertex]])
        for vertex in other.vertexSet:
            sformula = "step_occupied_%s_%s" % (ncluster.id, str(vertex))
            ncluster.stepFormulaDict[vertex] = sformula
            self.graph.ckt.andN(sformula, [other.stepFormulaDict[vertex]])

        return ncluster
    
    def flush(self):
        vlist = [self.okFormula] 
        vlist += [self.vertexFormulaDict[step] for step in inclusiveRange(self.stepMin, self.stepMax)] 
        vlist += [self.stepFormulaDict[vertex] for vertex in self.vertexSet]
        vec = self.graph.ckt.vec(vlist)
        self.graph.ckt.decRefs([vec])
                
        

        
        

            


        
        
    
