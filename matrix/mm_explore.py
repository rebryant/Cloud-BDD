#!/usr/bin/python

# Select existing solution file
# Generate command file based on that solution
# Run command file
# Parse solution

from xmlrpc.server import SimpleXMLRPCServer
import xmlrpc.client
import glob
import os.path
import sys
import getopt

import brent
import mm_generate
import mm_parse

def usage(name):
    print("Usage %s [-h] [(-P PORT|-H HOST:PORT)] [-r] [-R] [-d DBFILE] [-t SECS] [-c APROB:BPROB:CPROB] [-p PROCS] [-v VERB]")
    print("   -h               Print this message")
    print("  Server options")
    print("   -P PORT          Set up server on specified port")
    print("  Client options")
    print("   -H HOST:PORT     Retrieve source file name from server at HOST:PORT")
    print("  Local & server options")
    print("   -R               Allow unrestricted solution types")
    print("   -d DBFILE        Solution database file")
    print("  Local & client options")
    print("   -t SECS          Set runtime limit (in seconds)")
    print("   -c APROB:BPROB:CPROB Assign probabilities (in percent) of fixing each variable class")
    print("   -p P1:P2...      Specify simplification processing options NON, (U|S)(L|R)N")
    print("   -v VERB          Set verbosity level")
    sys.exit(0)

# Set to home directory for program, split into tokens
homePathFields = ['.']

runbddFields = ["..", "runbdd"]

isServer = False
isClient = False
port = 6616
host = 'localhost'

supersetFraction = None

softRedo = False
hardRedo = False

chain = True

verbLevel = None
quietMode = False

cmdPrefix = "cmd>"

reductionList = ['T', 'B', 'L', 'P', 'D', 'S']
processingList = ['NON', 'ULN', 'URN', 'SLN', 'SLY', 'SRN', 'SRY']

# Like an iterator, returning pairs from each of two lists
# Returns None when run out of pairs
class Pair:
    leftList = []
    rightList = []
    restLeft = []
    restRight = []

    def __init__(self, leftList, rightList):
        self.leftList = leftList
        self.rightList = rightList
        self.restLeft = leftList
        self.restRight = rightList

    def nextOrEmpty(self):
        if self.restLeft == []:
            return ()
        if self.restRight == []:
            self.restLeft = self.restLeft[1:]
            self.restRight = self.rightList
        if self.restLeft == []:
            return ()
        result = (self.restLeft[0], self.restRight[0])
        self.restRight = self.restRight[1:]
        return result

class Server:
    pairGenerator = None
    server = None

    def __init__(self, port, generator):
        host = ''
        self.pairGenerator = generator
        self.server = SimpleXMLRPCServer((host, port))
        self.server.register_function(self.__next__, "next")
    
    def __next__(self):
        pair = self.pairGenerator.nextOrEmpty()
        print("Returning %s" % str(pair))
        return pair

    def run(self):
        self.server.serve_forever()


# Is a combination of reduction and processing modes compatible?
def compatibleModes(reduction, processing):
    if reduction not in reductionList:
        return False
    if processing not in processingList:
        return False
    if reduction in ['D', 'S'] and processing[-1] == 'Y':
        return False
    return True;
    
def makeMode(reduction, processing):
    return reduction + processing

def logPath(cmdPath, mode):
    fields = cmdPath.split('.')
    fields[-2] += '-' + mode
    fields[-1] = 'log'
    return ".".join(fields)
        

# Check if should run.  Return either None (don't run) or name of log file
def shouldRun(cmdPath, mode):
    fields = cmdPath.split('.')
    if fields[-1] != 'cmd':
        print("Path does not appear to be a command file '%s'" % cmdPath)
        return False
    lpath = logPath(cmdPath, mode)
    if hardRedo:
        return lpath
    if os.path.exists(lpath):
        if not softRedo:
            return None
        try:
            lfile = open(lpath, 'r')
        except Exception as ex:
            print("Could not open log file '%s' (%s)" % (lpath, str(ex)))
            return lpath
        quitLine = cmdPrefix + "quit"
        for line in lfile:
            line = brent.trim(line)
            if line == quitLine:
                lfile.close()
                return None
        lfile.close()
        # Soft redo, and didn't finish previous run
        return lpath
    else:
        return lpath
    
