CC = gcc
CFLAGS =-O2 -Wall
OBJS_EDI2ETI = network.o af_parser.o pf_parser.o tag_parser.o crc.o eti_assembler.o logging.o edi2eti.o
OBJS_TS2NA = ts2na.o
OBJS_TS2NA_DREAMBOX = ts2na.o tune.o
OBJS_NA2NI = na2ni.o
OBJS_NI2HTTP = ni2http.o wffigproc.o wfficproc.o wfbyteops.o wftables.o wffirecrc.o wfcrc.o parse_config.o
CFLAGS+=-I. -Ilibshout-2.2.2/include
LDFLAGS+=-lm


#####################################################
# Uncomment this 2 lines if you want to enable ZeroMQ
#####################################################
CFLAGS+= -DHAVE_ZMQ
LDFLAGS+= -lzmq


##################################################
# Uncomment this 2 lines if you want to enable FEC
##################################################
#CFLAGS+= -DHAVE_FEC
#LDFLAGS+= -lfec


all: cleanapps ni2http  ts2na na2ni edi2eti

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

edi2eti: $(OBJS_EDI2ETI)
	$(CC) -o $@ $(OBJS_EDI2ETI) $(LDFLAGS)

ts2na: $(OBJS_TS2NA)
	$(CC) -o $@ $(OBJS_TS2NA) $(LDFLAGS)

ts2na_dreambox: $(OBJS_TS2NA_DREAMBOX)
	$(CC) -o $@ $(OBJS_TS2NA_DREAMBOX) $(LDFLAGS)

na2ni: $(OBJS_NA2NI)
	$(CC) -o $@ $(OBJS_NA2NI) $(LDFLAGS)

ni2http: libshout-2.2.2/src/.libs/libshout.a $(OBJS_NI2HTTP)
	$(CC) -o $@ $(OBJS_NI2HTTP) libshout-2.2.2/src/.libs/libshout.a -lpthread $(LDFLAGS)

libshout-2.2.2/src/.libs/libshout.a:
	tar -xvzf libshout-2.2.2.tar.gz; cd libshout-2.2.2; ./configure --enable-shared=no --enable-static=yes; make; cd ..;

cleanapps:
	rm -f $(OBJS_EDI2ETI) $(OBJS_TS2NA) $(OBJS_TS2NA_DREAMBOX) $(OBJS_NA2NI) $(OBJS_NI2HTTP)
	rm -f ts2na na2ni ni2http edi2eti

clean: cleanapps
	if [ -f ./libshout-2.2.2/src/.libs/libshout.a ]; then cd libshout-2.2.2; make clean; cd ..;  fi;
