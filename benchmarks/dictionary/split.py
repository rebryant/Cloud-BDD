#!/usr/bin/python

# Split into file into k parts
import sys
import getopt

def splitter(inname, outtemplate, k):
    try:
        infile = open(inname, 'r')
    except:
        print "Couldn't open input file '%s'" % inname
        sys.exit(1)
    outfiles = []
    for i in range(k):
        outname = outtemplate % i
        try:
            outfile = open(outname, 'w')
        except:
            print "Couldn't open output file '%s'" %  outname
            sys.exit(1)
        outfiles.append(outfile)
    count = 0
    for line in infile:
        outfile = outfiles[count % k]
        outfile.write(line)
        count += 1
    infile.close()
    for f in outfiles:
        f.close()
    return count

def usage(name):
    print "Usage %s [-h] [-k count] [-r root]" % name
    print "  -h          Print this message"
    print "  -k count    Specify number of output files"
    print "  -r root     Specify file root"

def run(name, args):
    root = 'source-words'
    k = 2
    optlist, args = getopt.getopt(args, 'hk:r:')
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-k':
            k = int(val)
        elif opt == '-r':
            root = val

    inname = root + '.txt'
    outtemplate = root + '+%.2d.txt'

    count = splitter(inname, outtemplate, k)
    print "Split input file '%s' (%d lines) into %d files" % (inname, count, k)
    
if  __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])


        
        
        
