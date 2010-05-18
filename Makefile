CPPFLAGS=-O2 -I. -Wall -Wno-sign-compare -Wno-unused

all: swift

swift: swift.o sha1.o compat.o sendrecv.o send_control.o hashtree.o bin64.o bins.o channel.o datagram.o transfer.o httpgw.o
	g++ -I. *.o -o swift

clean:
	rm *.o swift 2>/dev/null

.PHONY: all clean
