#!/usr/bin/python

# Read in file consisting of literals or polynomial representation.
# Generate list of the corresponding unit clauses

import brent
import getopt
import sys

dim = (3,3,3)
auxCount = 23

def usage(name):
    print("%s: [-h] [-n (N|N1:N2:N3)] [-p AUX] (-L LFILE|-P PFILE) [-o UFILE")
    print(" -h               Print this message")
    print " -n N or N1:N2:N3 Matrix dimension(s)"
    print(" -p AUX           Number of auxiliary variables")
    print(" -L LFILE         Input literal file")
    print(" -P PFILE         Input polynomial file")
    print(" -o UFILE         Output unit file")

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
    fname = None
    isPolynomial = False
    outfile = sys.stdout
    optlist, args = getopt.getopt(args, 'hn:p:L:P:o:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-L':
            fname = val
        elif opt == '-P':
            isPolynomial = True
            fname = val
        elif opt == '-o':
            try:
                outfile = open(val, 'w')
            except Exception as ex:
                print("Couldn't open output file '%s' (%s)" % (val, str(ex)))
                outfile = None
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
    if fname is None:
        print("Must have input literal or polynomial file")
        usage(name)
        return
    if outfile is None:
        return
    dim = (n1, n2, n3)
    asst = getFromPolynomial(fname) if isPolynomial else getFromLiterals(fname)
    if asst is not None:
        fromAssignment(asst, outfile)
    if outfile != sys.stdout:
        outfile.close()
    
if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])

    


