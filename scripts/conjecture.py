#!/usr/bin/python

import sys
import circuit

# Create vector of function corresponding to possible residues of vector mod k

def residue(ckt, valvec, resvec):
    n = len(valvec.nodes)
    k = len(resvec.nodes)
    lastvec = circuit.Vec(["one"]).extend(k)
    # Identify source indices
    zsrc = [None for _ in range(k)]
    osrc = [None for _ in range(k)]
    for j in range(k):
        zd = (2*j) % k
        od = (2*j+1) % k
        zsrc[zd] = j
        osrc[od] = j
    for i in range(n):
        nextvec =  resvec if i == n-1 else ckt.tmpVec(k)
        for j in range(k):
            ckt.iteN(nextvec.nodes[j], [valvec.nodes[i], lastvec.nodes[osrc[j]], lastvec.nodes[zsrc[j]]])
        if i > 0:
            ckt.decRefs([lastvec])
        lastvec = nextvec
        
# Or together even elements of vector
def evenOuts(ckt, vec, out):
    nodes = [vec.nodes[i] for i in range(len(vec.nodes)) if i % 2 == 0]
    print "Generated list of nodes %s" % nodes
    ckt.orN(out, nodes)

def tmod(n, k):
    ckt = circuit.Circuit(sys.stdout)
    a = ckt.nameVec("a", n)
    vars = a
    ckt.declare(vars)
    m = ckt.nameVec("m", k)
    residue(ckt, a, m)
    out = circuit.Node("out")
    ckt.orN(out, [m.nodes[0]])
    ckt.decRefs([m])
    ckt.collect()
    
def cj(n, k1, k2):
    ckt = circuit.Circuit(sys.stdout)
    a = ckt.nameVec("a", n)
    x = circuit.Vec(["x"])
    vars = a.concatenate(x)
    ckt.declare(vars)
    m1 = ckt.nameVec("m1", k1)
    residue(ckt, a, m1)
    m1e = ckt.Node("m1e")
    evenOuts(ckt, m1, m1e)
    ckt.decRefs([m1])
    m2 = ckt.nameVec("m2", k2)
    residue(ckt, a, m2)
    m2e = ckt.Node("m2e")
    evenOuts(ckt, m2, m2e)
    ckt.decRefs([m2])
    ckt.collect()
    out = circuit.Node("out")
    ckt.iteN(out, [x.nodes[0], m1e, m2e])
    ckt.information(m1e)
    ckt.information(m2e)
    ckt.information(out)


n = int(sys.argv[1])
k1 = int(sys.argv[2])
k2 = int(sys.argv[3])

cj(n, k1, k2)


    







