#!/usr/bin/python

import sys
import circuit

n = int(sys.argv[1])

careful = True

info = True

circuit.lQueens(n, careful = careful, info = info)
