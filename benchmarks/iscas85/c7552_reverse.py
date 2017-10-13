# Taken from web page:
# http://web.eecs.umich.edu/~jhayes/iscas.restore/c7552/c7552.html

class Reverse:

    # These values get extracted out

    logic0 = -10
    logic1 = -20

    # Pad original definitions to make all data words 34 bits long

    xa0 = 2 * [logic0] + [213, 214, 215, 216, 209, 153, 154, 155, 156, 157,
                          158, 159, 160, 151, 219, 220, 221, 222, 223, 224,
                          225, 226, 217, 231, 232, 233, 234, 235, 236, 237,
                          238, logic0]

    xa1 = 2 * [logic0] + 10 * [logic1] + [135, 144, 138, 147, 66, 50, 32, 35, 47, 121,
                                          94, 97, 118, 100, 124, 127, 130, 103, 23, 26, 29, 41]

    xb = [1496, 1492, 1486, 1480, 106, 1469, 1462, 2256, 2253, 2247,
          2239, 2236, 2230, 2224, 2218, 2211, 4437, 4432, 4427, 4420,
          4415, 4410, 4405, 4400, 4394, 3749, 3743, 3737, 3729, 3723,
          3717, 3711, 3705, 3701]

    ya1 = 2 * [logic0] + [88, 112, 87, 111, 113, 110, 109, 86, 63, 64, 85,
                          84, 83, 65, 62, 61, 60, 79, 80, 81, 59, 78, 77,
                          56, 55, 54, 53, 73, 75, 76, 74, 70]

    yb0 = [2204, 1455, 166, 167, 168, 169, logic1, 173, 174, 175,
           176, 177, 178, 179, 180, 171, 189, 190, 191, 192, 193,
           194, 195, 196, 187, 200, 201, 202, 203, 204, 205, 206, 207, logic0]

    sel = [18]

    cin = [4526, 89]
    
    # 10/09/2017.  Deduce that mask[1] should be 12, not 112.
    mask = [12, 9]
    
    xext = [38, 4528]

    pcxa0 = [logic1, 211, 212, 161, 227, 239, 229]

    pcxa1 = 3* [logic1] + [141, 115, 44, 41]

    # 10/09/2017.  Deduce that pcya0[2] should be 4393, not 4398
    pcya0 = [1459, 1496, 1492, 2208, 4393, 3701, 3698]

    pcya1 = [114, 2204, 1455, 82, 58, 70, 69]
    
    pcyb0 = [170, 164, 165, 181, 197, 208, 198]

    strbin = [199, 188, 172, 162, 186, 185, 182, 183, 230, 218, 152, 210, 240, 228, 184, 150]

    misc = [57, 5, 133, 134, 1197, 15, 163, 1]

    stray = [339]

    # Ordering of inputs generated from paper:
    # M. R. Mercer and R. Kapur and D. E. Ross
    # "Functional Approaches to Generating Orderings for Efficient Symbolic Representations"
    # DAC '92

    mercer = [4526, 3749, 74, 73, 4394, 15, 55, 65, 57, 124, 111, 172, 59, 160, 78, 88, 231, 221, 215, 216, 4410, 194, 197, 214, 210, 205, 186, 193, 206, 94, 198, 211, 207, 209, 219, 201, 189, 203, 208, 212, 200, 192, 190, 191, 178, 196, 171, 174, 4400, 109, 87, 32, 41, 169, 195, 202, 199, 185, 188, 182, 181, 86, 80, 58, 23, 38, 4437, 4427, 135, 144, 176, 112, 159, 4432, 157, 4393, 70, 61, 62, 47, 133, 134, 147, 164, 161, 2236, 1, 222, 225, 237, 220, 217, 223, 224, 235, 3705, 239, 230, 226, 236, 240, 229, 218, 228, 213, 3698, 1496, 3723, 3729, 2211, 1455, 3717, 1480, 1469, 2224, 232, 339, 234, 1459, 238, 1462, 1492, 3737, 2208, 233, 1197, 1486, 2256, 2253, 2204, 2230, 2218, 158, 2247, 167, 177, 152, 163, 153, 150, 76, 3743, 82, 118, 130, 121, 85, 83, 100, 106, 156, 2239, 56, 3711, 29, 44, 18, 4420, 50, 54, 35, 66, 184, 3701, 4405, 4415, 179, 170, 154, 162, 173, 166, 155, 168, 187, 183, 175, 138, 165, 151, 127, 69, 64, 75, 97, 204, 84, 26, 81, 53, 9, 4528, 12, 5, 60, 227, 79, 89, 110, 114, 77, 113, 141, 115, 103, 63, 180]


    def __init__(self):
        pass

    def filter(self, v):
        return [e for e in v if e >= 0]

    def interleave(self, vlist):
        out = []
        rest = list(vlist)
        while len(rest) > 0:
            nrest = []
            for v in rest:
                if len(v) > 0:
                    out.append(v[0])
                    nrest.append(v[1:])
            rest = nrest
        return out

    # Customized padding of parity check signals, based on how these match different portions of 34-bit data word
    def padPC(self, v):
        if (len(v) != 7):
            print "Padding only works for 7-bit buses"
            return v
        return v[0:3] + 12 * [self.logic0] + v[3:4] + 8 * [self.logic0] + v[4:5] + 7 * [self.logic0] + v[5:7]
        
    def genData(self):
        pcs = [self.pcxa0, self.pcxa1, self.pcya0, self.pcya1, self.pcyb0]
        ppc = [self.padPC(v) for v in pcs]
        data = [self.xa0, self.xa1, self.xb, self.ya1, self.yb0]
        return self.interleave(ppc + data)

    def genCntl(self):
        return self.sel + self.cin + self.mask + self.xext + self.strbin + self.misc + self.stray

    def unique(self, ls):
        uniq = []
        for e in ls:
            if e not in uniq:
                uniq.append(e)
        return uniq

    def genInputs(self):
        return self.unique(self.filter(self.genCntl() + self.genData()))

    def duplicates(self, ls):
        nls = list(ls)
        nls.sort()
        dup = []
        for i in range(len(ls)-1):
            if nls[i] == nls[i+1]:
                dup.append(nls[i])
        return dup

    def diff(self, bigls, smallls):
        d = []
        for e in bigls:
            if e not in smallls:
                d.append(e)
        return d

        


        


