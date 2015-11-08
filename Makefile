CC=cc
CFLAGS=-Wextra -g
LDFLAGS=

all: urftops urftobmp

clean:
	rm -f *.o

urf.o: urf.c urf.h
	$(CC) -c $(CFLAGS) -o urf.o urf.c

conv_ps.o: conv_ps.c urf.h
	$(CC) -c $(CFLAGS) -o conv_ps.o conv_ps.c

conv_bmp.o: conv_bmp.c urf.h
	$(CC) -c $(CFLAGS) -o conv_bmp.o conv_bmp.c

urftops: urf.o urftox.c conv_ps.o
	$(CC) $(CFLAGS) $(LDFLAGS) -DURF_CONV=postscript -o urftops urftox.c conv_ps.o urf.o

urftobmp: urf.o urftox.c conv_bmp.o
	$(CC) $(CFLAGS) $(LDFLAGS) -DURF_CONV=bmp -o urftobmp urftox.c conv_bmp.o urf.o

