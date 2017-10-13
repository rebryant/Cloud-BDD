
# Read ISCAS85 benchmark circuit file
# Generate evaluation commands for BDD

import sys


class DD:
    prefixes = ["b", "z", "a"]
    BDD, ZDD, ADD = range(len(prefixes))

    def prefix(self, type):
        if type >= 0 and type < len(self.prefixes):
            return self.prefixes[type]
        return "?"

# Custom order for ISCAS circuit C7552 from:
# M. R. Mercer and R. Kapur and D. E. Ross
# "Functional Approaches to Generating Orderings for Efficient Symbolic Representations"
# DAC '92
mercerOrderC7552 =[205, 194, 33, 32, 196, 4, 18, 28, 20, 63, 56, 95, 22, 83, 
             37, 47, 154, 144, 138, 139, 199, 117, 120, 137, 133, 128, 109, 
             116, 129, 49, 121, 134, 130, 132, 142, 124, 112, 126, 131, 135, 
             123, 115, 113, 114, 101, 119, 94, 97, 197, 54, 46, 9, 12, 92, 118, 
             125, 122, 108, 111, 105, 104, 45, 39, 21, 6, 11, 204, 202, 68, 71, 
             99, 57, 82, 203, 80, 195, 31, 24, 25, 14, 66, 67, 72, 87, 84, 180, 0, 
             145, 148, 160, 143, 140, 146, 147, 158, 187, 162, 153, 149, 159, 
             163, 152, 141, 151, 136, 185, 173, 190, 191, 176, 166, 189, 170, 
             169, 178, 155, 164, 157, 167, 161, 168, 172, 192, 175, 156, 165, 
             171, 184, 183, 174, 179, 177, 81, 182, 90, 100, 75, 86, 76, 73, 35, 
             193, 41, 61, 65, 62, 44, 42, 51, 53, 79, 181, 19, 188, 8, 13, 5, 201, 
             15, 17, 10, 29, 107, 186, 198, 200, 102, 93, 77, 85, 96, 89, 78, 91, 
             110, 106, 98, 69, 88, 74, 64, 30, 27, 34, 50, 127, 43, 7, 40, 16, 2, 
             206, 3, 1, 23, 150, 38, 48, 55, 59, 36, 58, 70, 60, 52, 26, 103]

# Inputs indexed by mercerOrderC7552
mercerList = [4526, 3749, 74, 73, 4394, 15, 55, 65, 57, 124, 111, 172, 59, 160, 78, 88,
          231, 221, 215, 216, 4410, 194, 197, 214, 210, 205, 186, 193, 206, 94, 198,
          211, 207, 209, 219, 201, 189, 203, 208, 212, 200, 192, 190, 191, 178, 196,
          171, 174, 4400, 109, 87, 32, 41, 169, 195, 202, 199, 185, 188, 182, 181,
          86, 80, 58, 23, 38, 4437, 4427, 135, 144, 176, 112, 159, 4432, 157, 4393, 70,
          61, 62, 47, 133, 134, 147, 164, 161, 2236, 1, 222, 225, 237, 220, 217, 223,
          224, 235, 3705, 239, 230, 226, 236, 240, 229, 218, 228, 213, 3698, 1496,
          3723, 3729, 2211, 1455, 3717, 1480, 1469, 2224, 232, 339, 234, 1459, 238,
          1462, 1492, 3737, 2208, 233, 1197, 1486, 2256, 2253, 2204, 2230, 2218, 158,
          2247, 167, 177, 152, 163, 153, 150, 76, 3743, 82, 118, 130, 121, 85, 83, 100,
          106, 156, 2239, 56, 3711, 29, 44, 18, 4420, 50, 54, 35, 66, 184, 3701, 4405,
          4415, 179, 170, 154, 162, 173, 166, 155, 168, 187, 183, 175, 138, 165, 151,
          127, 69, 64, 75, 97, 204, 84, 26, 81, 53, 9, 4528, 12, 5, 60, 227, 79, 89,
          110, 114, 77, 113, 141, 115, 103, 63, 180]

