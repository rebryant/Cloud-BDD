#!/usr/bin/python

# 1. Generate literal files
# 2. Generate command files based on those literals

import subprocess
import glob

# Parameters for literal file generation
lprogram = "../../lcompress.py"
root = "smirnov"
lname = "smirnov-family.lit"
mergeMode = 2
sourceAuxCount = 23
toMin = 5
fromMin = 6

# Parameters for command file generation

gprogram = "../../mm_generate.py"
destAuxCount = 22
time = 7200
template = "smirnov-m*+*.lit"
singleton = True


# Literal file generation
def genLiterals():
    cmd = [lprogram, '-r', root, '-L', lname, '-m', str(mergeMode), '-p', str(sourceAuxCount), '-f', str(fromMin), '-t', str(toMin)]
    print("Running: %s" % " ".join(cmd))
    try:
        process = subprocess.Popen(cmd)
        process.wait()
    except Exception as ex:
        print("Failed (%s)" % str(ex))
        return False
    return True

# Name of command file
def cname(lfname):
    fields = lfname.split('.')
    croot = '.'.join(fields[:-1])
    return "run-%s.cmd" % croot

def genCommand(lfname, oname):
    cmd = [gprogram, '-B', '2:6',  '-t', str(time), '-L', lfname, '-o', oname, '-p', str(destAuxCount)]
    if singleton:
        cmd.append('-e')
    return cmd

def genCommands():
    for lfname in glob.glob(template):
        oname = cname(lfname)
        cmd = genCommand(lfname, oname)
        print "Running: %s" % " ".join(cmd)
        try:
            process = subprocess.Popen(cmd)
            process.wait()
        except Exception as ex:
            print("Failed (%s)" % str(ex))
            return

def run():
    if genLiterals():
        genCommands()

run()

    


