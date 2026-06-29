all: odmget

odmget: odmget.o
	powerpc-ibm-aix4-ld -o odmget crt0.o odmget.o -L. -lodm -lc

odmget.o: odmget.s
	powerpc-ibm-aix4-as -o odmget.o odmget.s
