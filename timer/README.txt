tester.py: first version of testing code, outputting files for copying into google docs

tester2.py: revised with nicer output in terminal

script-tester2.py: version of tester2.py to support scripts via ./runbdd

csv-tester.py: rewrite for outputting comma-separated value files, for auto-import
into google docs. will support both scripts and fork, and getopt.

some features:
input/output file picking
distributed, local, and cudd
timing in python vs. parsing the delta time output from runbdd
grabbing ite/memory utilization
