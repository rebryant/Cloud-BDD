#!/usr/bin/python

# Read in file consisting of literals or polynomial representation.
# Generate list of the corresponding unit clauses

import brent
import getopt
import sys

dim = (3,3,3)
auxCount = 23

def usage(name):
    print("%s: [-h] [-n (N|N1:N2:N3)] [-p AUX] FILE.{lit,exp} ...")
    print(" -h               Print this message")
    print " -n N or N1:N2:N3 Matrix dimension(s)"
    print(" -p AUX           Number of auxiliary variables")
    print("Files with suffix '.lit' consist of literals")
    print("Files with suffix '.exp' consist of polynomial representations")
    print("The results will be in files of the form FILE.units")

def generateFromAssignment(asst, outname):
    try:
        outfile = open(outname, 'w')
    except Exception as ex:
        print("Couldn't open file '%s' (%s)" % (outname, str(ex)))
        return
    units = asst.generateUnitClauses(dim)
    for u in units:
        outfile.write("%d 0\n" % u)
    outfile.close()
    print("Generated file %s" % outname)

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

    for fname in args:
        fields = fname.split(".")
        extension = fields[-1]
        root = ".".join(fields[:-1])
        outname = root + ".units"
        asst = None
        if extension == 'lit':
            asst = getFromLiterals(fname)
        elif extension == '.exp':
            asst = getFromPolynomial(fname)
        if asst is not None:
            generateFromAssignment(asst, outname)
    
if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])

    


