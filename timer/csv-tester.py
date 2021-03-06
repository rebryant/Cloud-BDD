import shlex, subprocess, sys, time, getopt
from subprocess import PIPE

inputFileName = 'instructions-scripts.txt'
outputFileName = 'times-scripts.csv'
portStr = '6616'
hostStr = ''
numTrials = 1
useDeltaTime = False
specialDeltaTime = True
getUtilDetailsBool = False

verbosity = 0

details = []

numDistributedDetails = 5
numLocalDetails = 2
numCUDDDetails = 2
numTotalDetails = max(max(numDistributedDetails, numLocalDetails), numCUDDDetails)

'''
This function initializes any utilization details we want to track. I.E., if we're tracking
the difference between two values before and after execution, we would track the value prior
to execution in this code, add it to the 'details' list, and then pop it off later.
'''
def initUtilizationDetails(timeFile, p, runMode):
    global verbosity, details
    if (runMode == "-d"):
        p.stdin.write("flush")
        p.stdin.write("\n")
        readStr = "s"
        while ((readStr.startswith('Unary stores')) == False and len(readStr) != 0):
            readStr = p.stdout.readline().lstrip('cmd>')

            # this is an example of a general detail
            if (readStr.startswith('ITEs. Total ')):
                readStr = readStr.lstrip('ITEs. Total ')
                (total, local, cache, recursion) = readStr.split('.', 3)
                total = total.lstrip(' Total ')
                details.append(int(total))

            # this is an example of a detail from the workers
            elif (readStr.startswith('Total operations sent')):
                readStr = readStr.lstrip('Total operations sent')
                (temp1, temp2, minimum, maximum, average, Sum) = readStr.split(':', 5)
                Sum = Sum.lstrip().rstrip("Sum").rstrip()
                details.append(int(Sum))
            elif (readStr.startswith('Total number of ITEs')):
                readStr = readStr.lstrip('Total number of ITEs')
                (temp1, temp2, minimum, maximum, average, Sum) = readStr.split(':', 5)
                Sum = Sum.lstrip().rstrip("Sum").rstrip()
                details.append(int(Sum))
            elif (readStr.startswith('Peak unique entries')):
                readStr = readStr.lstrip('Peak unique entries')
                (temp1, temp2, minimum, maximum, average, Sum) = readStr.split(':', 5)
                Sum = Sum.lstrip().rstrip("Sum").rstrip()
                details.append(int(Sum))

    elif (runMode == "-l"):
        p.stdin.write("flush")
        p.stdin.write("\n")
        readStr = "s"
        while ((readStr.startswith('Allocated cnt/bytes:')) == False and len(readStr) != 0):
            readStr = p.stdout.readline().lstrip('cmd>')
            if (readStr.startswith('ITEs. Total ')):
                readStr = readStr.lstrip('ITEs. Total ')
                (total, local, cache, recursion) = readStr.split('.', 3)
                total = total.lstrip(' Total ')
                details.append(int(total))

    else:
        # CUDD mode
        p.stdin.write("flush")
        p.stdin.write("\n")


