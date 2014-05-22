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
timeFile = open('times.csv', "w")
timeFile.write(',,')


p = subprocess.Popen(['./tclient', '-P', port], stdin=PIPE, stdout=PIPE, bufsize = 0)
for line in inputFile:
    print(line)
    startTime = time.time()
    p.stdin.write(line)
    print(p.stdout.readline().lstrip('cmd>').rstrip())
    endTime = time.time()
    diffTime = endTime - startTime
    writerStr = str(diffTime) + '\n' + spacedStr
    print('operation \'' + line.rstrip() + '\' took ' + ('%.3f' % diffTime) + ' seconds.\n')
    timeFile.write(writerStr)
    timeFile.flush()

p.kill()
file.close(inputFile)
file.close(timeFile)
