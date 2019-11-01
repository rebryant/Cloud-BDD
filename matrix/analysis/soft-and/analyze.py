
# Lines of form:
# run-ab000-c090-s01-A50N.log:Soft_And.  preprocess_old2new.  cov = 0.917.  size = 2937022.  Other size = 4183.  Size --> 123895 (0.042X)

import math
import re
import sys

# Source information:
# Context: (preprocess|conj)_(old2new|new2old)
# Coverage metric 0.0--1.0
# Size of two arguments
# Size of result, or "Requires more than XXXX nodes" or "Skipping"
# Reduction Ratio

# Raw data to extract
# context: preprocess vs. conjunction + old2new vs new2old   (preprocess Boolean)
# coverage metric           
# Size of arg1, arg2
# Result status: skipped, failed, or completed
# Size of result, if generated

contextExpression = re.compile("(preprocess|conj)_(old2new|new2old)")
sizeExpression = re.compile("  size = ([0-9]+)")
otherSizeExpression = re.compile("Other size = ([0-9]+)")
coverageExpression = re.compile("cov = ([\.0-9]+)")
skippedExpression = re.compile("Skipping")
failedExpression = re.compile("Requires more than")
resultSizeExpression = re.compile("Size --> ([0-9]+)")

class ParseFailure(Exception):

    reason = ""

    def __init__(self, reason):
        self.reason = reason

    def __str__(self):
        return "Parse failure: %s" % self.reason

class Entry:
    preprocess = False
    old2new = False
    coverage = 0.0
    size = 0
    otherSize = 0
    skipped = False
    failed = False
    resultSize = 0

    floatFormat = "%.5f"

    def __init__(self, entry):
        m = contextExpression.search(entry)
        if m is None:
            raise ParseFailure("Bad context")
        else:
            self.preprocess = m.group(1) == 'preprocess'
            self.old2new = m.group(2) == 'old2new'

        m = coverageExpression.search(entry)
        if m is None:
            raise ParseFailure("No coverage")
        else:
            fstring = m.group(1)
            if fstring[-1] == ".":
                fstring = fstring[:-1]
            self.coverage = float(fstring)
        
        m = sizeExpression.search(entry)
        if m is None:
            raise ParseFailure("No size")
        else:
            self.size = int(m.group(1))

        m = otherSizeExpression.search(entry)
        if m is None:
            raise ParseFailure("No other size")
        else:
            self.otherSize = int(m.group(1))

        m = failedExpression.search(entry)
        if m is not None:
            self.failed = True

        m = skippedExpression.search(entry)
        if m is not None:
            self.skipped = True

        if not self.failed and not self.skipped:
            m = resultSizeExpression.search(entry)
            if m is None:
                raise ParseFailure("No result size")
            else:
                self.resultSize = int(m.group(1))

         
    def commafy(self, fields):
        return ", ".join(fields)        

    def nameToValue(self, name):
        if name == 'prep':
            return self.preprocess
        if name == 'conj':
            return not self.preprocess
        if name == 'cov':
            return self.coverage
        if name == 'o2n':
            return self.old2new
        if name == 'n2o':
            return not self.old2new
        if name == 'size':
            return self.size
        if name == 'osize':
            return self.otherSize
        if name == 'aratio':
            return self.argRatio()
        if name == 'laratio':
            return self.argRatio(log = True)
        if name == 'skipped':
            return self.skipped
        if name == 'failed':
            return self.failed
        if name == 'completed':
            return not self.skipped and not self.failed
        if name == 'rsize':
            return self.resultSize
        if name == 'rratio':
            return self.resultRatio()
        if name == 'lrratio':
            return self.resultRatio(log = True)
        else:
            None

    def nameToString(self, name):
        value = self.nameToValue(name)
        svalue = str(value)
        if value is None:
            svalue = '???'
        elif type(value) == type(0.0):
            svalue = self.floatFormat % value
        elif type(value) == type(True):
            svalue = "T" if value else "F"
        return svalue

    def nameToFloat(self, name):
        value = self.nameToValue(name)
        if value is None:
            return 0.0
        elif type(value) == type(False):
            return 1.0 if value else 0.0
        return float(value)

    def composeFields(self, nameList):
        return [self.nameToString(name) for name in nameList]

    # Define filter as dictionary mapping names to
    # Exact values or ranges of the form (lower, upper), where '.' for either denotes no bound
    def filter(self, fdict):
        for k in fdict.keys():
            checkValue = fdict[k]
            value = self.nameToValue(k)
            if value is None:
                return False
            if type(checkValue) == type((0.0, 1.0)):
                lower = checkValue[0]
                upper = checkValue[1]
                if lower != '.' and lower > value:
                    return False
                if upper != '.' and upper < value:
                    return False
            elif checkValue != value:
                return False
        return True

    # Compute weighted sum of values
    # weights is either the name of some field, or a dictionary mapping from names to weights
    def score(self, weights):
        score = 0.0
        if type(weights) == type('abcd'):
            return self.nameToFloat(weights)
        for k in weights.keys:
            value = self.nameToFloat(k)
            weight = weights[k]
            score += value * weight
        return score

    def argRatio(self, log = False):
        ratio = 0.0 if self.size == 0 else float(self.otherSize)/float(self.size)
        return math.log(ratio, 2) if log else ratio

    def resultRatio(self, log = False):
        ratio = 0.0 if self.size == 0 else float(self.resultSize)/float(self.size)
        return math.log(ratio, 2) if log else ratio



    def __str__(self):
        fields = ['prep' if self.preprocess else 'conj',
                  'o2n' if self.old2new else 'n2o',
                  self.floatFormat % self.coverage,
                  str(self.size),
                  str(self.otherSize),
                  "skip" if self.skipped else "fail" if self.failed else str(self.resultSize)
                  ]
        return "<" + self.commafy(fields) + ">"
            
