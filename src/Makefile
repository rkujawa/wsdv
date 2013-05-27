all: wsdv 

CC=gcc
LIBDIR=/usr/pkg/lib
INCDIR=/usr/pkg/include
LIBS=-lprop -lz
CFLAGS=-O1 -g -Wall -Werror

wsdv: wsdv.o png_codec.o keymap.o
	$(CC) $(CFLAGS) -o wsdv -L$(LIBDIR) $(LIBS) -Wl,-R/usr/pkg/lib \
		wsdv.o png_codec.o keymap.o

wsdv.o: png_codec.h keymap.h
	$(CC) $(CFLAGS) -I$(INCDIR) -c wsdv.c

png_codec.o: png_codec.c png_codec.h
	$(CC) $(CFLAGS) -I$(INCDIR) -c png_codec.c

keymap.o: keymap.h
	$(CC) $(CFLAGS) -I$(INCDIR) -c keymap.c

clean cleandir:
	rm -f wsdv
	rm -f *.o
	rm -f *~
	rm -f *.core
