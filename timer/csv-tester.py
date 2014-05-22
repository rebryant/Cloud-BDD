import shlex, subprocess, sys, time, getopt
from subprocess import PIPE

inputFileName = 'instructions-scripts.txt'
outputFileName = 'times-scripts.csv'
portStr = '6616'
hostStr = ''
numTrials = 1
useDeltaTime = False
verbosity = 0


def runTests(inputFile, timeFile, p):
    global useDeltaTime, numTrials, verbosity
    for line in inputFile:
        timeFile.write(line.rstrip() + ",")
        for i in range(numTrials):
            if (verbosity > 0):
                print(line.rstrip())
            # we keep track of the number of times that
            # time is called, so that we know when to
            # stop reading STDOUT (we check for the
            # time output)
            numTime = 0
            p.stdin.write('time')
            p.stdin.write('\n')
            numTime = numTime + 1

            # since source doesn't work correctly, we open
            # the source files ourselves and write the commands in
            if (line.startswith('source')):
                tempFile = open('../' + line.lstrip('source').lstrip().rstrip(), 'r')
                startTime = time.time()
                for cmdLine in tempFile:
                    if (cmdLine.lstrip().rstrip() == 'time'):
                        numTime = numTime + 1
                    p.stdin.write(cmdLine.rstrip())
                    p.stdin.write('\n')
                p.stdin.write('time')
                p.stdin.write('\n')
                numTime = numTime + 1
            else:
                tempFile = None
                startTime = time.time()
                p.stdin.write(line.rstrip())
                p.stdin.write('\n')
                p.stdin.write('time')
                p.stdin.write('\n')
                numTime = numTime + 1

            newNumTime = 0
            # find all the delta times from the program output
            deltaTimeList = []
            while (newNumTime < numTime):
                readStr = p.stdout.readline().lstrip('cmd>')
                if (verbosity > 1):
                    print(readStr.rstrip())
                if (readStr.startswith('Elapsed')):
                    # capture delta time output here
                    (temp1, temp2, deltaTime) = readStr.split('=', 2)
                    deltaTimeList.append(float(deltaTime))
                    newNumTime = newNumTime + 1
            endTime = time.time()
            diffTime = endTime - startTime
            deltaTime = 0.0
            # we want to ignore the delta time between the start
            # of this operation and the end of the previous
            deltaTimeList = deltaTimeList[1:]
            for x in deltaTimeList:
                deltaTime = deltaTime + x
            if (verbosity > 0):
                print('operation \'' + line.rstrip() + '\' took ' + ('%.4f' % diffTime) + ' seconds. Delta time (from program): ' + ('%.4f' % deltaTime) + ' \n')
            if (tempFile != None):
                file.close(tempFile)

            if (useDeltaTime):
                timeFile.write("," + ('%.4f' % deltaTime))
            else:
                timeFile.write("," + ('%.4f' % diffTime))
        timeFile.write("\r\n")

def runTimer(runOptions):
    global useDeltaTime, inputFileName, outputFileName
    global portStr, hostStr, numTrials

    timeFile = open(outputFileName, 'w')

    timeFile.write(",")
    for i in range(numTrials):
        timeFile.write("(seconds),Trial " + str(i + 1))
    timeFile.write("\r\n")



    for opt in runOptions:
        inputFile = open(inputFileName, 'r')

        # write current mode to row
        modeStr = ""
        if (opt == "-c"):
            modeStr = "CUDD"
        elif (opt == "-d"):
            modeStr = "Distributed Refs"
        elif (opt == "-l"):
            modeStr = "Local Refs"
        timeFile.write(modeStr + ",")
        for i in range(numTrials):
            timeFile.write(",")
        timeFile.write("\r\n")

        #write empty row
        timeFile.write(",")
        for i in range(numTrials):
            timeFile.write(",")
        timeFile.write("\r\n")

        # build list of arguments
        argList = ['.././runbdd']
        argList.append(opt)
        if (hostStr != ''):
            argList.append("-H")
            argList.append(hostStr)
        argList.append("-P")
        argList.append(portStr)

        # run timer and output to file
        p = subprocess.Popen(argList, stdin=PIPE, stdout=PIPE, bufsize = 0)

        runTests(inputFile, timeFile, p)
        p.kill()
        file.close(inputFile)
        # create empty row
        timeFile.write(",")
        for i in range(numTrials):
            timeFile.write(",")
        timeFile.write("\r\n")

    file.close(timeFile)

def usage():
    print("This little program times the distributed BDD package.")
    usageStr = "Usage: python csv-tester.py [-h] [-H HOST] [-P PORT]"
    usageStr += " [-i INPUTFILE] [-o OUTPUTFILE] [-t USE DELTATIME] [-n NUM]"
    usageStr += " [-c][-d][-l]"
    print(usageStr)
    print("\t-h               This help output.")
    print("\t-H HOST          The  host of the controller. Default: localhost")
    print("\t-P PORT          The port of the controller. Default: 6616")
    print("\t-i INPUTFILE     The file name to take input from. Default: instructions-scripts.txt")
    print("\t-o OUTPUTFILE    The file name to take output from. Default: times-scripts.txt")
    print("\t-t USE DELTATIME 1 to use the delta times from the program, 0 to use the timer in this script. Default: 0")
    print("\t-n NUM           The number of trials for each command. Default: 1")
    print("\t-c               Uses the CUDD package for timing. Default: disabled.")
    print("\t-d               Uses the distributed package for timing. Default: enabled.")
    print("\t-l               Uses the local refs for timing. Default: disabled.")
    print("\t-v VERBOSITY     Verbose mode. Prints output. Level 1: Prints individual commands and times; Level 2: Prints everything. Default: 0.")
    print("")

def main():
    try:
        opts, args = getopt.getopt(sys.argv[1:], "H:P:dcli:o:t:n:hv:", [])
    except getopt.GetoptError as err:
        print(err)
        usage()
        sys.exit(2)

    global useDeltaTime, inputFileName, outputFileName
    global portStr, hostStr, numTrials, verbosity
    runOptions = []

    for opt, arg in opts:
        if opt == "-P":
            portStr = arg
        elif opt == "-H":
            hostStr = arg
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
        else:
            print("Invalid option.")
            usage()
            sys.exit(2)

    # by default we test the distributed refs
    if len(runOptions) == 0:
        runOptions.append("-d")

    runTimer(runOptions)


if __name__ == "__main__":
    main()
    print("Done!")
