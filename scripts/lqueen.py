#!/usr/bin/python

import sys
import circuit

n = int(sys.argv[1])

careful = n >= 13

info = True

circuit.lQueens(n, careful = careful, info = info)
