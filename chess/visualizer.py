#!/usr/bin/python

# Generate visualization of conjunct merging with chessboard constraints

# Process lines of form:
# Initial arguments squareok-ROW.COL ID
#          TRACK	ARG	squareOK-09.09	144
# Combining arg1 & arg2 -> new
#          TRACK	AND	118	111	145


import sys
import colorsys
import random
import getopt
import math
import time
import Tkinter
from PIL import Image, ImageDraw

def usage(name):
    print("Usage: %s [-h] [-N N] [-p PSECS] < BddOut")


class Colors:
    black = (0.0, 0.0, 0.0)
    # Parameters for choosing colors
    minHue = 0.0
    maxHue = 1.0
    minSaturation = 0.4
    maxSaturation = 0.9
    minValue = 0.4
    maxValue = 1.0
    minLevel = 0.2
    maxLevel = 0.9

    def cstring(self, c):
        fields = [("%.2x" % int(255*x)) for x in c]
        return "#" + "".join(fields)


    def background(self):
        return self.cstring(self.black)

    def randomColorHSV(self):
        h = random.uniform(self.minHue, self.maxHue)
        s = random.uniform(self.minSaturation, self.maxSaturation)
        v = random.uniform(self.minValue, self.maxValue)
        c = colorsys.hsv_to_rgb(h, s, v)
        return self.cstring(c)

    def randomColorRGB(self):
        r = random.uniform(self.minLevel, self.maxLevel)
        g = random.uniform(self.minLevel, self.maxLevel)
        b = random.uniform(self.minLevel, self.maxLevel)
        return self.cstring((r, g, b))

    def randomColor(self):
        return self.randomColorHSV()

class Square:
    row  = -1
    column = -1

    def __init__(self, row = None, column = None):
        if row is not None:
            self.row = row
        if self.column is not None:
            self.column = column

    def parse(self, s):
        parts = s.split("-")
        if len(parts) != 2 or parts[0] != 'squareOK':
            return None
        try:
            suffix = parts[1]
            rstring, cstring = suffix.split(".")
            self.row = int(rstring)
            self.column = int(cstring)
        except Exception:
            return None
        return self

    def __str__(self):
        return "sq-%.2d.%.2d" % (self.row, self.column)

class Tile:
    rowStart = 0
    rowEnd = 0
    columnStart = 0
    columnEnd = 0
    
    def parse(self, s):
        parts = s.split("-")
        if len(parts) != 2 or parts[0] != "tileOK":
            return None
        try:
            suffix = parts[1]
            fields = suffix.split(".")
            self.rowStart = int(fields[0])
            self.rowEnd = int(fields[2])
            self.columnStart = int(fields[3])
            self.columnEnd = int(fields[5])
        except Exception:
            return None
        return self
        
    # Given list of squares, find smallest enclosing tile
    def encloseSquares(self, slist):
        rowStart = None
        rowEnd = None
        columnStart = None
        columnEnd = None
        for sq in slist:
            rowStart = sq.row if rowStart is None else min(sq.row, rowStart)
            rowEnd = sq.row if rowEnd is None else max(sq.row, rowEnd)
            columnStart = sq.column if columnStart is None else min(sq.column, columnStart)
            columnEnd = sq.column if columnEnd is None else max(sq.column, columnEnd)
        self.rowStart = rowStart
        self.rowEnd = rowEnd
        self.columnStart = columnStart
        self.columnEnd = columnEnd
        return self

    def __str__(self):
        return "tileOK-%d..%d.%d..%d" % (self.rowStart, self.rowEnd, self.columnStart, self.columnEnd)

class Display:
    nrow = 16
    ncol = 16
    squareSize = 16
    display = None  # TK Window
    frame = None    # Frame within window
    canvas = None   # Canvas within frame
    gridSquares = [] # Set of rectangles, nrow * ncol total
    colorList = []  # Most recent set of colors
    colorGenerator = None

    def __init__(self, nrow, ncol, maxdim = None):
        if maxdim is None:
            maxdim = 800
        self.nrow = nrow
        self.ncol = ncol
        self.squareSize = maxdim // max(self.ncol, self.nrow)
        self.colorGenerator = Colors()
        self.display = Tkinter.Tk()
        self.display.title('Conjunctions for  %d X %d chess board' % (nrow, ncol))
        self.frame = Tkinter.Frame(self.display)
        self.frame.pack(fill=Tkinter.BOTH)
        iwidth = self.imageWidth()
        iheight = self.imageHeight()
        self.canvas = Tkinter.Canvas(self.frame,
                                     width = iwidth,
                                    height = iheight)
        self.canvas.pack(fill=Tkinter.BOTH)
        self.gridSquares = []
        self.colorList = []
        blank = self.colorGenerator.background()
        for r in range(0, self.nrow):
            for c in range(0, self.ncol):
                (x, y) = self.xyPos(r, c)
                sq = self.canvas.create_rectangle(x, y, x+self.squareSize, y+self.squareSize, width = 0,
                                                  fill = blank)
                self.gridSquares.append(sq)
                self.colorList.append(blank)
        self.update()

    def imageWidth(self):
        return self.ncol * self.squareSize

    def imageHeight(self):
        return self.nrow * self.squareSize

    def update(self):
        self.canvas.update()

    def xyPos(self, r, c):
        x = self.squareSize * c
        y = self.squareSize * r
        return (x, y)

    def index(self, r, c):
        return r * self.ncol + c

    def rowCol(self, idx):
        r = idx // self.ncol
        c = idx % self.ncol
        return (r, c)

    def colorSquare(self, idx, color):
        if idx >= 0 and idx < len(self.gridSquares):
            square =  self.gridSquares[idx]
            self.canvas.itemconfig(square, fill = color)
            self.colorList[idx] = color

    def finish(self):
        self.display.destroy()

    def capture(self, fname):
        img = Image.new('RGB', (self.imageWidth(), self.imageHeight()), "gray")
        dimg = ImageDraw.Draw(img)
        for idx in range(len(self.colorList)):
            r, c = self.rowCol(idx)
            x, y = self.xyPos(r, c)
            dimg.rectangle((x, y, x + self.squareSize, y + self.squareSize), fill = self.colorList[idx])
        try:
            img.save(fname)
            print("Saved image in file %s." % (fname))
        except Exception as ex:
            print("Could not save image to file %s (%s)" % (fname, str(ex)))

    def showRandom(self):
        for idx in range(self.nrow * self.ncol):
            self.colorSquare(idx, self.colorGenerator.randomColor())
        self.update()

