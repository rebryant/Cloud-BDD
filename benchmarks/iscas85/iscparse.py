#!/usr/bin/python

cktdir = "./isc"

import sys
import getopt

import iscnet

allIds = ["c17", "c432", "c499", "c880", "c1355", "c1908", "c2670", "c3540", "c5315", "c6288", "c7552"]
# Ordering: h=heuristic, f=file, c=custom
allOrders = ['r', 'r', 'r', 'r', 'r', 'r', 'r', 'r', 'r', 'u', 'c']

def usage(name):
    print "Usage: %s [-h] [-A] [-i ID] [-r] [-b] [-z] [-a]" % name
    print "  -A    Generate all benchmarks"
    print "  The remaining options apply only for generating single benchmarks"
    print "  -h    Print this message"
    print "  -i ID Specify circuit ID (e.g., c432)"
    print "  -r    Reorder inputs (using fanin heuristic)"
    print "  -R    Reorder inputs using custom order (c7552 only)"
    print "  -b    Generate BDDs"
    print "  -z    Generate ZDDs"
    print "  -a    Generate ADDs"

def cktpath(id):
    return cktdir + "/" + id + ".isc"

def outpath(id, dtype, order):
    dd = iscnet.DD()
    return id + "-" + dd.prefix(dtype) + order + ".cmd"

def run(name, args):
    ids = []
    types = []
    orders = []
    optlist, args = getopt.getopt(args, 'hAi:rRbza')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        if opt == '-A':
            ids = allIds
            orders = allOrders
            types = [iscnet.DD.BDD, iscnet.DD.ZDD, iscnet.DD.ADD]
        elif opt == '-i':
            ids = [val]
        elif opt == '-r':
            orders = ['r'] * len(ids)
        elif opt == '-R':
            orders = ['c'] * len(ids)
        elif opt == '-b':
            if iscnet.DD.BDD not in types:
                types += [iscnet.DD.BDD]
        elif opt == '-z':
            if iscnet.DD.ZDD not in types:
                types += [iscnet.DD.ZDD]
        elif opt == '-a':
            if iscnet.DD.ADD not in types:
                types += [iscnet.DD.ADD]
    if len(orders) < len(ids):
        orders = ['u'] * len(ids)
    for id, order in zip(ids, orders):
        inname = cktpath(id)
        c = iscnet.Netlist()
        if not c.readFile(inname):
            print "No output generated"
            sys.exit(0)
        if order == 'r':
            c.reorder()
        if order == 'c':
            if id == "c7552":
                 c.reorderReversed()
            else:
                print "No custom order available for circuit %s" % id
        icount = len(c.inputs)
        ocount = len(c.outputs)
        gcount = len(c.gates) - icount
        print "Circuit %s has %d inputs, %d outputs, %d gates" % (id, icount, ocount, gcount)
        for t in types:
            outname = outpath(id, t, order)
            try:
                outfile = open(outname, "w")
            except:
                print "Could not open output file '%s'" % outname
                continue
            c.gen(outfile = outfile, dtype = t)
            outfile.close()
    
run(sys.argv[0], sys.argv[1:])
    