'''
This function writes utilization details for a given test run
to the output file. It parses the output from the process and
writes the values to the CSV file, optionally computing a difference
or other user-defined changes from a value stored previous to
the test run.
'''
def printUtilizationDetails(timeFile, p, runMode):
    global verbosity, details, numTotalDetails, numCUDDDetails, numDistributedDetails, numLocalDetails
    if (runMode == "-c"):
        p.stdin.write("status")
        p.stdin.write("\n")
        readStr = ""
        while ((readStr.startswith('Time for reordering')) == False):
            readStr = p.stdout.readline().lstrip().rstrip()
            if (readStr.startswith('Memory in use: ')):
                readStr = readStr.lstrip('Memory in use:').rstrip()
                timeFile.write(readStr + ",")
            elif (readStr.startswith('Peak number of nodes: ')):
                readStr = readStr.lstrip('Peak number of nodes:').rstrip()
                timeFile.write(readStr + ",")
        for x in range(numTotalDetails - numCUDDDetails):
            timeFile.write(",")
        p.stdin.write("flush")
        p.stdin.write("\n")
    elif (runMode == "-l"):
        p.stdin.write("flush")
        p.stdin.write("\n")
        readStr = ""
        while ((readStr.startswith('Allocated cnt/bytes:')) == False):
            readStr = p.stdout.readline().lstrip('cmd>')
            if (readStr.startswith('Peak bytes ')):
                readStr = readStr.lstrip('Peak bytes ').rstrip()
                timeFile.write(readStr)
                timeFile.write(',')
            if (readStr.startswith('ITEs. Total ')):
                readStr = readStr.lstrip('ITEs.')
                (total, local, cache, recursion) = readStr.split('.', 3)
                total = total.lstrip(' Total ')
                timeFile.write(str(int(total) - int(details.pop(0))))
                timeFile.write(',')
        for x in range(numTotalDetails - numLocalDetails):
            timeFile.write(",")

    else:
        # Distributed mode
        p.stdin.write("flush")
        p.stdin.write("\n")
        readStr = ""
        detailsToPrint = []
        while ((readStr.startswith('Unary stores')) == False):
            readStr = p.stdout.readline().lstrip('cmd>')
            if (readStr.startswith('ITEs. Total ')):
                readStr = readStr.lstrip('ITEs.')
                (total, local, cache, recursion) = readStr.split('.', 3)
                total = total.lstrip(' Total ')
                detailsToPrint.append(str(int(total) - int(details.pop(0))))
            elif (readStr.startswith('Total operations sent')):
                readStr = readStr.lstrip('Total operations sent')
                (temp1, temp2, minimum, maximum, average, Sum) = readStr.split(':', 5)
                Sum = Sum.lstrip().rstrip("Sum").rstrip()
                detailsToPrint.append(str(int(Sum) - int(details.pop(0))))
            elif (readStr.startswith('Total number of ITEs')):
                readStr = readStr.lstrip('Total number of ITEs')
                (temp1, temp2, minimum, maximum, average, Sum) = readStr.split(':', 5)
                Sum = Sum.lstrip().rstrip("Sum").rstrip()
                detailsToPrint.append(str(int(Sum) - int(details.pop(0))))
            elif (readStr.startswith('Peak unique entries')):
                readStr = readStr.lstrip('Peak unique entries')
                (temp1, temp2, minimum, maximum, average, Sum) = readStr.split(':', 5)
                Sum = Sum.lstrip().lstrip("Sum").rstrip()
                detailsToPrint.append(str(int(Sum) - int(details.pop(0))))

        # We save all the previous details and flush again to get
        # the peak bytes allocated statistic, since it only returns
        # the peak bytes allocated statistic after the second flush
        p.stdin.write("flush")
        p.stdin.write("\n")
        readStr = ""
        while ((readStr.startswith('Unary stores')) == False):
            readStr = p.stdout.readline().lstrip('cmd>')
            if (readStr.startswith('Peak bytes allocated')):
                readStr = readStr.lstrip('Peak bytes allocated')
                (temp1, temp2, Min, Max, Avg, Sum) = readStr.split(':', 5)
                timeFile.write(str(int(Sum.rstrip().lstrip())))
                timeFile.write(',')
        for x in detailsToPrint:
            timeFile.write(x)
            timeFile.write(',')
        for x in range(numTotalDetails - numDistributedDetails):
            timeFile.write(",")

'''
Writes the header to the CSV file for a distributed run.
'''
def utilizationDetailsHeaderDistributed(timeFile):
    global numTotalDetails, numDistributedDetails
    timeFile.write("Sum Peak bytes allocated,")
    timeFile.write("Total number of ITEs (Controller),")
    timeFile.write("Sum Total operations sent,")
    timeFile.write("Sum Peak Unique Entries,")
    timeFile.write("Sum Total number of ITEs (Workers),")
    for x in range(numTotalDetails - numDistributedDetails):
            timeFile.write(",")

'''
Writes the header to the CSV file for a local run.
'''
def utilizationDetailsHeaderLocal(timeFile):
    global numTotalDetails, numLocalDetails
    timeFile.write("Peak bytes,")
    timeFile.write("Total number of ITEs,")
    for x in range(numTotalDetails - numLocalDetails):
            timeFile.write(",")

'''
Writes the header to the CSV file for a CUDD run.
'''
def utilizationDetailsHeaderCUDD(timeFile):
    global numTotalDetails, numCUDDDetails
    timeFile.write("Memory in use,")
    timeFile.write("Peak number of nodes,")
    for x in range(numTotalDetails - numCUDDDetails):
            timeFile.write(",")

