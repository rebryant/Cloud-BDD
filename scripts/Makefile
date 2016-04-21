all: aa ma nq
aa: aa2.cmd aa3.cmd aa4.cmd aa6.cmd aa8.cmd aa12.cmd aa16.cmd aa24.cmd aa32.cmd aa48.cmd aa64.cmd
ma: ma2.cmd ma3.cmd ma4.cmd ma5.cmd ma6.cmd ma7.cmd ma8.cmd ma9.cmd ma10.cmd ma12.cmd

aa2.cmd: circuit.py addassoc.py
	./addassoc.py 2 > aa2.cmd

aa3.cmd: circuit.py addassoc.py
	./addassoc.py 3 > aa3.cmd

aa4.cmd: circuit.py addassoc.py
	./addassoc.py 4 > aa4.cmd

aa6.cmd: circuit.py addassoc.py
	./addassoc.py 6 > aa6.cmd

aa8.cmd: circuit.py addassoc.py
	./addassoc.py 8 > aa8.cmd

aa12.cmd: circuit.py addassoc.py
	./addassoc.py 12 > aa12.cmd

aa16.cmd: circuit.py addassoc.py
	./addassoc.py 16 > aa16.cmd

aa24.cmd: circuit.py addassoc.py
	./addassoc.py 24 > aa24.cmd

aa32.cmd: circuit.py addassoc.py
	./addassoc.py 32 > aa32.cmd

aa48.cmd: circuit.py addassoc.py
	./addassoc.py 48 > aa48.cmd

aa64.cmd: circuit.py addassoc.py
	./addassoc.py 64 > aa64.cmd

ma2.cmd: circuit.py multassoc.py
	./multassoc.py 2 > ma2.cmd

ma3.cmd: circuit.py multassoc.py
	./multassoc.py 3 > ma3.cmd

ma4.cmd: circuit.py multassoc.py
	./multassoc.py 4 > ma4.cmd

ma5.cmd: circuit.py multassoc.py
	./multassoc.py 5 > ma5.cmd

ma6.cmd: circuit.py multassoc.py
	./multassoc.py 6 > ma6.cmd

ma7.cmd: circuit.py multassoc.py
	./multassoc.py 7 > ma7.cmd

ma8.cmd: circuit.py multassoc.py
	./multassoc.py 8 > ma8.cmd

ma9.cmd: circuit.py multassoc.py
	./multassoc.py 9 > ma9.cmd

ma10.cmd: circuit.py multassoc.py
	./multassoc.py 10 > ma10.cmd

ma12.cmd: circuit.py multassoc.py
	./multassoc.py 12 > ma12.cmd

ma16.cmd: circuit.py multassoc.py
	./multassoc.py 16 > ma16.cmd

nq: nqueen.py
	./nqueen.py -a
	./nqueen.py -z -a

clean:
	rm -f *.pyc *~ *.cmd


