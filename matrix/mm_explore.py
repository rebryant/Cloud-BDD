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
import datetime

import brent
import mm_parse
import circuit

def usage(name):
    print("Usage %s [-h] [-k] [(-P PORT|-H HOST:PORT)] [-R] [-t SECS] [-c APROB:BPROB:CPROB] [-p PROCS] [-v VERB] [-l LIMIT]")
    print("   -h               Print this message")
    print("  Server options")
    print("   -P PORT          Set up server on specified port")
    print("  Client options")
    print("   -H HOST:PORT     Retrieve source file name from server at HOST:PORT")
    print("  Local & server options")
    print("   -R               Allow unrestricted solution types")
    print("   -l LIMIT         Set limit on number of schemes generated")
    print("   -k               Put more weight on underrepresented kernels")
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

fixedProbabilities = False
categoryProbabilities = {'alpha':0.0, 'beta':0.0, 'gamma':1.0}
seedLimit = 100
errorLimit = 1000

restrictSolutions = True
keepFiles = False

balanceKernels = False

dim = (3, 3, 3)
auxCount = 27

defaultHost = 'localhost'
defaultPort = 6616

ckt = circuit.Circuit()

# Good choices for probabilities
abcList = ["20:55:70", "25:50:70",  "30:45:70", "15:45:80", "20:40:80", "25:35:80", "15:25:90", "20:25:90", "10:30:90"]

def report(level, s):
    if level <= verbLevel:
        print(s)

def deltaSeconds(dt):
    return dt.days*(24.0 * 3600) + dt.seconds + dt.microseconds * 1.0e-6

def setVerbLevel(level):
    global verbLevel, runbddQuiet
    verbLevel = level
    mm_parse.quietMode = level <= 2
    runbddQuiet = level <= 3

# Generate schemes for further processing
class SchemeGenerator:

    balanceKernels = False
    # Flat weighting mode: List of all solutions.
    # Kernel balancing mode.  Index candidates by kernel
    candidates = []
    
    dim = (3,3,3)
    auxCount = 23
    permute = True
    tryLimit = 50
    vpList = []
    limit = 100
    count = 0

    def __init__(self, dim, auxCount, permute = True, balanceKernels = False, limit = 10000000):
        self.dim = dim
        self.auxCount = auxCount
        self.permute = permute
        self.tryLimit = limit
        self.count = 0
        self.limit = limit
        self.balanceKernels = balanceKernels
        db = {}
        mm_parse.loadDatabase(db, mm_parse.generatedDatabasePathFields, True)
        mm_parse.loadDatabase(db, mm_parse.heuleDatabasePathFields, True)
        if self.balanceKernels:
            kcount = {}
            tcount = 0
            self.candidates = {}
            for v in db.values():
                hash = v[mm_parse.fieldIndex['kernel hash']]
                path = v[mm_parse.fieldIndex['path']]
                if hash in self.candidates:
                    self.candidates[hash].append(path)
                    kcount[hash] += 1
                else:
                    self.candidates[hash] = [path]
                    kcount[hash] = 1
            report(1, "%d candidates, %d kernels in database." % (len(db), len(kcount)))
            for hash in kcount.keys():
                report(2, "\t%s\t%d" % (hash, kcount[hash]))
        else:
            self.candidates = [v[mm_parse.fieldIndex['path']] for v in db.values()]
            report(1, "%d candidates in database." % len(db))
        self.vpList = [brent.ijk2var(p) for p in brent.allPermuters(list(range(3)))]
        
    def chooseCandidate(self):
        if self.balanceKernels:
            weights = [(hash, 1.0/(1+len(self.candidates[hash]))) for hash in self.candidates.keys()]
            total = sum([wt[1] for wt in weights])
            cval = random.random() * total
            accum = 0.0
            for i in range(len(self.candidates)):
                hash = weights[i][0]
                wt = weights[i][1]
                accum += wt
                if cval <= accum:
                    report(3, "Choosing candidate with hash %s, weight %f/%f" % (hash, wt, total))
                    return random.choice(self.candidates[hash])
            report(0, "Selection error.  Reach accumulated value %f looking for total %f" % (accum, total))
        else:
            return random.choice(self.candidates)

    def select(self):
        if self.count >= self.limit:
            if self.count == self.limit:
                report(1, "Generated %d schemes" % self.count)
            return None
        self.count += 1
        for t in range(self.tryLimit):
            p = self.chooseCandidate()
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
                report(2, "   Permuted as: %s (Hash = %s)" % (brent.showPerm(vp), s.sign()))
            return s
        report(0, "Couldn't find any candidates after %d tries" % self.tryLimit)
        return None

    def addCandidate(self, path):
        self.candidates.append(path)

    def countCandidates(self):
        return len(self.candidates)

