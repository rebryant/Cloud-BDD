import shlex, subprocess, sys, time, getopt, signal, string, os
from subprocess import PIPE

inputFileName = 'instructions-runner.txt'
outputFileName = ''
portStr = '6616'
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
            if (nameStr.startswith('h')):
                hostsList.append(nameStr)
    file.close(hostsFile)

    maxHosts = len(hostsList)
    print(hostsList)

    for (routers, workers) in routerWorkerList:
        if (maxHosts < routers + workers + 2):
            print("Not enough nodes allocated for " +
                  ('%i routers and %i workers' % (routers, workers)))
            sys.exit(2)

        # create final name for each outputfile
        thisOutputFileName = outputFileName + "-r" + str(routers) + "-w" + str(workers) + ".csv"

        controllerHost = ''
        clientHost = ''
        routerHosts = []
        workerHosts = []
        tempHosts = list(hostsList)
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
        controllerArgList.append('%h')

        controllerArgSSHList = []
        controllerArgSSHList.append('/proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/controller')
        controllerArgSSHList.append("-p")
        controllerArgSSHList.append(portStr)
        controllerArgSSHList.append("-r")
        controllerArgSSHList.append(str(routers))
        controllerArgSSHList.append("-w")
        controllerArgSSHList.append(str(workers))
        controllerArgList.append("\'" + string.join(controllerArgSSHList) + "\'") 

        print(string.join(controllerArgList, " "))
        controllerProc = subprocess.Popen(string.join(controllerArgList, " "), shell=True)
        print("Created controller.")
        
        time.sleep(2)

        # build router arglist
        routerArgList = ['pdsh', '-R', 'exec', '-w']
        routerListStr = routerHosts.pop()
        for routerHost in routerHosts:
            routerListStr = routerListStr + "," + routerHost
        routerArgList.append(routerListStr)
        routerArgList.append('ssh')
        routerArgList.append('-oStrictHostKeyChecking=no')
        routerArgList.append('-x')
        routerArgList.append('%h')

        routerArgSSHList = []
        routerArgSSHList.append('/proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/router')
        routerArgSSHList.append("-H")
        routerArgSSHList.append(controllerHost)
        routerArgSSHList.append("-P")
        routerArgSSHList.append(portStr)
        
        routerArgList.append("\'" + string.join(routerArgSSHList, " ") + "\'")
        print(string.join(routerArgList, " "))
        routerProc = subprocess.Popen(string.join(routerArgList, " "), shell=True)
        
        time.sleep(2)
        # build worker arglist
        workerArgList = ['pdsh', '-R', 'exec', '-w']
        workerListStr = workerHosts.pop()
        for workerHost in workerHosts:
            workerListStr = workerListStr + "," + workerHost
        workerArgList.append(workerListStr)
        workerArgList.append('ssh')
        workerArgList.append('-oStrictHostKeyChecking=no')
        workerArgList.append('-x')
        workerArgList.append('%h')
 
        workerArgSSHList = []
        workerArgSSHList.append('/proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/bworker')
        workerArgSSHList.append("-H")
        workerArgSSHList.append(controllerHost)
        workerArgSSHList.append("-P")
        workerArgSSHList.append(portStr)
        workerArgList.append("\'" + string.join(workerArgSSHList, " ") + "\'")
        print(string.join(workerArgList, " "))

        workerProc = subprocess.Popen(string.join(workerArgList, " "), shell=True)
        time.sleep(2)
        # finally, build client arglist

        clientArgList = ['pdsh', '-R', 'exec', '-w']
        clientArgList.append(clientHost)
        clientArgList.append('ssh')
        clientArgList.append('-oStrictHostKeyChecking=no')
        clientArgList.append('-x')
        clientArgList.append('%h')

        compoundStr = '\'cd /proj/CloudBDD/CloudBDD-Test/Cloud-BDD-master/timer; python csv-tester.py'
        for option in runOptions:
            compoundStr = compoundStr + ' ' + option
        compoundStr = compoundStr + ' -H ' + controllerHost
        compoundStr = compoundStr + ' -o ' + thisOutputFileName
        compoundStr = compoundStr + ' -i ' + sourceFileName
        compoundStr = compoundStr + '\''
        clientArgList.append(compoundStr)

        # create the script process; wait for it to execute
        # error-checking goes HERE
        print(string.join(clientArgList, " "))
        clientProc = subprocess.Popen(string.join(clientArgList, " "), shell=True)
        clientProc.wait()

        # kill off the workers, routers, and controllers via
        # the pdsh signal.SIGINT method
        workerProc.send_signal(signal.SIGINT)
        workerProc.send_signal(signal.SIGINT)
        routerProc.send_signal(signal.SIGINT)
        routerProc.send_signal(signal.SIGINT)
        controllerProc.send_signal(signal.SIGINT)
        controllerProc.send_signal(signal.SIGINT)


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
    pid = os.fork()
    if (pid == 0): 
        main()
        print("Done!")
        sys.exit(0)
    else:
        print("Forked a child...")
        print("Child pid: %i" % pid)
        sys.exit(0)
