#!/usr/bin/python

# Fix Heule database
# Original version did not properly canonize the solutions
# For each entry:
#   Convert to canonical form
#   Replace hash in database
#   Store new version of file

import os
import sys

import circuit
import brent
import mm_parse

dim = (3,3,3)
auxCount = 23
ckt = circuit.Circuit()

db = {}
mm_parse.loadDatabase(db, mm_parse.heuleDatabasePathFields, False)

ndb = {}


# Fix database entry.  This does the real thing
def processEntryFirst(e):
    path = e[mm_parse.fieldIndex['path']]
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(path)
    except Exception as ex:
        print("Couldn't open file %s" % path)
        return False
    sc = s.canonize()
    hash = sc.sign()
    ohash = e[mm_parse.fieldIndex['hash']]
    if hash != ohash:
        try:
            outf = open(path, 'w')
        except Exception as ex:
            print("Couldn't open file %s to write" % path)
            return False
        sc.printPolynomial(outf)
        outf.close()
        e[mm_parse.fieldIndex['hash']] = hash
    return hash != ohash

# Fix database entry.  This one assumes files have been canonized
def processEntry(e):
    path = e[mm_parse.fieldIndex['path']]
    try:
        s = brent.MScheme(dim, auxCount, ckt).parseFromFile(path)
    except Exception as ex:
        print("Couldn't open file %s" % path)
        return False
    sc = s
    hash = sc.sign()
    ohash = e[mm_parse.fieldIndex['hash']]
    if hash != ohash:
        e[mm_parse.fieldIndex['hash']] = hash
    return hash != ohash

    
def process():
    tcount = 0
    ccount = 0
    ecount = 0
    dcount = 0
    for e in db.values():
        tcount += 1
        if processEntry(e):
            ccount += 1
        hash = e[mm_parse.fieldIndex['hash']]
        if hash in ndb:
            print("Duplicate entry found in old database.  Signature = %s" % hash)
            print("  Old Path = %s" % ndb[hash][mm_parse.fieldIndex['path']])
            print("  New Path = %s" % e[mm_parse.fieldIndex['path']])
            dcount += 1
        else:
            ndb[hash] = e
            ecount += 1
        if tcount % 100 == 0:
            print("%d entries processed.  %d changed.  %d duplicates. %d in final version" % (tcount, ccount, dcount, ecount))
    if ecount == 0:
        print("Leaving database unchanged")
        return
    dbname = '/'.join(mm_parse.homePathFields + mm_parse.heuleDatabasePathFields)
    try:
        dbfile = open(dbname, 'w')
    except Exception as ex:
        print("Couldn't open database file %s to write" % dbname)
        return
    dbfile.write('\t'.join(mm_parse.fieldTitles) + '\n')
    for e in ndb.values():
        fields = [str(f) for f in e]
        dbfile.write('\t'.join(fields) + '\n')
    dbfile.close()
    print("%d entries processed.  %d changed.  %d duplicates. %d in final version" % (tcount, ccount, dcount, ecount))
        
if __name__ == "__main__":
    current = os.path.realpath(__file__)
    mm_parse.homePathFields = current.split('/')[:-1]
    process()
