#!/usr/bin/python

# Select existing solution file
# Generate command file based on that solution
# Run command file
# Parse solution

import sys

if sys.version_info.major == 3:
    from xmlrpc.server import SimpleXMLRPCServer
    import xmlrpc.client
    xml_client = xmlrpc.client
else:
    from SimpleXMLRPCServer import SimpleXMLRPCServer
    import xmlrpclib
    xml_client = xmlrpclib

import getopt
import random
import os.path
import subprocess

import brent
import mm_parse
import circuit

def usage(name):
    print("Usage %s [-h] [(-P PORT|-H HOST:PORT)] [-R] [-t SECS] [-c APROB:BPROB:CPROB] [-p PROCS] [-v VERB] [-l LIMIT]")
    print("   -h               Print this message")
    print("  Server options")
    print("   -P PORT          Set up server on specified port")
    print("  Client options")
    print("   -H HOST:PORT     Retrieve source file name from server at HOST:PORT")
    print("  Local & server options")
    print("   -R               Allow unrestricted solution types")
    print("   -l LIMIT         Set limit on number of schemes generated")
    print("  Local & client options")
    print("   -t SECS          Set runtime limit (in seconds)")
    print("   -c APROB:BPROB:CPROB Assign probabilities (in percent) of fixing each variable class")
    print("   -p P1:P2...      Specify simplification processing options NON, (U|S)(L|R)N")
    print("   -v VERB          Set verbosity level")
    sys.exit(0)

# Set to home directory for program, split into tokens
homePathFields = ['.']

runbddFields = ["..", "runbdd"]

# Verbosity levels:
# 0: Error messages only
# 1: Result of each run
# 2: Stepwise progress of each run
# 3:
# 4: Runbdd output
verbLevel = 1
runbddQuiet = True

cmdPrefix = "cmd>"

reductionList = ['S']
processingList = ['URN']

timeLimit = 1800
levelList = [2,3,6]

categoryProbabilities = {'alpha':0.0, 'beta':0.0, 'gamma':1.0}
seedLimit = 100
errorLimit = 5

restrictSolutions = True

dim = (3, 3, 3)
auxCount = 27

defaultHost = 'localhost'
defaultPort = 6616

ckt = circuit.Circuit()

def report(level, s):
    if level <= verbLevel:
        print(s)

def setVerbLevel(level):
    global verbLevel, runbddQuiet
    verbLevel = level
    mm_parse.quietMode = level <= 2
    runbddQuiet = level <= 3

# Generate schemes for further processing
class SchemeGenerator:

    candidates = []
    dim = (3,3,3)
    auxCount = 23
    permute = True
    tryLimit = 50
    vpList = []
    limit = 100
    count = 0

    def __init__(self, dim, auxCount, permute = True, limit = 100):
        self.dim = dim
        self.auxCount = auxCount
        self.permute = permute
        self.tryLimit = limit
        self.count = 0
        self.limit = limit
        db = {}
        mm_parse.loadDatabase(db, mm_parse.generatedDatabasePathFields, True)
        mm_parse.loadDatabase(db, mm_parse.heuleDatabasePathFields, True)
        report(1, "Loaded %d entries into database" % len(db))
        self.candidates = [v[mm_parse.fieldIndex['path']] for v in db.values()]
        self.vpList = [brent.ijk2var(p) for p in brent.allPermuters(list(range(3)))]
        
    def select(self):
        if self.count >= self.limit:
            report(1, "Generated %d schemes" % self.count)
            return None
        self.count += 1
        for t in range(self.tryLimit):
            p = random.choice(self.candidates)
            fields = homePathFields + p.split('/')
            path = "/".join(fields)
            s = brent.MScheme(self.dim, self.auxCount, None)
            try:
                s.parseFromFile(path)
            except:
                report(0, "Couldn't read solution file '%s'" % path)
                continue
            if restrictSolutions and not (s.obeysUniqueUsage() and s.obeysMaxDouble() and s.obeysSingletonExclusion()):
                continue
            report(2, "Returning scheme from file '%s'" % p)
            if self.permute:
                vp = random.choice(self.vpList)
                s = s.permute(variablePermuter = vp)
                report(2, "   Permuted as: %s" % brent.showPerm(vp))
            return s
        report(0, "Couldn't find any candidates after %d tries" % self.tryLimit)
        return None

    def addCandidate(self, path):
        self.candidates.append(path)

