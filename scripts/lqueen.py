#!/usr/bin/python

import sys
import circuit

n = int(sys.argv[1])

careful = False

info = False

circuit.lQueens(n, careful = careful, info = info)