class EntrySet:
    data = []

    def __init__(self, data = []):
        self.data = data

    def trim(self, s):
        while len(s) > 0 and s[-1] in '\t\n\r ':
            s = s[:-1]
        return s

    def tabify(self, fields):
        return "\t".join(fields)

    def load(self, infile = sys.stdin):
        lineNumber = 1
        for line in infile:
            line = self.trim(line)
            try:
                e = Entry(line)
                self.data.append(e)
            except Exception as ex:
                print("Line %d.  %s.  Line = '%s'" % (lineNumber, str(ex), line))
            lineNumber += 1

    def show(self, nameList, outf = sys.stdout):
        header = self.tabify(nameList)
        outf.write(header + '\n')
        for e in self.data:
            fields = e.composeFields(nameList)
            outf.write(self.tabify(fields) + '\n')
    
    def filter(self, fdict):
        ndata = [e for e in self.data if e.filter(fdict)]
        return EntrySet(ndata)

    # weights either a single name, or a dictionary mapping from names to weights
    def maxScore(self, weights):
        vals = [e.score(weights) for e in self.data]
        return max(vals)

    def minScore(self, weights):
        vals = [e.score(weights) for e in self.data]
        return min(vals)

    def averageScore(self, weights):
        if len(self.data) == 0:
            return 0.0
        vals = [e.score(weights) for e in self.data]
        s = sum(vals)
        l = len(vals)
        avg = sum(vals)/len(vals)
        # Debugging
        svals = ["%.3f" % v for v in vals]
        print("Average %s = %.5f" % (str(svals), avg))
        return avg

    def standardDeviationScore(self, weights):
        if len(self.data) == 0:
            return 0.0
        vals = [e.score(weights) for e in self.data]
        aval = sum(vals)/len(vals)
        squareVals = [v*v for v in vals]
        asval = sum(squareVals)/len(vals)
        sigmaSquared = asval - (aval*aval)
        stdev = math.sqrt(sigmaSquared) 
        # Debugging
        svals = ["%.3f" % v for v in vals]
        print("Std Dev %s.  avg = %.5f, sqAvg = %.5f, sqSig = %.5f, sig = %.5f" %
              (str(svals), aval, asval, sigmaSquared, stdev))
        return stdev

    def covarianceScores(self, weights1, weights2):
        if len(self.data) == 0:
            return 0.0
        val1 = [e.score(weights1) for e in self.data]
        val2 = [e.score(weights2) for e in self.data]
        avg1 = sum(val1)/len(val1)
        avg2 = sum(val2)/len(val2)
        prods = [(v1-avg1) * (v2-avg2) for v1, v2 in zip(val1, val2)]
        cov sum(prods)/len(prods)
        # Debugging
        svals = ["%.3f" % v for v in vals]
        print("Cov %s.  avg = %.5f, sqAvg = %.5f, sqSig = %.5f, sig = %.5f" %
              (str(svals), aval, asval, sigmaSquared, stdev))

        return cov
        
    def correlationScores(self, weights1, weights2):
        if len(self.data) == 0:
            return 0.0
        cov = self.covarianceScores(weights1, weights2)
        sig1 = self.standardDeviationScore(weights1)
        sig2 = self.standardDeviationScore(weights2)
        return cov/(sig1 * sig2)


        
