#!/usr/bin/python
# Run ISCAS benchmarks.
# Assunes the command files have been generated

import subprocess
import sys
import getopt

rdir = "../.."
rbdd = "%s/runbdd" % rdir

import iscnet

allIds = ["c17", "c432", "c499", "c880", "c1355", "c1908", "c2670", "c3540", "c5315", "c6288", "c7552"]

def usage(name):
    print "Usage: %s [-h] [-i ID1:..:IDn] [-r] [-c n|a] [-b] [-z] [-a]" % name
    print "  -h    Print this message"
    print "  -i ID1:..:IDn Specify circuit IDs (e.g., c432:c499)"
    print "  -r    Use reordered inputs"
    print "  -R    Use custom ordered inputs (c7522 only)"
    print "  -c n|a  Specify chaining type (default = all)"
    print "  -b    Run with BDDs"
    print "  -z    Run with ZDDs"
    print "  -a    Run with ADDs"

def inname(id, dtype, reorder = False, customOrder = False):
    dd = iscnet.DD()
    ostring = "r" if reorder else "c" if customOrder else "u"
    return id + "-" + dd.prefix(dtype) + ostring + ".cmd"

def outname(id, dtype, reorder, customOrder, ctype):
    dd = iscnet.DD()
    ostring = "r" if reorder else "c" if customOrder else "u"
    return id + "-" + dd.prefix(dtype) + ostring + "-c" + ctype + ".out"

def run(name, args):
    ids = allIds
    types = []
    ctype = "na"
    reorder = False
    customOrder = False
    optlist, args = getopt.getopt(args, 'hi:rRc:bza')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-i':
            ids = val.split(":")
        elif opt == '-r':
            reorder = True
        elif opt == '-R':
            customOrder = True
        elif opt == '-c':
            ctype = val
        elif opt == '-b':
            types.append(iscnet.DD.BDD)
        elif opt == '-z':
            types.append(iscnet.DD.ZDD)
        elif opt == '-a':
            types.append(iscnet.DD.ADD)
    for id in ids:
        for t in types:
            for c in ctype:
                iname = inname(id, t, reorder, customOrder)
                oname = outname(id, t, reorder, customOrder, c)
                try:
                    logfile = open(oname, "w")
                except:
                    print "Couldn't open log file '%s'" % oname
                    return
                clist = [rbdd, "-c", "-C", c, "-f", iname]
                print "Running '%s'" % " ".join(clist)
                rcode = subprocess.call(clist, stdout = logfile, stderr = logfile)
    
run(sys.argv[0], sys.argv[1:])
    
