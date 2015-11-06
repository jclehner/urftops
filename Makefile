CC=cc
CFLAGS=-Wextra -g
LDFLAGS=

all: urf.o conv_ps.o urftops.o
	$(CC) $(LDFLAGS) urf.o conv_ps.o urftops.o -o urftops

clean:
	rm -f *.o

urf.o: urf.c urf.h
	$(CC) -c $(CFLAGS) -o urf.o urf.c

conv_ps.o: conv_ps.c urf.h
	$(CC) -c $(CFLAGS) -o conv_ps.o conv_ps.c

urftops.o: urftops.c
	$(CC) -c $(CFLAGS) -o urftops.o urftops.c

