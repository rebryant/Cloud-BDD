#!/usr/bin/python

cktdir = "./isc"

import sys
import getopt

import iscnet

def allIds():
    return ["c17", "c432", "c499", "c880", "c1355", "c1908", "c2670", "c3540", "c5315", "c6288", "c7552"]

def usage(name):
    print "Usage: %s [-h] [-i ID] [-r] [-b] [-z] [-a]" % name
    print "  -h    Print this message"
    print "  -i ID Specify circuit ID (e.g., c432)"
    print "  -r    Reorder inputs (using fanin heuristic)"
    print "  -R    Reorder inputs using custom order (c7552 only)"
    print "  -b    Generate BDDs"
    print "  -z    Generate ZDDs"
    print "  -a    Generate ADDs"

def cktpath(id):
    return cktdir + "/" + id + ".isc"

def outpath(id, dtype, reorder = False, customOrder = False):
    dd = iscnet.DD()
    ostring = "r" if reorder else "c" if customOrder else "u"
    return id + "-" + dd.prefix(dtype) + ostring + ".cmd"

def run(name, args):
    ids = allIds()
    types = []
    reorder = False
    customOrder = False
    optlist, args = getopt.getopt(args, 'hi:rRbza')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-i':
            ids = [val]
        elif opt == '-r':
            reorder = True
        elif opt == '-R':
            customOrder = True
        elif opt == '-b':
            types.append(iscnet.DD.BDD)
        elif opt == '-z':
            types.append(iscnet.DD.ZDD)
        elif opt == '-a':
            types.append(iscnet.DD.ADD)
    for id in ids:
        inname = cktpath(id)
        c = iscnet.Netlist()
        if not c.readFile(inname):
            print "No output generated"
            sys.exit(0)
        if reorder:
            c.reorder()
        if customOrder:
            if id == "c7552":
                 c.reorderReversed()
            else:
                print "No custom order available for circuit %s" % id
        icount = len(c.inputs)
        ocount = len(c.outputs)
        gcount = len(c.gates) - icount
        print "Circuit %s has %d inputs, %d outputs, %d gates" % (id, icount, ocount, gcount)
        for t in types:
            outname = outpath(id, t, reorder, customOrder)
            try:
                outfile = open(outname, "w")
            except:
                print "Could not open output file '%s'" % outname
                continue
            c.gen(outfile = outfile, dtype = t)
            outfile.close()
    
run(sys.argv[0], sys.argv[1:])
    
