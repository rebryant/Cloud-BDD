import shlex, subprocess, sys, time
from subprocess import PIPE

#how many empty lines between vals we want
#makes it easy to paste into google
LinesBetweenVals = 6
if len(sys.argv) < 2 or sys.argv[1] is None:
    port = '6616'
else:
    port = sys.argv[1]


def numLines():
    new = ""
    for i in range(LinesBetweenVals):
        new = new + "\n"
    return new

spacedStr = numLines()

inputFile = open('instructions-scripts.txt', 'r')
timeFile = open('times-scripts.txt', "w")

p = subprocess.Popen(['.././runbdd', '-d', '-P', port], stdin=PIPE, stdout=PIPE, bufsize = 0)
for line in inputFile:
    print(line.rstrip())
    # we keep track of the number of times that
    # time is called, so that we know when to
    # stop reading STDOUT (we check for the
    # time output)
    numTime = 0
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
        p.stdin.write(line)
        p.stdin.write('time')
        p.stdin.write('\n')
        numTime = numTime + 1
    ''' p.stdin.write(line.rstrip())
    p.stdin.write("\n") '''

    newNumTime = 0
    deltaTimeList = []
    while (newNumTime < numTime):
        readStr = p.stdout.readline().lstrip('cmd>')
        #print(readStr.rstrip())
        if (readStr.startswith('Elapsed')):
            #capture delta time output here
            print(readStr.rstrip())
            (temp1, temp2, deltaTime) = readStr.split('=', 2)
            deltaTimeList.append(float(deltaTime))
            newNumTime = newNumTime + 1
    endTime = time.time()
    diffTime = endTime - startTime
    writerStr = str(diffTime) + '\n' + spacedStr
    deltaTime = 0.0
    for x in deltaTimeList:
        deltaTime = deltaTime + x
    print('operation \'' + line.rstrip() + '\' took ' + ('%.3f' % diffTime) + ' seconds. Delta time (from program): ' + str(deltaTime) + ' \n')
    if (tempFile != None):
        file.close(tempFile)


    timeFile.write(writerStr)
    timeFile.flush()

p.kill()
file.close(inputFile)
file.close(timeFile)