class Server:
    generator = None
    server = None
    recordedCount = 0

    def __init__(self, port, generator):
        host = ''
        self.generator = generator
        self.server = SimpleXMLRPCServer((host, port))
        self.server.register_function(self.next, "next")
        self.server.register_function(self.record, "record")
        self.recordedCount = 0
    
    def next(self):
        s = self.generator.select()
        if s is None:
            return False
        else:
            report(1, "Server returning scheme %s" % s.sign())
            return s.bundle()

    def record(self, schemeBundle, metadata):
        scheme = brent.MScheme(dim, auxCount, ckt).unbundle(schemeBundle)
        hash = scheme.sign()
        report(2, "Received bundle from client giving scheme %s" % hash)
        signature = scheme.signature()
        found = signature in mm_parse.solutionDict or hash in mm_parse.heuleDatabaseDict or hash in mm_parse.generatedDatabaseDict
        if found:
            report(2, "Solution %s duplicates existing one" % hash)
            return False
        else:
            mm_parse.recordSolution(scheme, metadata)
            self.recordedCount += 1
            report(1, "New solution %s recorded.  Session total = %d" % (hash, self.recordedCount))
            return True

    def run(self):
        self.server.serve_forever()

class Client:
    host = ""
    port = ""

    def __init__(self, host, port):
        self.host = host
        self.port = port

    def connect(self):
        try:
            cname = 'http://%s:%d' % (self.host, self.port)
            report(3, "Attempting to connect to '%s'" % cname)
            c = xml_client.ServerProxy(cname)
            return c
        except Exception as ex:
            report(0, "Failed to connect to server (%s)" % str(ex))
            return None

    def next(self):
        c =  self.connect()
        if c is None:
            return None
        try:
            bundle = c.next()
        except Exception as ex:
            report(0, "Error.  Could not get another scheme (%s)" % str(ex))
            return None
        if bundle == False:
            report(2, "Empty bundle")
            return None
        scheme = brent.MScheme(dim, auxCount, ckt).unbundle(bundle)
        if scheme is None:
            report(0, "Couldn't unbundle scheme from '%s'" % str(bundle))
        else:
            report(3, "Retrieved scheme %s" % scheme.sign())
        return scheme

    def record(self, scheme, metadata):
        c =  self.connect()
        if c is None:
            report(0, "Failed to record scheme %s.  No connection formed" % scheme.sign())
            return None
        bundle = scheme.bundle()
        hash = scheme.sign()
        report(3, "Transmitting bundle for scheme %s to server" % hash)
        try:
            if c.record(bundle, metadata):
                report(1, "New scheme %s recorded" % hash)
            else:
                report(2, "Duplicates existing solution")
        except Exception as ex:
            report(0, "Failed to transmit scheme %s to server.  (%s)" % (scheme.sign(), str(ex)))


# Given scheme, what will you do about it?
def fileRoot(scheme, categoryProbabilities, seed):
    fields = ['run', scheme.sign()]
    aprob = categoryProbabilities['alpha']
    bprob = categoryProbabilities['beta']
    cprob = categoryProbabilities['gamma']
    if aprob == bprob:
        sab = "ab%.3d" % int(aprob * 100)
        fields += [sab]
    else:
        sa = "a%.3d" % int(aprob * 100)
        sb = "b%.3d" % int(bprob * 100)
        fields += [sa, sb]
    sc =  "c%.3d" % int(cprob * 100)
    fields += [sc]
    sseed = "S%.2d" % seed
    fields += [sseed]
    root = "-".join(fields)
    return "-".join(fields)


# Generate command file.  Return "" or root name of command file
def generateCommandFile(scheme, seed):
    froot = fileRoot(scheme, categoryProbabilities, seed)
    fname = froot + ".cmd"
    try:
        outf = open(fname, 'w')
    except Exception as ex:
        report(0, "Couldn't open '%s' to write" % fname)
        return ""
    scheme.ckt = circuit.Circuit(outf)
    scheme.generateProgram(categoryProbabilities, seed, timeLimit, fixKV = True, excludeSingleton = restrictSolutions, breadthFirst = True, levelList = levelList, useZdd = False)
    outf.close()
    return froot

