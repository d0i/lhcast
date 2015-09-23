CFLAGS=-g -Ilonghair/include 
# just to avoid gdb bug 
# https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=701935#18
CC=g++


default: lhsplit lhjoin

clean::
	rm -f *.o core *~ lhsplit lhjoin *.lhb *-recovered.jpg

lhsplit: lhsplit.o 
	$(CC) $(CFLAGS) -o $@ lhsplit.o longhair/*.o $(LIBS)

lhjoin: lhjoin.o lhasm.o
	$(CC) $(CFLAGS) -o $@ lhjoin.o lhasm.o longhair/*.o $(LIBS)

longhair/bin/liblonghair.a:
	(cd longhair; make)




