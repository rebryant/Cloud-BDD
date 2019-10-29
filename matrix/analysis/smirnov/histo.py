#!/usr/bin/python
# Generate histogram information about probabilities in variable-probabilities.txt


# Number of buckets
bucketCount = 101

buckets = {}

path = "variable-probabilities.txt"

def assignBucket(v):
    if v == 0.0:
        return 0
    return 1 + int(v * (bucketCount-2))

def bucketRange(b):
    if b == 0:
        return "[0.000, 0.000]"
    if b == bucketCount-1:
        return "[1.000, 1.000]"
    vmin = float(b-1)/(bucketCount-1)
    vmax = float(b)/(bucketCount-1)
    return "[%.3f, %.3f)" % (vmin, vmax)

def process(path):
    global buckets
    try:
        inf = open(path, 'r')
    except Exception as ex:
        print("Couldn't open file '%s' (%s)" % (path, str(ex)))
        return
    first = True
    count = 0
    for line in inf:
        if first:
            # Skip header
            first = False
            continue
        fields = line.split()
        # Skip first column
        fields = fields[1:]
        for sv in fields:
            v = float(sv)
            b = assignBucket(v)
            if b in buckets:
                buckets[b] += 1
            else:
                buckets[b] = 1
            count += 1
    print("Processed %d entries" % count)
        
def show():
    for b in range(bucketCount):
        cnt = buckets[b] if b in buckets else 0
        print ("%s: %d" % (bucketRange(b), cnt))
    
def run():
    process(path)
    show()

run()
    
