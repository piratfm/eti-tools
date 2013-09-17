
CFLAGS=-O2 -I.
LDFLAGS=-lm

all: clean ts2na eti_na2ni eti_ni2http

ts2na:
		gcc -g $(CFLAGS) -Wall -o ts2na ts2na.c
		
eti_na2ni:
		gcc -g $(CFLAGS) -Wall -o eti_na2ni eti_na2ni.c $(LDFLAGS) -lfec

eti_ni2http:
		gcc -g $(CFLAGS) -Wall -o eti_ni2http eti_ni2http.c wffigproc.c wfficproc.c wfbyteops.c wftables.c parse_config.c $(LDFLAGS) -lshout

clean:
		rm -f *.o
		rm -f ts2na eti_na2ni eti_ni2http