# Inputs generated based on reverse engineering
reversedList = [18, 4526, 89, 12, 9, 38, 4528, 199, 188, 172, 162, 186, 185, 182, 183,
                230, 218, 152, 210, 240, 228, 184, 150, 57, 5, 133, 134, 1197, 15, 163,
                1, 339, 1459, 114, 170, 1496, 2204, 211, 164, 1492, 1455, 212, 165, 213,
                1486, 88, 166, 214, 1480, 112, 167, 215, 106, 87, 168, 216, 1469, 111,
                169, 209, 1462, 113, 153, 2256, 110, 173, 154, 2253, 109, 174, 155, 2247,
                86, 175, 156, 2239, 63, 176, 157, 2236, 64, 177, 158, 135, 2230, 85, 178,
                159, 144, 2224, 84, 179, 160, 138, 2218, 83, 180, 161, 141, 2208, 82, 181,
                151, 147, 2211, 65, 171, 219, 66, 4437, 62, 189, 220, 50, 4432, 61, 190,
                221, 32, 4427, 60, 191, 222, 35, 4420, 79, 192, 223, 47, 4415, 80, 193, 224,
                121, 4410, 81, 194, 225, 94, 4405, 59, 195, 226, 97, 4400, 78, 196, 227, 115,
                4393, 58, 197, 217, 118, 4394, 77, 187, 231, 100, 3749, 56, 200, 232, 124,
                3743, 55, 201, 233, 127, 3737, 54, 202, 234, 130, 3729, 53, 203, 235, 103,
                3723, 73, 204, 236, 23, 3717, 75, 205, 237, 26, 3711, 76, 206, 239, 44, 3701,
                70, 208, 238, 29, 3705, 74, 207, 229, 41, 3698, 69, 198]


class GateType:
    names = ["inpt", "and", "nand", "or", "nor", "xor", "xnor", "buff", "not"]
    INPT, AND, NAND, OR, NOR, XOR, XNOR, BUFF, NOT = range(len(names))
    op = {
        INPT : "# input",
        AND : "and", NAND : "or",
        OR : "or", NOR : "and",
        XOR : "xor", XNOR : "xor",
        BUFF : "and", NOT : "not"}
    compInput = {
        INPT : False,
        AND : False, NAND : True,
        OR : False, NOR : True,
        XOR : False, XNOR : False,
        BUFF : False, NOT : False}
    compFirst = {
        INPT : False,
        AND : False, NAND : False,
        OR : False, NOR : False,
        XOR : False, XNOR : True,
        BUFF : False, NOT : False}
    dict = {}

    def __init__(self):

        self.dict = { self.names[i] : i for i in range(len(self.names)) }

    def name(self, t):
        if t >= len(self.names):
            return "unknown"
        return self.names[t]

    def type(self, name):
        if name in self.names:
            return self.dict[name]
        else:
            return len(self.names)

class Gate:
    type = 0
    name = ""
    inputs = []
    refs = 0  # Used to determine when node can be freed
    fanoutCount = 0
    fanouts = []
    gt = None
    level = 0
    maxdepth = 0
    visited = False
    
    def __init__(self, out, type, fanoutCount):
        self.type = type
        self.name = out
        self.fanoutCount = fanoutCount
        self.inputs = []
        self.fanouts = []
        self.gt = GateType()
        self.level = 0
        self.maxdepth = 0
        self.visited = False
        self.reset()

    def addInput(self, i):
        self.inputs.append(i)

    def addFanout(self, n):
        self.fanouts.append(n)

    def setLevel(self):
        self.level = 0
        for n in self.fanouts:
            self.level = max(self.level, n.level + 1)

    def setDepth(self):
        self.maxdepth = self.level
        for n in self.inputs:
            self.maxdepth = max(self.maxdepth, n.maxdepth)

    def reset(self):
        self.refs = 0

    def cmd(self):
        s = self.gt.op[self.type] + " " + self.name
        compIn = self.gt.compInput[self.type]
        comp = self.gt.compFirst[self.type]
        for i in self.inputs:
            s += " "
            if comp or compIn:
                s += "!"
                comp = False
            s += i.name
        return s

    def __str__(self):
        args = "("
        first = True
        for i in self.inputs:
            if not first:
                args += ", "
            first = False
            args += i.name
        args += ")"
        return self.name + " =  " + self.gt.name(self.type) + args

