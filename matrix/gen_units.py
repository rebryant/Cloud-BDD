#!/usr/bin/python

# Read in file consisting of literals or polynomial representation.
# Generate list of the corresponding unit clauses

import brent
import getopt
import sys

dim = (3,3,3)
auxCount = 23

def usage(name):
    print("%s: [-h] [-n (N|N1:N2:N3)] [-p AUX] file1 ... fileK")
    print(" -h               Print this message")
    print " -n N or N1:N2:N3 Matrix dimension(s)"
    print(" -p AUX           Number of auxiliary variables")
    print(" fileX            Literal (.lit) or polynomial (.exp) file")

def fromAssignment(asst, outfile):
    units = asst.generateUnitClauses(dim)
    for u in units:
        outfile.write("%d 0\n" % u)

def getFromLiterals(fname):
    try:
        asst = brent.Assignment().parseLiteralsFromFile(fname)
    except Exception as ex:
        print("Couldn't read literals: %s" % str(ex))
        return None
    return asst
        
def getFromPolynomial(fname):
    try:
        s = brent.MScheme(dim, auxCount, None).parseFromFile(fname)
    except Exception as ex:
        print("Couldn't read literals: %s" % str(ex))
        return None
    return s.assignment
    
def run(name, args):
    global dim, auxCount
    n1, n2, n3 = 3, 3, 3
    optlist, args = getopt.getopt(args, 'hn:p:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-p':
            auxCount = int(val)
        elif opt == '-n':
            fields = val.split(':')
            if len(fields) == 1:
                n1 = n2 = n3 = int(fields[0])
            elif len(fields) == 3:
                n1, n2, n3 = int(fields[0]), int(fields[1]), int(fields[2])
            else:
                print "Invalid matrix dimension '%s'" % val
                usage(name)
                return

    dim = (n1, n2, n3)

    errLimit = 5

    for fname in args:
        fields = fname.split('.')
        extension = fields[-1]
        ofname = ".".join(fields[:-1] + ['units'])
        asst = None
        if extension == 'exp':
           asst = getFromPolynomial(fname)
        elif extension == 'lit':
           asst = getFromLiterals(fname)
        else:
            print("Unrecognized extension '%s'" % extension)
            errLimit -= 1
            if (errLimit <= 0):
                print("Too many errors.  Exiting")
                return
            continue
        try:
            outfile = open(ofname, 'w')
        except Exception as ex:
            print("Couldn't open output file '%s' (%s)" % (ofname, str(Ex)))
            errLimit -= 1
            if (errLimit <= 0):
                print("Too many errors.  Exiting")
                return
            continue
        if asst is not None:
            fromAssignment(asst, outfile)
            print("Generated output file %s" % ofname)
        outfile.close()
    
if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])

    


