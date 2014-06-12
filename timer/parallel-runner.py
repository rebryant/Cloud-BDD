import shlex, subprocess, sys, time, getopt
from subprocess import PIPE

inputFileName = 'instructions-runner.txt'
outputFileName = ''
portStr = ''
numTrials = 1
useDeltaTime = False
verbosity = 0
getUtilDetailsBool = True

def runRounds(runOptions):
    global inputFileName, outputFileName, portStr
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
    print("Parsed inputFile: " + inputFileName)
    print("Source File for tests: " + sourceFileName)
    print("Router Worker List: " + str(routerWorkerList))

    # parse the hosts list to get the correct hosts and interfaces
    hostsFile = open("/etc/hosts", 'r')
    hostsList = []
    for line in hostsFile:
        hostStr = line.lstrip().rstrip()
        if (hostStr.endswith('-deth')):
            (ipStr, nameStr) = hostStr.split(" ", 1)
            nameStr = nameStr.lstrip().rstrip()
            hostsList.append(nameStr)
    file.close(hostsFile)

    maxHosts = len(hostsList)

    for (routers, workers) in routerWorkerList:
        if (maxHosts < routers + workers + 2):
            print("Not enough nodes allocated for " +
                  ('%i routers and %i workers' % (routers, workers)))
            sys.exit(2)

        # create final name for each outputfile
        outputFileName = outputFileName + "-r" + str(routers) + "-w" + str(workers) + ".csv"

        controllerHost = ''
        clientHost = ''
        routerHosts = []
        workerHosts = []
        tempHosts = hostsList
        # assign each node to a job
        controllerHost = tempHosts.pop()
        clientHost = tempHosts.pop()
        for x in range(routers):
            routerHosts.append(tempHosts.pop())
        for x in range(workers):
            workerHosts.append(tempHosts.pop())

        # build controller arglist
        controllerArgList = ['pdsh', '-R', 'exec', '-w']
        controllerArgList.append(controllerHost)
        controllerArgList.append('ssh')
        controllerArgList.append('-oStrictHostKeyChecking=no')
        controllerArgList.append('-x')
        controllerArgList.append('%%h')
        controllerArgList.append('/proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/controller')
        controllerArgList.append("-p")
        controllerArgList.append(portStr)
        controllerArgList.append("-r")
        controllerArgList.append(str(routers))
        controllerArgList.append("-w")
        controllerArgList.append(str(workers))

        controllerProc = subprocess.Popen(controllerArgList)

        # build router arglist
        routerArgList = ['pdsh', '-R', 'exec', '-w']
        routerListStr = routerHosts.pop()
        for routerHost in routerHosts:
            routerListStr = routerListStr + "," + routerHost
        routerArgList.append(routerListStr)
        routerArgList.append('ssh')
        routerArgList.append('-oStrictHostKeyChecking=no')
        routerArgList.append('-x')
        routerArgList.append('%%h')
        routerArgList.append('/proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/router')
        routerArgList.append("-H")
        routerArgList.append(controllerHost)
        routerArgList.append("-P")
        routerArgList.append(portStr)

        routerProc = subprocess.Popen(routerArgList)

        # build worker arglist
        workerArgList = ['pdsh', '-R', 'exec', '-w']
        workerListStr = workerHosts.pop()
        for workerHost in workerHosts:
            workerListStr = workerListStr + "," + workerHost
        workerArgList.append(workerListStr)
        workerArgList.append('ssh')
        workerArgList.append('-oStrictHostKeyChecking=no')
        workerArgList.append('-x')
        workerArgList.append('%%h')
        workerArgList.append('/proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/bworker')
        workerArgList.append("-H")
        workerArgList.append(controllerHost)
        workerArgList.append("-P")
        workerArgList.append(portStr)

        workerProc = subprocess.Popen(workerArgList)

        # finally, build client arglist

        clientArgList = ['pdsh', '-R', 'exec', '-w']
        clientArgList.append(clientHost)
        clientArgList.append('ssh')
        clientArgList.append('-oStrictHostKeyChecking=no')
        clientArgList.append('-x')
        clientArgList.append('%%h')

        compoundStr = '\'cd /proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/timer; python csv-tester.py'
        for option in runOptions:
            compoundStr = compoundStr + ' ' + option
        compoundStr = compoundStr + ' -H ' + controllerHost
        compoundStr = compoundStr + ' -o ' + outputFileName
        compoundStr = compoundStr + ' -i ' + sourceFileName
        compoundStr = compoundStr + '\''
        print(compoundStr)
        clientArgList.append(compoundStr)

        # create the script process; wait for it to execute
        # error-checking goes HERE
        clientProc = subprocess.Popen(clientArgList)
        clientProc.wait()

        # kill off the workers, routers, and controllers via
        # the pdsh SIGINT method
        workerProc.send_signal(SIGINT)
        workerProc.send_signal(SIGINT)
        routerProc.send_signal(SIGINT)
        routerProc.send_signal(SIGINT)
        controllerProc.send_signal(SIGINT)
        controllerProc.send_signal(SIGINT)


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
    print("\t-v VERBOSITY     Verbose mode. Prints output. Level 1: Prints individual commands and times; Level 2: Prints everything. Default: 0.")
    print("\t-u UTIL DETAILS  1 to list utilization details (peak bytes, peak ITEs, etc.) Default: 1")
    print("")


def main():
    try:
        opts, args = getopt.getopt(sys.argv[1:], "P:dcli:o:t:n:hv:u:", [])
    except getopt.GetoptError as err:
        print(err)
        usage()
        sys.exit(2)

    global useDeltaTime, inputFileName, outputFileName
    global portStr, numTrials, verbosity
    global getUtilDetailsBool
    runOptions = []

    for opt, arg in opts:
        if opt == "-P":
            portStr = arg
        elif opt == "-t":
            if (int(arg) == 1):
                useDeltaTime = True
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
    runOptions.append("-P")
    runOptions.append(portStr)
    runOptions.append("-t")
    runOptions.append(str((1 if useDeltaTime else 0)))
    runOptions.append("-n")
    runOptions.append(str(numTrials))
    runOptions.append("-v")
    runOptions.append(str(verbosity))
    runOptions.append("-u")
    runOptions.append(str((1 if getUtilDetailsBool else 0)))

    runRounds(runOptions)


if __name__ == "__main__":
    main()
    print("Done!")
