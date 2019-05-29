# Test matrix code

import circuit
import brent

file = "mm-solutions/classic/smirnov.exp"

ckt = circuit.Circuit

mainScheme = brent.MScheme(3, 23, ckt).parseFromFile(file)

def tester(scheme, ijkPermuter, indexPermuter):
    kset = scheme.kernelTerms
    
    pkset = kset.permute(ijkPermuter = ijkPermuter, indexPermuter = indexPermuter)
    psig = pkset.signature()

    variablePermuter = brent.ijk2var(ijkPermuter)

    permScheme = mainScheme.permute(variablePermuter = variablePermuter, indexPermuter = indexPermuter)
    permkset = permScheme.kernelTerms
    permsig = permkset.signature()

    print "Testing with permutations:"
    print "  ijk: %s" % (brent.showPerm(ijkPermuter))
    print "  index: %s" % (brent.showPerm(indexPermuter))

    # Make sure this thing works
    if not permScheme.obeysBrent():
        print "Uh oh.  Permuted scheme does not satisfy Brent equations"

    if psig != permsig:
        print "Uh oh.  Permuted scheme.  Predicted signature does not match actual"
        print "  predicted signature: %s" % psig
        print "  actual signature:    %s" % permsig

        mstring = ""
        for idx in range(len(psig)):
            mstring += ("^" if psig[idx] != permsig[idx] else " ")
        print "   MISMATCH          : %s" % mstring
    return permScheme
    
def signPerm(p):
    slist = [str(p[k]) for k in sorted(p.keys())]
    return "".join(slist)

def ijk2var(ijkp):
    matchDict = { '012' : 'abc', '021' : 'cba', '102' : 'acb', '120' : 'cab', '201' : 'bca', '210' : 'bac' }
    key = signPerm(ijkp)
    sig = matchDict[key]
    vals = [brent.BrentVariable.namer[c] for c in sig]
    vp = { v1 : v2 for v1, v2 in zip(['alpha', 'beta', 'gamma'], vals) }
    return vp

def findMatches(scheme = None):
    if scheme is None:
        scheme = mainScheme
    kset = scheme.kernelTerms
    variablePermuterList = brent.allPermuters(['alpha', 'beta', 'gamma'])
    ijkPermuterList = brent.allPermuters(range(3))
    matchList = []
    for variablePermuter in variablePermuterList:
        symbolPermuter = brent.convertPermuter(variablePermuter, {'alpha' : 'a', 'beta' : 'b', 'gamma' : 'c'})
        print "Variable permuter [%s]" % brent.showPerm(variablePermuter)
        permScheme = scheme.permute(variablePermuter = variablePermuter)
        permkset = permScheme.kernelTerms
        permsig = permkset.signature()
        if not permScheme.obeysBrent():
            print "  Oops.  Does not obey Brent equations"
        found = False
        for ijkPermuter in ijkPermuterList:
            pkset = kset.permute(ijkPermuter = ijkPermuter)
            psig = pkset.signature()
            print "   ijk permuter: [%s]" % (brent.showPerm(ijkPermuter))
            print "   Tgt Signature: %s" % permsig
            print "       Signature: %s" % psig
            if permsig == psig:
                print "   Match: [%s] [%s] " % (signPerm(symbolPermuter), signPerm(ijkPermuter))
                matchList.append("'%s' : '%s'" % (signPerm(ijkPermuter), signPerm(symbolPermuter)))
                found = True
        if not found:
            print "   NO Match"
    matchList.sort()
    print "{ " + ", ".join(matchList) + " }"

def checkMatches(scheme = None):
    if scheme is None:
        scheme = mainScheme
    kset = scheme.kernelTerms
    ijkPermuterList = brent.allPermuters(range(3))
    matchList = []
    for ijkPermuter in ijkPermuterList:
        pkset = kset.permute(ijkPermuter = ijkPermuter).levelCanonize()[0]
        psig = pkset.signature()
        variablePermuter = ijk2var(ijkPermuter)
        permScheme = scheme.permute(variablePermuter = variablePermuter)
        permkset = permScheme.kernelTerms.levelCanonize()[0]
        permsig = permkset.signature()
        print "Comparing ijk permuter [%s] to variable permuter [%s]" % (brent.showPerm(ijkPermuter), brent.showPerm(variablePermuter))
        print "   ijk Signature: %s" % psig
        print "   var Signature: %s" % permsig
        if permsig == psig:
            print "   Match"
        else:
            mstring = ""
            for idx in range(len(psig)):
                mstring += ("^" if psig[idx] != permsig[idx] else " ")
            print "   MISMATCH     : %s" % mstring
    

def sweep(scheme = None):
    if scheme is None:
        scheme = mainScheme
    indexPermuterList = brent.allPermuters(brent.unitRange(3))
    ijkPermuterList = brent.allPermuters(range(3))
    for ijkPermuter in ijkPermuterList:
        for indexPermuter in indexPermuterList:
            tester(scheme, ijkPermuter, indexPermuter)

