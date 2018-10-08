CC ?= gcc
CFLAGS ?= -O2 -Wall
OBJS_EDI2ETI = network.o af_parser.o pf_parser.o tag_parser.o crc.o eti_assembler.o logging.o edi2eti.o
OBJS_FEDI2ETI = af_parser.o pf_parser.o tag_parser.o crc.o eti_assembler.o logging.o fedi2eti.o
OBJS_TS2NA = ts2na.o
OBJS_TS2NA_DREAMBOX = ts2na.o tune.o
OBJS_NA2TS = na2ts.o
OBJS_NA2NI = na2ni.o
OBJS_NI2HTTP = ni2http.o wffigproc.o wfficproc.o wfbyteops.o wftables.o wffirecrc.o wfcrc.o parse_config.o
OBJS_NI2OUT = ni2out.o wffigproc.o wfficproc.o wfbyteops.o wftables.o wffirecrc.o wfcrc.o
OBJS_MPE2AAC = mpe2aac.o
OBJS_MPE2TS = mpe2ts.o
OBJS_ETI2ZMQ = eti2zmq.o
CFLAGS += -I.
LDFLAGS += -lm


#####################################################
# Uncomment this 2 lines if you want to enable ZeroMQ
#####################################################
#CFLAGS+= -DHAVE_ZMQ
#LDFLAGS+= -lzmq


##################################################
# Uncomment this 2 lines if you want to enable FEC
##################################################
#CFLAGS+= -DHAVE_FEC
#LDFLAGS+= -lfec


all: cleanapps ni2out ts2na na2ts na2ni edi2eti fedi2eti mpe2aac mpe2ts

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

edi2eti: $(OBJS_EDI2ETI)
	$(CC) -o $@ $(OBJS_EDI2ETI) $(LDFLAGS)

fedi2eti: $(OBJS_FEDI2ETI)
	$(CC) -o $@ $(OBJS_FEDI2ETI) $(LDFLAGS)

ts2na: $(OBJS_TS2NA)
	$(CC) -o $@ $(OBJS_TS2NA) $(LDFLAGS)

ts2na_dreambox: $(OBJS_TS2NA_DREAMBOX)
	$(CC) -o $@ $(OBJS_TS2NA_DREAMBOX) $(LDFLAGS)

na2ts: $(OBJS_NA2TS)
	$(CC) -o $@ $(OBJS_NA2TS) $(LDFLAGS)

na2ni: $(OBJS_NA2NI)
	$(CC) -o $@ $(OBJS_NA2NI) $(LDFLAGS)

eti2zmq: $(OBJS_ETI2ZMQ)
	$(CC) -o $@ $(OBJS_ETI2ZMQ) $(LDFLAGS)


ni2http: libshout-2.2.2/src/.libs/libshout.a $(OBJS_NI2HTTP)
	$(CC) -o $@ $(OBJS_NI2HTTP) libshout-2.2.2/src/.libs/libshout.a -lpthread $(LDFLAGS)

ni2out: $(OBJS_NI2OUT)
	$(CC) -o $@ $(OBJS_NI2OUT) -lpthread $(LDFLAGS)

mpe2aac: $(OBJS_MPE2AAC)
	$(CC) -o $@ $(OBJS_MPE2AAC) $(LDFLAGS)

mpe2ts: $(OBJS_MPE2TS)
	$(CC) -o $@ ($OBJS_MPE2TS) $(LDFLAGS)

libshout-2.2.2/src/.libs/libshout.a:
	tar -xvzf libshout-2.2.2.tar.gz; cd libshout-2.2.2; ./configure --enable-shared=no --enable-static=yes; make; cd ..;

cleanapps:
	rm -f $(OBJS_EDI2ETI) $(OBJS_FEDI2ETI) $(OBJS_TS2NA) $(OBJS_TS2NA_DREAMBOX) $(OBJS_NA2NI) $(OBJS_NA2TS) $(OBJS_NI2HTTP) $(OBJS_ETI2ZMQ) $(OBJS_NI2OUT) $(OBJS_MPE2AAC) $(OBJS_MPE2TS)
	rm -f ts2na na2ts na2ni ni2http edi2eti eti2zmq fedi2eti ni2out mpe2aac mpe2ts

clean: cleanapps
	if [ -f ./libshout-2.2.2/src/.libs/libshout.a ]; then cd libshout-2.2.2; make clean; cd ..;  fi;

install:
	install -d $(DESTDIR)/usr/bin
	install -m 755 edi2eti $(DESTDIR)/usr/bin
	install -m 755 fedi2eti $(DESTDIR)/usr/bin
	install -m 755 na2ni $(DESTDIR)/usr/bin
	install -m 755 na2ts $(DESTDIR)/usr/bin
	install -m 755 ni2out $(DESTDIR)/usr/bin
	install -m 755 ts2na $(DESTDIR)/usr/bin
	install -m 755 mpe2aac $(DESTDIR)/usr/bin
	install -m 755 mpe2ts $(DESTDIR)/usr/bin