def process(cmdPath, mode):
    lpath = shouldRun(cmdPath, mode)
    if lpath is None:
        print("Already ran %s + %s.  Skipping" % (cmdPath, mode))
        return
    cmd = ["/".join(homePathFields + runbddFields), '-c']
    if not chain:
        cmd += ['-C', 'n']
    cmd += ['-O', mode]
    cmd += ['-f', cmdPath]
    cmd += ['-L', lpath]
    if supersetFraction is not None:
        cmd += ['-s', str(supersetFraction)]
    if verbLevel is not None:
        cmd += ['-v', str(verbLevel)]
    cmdLine = " ".join(cmd)
    if not quietMode:
        print("Running '%s'" % cmdLine)
    if quietMode:
        p = subprocess.Popen(cmd, stdout = subprocess.PIPE, stderr = subprocess.PIPE)
        (stdout, stderr) = p.communicate()
        lines = stdout.split('\n')
        for line in lines:
            if "Conjunction" in line:
                print(line)
        lines = stderr.split("\n")
        for line in lines:
            print("stderr: " + line)
    else:
        p = subprocess.Popen(cmd)
        p.wait()
        if p.returncode != 0:
            print("Running command '%s' failed.  Return code = %d" % (cmdLine, p.returncode))

def run(name, args):
    global softRedo, hardRedo, chain, verbLevel, quietMode
    global isClient, isServer, host, port
    global supersetFraction
    reductions = reductionList
    processings = processingList
    cmdPaths = []
    optlist, args = getopt.getopt(args, 'hP:H:rRCv:qf:I:m:p:s:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-P':
            isServer = True
            port = int(val)
        elif opt == '-H':
            isClient = True
            fields = val.split(':')
            if len(fields) != 2:
                print("Must have host of form HOST:PORT")
                usage(name)
                return
            host = fields[0]
            port = int(fields[1])
        elif opt == '-r':
            softRedo = True
        elif opt == '-R':
            hardRedo = True
        elif opt == '-C':
            chain = False
        elif opt == '-f':
            cmdPaths = [val]
        elif opt == '-I':
            template = '%s/*.cmd' % val
            cmdPaths = glob.glob(template)
        elif opt == '-v':
            verbLevel = int(val)
        elif opt == '-q':
            quietMode = True
        elif opt == '-m':
            reductions = val.split(':')
        elif opt == '-p':
            processings = val.split(':')
        elif opt == '-s':
            supersetFraction = int(val)
    if len(cmdPaths) == 0 and not isClient:
        print("Need command file(s)")
        usage(name)
        return

    if isClient:
        try:
            cname = 'http://%s:%d' % (host, port)
            print("Attempting to connect to '%s'" % cname)
            c = xmlrpc.client.ServerProxy(cname)
        except Exception as ex:
            print("Failed to connect to server (%s)" % str(ex))
            return
        while True:
            try:
                tuple = next(c)
            except Exception as ex:
                print("Could not get value from server (%s)" % str(ex))
                return
            if len(tuple) > 0:
                cmdPath = tuple[0]
                mode = tuple[1]
                process(cmdPath, mode)
            else:
                print("Terminated")
                return

    modeList = []
    for r in reductions:
        for p in processings:
            if compatibleModes(r,p):
                mode = makeMode(r,p)
                modeList.append(mode)
            else:
                print("Invalid mode: %s.  Skipping" % makeMode(r, p))
    pairGenerator = Pair(cmdPaths, modeList)

    if isServer:
        try:
            server = Server(port, pairGenerator)
        except Exception as ex:
            print("Couldn't set up server on port %d (%s)" % (port, str(ex)))
            return
        server.run()
    else:
        while True:
            tuple = pairGenerator.nextOrEmpty()
            if len(tuple) == 0:
                return
            cmdPath = tuple[0]
            mode = tuple[1]
            process(cmdPath, mode)

if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    run(sys.argv[0], sys.argv[1:])

    