class Tracker:
    display = None
    # Mapping from argument number to list of squares
    regionDict = {}
    pauseIncrement = 0.1
    colorDict = {}
    # mapping from tile name to argument number
    tileDict = {}
    
    def __init__(self, display, pauseIncrement = None):
        self.display = display
        self.regionDict = {}
        self.colorDict = {}
        self.tileDict = {}
        if pauseIncrement is not None:
            self.pauseIncrement = pauseIncrement

    def newArgument(self, id, sname):
        sq = Square().parse(sname)
        if sq is None:
            tile = Tile().parse(sname)
            if tile is None:
                print("ERROR: Could not parse argument %s" % sname)
                return
            else:
                oldId = self.findTileRegion(tile)
                self.rename(oldId, id)
        else:
            self.regionDict[id] = [sq]
            self.colorDict[id] = None

    def merge(self, id1, id2, newId):
        if id1 not in self.regionDict:
            print("merge: No region with id1 %d" % id1)
            return
        if id2 not in self.regionDict:
            print("merge: No region with id2 %d" % id2)
            return
        ls1 = self.regionDict[id1]
        del self.regionDict[id1]
        ls2 = self.regionDict[id2]
        del self.regionDict[id2]
        nls = ls1 + ls2
        self.regionDict[newId] = ls1 + ls2
        color = self.display.colorGenerator.randomColor()
        if len(ls1) > len(ls2) and self.colorDict[id1] is not None:
            color = self.colorDict[id1]
        elif self.colorDict[id2] is not None:
            color = self.colorDict[id2]
        self.colorDict[newId] = color
        for sq in nls:
            idx = self.display.index(sq.row, sq.column)
            self.display.colorSquare(idx, color)
        self.display.update()
            
    def rename(self, oldId, newId):
        if oldId == newId:
            return
        self.regionDict[newId] = self.regionDict[oldId]
        del self.regionDict[oldId]
        print("Renamed region %d to be %d" % (oldId, newId))
        self.colorDict[newId] = self.colorDict[oldId]

    # Once encounter tiles, form tile for each remaining region
    def buildTiles(self):
        for id in self.regionDict.keys():
            tile = Tile().encloseSquares(self.regionDict[id])
            print("Generated tile %s" % str(tile))
            self.tileDict[str(tile)] = id

    def findTileRegion(self, tile):
        tstring = str(tile)
        if len(self.tileDict) == 0:
            self.buildTiles()
        if tstring not in self.tileDict:
            print("ERROR: Could not find region matching tile %s" % tstring)
            return None
        return self.tileDict[tstring]

    def showRegion(self, id):
        if id not in self.regionDict:
            print("ERROR: Region %d does not exist" % id)
        else:
            slist = [str(sq) for sq in self.regionDict[id]]
            s = ", ".join(slist)
            print("Region %d: [%s]" % (id, s))

    # How much time should we pause after creating region?
    def pauseTime(self, id):
        cnt = len(self.regionDict[id])
        return math.sqrt(cnt) * self.pauseIncrement

    def process(self, infile):
        for line in infile:
            fields = line.split("\t")
            if fields[0] != "TRACK":
                continue
            if fields[1] == "ARG":
                id = int(fields[3])
                sname = fields[2]
                self.newArgument(id, sname)
            elif fields[1] == "AND":
                id1 = int(fields[2])
                id2 = int(fields[3])
                newId = int(fields[4])
                self.merge(id1, id2, newId)
                time.sleep(self.pauseTime(newId))


def run(name, args):
    N = 8
    ptime = None
    runFile = sys.stdin
    optlist, args = getopt.getopt(args, "hN:p:")
    for (opt, val) in optlist:
        if opt == '-h':
            usage(name)
            return
        elif opt == '-N':
            N = int(val)
        elif opt == '-p':
            ptime = float(val)
    display = Display(N, N)
    tracker = Tracker(display, ptime)
    tracker.process(runFile)

if __name__ == "__main__":
    run(sys.argv[0], sys.argv[1:])

    
