all: odmget

odmget: odmget.o
	powerpc-ibm-aix4-ld '--remap-inputs=/lib/libc.a=../aix415-fs/lib/libc.a' '--remap-inputs=/lib/libodm.a=../aix415-fs/lib/libodm.a' -o odmget crt0.o odmget.o ../aix415-fs/lib/libodm.a ../aix415-fs/lib/libc.a

odmget.o: odmget.s
	powerpc-ibm-aix4-as -o odmget.o odmget.s