class Netlist:

    inputs = []
    outputs = []
    gates = []
    gt = None
    outfile = None

    def __init__(self):
        self.inputs = []
        self.names = []
        self.gates = []
        self.gt = GateType()
        self.outfile = sys.stdout
        
    def show(self):
        print "Inputs: %s" % [n.name for n in self.inputs]
        print "Outputs: %s" % [n.name for n in self.outputs]
        for g in self.gates:
            print g

    def decl(self, dtype = DD.BDD):
        names = [n.name for n in self.inputs]
        if dtype == DD.BDD:
            namestring = " ".join(names)
            self.outfile.write("var %s\n" % namestring)
        else:
            vnames = ["v_" + ns for ns in names]
            namestring = " ".join(vnames)
            self.outfile.write("var %s\n" % namestring)
            cmd = "zconvert" if dtype == DD.ZDD else "aconvert"
            for i in range(len(names)):
                self.outfile.write("%s %s %s\n" % (cmd, names[i], vnames[i]))

    def genGates(self):
        for g in self.gates:
            g.reset()
            if g.type != self.gt.INPT:
                self.outfile.write(g.cmd() + "\n")
                for i in g.inputs:
                    i.refs += 1
                    if i.type != self.gt.INPT and i.refs == i.fanoutCount:
                        self.outfile.write("delete %s\n" % i.name)
                        
    def computeLevels(self, dlist):
        for n in dlist:
            if n.level == 0:
                self.computeLevels(n.fanouts)
                n.setLevel()

    def computeDepths(self, dlist):
        for n in dlist:
            if n.maxdepth == 0:
                self.computeDepths(n.inputs)
                n.setDepth()

    def reorderTraverse(self, dlist, sofar = []):
        ndlist = list(dlist)
        ndlist.sort(reverse = True, key = lambda nd : nd.maxdepth)
        for n in ndlist:
            if n.visited:
                continue
            n.visited = True
            if n.type == self.gt.INPT:
                sofar.append(n)
            else:
                inlist = n.inputs
                sofar = self.reorderTraverse(inlist, sofar)
        return sofar

    def reorder(self):
        for n in self.gates:
            n.level = 0
            n.maxdepth = 0
            n.visited = False
        self.computeLevels(self.inputs)
        self.computeDepths(self.outputs)
        ninputs = self.reorderTraverse(self.outputs, [])
        for n in self.inputs:
            if n not in self.inputs:
                self.ninputs.append(n)
        self.inputs = ninputs

    # This ordering didn't prove successful
    def reorderMercer(self):
        if len(mercerOrderC7552) != len(self.inputs):
            print "Error.  Mercer ordering has %d inputs.  Should have %d" % (len(mercerOrderC7552), len(self.inputs))
            return
        ninputs = []
        used = [False for i in self.inputs]
        for i in mercerOrderC7552:
            ninputs.append(self.inputs[i])
            used[i] = True
        for i in range(len(self.inputs)):
            if not used[i]:
                print "Error.  Didn't use input %s (#%d)" % (self.inputs[i], i)
                return
        self.inputs = ninputs

    def reorderReversed(self):
        if len(reversedList) != len(self.inputs):
            print "Error.  Reverse-engineered ordering has %d inputs.  Should have %d" % (len(reversedList), len(self.inputs))
            return
        dict = {}
        for n in self.inputs:
            dict[int(n.name)] = n
        self.inputs = [dict[i] for i in reversedList]

    def finish(self):
        outnames = [n.name for n in self.outputs]
        outstring = " ".join(outnames)
        self.outfile.write("time\n")
        self.outfile.write("info %s\n" % outstring)
        self.outfile.write("status\n")
        self.outfile.write("quit\n")
                        
    def gen(self, outfile = None, dtype = DD.BDD):
        if outfile != None:
            self.outfile = outfile
        self.decl(dtype)
        self.genGates()
        self.finish()
                                       
    # Read file in .isc format.  Return True if successful
    def readFile(self, fname):
        ok = True
        self.inputs = []
        self.outputs = []
        self.gates = []
        address2Gate = {} # Map (string) addresses to gates
        lineCount = 0
        try:
            f = open(fname, "r")
        except:
            print "Could not open ISC file '%s'" % fname
            return False
        needInput = False
        lastg = None
        for l in f:
            lineCount += 1
            while l[-1] in '\n\r':
                l = l[:-1]
            fields = l.split()
            if fields[0][0] == '*':
                continue
            if needInput:
                for iadr in fields:
                    if iadr in address2Gate:
                        ig = address2Gate[iadr]
                        lastg.addInput(ig)
                        ig.addFanout(lastg)
                    else:
                        print "Line %d.  Invalid address '%s'" % (lineCount, iadr)
                        ok = False
                        continue
                needInput = False
            else:
                adr = fields[0]
                tname = fields[2]
                if tname == 'from':
                    iname = fields[3]
                    address2Gate[adr] = lastg
                else:
                    name = fields[1]
                    fanoutCount = 0
                    try:
                        fanoutCount = int(fields[3])
                    except:
                        print "Line %d.  Invalid fanout value '%s'" % (lineCount, fields[3])
                        ok = False
                    type = self.gt.type(tname)
                    lastg = Gate(name, type, fanoutCount)
                    self.gates.append(lastg)
                    address2Gate[adr] = lastg
                    if type == self.gt.INPT:
                        self.inputs.append(lastg)
                    else:
                        needInput = True
                    if fanoutCount == 0:
                        self.outputs.append(lastg)
        return ok

                    
                    
                    
                
            
