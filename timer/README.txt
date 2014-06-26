csv-tester.py

Runs a set of commands specified in a file by spawning a runbdd process and writing the commands to STDIN. Times each command and optionally tracks specific details printed to STDOUT, as specified in the script. Can customize the method of timing, the number of trials, the filenames, and more.

--
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


----

parallel-runner.py

This script runs a series of scripts in different worker and router configurations as specified in an input file. It spawns off jobs on different hosts using a hosts file ("/etc/hosts" on the PRObE clusters, but can be customized as shown below) and runs the tests using csv-tester.py, then kills them off.

--
Alternate Hosts file format:

xx.xx.xx.xx hxxxx-deth
xx.xx.xx.xx hxxxx-deth

(All lines must be in this format. The program will only connect to hosts whose hostnames begin with an 'h' and end with a '-deth', as in the PRObE cluster's configurations.)

--
Instructions file format:

scripts-source-filename
x,x
y,y

(The first line must be the path to the file containing the 'source' commands to run, one per line. Every line thereafter must contain two numbers separated by a comma; the first indicates the number of routers, the second indicates the number of workers.)

----

Previous README.txt history:

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
