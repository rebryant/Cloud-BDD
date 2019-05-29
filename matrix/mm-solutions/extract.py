#!/usr/bin/python
import os
import os.path
import re
import requests
import sys

# Extract solutions to matrix multiplication problems

# Top-level page (retrieved 5/19/2019)
topPage = "schemes.html"
topURL = "http://140.78.94.22/research/matrix-multiplication/schemes"


subMatcher = re.compile('<a href="([+a-z0-9]+)/"')
fileMatcher = re.compile('<a href="([-+a-z0-9]+\.exp)"')

retrieveCount = 0

def topLevel():
    inf = open(topPage)
    limit = 1000
    for line in inf:
        m = subMatcher.search(line) 
        if m:
            subName = m.group(1)
            print "Found sub name '%s'" % subName
            getSubdirectory(subName)
            # Temporary
            limit -= 1
            if limit == 0:
                break
    inf.close()
        

def getSubdirectory(subName):
    subURL = topURL + '/' + subName + '/'
    subDir = './' + subName
    if not os.path.exists(subDir):
        try:
            os.mkdir(subDir)
        except:
            print "Failed to create directory '%s'" % subDir
            sys.exit(1)
    getPage(subDir, subURL)

def getPage(subDir, subURL):
    r = requests.get(subURL)
    if not r:
        print "Couldn't get URL '%s'" % subURL
        return False
    extractSchemes(subDir, subURL, r.text)

def extractSchemes(subDir, subURL, html):
    global retrieveCount
    lines = html.split('\n')
    for line in lines:
        m = fileMatcher.search(line)
        if m:
            s = m.group(1)
            schemeFile = subDir + '/' + s 
            schemeURL = subURL + s 
            print "Retrieving scheme %s and storing in %s" % (schemeURL, schemeFile)
            r = requests.get(schemeURL)
            if not r:
                print "Failed to get URL"
                continue
            try:
                outf = open(schemeFile, 'w')
                outf.write(r.text)
                outf.close()
            except:
                print "Failed to open/write file"
                continue
            retrieveCount += 1

topLevel()
print "Retrieved %d schemes" % retrieveCount
