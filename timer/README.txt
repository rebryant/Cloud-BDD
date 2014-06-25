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

parallel-runner.py: Runs jobs on the Marmot (NMC PRObE) clusters. More to come.

---

csv-tester.py

Preconditions for tracking details:

CUDD:
Expects that any details to be tracked can be found by running 'status'.

Distributed:
Expects that any details to be tracked can be found by running 'flush'.

Local:
Expects that any details to be tracked can be found by running 'flush'.


To add a detail to be tracked:

1. Add the parsing code in printUtilizationDetails for the given mode. Please try to put it in the order in which it would appear in program output.

2. (Optional): Add the parsing code in initUtilizationDetails to store a value retained from before execution. Then, you can retrieve it from the 'details' array.

3. Update the header in utilizationDetails__ (where __ is the mode in which you want to track the given detail), making sure to place the header in the same order as in step 1.

4. Update the number of details that you're tracking for the given mode at the top of the source file - the script uses this to generate "trailing commas" for sections with less details being tracked. This is needed to adhere to RFC 4180's format for CSV files (Section 2.4).


---

parallel-runner.py


--
