#!/usr/bin/python

# Copy solutions from old directory to new
# Convert to new canonical form
# Update database file

import os.path
import sys
import getopt


import circuit
import brent
import mm_parse

def usage(name):
    print("Usage: %s [-h] [-q] [-l LIM]" % name)
    print(" -h     Print this message")
    print(" -q     Quiet mode")
    print(" -l LIM Set limit on number of schemes processed")


dim = (3,3,3)
auxCount = 23
ckt = circuit.Circuit()

quietMode = False
   
# Heule database
hdb = {}
# Existing database
db = {}
# New database
ndb = {}
# Translation from old hash to new
translationDict = {}

oldGeneratedPathFields =[mm_parse.databaseDirectory, "generated-save"]
oldGeneratedDatabasePathFields = [mm_parse.databaseDirectory, "generated-database-save.txt"]
translationPathFields = [mm_parse.databaseDirectory, "translate.txt"]

mm_parse.loadDatabase(hdb, mm_parse.heuleDatabasePathFields, False)
mm_parse.loadDatabase(db, oldGeneratedDatabasePathFields, False)

tname = '/'.join(mm_parse.homePathFields + translationPathFields)
if os.path.exists(tname):
    tfile = open(tname, 'r')
    tcount = 0
    for line in tfile:
        line = brent.trim(line)
        fields = line.split()
        if len(fields) == 2:
            translationDict[fields[0]] = fields[1]
        tcount += 1
    tfile.close()
    print("Already have processed %d entries" % tcount)

ndbname = '/'.join(mm_parse.homePathFields + mm_parse.generatedDatabasePathFields)
if os.path.exists(ndbname):
    mm_parse.loadDatabase(ndb, mm_parse.generatedDatabasePathFields, False)
else:
    dbfile = open(ndbname, 'w')
    dbfile.write('\t'.join(mm_parse.fieldTitles) + '\n')
    dbfile.close()

# Record translation
def recordTranslation(ohash, nhash):
    translationDict[ohash] = nhash
    tfile = open(tname, 'a')
    tfile.write('\t'.join([ohash, nhash]) + '\n')
    tfile.close()

def processEntry(e):
    ohash = e[mm_parse.fieldIndex['hash']]
    path = e[mm_parse.fieldIndex['path']]
    pfields = path.split('/')
    pfields[1] = "generated-save"
    opath = '/'.join(pfields)
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(opath)
    except Exception as ex:
        print("Couldn't open file %s" % opath)
        return False
    sc = s.canonize()
    hash = sc.sign()
    if hash in hdb:
        if not quietMode:
            print("Old scheme %s maps to Heule scheme %s" % (ohash, hash))
        recordTranslation(ohash, hash)
        return False
    if hash in ndb:
        if not quietMode:
            print("Old scheme %s maps to generated scheme %s" % (ohash, hash))
        recordTranslation(ohash, hash)
        return False
    mm_parse.recordSolution(sc, metadata = ["Updated version of scheme with hash %s" % ohash])
    recordTranslation(ohash, hash)
    if not quietMode:
        if ohash == hash:
            print("Scheme %s copied over" % (hash))
        else:
            print("Scheme %s mapped to fresh scheme %s" % (ohash, hash))
    return True

def process(limit):
    pcount = 0
    ecount = 0
    for e in db.values():
        ohash = e[mm_parse.fieldIndex['hash']]
        if ohash in translationDict:
            # This one has already been processed
            if not quietMode:
                print("Entry %s already mapped to entry %s" % (ohash, translationDict[ohash]))
            continue
        if processEntry(e):
            ecount += 1
        pcount += 1
        if pcount >= limit:
            print("Processed %d entries.  %d added to database" % (pcount, ecount))
            return
    
def run(name, args):
    global quietMode
    limit = 1000
    optlist, args = getopt.getopt(args, 'hql:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        if opt == '-q':
            quietMode = True
        if opt == '-l':
            limit = int(val)
    process(limit)

if __name__ == "__main__":
    current = os.path.realpath(__file__)
    mm_parse.homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])

