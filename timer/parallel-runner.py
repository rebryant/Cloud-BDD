import shlex, subprocess, sys, time, getopt, signal, string, os
from subprocess import PIPE

inputFileName = 'instructions-runner.txt'
outputFileName = ''
portStr = '6616'
numTrials = 1
useDeltaTime = False
verbosity = 0
getUtilDetailsBool = True
specialDeltaTime = False
hostFileStr = "/etc/hosts"
localizeRouters = False

'''
Creates the controller, routers, workers, and clients via pdsh commands, runs the csv-tester script, then kills off the jobs.
'''
def runRounds(runOptions):
    global inputFileName, outputFileName, portStr, hostFileStr, localizeRouters
    inputFile = open(inputFileName, 'r')
    sourceFileName = ''
    routerWorkerList = []
    # parse input as a file containing the tests to run, and a set of lines, each containing a tuple of numbers representing the router and worker configurations
    for line in inputFile:
        if (len(sourceFileName) == 0):
            sourceFileName = line.rstrip()
        else:
            (routerStr, workerStr) = line.split(',', 1)
            routerStr = routerStr.lstrip().rstrip()
            workerStr = workerStr.lstrip().rstrip()
            routerWorkerList.append((int(routerStr), int(workerStr)))
    file.close(inputFile)
    if (verbosity >= 1):
        print("Parsed inputFile: " + inputFileName)
        print("Source File for tests: " + sourceFileName)
        print("Router Worker List: " + str(routerWorkerList))

    # parse the hosts list to get the correct hosts and interfaces
    hostsFile = open(hostFileStr, 'r')
    hostsList = []
    for line in hostsFile:
        hostStr = line.lstrip().rstrip()
        if (hostStr.endswith('-deth')):
            (ipStr, nameStr) = hostStr.split(" ", 1)
            nameStr = nameStr.lstrip().rstrip()
            if (nameStr.startswith('h')):
                hostsList.append(nameStr)
    file.close(hostsFile)

    # QUERY LIVE VS. DEAD HOSTS

    maxHosts = len(hostsList)
    if (verbosity >= 1):
        print("The list of hosts is:")
        print(hostsList)

    for (routers, workers) in routerWorkerList:
        # make sure there are enough hosts and workers to satisfy this request
        # CUSTOMIZE IF ROUTERS/WORKERS ARE STARTED TOGETHER
        if (maxHosts < routers + workers + 2 and not(localizeRouters)):
            print("Not enough nodes allocated for " +
                  ('%i routers and %i workers' % (routers, workers)))
            sys.exit(2)
        if (maxHosts < max(routers, workers) + 2 and localizeRouters):
            print("Not enough nodes allocated for " +
                  ('%i routers and %i workers (localized routers)' % (routers, workers)))

        # create final name for each outputfile
        thisOutputFileName = outputFileName + "-r" + str(routers) + "-w" + str(workers) + ".csv"

        # increment port
        portStr = str(int(portStr) + 1)
        if (verbosity >= 1):
            print("This job will be run on port " + portStr)

        controllerHost = ''
        clientHost = ''
        routerHosts = []
        workerHosts = []
        tempHosts = list(hostsList)
        # assign each node to a job
        controllerHost = tempHosts.pop()
        clientHost = tempHosts.pop()
        for x in range(workers):
            workerHosts.append(tempHosts.pop())
        for x in range(routers):
            if localizeRouters and x >= 0 and x < len(workerHosts):
                routerHosts.append(workerHosts[x])
            else:
                routerHosts.append(tempHosts.pop())


        # build controller arglist
        controllerArgList = ['/usr/bin/pdsh', '-R', 'exec', '-w']
        controllerArgList.append(controllerHost)
        controllerArgNewList = []
        controllerArgNewList.append('ssh')

        controllerArgNewList.append('-oStrictHostKeyChecking=no')
        controllerArgNewList.append('-x')
        controllerArgNewList.append('%h')

        controllerArgSSHList = []
        controllerArgSSHList.append('/proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/Cloud-BDD-master/controller')
        controllerArgSSHList.append("-p")
        controllerArgSSHList.append(portStr)
        controllerArgSSHList.append("-c")
        controllerArgSSHList.append(str(1))
        controllerArgSSHList.append("-r")
        controllerArgSSHList.append(str(routers))
        controllerArgSSHList.append("-w")
        controllerArgSSHList.append(str(workers))
        controllerArgNewList.append("\'" + string.join(controllerArgSSHList, " ") + "\'")
        if (verbosity >= 2):
            print("The arguments to create the controller process are:")
            print(controllerArgList)
            print("The arguments written to that process are:")
            print(string.join(controllerArgNewList, " ") + "\n")
        controllerProc = subprocess.Popen(controllerArgList, stdin=PIPE)
        controllerProc.stdin.write(string.join(controllerArgNewList, " ") + "\n")
        if (verbosity >= 1):
            print("Controller pid " + str(controllerProc.pid) + "\n Corrected pid: " + str(controllerProc.pid+1)) # pdsh spawns a second process, which generally has a pid that is one larger than the controller. We keep track of this to kill it off at the end of execution

        # provide a short delay to ensure that processes are created
        time.sleep(2)

        # build router arglist
        routerArgList = ['/usr/bin/pdsh', '-R', 'exec', '-w']
        routerListStr = routerHosts.pop()
        for routerHost in routerHosts:
            routerListStr = routerListStr + "," + routerHost
        routerArgList.append(routerListStr)

        routerArgNewList = []
        routerArgNewList.append('ssh')
        routerArgNewList.append('-oStrictHostKeyChecking=no')
        routerArgNewList.append('-x')
        routerArgNewList.append('%h')

        routerArgSSHList = []
        routerArgSSHList.append('/proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/Cloud-BDD-master/router')
        routerArgSSHList.append("-H")
        routerArgSSHList.append(controllerHost)
        routerArgSSHList.append("-P")
        routerArgSSHList.append(portStr)

        if (verbosity >= 2):
            print("The arguments to create the router process are:")
            print(routerArgList)
            print("The arguments written to that process are:")
            print(string.join(routerArgNewList, " ") + "\n")

        routerArgNewList.append("\'" + string.join(routerArgSSHList, " ") + "\'")
        routerProc = subprocess.Popen(routerArgList, stdin=PIPE, shell=False)
        routerProc.stdin.write(string.join(routerArgNewList, " ") + "\n")

        if (verbosity >= 1):
            print("Router pid " + str(routerProc.pid) + "\n Corrected pid: " + str(routerProc.pid+1))

        time.sleep(2)

        # build worker arglist
        workerArgList = ['/usr/bin/pdsh', '-R', 'exec', '-w']
        workerListStr = workerHosts.pop()
        for workerHost in workerHosts:
            workerListStr = workerListStr + "," + workerHost
        workerArgList.append(workerListStr)

        workerArgNewList = []
        workerArgNewList.append('ssh')
        workerArgNewList.append('-oStrictHostKeyChecking=no')
        workerArgNewList.append('-x')
        workerArgNewList.append('%h')

        workerArgSSHList = []
        workerArgSSHList.append('/proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/Cloud-BDD-master/bworker')
        workerArgSSHList.append("-H")
        workerArgSSHList.append(controllerHost)
        workerArgSSHList.append("-P")
        workerArgSSHList.append(portStr)
        workerArgNewList.append("\'" + string.join(workerArgSSHList, " ") + "\'")

        if (verbosity >= 2):
            print("The arguments to create the worker process are:")
            print(workerArgList)
            print("The arguments written to that process are:")
            print(string.join(workerArgNewList, " ") + "\n")

        workerProc = subprocess.Popen(workerArgList, stdin=PIPE, shell=False)
        workerProc.stdin.write(string.join(workerArgNewList, " ") + "\n")
        if (verbosity >= 1):
            print("Worker pid " + str(workerProc.pid) + "\n Corrected pid: " + str(workerProc.pid+1))


        time.sleep(2)

        # finally, build client arglist

        clientArgList = ['pdsh', '-R', 'exec', '-w']
        clientArgList.append(clientHost)
        clientArgList.append('ssh')
        clientArgList.append('-oStrictHostKeyChecking=no')
        clientArgList.append('-x')
        clientArgList.append('%h')

        compoundStr = '\'cd /proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/Cloud-BDD-master/timer; python csv-tester.py'
        for option in runOptions:
            compoundStr = compoundStr + ' ' + option
        compoundStr = compoundStr + ' -H ' + controllerHost
        compoundStr = compoundStr + ' -o ' + thisOutputFileName
        compoundStr = compoundStr + ' -i ' + sourceFileName
        compoundStr = compoundStr + " -P " + portStr

        compoundStr = compoundStr + '\''
        clientArgList.append(compoundStr)

        if (verbosity >= 2):
            print("The arguments to create the client process are:")
            print(string.join(clientArgList, " "))

        # create the script process; wait for it to execute
        # error-checking goes HERE
        clientProc = subprocess.Popen(string.join(clientArgList, " "), shell=True)
        if (verbosity >= 1):
            print("Client pid " + str(clientProc.pid) + "\n Corrected pid: " + str(clientProc.pid+1))

        clientProc.wait()

        if (verbosity >= 2):
            print("Worker pid " + str(workerProc.pid) + "\n Corrected pid: " + str(workerProc.pid+1))
            print("Router pid " + str(routerProc.pid) + "\n Corrected pid: " + str(routerProc.pid+1))
            print("Controller pid " + str(controllerProc.pid) + "\n Corrected pid: " + str(controllerProc.pid+1))
            print("Client pid " + str(clientProc.pid) + "\n Corrected pid: " + str(clientProc.pid+1))

        # kill off the host jobs
        time.sleep(2)
        killallWorkerList = ['/usr/bin/pdsh', '-R', 'exec', '-w']
        killallWorkerList.append(workerListStr)
        killallWorkerList.extend(['ssh', '-oStrictHostKeyChecking=no', '-x', '%h', "\'killall -9 bworker\'"])
        if (verbosity >= 2):
            print("The shell command to kill all workers:")
            print(string.join(killallWorkerList, " "))
        workerKillProc = subprocess.Popen(string.join(killallWorkerList, " "), shell=True)

        time.sleep(2)

        killallRouterList = ['/usr/bin/pdsh', '-R', 'exec', '-w']
        killallRouterList.append(routerListStr)
        killallRouterList.extend(['ssh', '-oStrictHostKeyChecking=no', '-x', '%h', "\'killall -9 router\'"])

        if (verbosity >= 2):
            print("The shell command to kill all routers:")
            print(string.join(killallRouterList, " "))

        routerKillProc = subprocess.Popen(string.join(killallRouterList, " "), shell=True)

        time.sleep(2)

        killallControllerList = ['/usr/bin/pdsh', '-R', 'exec', '-w']
        killallControllerList.append(controllerHost)
        killallControllerList.extend(['ssh', '-oStrictHostKeyChecking=no', '-x', '%h', "\'killall -9 controller\'"])

        if (verbosity >= 2):
            print("The shell command to kill the controller:")
            print(string.join(killallControllerList, " "))

        controllerKillProc = subprocess.Popen(string.join(killallControllerList, " "), shell=True)

        time.sleep(2)

