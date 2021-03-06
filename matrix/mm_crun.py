#!/usr/bin/python

# Execute runbdd on single command file, but with multiple configurations of conjunct parameters

import sys

if sys.version_info.major == 3:
    from xmlrpc.server import SimpleXMLRPCServer
    import xmlrpc.client
    xml_client = xmlrpc.client
else:
    from SimpleXMLRPCServer import SimpleXMLRPCServer
    import xmlrpclib
    xml_client = xmlrpclib


import glob
import os.path

import getopt
import subprocess

import brent

def usage(name):
    print("Usage %s [-h] [(-P PORT|-H HOST:PORT)] [-r] [-R] [-C] [-v VERB] [-q] [-m MODES] [-p PROCS] (-f FILE|-I DIR) [-s SUP]")
    print("   -h               Print this message")
    print("  Server options")
    print("   -P PORT          Set up server on specified port")
    print("  Client options")
    print("   -H HOST:PORT     Retrieve command file and mode information from server at HOST:PORT")
    print("  Local & server options")
    print("   -m M1:M2...      Specify reduction mode(s) T, B, L, P, D, S")
    print("   -p P1:P2...      Specify simplification processing options NNNN, (U|S)(L|R)(AN|AY|RN)")
    print("   -f FILE          Command file")
    print("   -I DIR           Process all .cmd files in DIR")
    print("  Local & client options")
    print("   -r               Redo runs that didn't complete")
    print("   -R               Redo all runs")
    print("   -C               Disable chaining")
    print("   -v VERB          Set verbosity level")
    print("   -q               Don't print output results")
    print("   -s SUP           Set superset fraction in similarity metric [0-100]")
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
processingList = ['NNNN',
                  'ULAN', 'URAN', 'SLAN', 'SLAY', 'SRAN', 'SRAY',
                  'ULRN', 'URRN', 'SLRN', 'SLRY', 'SRRN', 'SRRY',
                  ]

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
        self.server.register_function(self.next, "next")
    
    def next(self):
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
    if reduction in ['D', 'S'] and processing[-2:] == 'RY':
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
        (bstdout, bstderr) = p.communicate()
        stdout = bstdout.decode('ASCII')
        stderr = bstderr.decode('ASCII')
        lines = stdout.split('\n')
        for line in lines:
            if "Conjunction" in line:
                print(line)
        lines = stderr.split("\n")
        for line in lines:
            if len(line) > 0:
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
            c = xml_client.ServerProxy(cname)
        except Exception as ex:
            print("Failed to connect to server (%s)" % str(ex))
            return
        while True:
            try:
                tuple = c.next()
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

    
