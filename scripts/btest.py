# Test matrix code

import circuit
import brent

file = "mm-solutions/classic/smirnov.exp"

ckt = circuit.Circuit

mainScheme = brent.MScheme(3, 23, ckt).parseFromFile(file)

def tester(ijkPermuter, indexPermuter):
    kset = mainScheme.kernelTerms
    
    pkset = kset.permute(ijkPermuter = ijkPermuter, indexPermuter = indexPermuter)
    psig = pkset.generateString(False)

    variablePermuter = brent.convertPermuter(ijkPermuter, {0:'alpha', 1:'beta', 2:'gamma'})

    permScheme = mainScheme.permute(variablePermuter = variablePermuter, indexPermuter = indexPermuter)
    permkset = permScheme.kernelTerms
    permsig = permkset.generateString(False)

    print "Testing with permutations:"
    print "  ijk: %s" % (str(ijkPermuter))
    print "  index: %s" % (str(indexPermuter))

    # Make sure this thing works
    if not permScheme.obeysBrent():
        print "Uh oh.  Permuted scheme does not satisfy Brent equations"

    if psig != permsig:
        print "Uh oh.  Permuted scheme.  Predicted signature does not match actual"
        print "  predicted signature: %s" % psig
        print "  actual signature: %s" % permsig

    return permScheme
    
def sweep():
    indexPermuterList = brent.allPermuters(brent.unitRange(3))
    ijkPermuterList = brent.allPermuters(range(3))
    for ijkPermuter in ijkPermuterList:
        for indexPermuter in indexPermuterList:
            tester(ijkPermuter, indexPermuter)