'''
Prints usage instructions.
'''
def usage():
    print("This little program times the distributed BDD package.")
    usageStr = "Usage: python csv-tester.py [-h] [-H HOST] [-P PORT]"
    usageStr += " [-i INPUTFILE] [-o OUTPUTFILE] [-t USE DELTATIME] [-n NUM]"
    usageStr += " [-c][-d][-l]"
    print(usageStr)
    print("\t-h               This help output.")
    print("\t-P PORT          The port of the controller. Default: 6616")
    print("\t-i INPUTFILE     The file name to take input from. Default: instructions-runner.txt")
    print("\t-o OUTPUTFILE    The base file name to take output from. Default: times-(timestamp)-r#-w#.csv")
    print("\t-t USE DELTATIME 1 to use the delta times from the program, 0 to use the timer in this script. Default: 0")
    print("\t-n NUM           The number of trials for each command. Default: 1")
    print("\t-c               Uses the CUDD package for timing. Default: disabled.")
    print("\t-d               Uses the distributed package for timing. Default: enabled.")
    print("\t-l               Uses the local refs for timing. Default: disabled.")
    print("\t-f               Allows you to specify the file containing the hosts and IPs, as described in the README")
    print("\t-r               Runs each router on the same system as a worker")
    print("\t-v VERBOSITY     Verbose mode. Prints output. Level 1: Prints individual commands and times; Level 2: Prints everything. Default: 0.")
    print("\t-u UTIL DETAILS  1 to list utilization details (peak bytes, peak ITEs, etc.) Default: 1")
    print("")