# Run command file and process results
def runCommand(scheme, froot, method, recordFunction):
    fname = froot + ".cmd"
    lname = froot + "-" + method + ".log"
    cmd = ["/".join(homePathFields + runbddFields), '-c']
    cmd += ['-f', fname]
    cmd += ['-L', lname]
    cmdLine = " ".join(cmd)
    report(2, "Running '%s'" % cmdLine)
    if runbddQuiet:
        onull = open('/dev/null', 'w')
        p = subprocess.Popen(cmd, stdout = onull, stderr = onull)
    else:
        p = subprocess.Popen(cmd)
    p.wait()
    if p.returncode != 0:
        report(0, "Returning command '%s' failed.  Return code = %d" % (cmdLine, p.returncode))
        return False
    mm_parse.generateSolutions(lname, scheme, recordFunction)
    return True

def runScheme(scheme, recordFunction):
    seed = random.randrange(seedLimit)
    froot = generateCommandFile(scheme, seed)
    if froot == "":
        return False
    method = random.choice(reductionList) + random.choice(processingList)
    return runCommand(scheme, froot, method, recordFunction)

def runServer(port, generator):
    try:
        server = Server(port, generator)
    except Exception as ex:
        report(0, "Could not set up server on port %d (%s)" % (port, str(ex)))
    server.run()

def runClient(host, port):
    cli = Client(host, port)
    errorCount = 0
    generateCount = 0
    while errorCount < errorLimit:
        s = cli.next()
        if s is None:
            break
        generateCount += 1
        if not runScheme(s, cli.record):
            errorCount += 1
    report(0, "%d schemes generated.  %d errors" % (generateCount, errorCount))
    

def runStandalone(generator):
    errorCount = 0
    generateCount = 0
    while errorCount < errorLimit:
        s = generator.select()
        if s is None:
            break
        generateCount += 1
        if not runScheme(s, mm_parse.recordSolution):
            errorCount += 1
    report(0, "%d schemes generated.  %d errors" % (generateCount, errorCount))


#    [-h] [(-P PORT|-H HOST:PORT)] [-R] [-t SECS] [-c APROB:BPROB:CPROB] [-p PROCS] [-v VERB]
def run(name, args):
    global timeLimit
    global processingList
    global categoryProbabilities
    global restrictSolutions
    host = defaultHost
    port = defaultPort
    isServer = False
    isClient = False
    vlevel = 1
    limit = 100

    optlist, args = getopt.getopt(args, 'hP:H:Rt:c:p:l:v:')
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
                report(0, "Must have host of form HOST:PORT")
                usage(name)
                return
            host = fields[0]
            port = int(fields[1])
        elif opt == '-t':
            timeLimit = int(val)
        elif opt == '-c':
            fields = val.split(":")
            if len(fields) == 1:
                # Single probability for all categories
                try:
                    pct = int(fields[0])
                except:
                    print("Cannot find percentage of fixed assignments from '%s'" % val)
                    usage(name)
                prob = pct / 100.0
                categoryProbabilities = {'alpha':prob, 'beta':prob, 'gamma':prob}
            elif len(fields) == 3:
                try:
                    plist = [int(f)/100.0 for f in fields]
                except:
                    print("Cannot find 3 percentages of fixed assignments from '%s'" % val)
                    usage(name)
                categoryProbabilities = {'alpha':plist[0], 'beta':plist[1], 'gamma':plist[2]}
            else:
                print("Cannot find 3 percentages of fixed assignments from '%s'" % val)
                usage(name)
        elif opt == '-R':
            restrictSolutions = False
        elif opt == '-p':
            processingList = val.split(":")
        elif opt == '-l':
            limit = int(val)
        elif opt == '-v':
            vlevel = int(val)
    setVerbLevel(vlevel)
    mm_parse.loadDatabase(mm_parse.heuleDatabaseDict, mm_parse.heuleDatabasePathFields, mm_parse.quietMode)
    mm_parse.loadDatabase(mm_parse.generatedDatabaseDict, mm_parse.generatedDatabasePathFields, mm_parse.quietMode)
    if isClient:
        runClient(host, port)
    else:
        generator = SchemeGenerator(3, 23, permute = True, limit = limit)
        if isServer:
            runServer(port, generator)
        else:
            runStandalone(generator)
    

if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    mm_parse.homePathFields = homePathFields
    run(sys.argv[0], sys.argv[1:])

    
