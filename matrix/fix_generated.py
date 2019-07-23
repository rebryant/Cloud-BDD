#!/usr/bin/python

# Make sure generated solutions are canonical, unique (and possibly also valid)
# Update database

import os
import os.path
import glob
import getopt
import sys


import circuit
import brent
import mm_parse

def usage(name):
    print("Usage: %s [-h] [-C] [-V]" % name)
    print(" -h     Print this message")
    print(" -C     Check canonicity of solutions")
    print(" -V     Check validity of solutions")

dim = (3,3,3)
auxCount = 23
ckt = circuit.Circuit()

   

def process(checkCanonicity = False, checkValidity = False):
    mm_parse.loadDatabase(mm_parse.heuleDatabaseDict, mm_parse.heuleDatabasePathFields, False)
    fields = mm_parse.homePathFields + mm_parse.generatedDatabasePathFields
    dbPath = "/".join(fields)
    try:
        dbFile = open(dbPath, 'w')
    except Exception as ex:
        print("Couldn't open database file '%s' to write (%s)" % (dbPath, str(ex)))
        return

    fields = mm_parse.homePathFields + mm_parse.generatedPathFields
    path = "/".join(fields)
    template = path + "/*/*.exp"
    pathList = glob.glob(template)
    count = 0
    for p in pathList:
        fields = p.split('/')
        sfields = mm_parse.generatedPathFields + fields[-2:]
        sp = '/'.join(sfields)
        try:
            s = brent.MScheme(dim, auxCount, ckt).parseFromFile(p)
        except Exception as ex:
            print("ERROR: Could not extract solution from file '%s' (%s)" % (p, str(ex)))
            continue
        if checkCanonicity and not s.isCanonical():
            print("Generated solution %s is not canoncal.  Removing" % (sig))
            os.remove(p)
        sig = s.sign()
        if sig in mm_parse.heuleDatabaseDict:
            opath = mm_parse.heuleDatabaseDict[sig][mm_parse.fieldIndex['path']]
            print("Generated solution %s matches stored solution in %s.  Removing" % (sig, opath))
            os.remove(p)
            continue
        if checkValidity and not s.obeysBrent():
            print("WARNING: Generated solution %s does not obey Brent equations.  Removing" % sig)
            os.remove(p)
            continue
        dbEntryFields = [sig, str(s.addCount()), s.kernelTerms.sign(), sp]
        dbFile.write("\t".join(dbEntryFields) + "\n")
        count += 1
        if (count % 100 == 0):
            print("Generated %d entries" % count)
    dbFile.close()
    print("Found %d solutions" % count)

    
def run(name, args):
    checkCanonicity = False
    checkValidity = False
    optlist, args = getopt.getopt(args, 'hCV')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        if opt == '-C':
            checkCanonicity = True
        if opt == '-V':
            checkValidity = True
    process(checkCanonicity, checkValidity)

if __name__ == "__main__":
    current = os.path.realpath(__file__)
    mm_parse.homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])

