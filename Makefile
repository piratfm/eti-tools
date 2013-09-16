
CFLAGS=-O2 -I.
LDFLAGS=-lm -lfec

all: clean ts2na eti_na2li

ts2na:
		gcc -g $(CFLAGS) -Wall -o ts2na ts2na.c
		
eti_na2li:
		gcc -g $(CFLAGS) -Wall -o eti_na2ni eti_na2ni.c $(LDFLAGS)

clean:
		rm -f *.o
		rm -f ts2na eti_na2ni
