#!/usr/bin/python

# Extract partial results from log files

import re
import sys

def showFields(fields):
    sfields = [str(f) for f in fields]
    print "\t".join(sfields)

sizeSearcher = re.compile("Combined size = ([0-9]+)")
modeSearcher = re.compile("([A-Z][A-Z][A-Z][A-Z]).log:Partial")

sizeList = []
lastMode = None

for line in sys.stdin:
    mode = None
    size = None
    md = modeSearcher.search(line)
    if md:
        mode = md.group(1)
    ms = sizeSearcher.search(line)
    if ms:
        size = ms.group(1)
    if mode is None or size is None:
        continue
    if mode != lastMode:
        if len(sizeList) > 0:
            showFields([lastMode] + sizeList)
        lastMode = mode
        sizeList = []
    sizeList.append(size)

if len(sizeList) > 0:
    showFields([lastMode] + sizeList)