class Server:
    generator = None
    server = None
    recordedCount = 0
    startTime = None
    solutionDict = {}

    def __init__(self, port, generator):
        host = ''
        self.generator = generator
        self.server = SimpleXMLRPCServer((host, port))
        self.server.register_function(self.next, "next")
        self.server.register_function(self.record, "record")
        self.server.register_function(self.notify, "notify")
        self.recordedCount = 0
        self.startTime = None
        self.solutionDict = {}
    
    def next(self):
        # Don't start timing until first request received
        if self.startTime is None:
            self.startTime = datetime.datetime.now()
        s = self.generator.select()
        if s is None:
            return False
        else:
            return s.bundle()

    def record(self, schemeBundle, metadata):
        # In case server gets restarted while client is executing
        if self.startTime is None:
            self.startTime = datetime.datetime.now()
        scheme = brent.MScheme(dim, auxCount, ckt).unbundle(schemeBundle).canonize()
        hash = scheme.sign()
        report(3, "Received bundle from client giving scheme %s" % hash)
        signature = scheme.signature()
        found = signature in self.solutionDict or hash in mm_parse.heuleDatabaseDict or hash in mm_parse.generatedDatabaseDict
        if found:
            report(2, "Solution %s duplicates existing one" % hash)
            return False
        else:
            self.solutionDict[signature] = hash
            path = mm_parse.recordSolution(scheme, metadata)
            self.recordedCount += 1
            self.generator.addCandidate(path)
            dt = datetime.datetime.now() - self.startTime
            secs = deltaSeconds(dt)
            prate = self.recordedCount * 3600.0 / secs
            ccount = self.generator.countCandidates()
            report(1, "New solution %s recorded.  Session total = %d (Avg %.1f solutions/hour).  Now have %d candidates" % (hash, self.recordedCount, prate, ccount))
            return True

    def notify(self, abc, secs, scount, gcount):
        crate = 100.0 * float(gcount)/(scount-1) if scount > 1 else 0.0
        grate = float(gcount) * 3600.0 / secs if secs > 0 else 0.0
        report(1, "Generation with probs %s.  Time = %.2f secs. Solutions = %d.  New Schemes = %d (%.1f%% of solutions).  Generation rate = %.1f schemes/hour"% (abc, secs, scount, gcount, crate, grate))
        return True

    def run(self):
        self.server.serve_forever()

class Client:
    host = ""
    port = ""
    startTime = None
    generatedCount = 0
    lastGeneratedCount = 0

    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.startTime = datetime.datetime.now()
        self.generatedCount = 0
        self.lastGeneratedCount = 0

    def connect(self):
        try:
            cname = 'http://%s:%d' % (self.host, self.port)
            report(3, "Attempting to connect to '%s'" % cname)
            c = xml_client.ServerProxy(cname)
            return c
        except Exception as ex:
            report(0, "Failed to connect to server (%s)" % str(ex))
            return None

    def incrCount(self, incr = 1):
        self.generatedCount += incr
        dt = datetime.datetime.now() - self.startTime
        secs = deltaSeconds(dt)
        return secs/self.generatedCount if self.generatedCount > 0 else 0.0

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

    def notify(self, abc, secs, scount):
        gcount = self.generatedCount - self.lastGeneratedCount
        self.lastGeneratedCount = self.generatedCount
        c =  self.connect()
        if c is None:
            return
        c.notify(abc, secs, scount, gcount)

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
                avg = self.incrCount()
                report(1, "New scheme %s recorded (avg time/scheme = %.1f secs)" % (hash, avg))
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