def main():
    try:
        opts, args = getopt.getopt(sys.argv[1:], "P:dcli:o:t:n:hv:u:f:r", [])
    except getopt.GetoptError as err:
        print(err)
        usage()
        sys.exit(2)

    global useDeltaTime, inputFileName, outputFileName
    global portStr, numTrials, verbosity, hostFileStr, localizeRouters
    global getUtilDetailsBool, specialDeltaTime
    runOptions = []

    for opt, arg in opts:
        if opt == "-P":
            portStr = arg
        elif opt == "-t":
            if (int(arg) == 1):
                useDeltaTime = True
            elif (int(arg) == 2):
                specialDeltaTime = True
        elif opt == "-i":
            inputFileName = arg
        elif opt == "-o":
            outputFileName = arg
        elif opt == "-d":
            runOptions.append(opt)
        elif opt == "-c":
            runOptions.append(opt)
        elif opt == "-l":
            runOptions.append(opt)
        elif opt == "-n":
            numTrials = int(arg)
        elif opt == "-h":
            usage()
            sys.exit(0)
        elif opt == "-v":
            verbosity = int(arg)
        elif opt == "-u":
            if (int(arg) == 0):
                getUtilDetailsBool = False
        elif opt == "-f":
            hostFileStr = arg
        elif opt == "-r":
            localizeRouters = True
        else:
            print("Invalid option.")
            usage()
            sys.exit(2)

    # by default we test the distributed refs
    if len(runOptions) == 0:
        runOptions.append("-d")

    # create a base output name
    if len(outputFileName) == 0:
        outputFileName = outputFileName + "times"

    outputFileName = outputFileName + "-"
    outputFileName = outputFileName + str(time.time())

    # add all options to run
    runOptions.append("-t")
    runOptions.append(str((1 if useDeltaTime else (2 if specialDeltaTime else 0))))
    runOptions.append("-n")
    runOptions.append(str(numTrials))
    runOptions.append("-v")
    runOptions.append(str(verbosity))
    runOptions.append("-u")
    runOptions.append(str((1 if getUtilDetailsBool else 0)))

    runRounds(runOptions)


if __name__ == "__main__":
    pid = os.fork()
    if (pid == 0):
        main()
        print("Done!")
        sys.exit(0)
    else:
        if (verbosity >= 2):
            print("Forked a child...")
            print("Child pid: %i" % pid)
        sys.exit(0)
