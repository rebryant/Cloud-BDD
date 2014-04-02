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

inputFile = open('instructions.txt', 'r')
timeFile = open('times.txt', "w")

for line in inputFile:
    # workaround to get faster script execution (rather than write to stdin)
    temp = open("temp.txt", "w")
    temp.write(line)
    file.close(temp)
    temp = open("temp.txt", "r")
    p = subprocess.Popen(['.././tclient', '-P', port], stdin=temp, stdout=PIPE, bufsize = 0)
    startTime = time.time()
    p.stdout.read(7)
    endTime = time.time()
    p.kill()
    file.close(temp)
    diffTime = endTime - startTime
    writerStr = str(diffTime) + "\n" + spacedStr
    timeFile.write(writerStr)
    timeFile.flush()


file.close(inputFile)
file.close(timeFile)