# Run command file and process results.  Return number of solutions generated (or -1 if error)
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
        return -1
    scount = mm_parse.generateSolutions(lname, scheme, recordFunction)
    if not keepFiles:
        try:
            os.remove(fname)
            os.remove(lname)
        except Exception as ex:
            report(0, "Could not remove files %s and %s (%s)" % (fname, lname, str(ex)))
    return scount

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
    runCount = 0
    startTime = datetime.datetime.now()
    while errorCount < errorLimit:
        schemeStart = datetime.datetime.now()
        if not fixedProbabilities:
            # Get a new set of probabilities
            parseABC(findABC())
        s = cli.next()
        if s is None:
            break
        runCount += 1
        scount = runScheme(s, cli.record)
        if scount < 0:
            errorCount += 1
        now = datetime.datetime.now()
        overallSeconds = deltaSeconds(now-startTime)
        currentSeconds = deltaSeconds(now-schemeStart)
        avg = runCount * 3600.0 / overallSeconds
        cli.notify(abcString(categoryProbabilities), currentSeconds, scount)
        report(1, "%.1f seconds (Average = %1.f runs/hour).  Generated %d solutions" % (currentSeconds, avg, scount))
    report(0, "%d schemes tested.  %d errors" % (runCount, errorCount))
    report(0, "%d new schemes recorded.  Average = %.1f secs/scheme" % (cli.generatedCount, cli.incrCount(0)))
    

def runStandalone(generator):
    errorCount = 0
    generateCount = 0
    while errorCount < errorLimit:
        s = generator.select()
        if s is None:
            break
        generateCount += 1
        if runScheme(s, mm_parse.recordSolution) < 0:
            errorCount += 1
    report(0, "%d schemes generated.  %d errors" % (generateCount, errorCount))

def findABC():
    return random.choice(abcList)

# Parse probabilities given in form P or Pa:Pb:Pc
# Return True or False
def parseABC(abc):
    global categoryProbabilities
    fields = abc.split(":")
    if len(fields) == 1:
        try:
            pct = int(fields[0])
        except:
            report(0, "Cannot find percentage of fixed assignments from '%s'" % abc)
            return False
        prob = pct / 100.0
        categoryProbabilities = {'alpha':prob, 'beta':prob, 'gamma':prob}
    elif len(fields) == 3:
        try:
            plist = [int(f)/100.0 for f in fields]
        except:
            report(0, "Cannot find 3 percentages of fixed assignments from '%s'" % abc)
            return False
        categoryProbabilities = {'alpha':plist[0], 'beta':plist[1], 'gamma':plist[2]}
        return True
    else:
        report(0, "Cannot find 3 percentages of fixed assignments from '%s'" % abc)
        return False

def abcString(categoryProbabilities):
    plist = [categoryProbabilities[cat] for cat in ('alpha', 'beta', 'gamma')]
    pctlist = [str(int(p*100)) for p in plist]
    return ':'.join(pctlist)

#    [-h] [(-P PORT|-H HOST:PORT)] [-R] [-t SECS] [-c APROB:BPROB:CPROB] [-p PROCS] [-v VERB]
def run(name, args):
    global timeLimit
    global processingList
    global categoryProbabilities
    global fixedProbabilities
    global restrictSolutions
    global keepFiles
    global balanceKernels
    host = defaultHost
    port = defaultPort
    isServer = False
    isClient = False
    vlevel = 1
    limit = 100000000
    abc = findABC()

         

    optlist, args = getopt.getopt(args, 'hkP:H:Rt:c:p:l:v:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        if opt == '-k':
            balanceKernels = True
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
            abc = val
            fixedProbabilities = True
        elif opt == '-R':
            restrictSolutions = False
        elif opt == '-p':
            processingList = val.split(":")
        elif opt == '-l':
            limit = int(val)
        elif opt == '-v':
            vlevel = int(val)
    setVerbLevel(vlevel)
    if not parseABC(abc):
        usage(name)
        return
    mm_parse.loadDatabase(mm_parse.heuleDatabaseDict, mm_parse.heuleDatabasePathFields, mm_parse.quietMode)
    mm_parse.loadDatabase(mm_parse.generatedDatabaseDict, mm_parse.generatedDatabasePathFields, mm_parse.quietMode)
    if isClient:
        runClient(host, port)
    else:
        generator = SchemeGenerator(3, 23, permute = True, balanceKernels = balanceKernels, limit = limit)
        if isServer:
            runServer(port, generator)
        else:
            runStandalone(generator)
    

if __name__ == "__main__":
    current = os.path.realpath(__file__)
    homePathFields = current.split('/')[:-1]
    mm_parse.homePathFields = homePathFields
    run(sys.argv[0], sys.argv[1:])

    
