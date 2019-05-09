#!/usr/bin/python
# Find or check solutions to Brent equations
# expressing different ways to multiply matrices with < n^3 multiplications

import sys
import getopt
import circuit
import brent

def usage(name):
    print "Usage %s [-h] [-c CAT] [-s PFILE] [-p AUX] [-n (N|N1:N2:N3)] [-o OUTF]" % name
    print " -h               Print this message"
    print " -c CAT           Hard code categories in CAT (substring of 'abc')"
    print " -s PFILE         Read hard-coded values from polynomial in PFILE"
    print " -p AUX           Number of auxiliary variables"
    print " -n N or N1:N2:N3 Matrix dimension(s)"
    print " -o OUTF          Output file"

def run(name, args):
    # Default is Strassens
    n1, n2, n3 = 2, 2, 2
    auxCount = 7
    check = False
    outf = sys.stdout
    categories = []
    pname = None
    optlist, args = getopt.getopt(args, 'hc:s:p:n:o:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-c':
            if 'a' in val:
                categories += ['alpha']
            if 'b' in val:
                categories += ['beta']
            if 'c' in val:
                categories += ['gamma']
        elif opt == '-s':
            pname = val
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
        elif opt == '-o':
            try:
                outf = open(val, 'w')
            except:
                print "Couldn't open output file '%s'" % val
                return
        else:
            print "Unknown option '%s'" % opt
            usage(name)
            return
    ckt = circuit.Circuit(outf)
    s = brent.MScheme((n1, n2, n3), auxCount, ckt)
    if len(categories) > 0 and pname is None:
        print "Need solution for categories %s" % (", ".join(categories))
        return
    if pname is not None:
        try:
            s.parseFromFile(pname)
        except brent.MatrixException as ex:
            print "Parse of file '%s' failed: %s" % (pname, str(ex))
            return
    s.generateProgram(categories)
    
    
            

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])