'''
Runs the tests/benchmarks in the supplied process, recording utilizationd etails and times
to an output CSV file
'''
def runTests(inputFile, timeFile, p, opt):
    global useDeltaTime, numTrials, verbosity, specialDeltaTime
    for line in inputFile:
        timeFile.write(line.rstrip() + ",,")
        for i in range(numTrials):
            if (verbosity > 0):
                print(line.rstrip())

            # if we want to track utilization details
            if getUtilDetailsBool:
                initUtilizationDetails(timeFile, p, opt)

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
                if (verbosity >= 2):
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
            deltaTimeList.reverse()
            directTime = deltaTimeList[1]
            if (verbosity >= 1):
                print('operation \'' + line.rstrip() + '\' took ' + ('%.4f' % diffTime) + ' seconds. Delta time (from program): ' + ('%.4f' % deltaTime) + ' seconds. Direct time: ' + ('%.4f' % directTime) + ' seconds \n')

            if (tempFile != None):
                file.close(tempFile)

            # write the correct time
            if (useDeltaTime):
                timeFile.write(('%.4f' % deltaTime) + ",")
            elif (specialDeltaTime):
                timeFile.write(('%.4f' % directTime) + ",")
            else:
                timeFile.write(('%.4f' % diffTime) + ",")

            if getUtilDetailsBool:
                printUtilizationDetails(timeFile, p, opt)

        timeFile.write("\r\n")

'''
Calls a series of tests on different modes of runbdd (local, CUDD, and distributed)
'''
def runTimer(runOptions):
    global useDeltaTime, inputFileName, outputFileName
    global portStr, hostStr, numTrials, getUtilDetailsBool
    global specialDeltaTime

    timeFile = open(outputFileName, 'w', 1)

    timeFile.write(",,")
    for i in range(numTrials):
        timeFile.write("Trial " + str(i + 1) + " (s),")
        if (getUtilDetailsBool):
            for i in range(numTotalDetails):
                timeFile.write(",")

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
        timeFile.write(modeStr + ",,")
        for i in range(numTrials):
            timeFile.write(",")
            if (getUtilDetailsBool):
                if (opt == "-c"):
                    utilizationDetailsHeaderCUDD(timeFile)
                elif (opt == "-l"):
                    utilizationDetailsHeaderLocal(timeFile)
                else:
                    utilizationDetailsHeaderDistributed(timeFile)
        timeFile.write("\r\n")

        #write empty row
        timeFile.write(",,")
        for i in range(numTrials):
            timeFile.write(",")
            if (getUtilDetailsBool):
                for i in range(numTotalDetails):
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

        runTests(inputFile, timeFile, p, opt)
        p.kill()
        file.close(inputFile)
        # create empty row
        timeFile.write(",,")
        for i in range(numTrials):
            timeFile.write(",")
            if (getUtilDetailsBool):
                for i in range(numTotalDetails):
                    timeFile.write(",")

        timeFile.write("\r\n")

    file.close(timeFile)

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
    print("\t-H HOST          The  host of the controller. Default: localhost")
    print("\t-P PORT          The port of the controller. Default: 6616")
    print("\t-i INPUTFILE     The file name to take input from. Default: instructions-scripts.txt")
    print("\t-o OUTPUTFILE    The file name to take output from. Default: times-scripts.txt")
    print("\t-t USE DELTATIME 1 to use the delta times from the program, 0 to use the timer in this script. 2 to use the delta times built into each test (the tester will not add additional 'time' commands before and after each test.)  Default: 2")
    print("\t-n NUM           The number of trials for each script, per mode. Default: 1")
    print("\t-c               Uses the CUDD package for timing. Default: disabled.")
    print("\t-d               Uses the distributed package for timing. Default: enabled.")
    print("\t-l               Uses the local refs for timing. Default: disabled.")
    print("\t-v VERBOSITY     Verbose mode. Prints output. Level 1: Prints individual commands and times; Level 2: Prints everything. Default: 0.")
    print("\t-u UTIL DETAILS  1 to list utilization details (peak bytes, peak ITEs, etc.) Default: 0")
    print("")

def main():
    try:
        opts, args = getopt.getopt(sys.argv[1:], "H:P:dcli:o:t:n:hv:u:", [])
    except getopt.GetoptError as err:
        print(err)
        usage()
        sys.exit(2)

    global useDeltaTime, inputFileName, outputFileName
    global portStr, hostStr, numTrials, verbosity
    global getUtilDetailsBool, specialDeltaTime
    runOptions = []

    # parse command-line options

    for opt, arg in opts:
        if opt == "-P":
            portStr = arg
        elif opt == "-H":
            hostStr = arg
        elif opt == "-t":
            if (int(arg) == 1):
                useDeltaTime = True
                specialDeltaTime = False
            elif (int(arg) == 2):
                specialDeltaTime = True
            elif (int(arg) == 0):
                specialDeltaTime = False
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
            if (int(arg) == 1):
                getUtilDetailsBool = True
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
