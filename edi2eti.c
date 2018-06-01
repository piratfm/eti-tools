/*
 * edi2eti.c
 *
 *  Created on: 13.07.2017
 *      Author: tipok
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>

#include "edi_parser.h"
#include "network.h"
#include "logging.h"

#ifdef HAVE_ZMQ
#include <zmq.h>
#endif

#define VERSION_ID "$Rev$"
#define DEFAULT_TTL 16
#define DEFAULT_PORT 5004
#define DEFAULT_HOST "239.100.254.254"

uint32_t errors_count = 0;
uint32_t cntr = 0;

int Interrupted = 0;
int sock = -1;

char *ip = DEFAULT_HOST;


/***********************************************
 * Usage
 ***********************************************/
static void usage(char *name) {
	fprintf(
			stderr,
			"%s udp unicast/multicast EDI-to-ETI converter.\n"
				"This is UDP unicast/multicast EDI-stream receiver that converts received stream to ETI-format %s.\n"
				"Usage: %s [options] [address]:[port]\n"
				"-v, --verbose               : Show more info (default: no)\n"
				"-q, --quiet                 : Minimum info (default: no)\n"
				"-I, --interval <value>      : Application udp reception time in seconds. Exit after that. (default:run forever)\n"
#ifdef HAVE_ZMQ
				"-o, --output <file|zmq-url> : Output file path or zeromq url (default: stdout)\n"
				"-L, --no-align              : Disable eti-packets alignment for ZeroMQ packing\n"
#else
				"-o, --output <file>         : Output file path (default: stdout)\n"
#endif
				"-a, --activity              : Display activity cursor (default: no)\n"
				"-h, --help                  : Show this help\n"
				"\n", name, VERSION_ID, name);
}

static void build_info(char *name) {
	fprintf(stderr, "ZeroMQ:%s, FEC:%s\n",
#ifdef HAVE_ZMQ
		"enabled",
#else
		"disabled",
#endif
#ifdef HAVE_FEC
		"enabled"
#else
		"disabled"
#endif
	);
}

static void signal_handler(int signum) {
	//    if (signum != SIGPIPE) {
	if (signum != SIGPIPE) {
		Interrupted = 1;
		//fprintf(stdout, "TOTAL ERRORS: %d\n", errors_count);
		msg_Log("TOTAL PACKETS: %d", cntr);

//		if (sock > 0)
//			close(sock);
//		exit(1);
	}
}



void write_file(void *privData, void *etiData, int etiLen)
{
	if(verbosity > 2)
		msg_Log("write:%u bytes to file", etiLen);
	FILE *fh = (FILE *) privData;
	fwrite(etiData, 1, etiLen, fh);
}





#ifdef HAVE_ZMQ

#define NUM_FRAMES_PER_ZMQ_MESSAGE 4

typedef struct {
    uint32_t version;
    int16_t buflen[NUM_FRAMES_PER_ZMQ_MESSAGE];
    /* The head stops here. Use the macro below to calculate
     * the head size.
     */

    uint8_t  buf[NUM_FRAMES_PER_ZMQ_MESSAGE*6144];
} PACKED zmq_dab_message_t;

#define ZMQ_DAB_MESSAGE_HEAD_LENGTH (4 + NUM_FRAMES_PER_ZMQ_MESSAGE*2)

int is_initial = 1;
zmq_dab_message_t zmq_msg;
int zmq_msg_ix;
int zmq_align = 1;

void write_zmq(void *privData, void *etiData, int etiLen)
{
    int i;
    int offset = 0;
    uint8_t *etiPkt = (uint8_t *) etiData;

    if(verbosity > 1)
        msg_Log("write:%u bytes to zmq part: %d", etiLen, zmq_msg_ix);

    // Increment the offset by the accumulated frame offsets
    for (i = 0; i < zmq_msg_ix; i++) {
        offset += zmq_msg.buflen[i];
    }

    if (offset + etiLen > NUM_FRAMES_PER_ZMQ_MESSAGE*6144) {
    	msg_Log("FAULT: invalid ETI frame size!");
    	return;
    }

    if(etiLen < 16 || (zmq_align && zmq_msg_ix != ((etiPkt[6] >> 5) & 0x03))) {
        zmq_msg_ix=0;
        zmq_msg.buflen[0] = -1;
        zmq_msg.buflen[1] = -1;
        zmq_msg.buflen[2] = -1;
        zmq_msg.buflen[3] = -1;
        msg_Log("ZMQ: skip non-aligned frames: %02x != %02x", zmq_msg_ix, ((etiPkt[6] >> 5) & 0x03));
        return;
    }

    // Append the new frame to our message
    memcpy(zmq_msg.buf + offset, etiPkt, etiLen);
    zmq_msg.buflen[zmq_msg_ix] = etiLen;
    zmq_msg_ix++;

    // As soon as we have NUM_FRAMES_PER_ZMQ_MESSAGE frames, we transmit
    if (zmq_msg_ix == NUM_FRAMES_PER_ZMQ_MESSAGE) {

        // Size of the header:
        int full_length = ZMQ_DAB_MESSAGE_HEAD_LENGTH;

        for (i = 0; i < NUM_FRAMES_PER_ZMQ_MESSAGE; i++) {
            full_length += zmq_msg.buflen[i];
        }

        zmq_msg_ix = 0;

        const int flags = 0;
        if(verbosity > 1)
        	msg_Log("zmq send:%u bytes", full_length);
        zmq_send(privData, (uint8_t*)&zmq_msg, full_length, flags);

        for (i = 0; i < NUM_FRAMES_PER_ZMQ_MESSAGE; i++) {
        	zmq_msg.buflen[i] = -1;
        }
    }
}
#endif


int main(int argc, char **argv) {
	int ret, vertex=0, continous=1, timeout=0;
	size_t recv_size = 0;
	FILE *out_fh = stdout;
	char *outpath = NULL;
	int port = DEFAULT_PORT;
	int activity=0;
	edi_handler_t *edi_p=NULL;
#ifdef HAVE_ZMQ
	void *zmq_context = NULL;
	void *zmq_publisher = NULL;
#endif

	/******************************************************
	 * Getopt
	 ******************************************************/
	const char short_options[] = "vLaqhI:o:";
	const struct option long_options[] = {
			{ "interval", optional_argument, NULL, 'I' },
			{ "verbose", optional_argument, NULL, 'v' },
			{ "quiet", optional_argument, NULL,	'q' },
			{ "output", optional_argument, NULL, 'o' },
			{ "activity", optional_argument, NULL, 'a' },
			{ "no-align", no_argument, NULL, 'L' },
			{ "help", no_argument, NULL, 'h' },
			{ 0, 0, 0, 0 }
		};
	int c, option_index = 0;

	if (argc == 1) {
		usage(argv[0]);
		build_info(argv[0]);
		exit(-1);
	}

	while (1) {
		c = getopt_long(argc, argv, short_options, long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'q':
			verbosity--;
			break;
		case 'v':
			verbosity++;
			break;

		case 'a':
			activity++;
			break;
#ifdef HAVE_ZMQ
		case 'L':
			zmq_align=0;
			break;
#endif
		case 'I':
			timeout = atoi(optarg);
			break;

		case 'o':
			outpath = optarg;
			break;

		case 'h':
			usage(argv[0]);
			build_info(argv[0]);
			exit(0);
			break;
		}
	}

	if(verbosity > 0)
		build_info(argv[0]);

	if (argc <= optind) {
		if (verbosity > 0)
			msg_Log("need to specify ip:port a:%d, o:%d", argc, optind);
		exit(-1);
	}


	if(outpath) {
		if(strncmp("zmq+", outpath, 4) == 0 && strlen(outpath) > 10) {
#ifdef HAVE_ZMQ
			zmq_context = zmq_ctx_new ();
			zmq_publisher = zmq_socket (zmq_context, ZMQ_PUB);
			zmq_bind (zmq_publisher, outpath+4);
			edi_p = initEDIHandle(ETI_FMT_ZMQ, write_zmq, zmq_publisher);
			zmq_msg.buflen[0] = -1;
			zmq_msg.buflen[1] = -1;
			zmq_msg.buflen[2] = -1;
			zmq_msg.buflen[3] = -1;
			zmq_msg.version = 1;
			zmq_msg_ix=0;
#else
			msg_Log("ZEROMQ is disabled! Can't stream to specified destination: %s", outpath);
			exit(-1);
#endif

		} else
		{
			out_fh = fopen(outpath, "wb");
			edi_p = initEDIHandle(ETI_FMT_RAW, write_file, out_fh);
		}
	} else {
		edi_p = initEDIHandle(ETI_FMT_RAW, write_file, out_fh);
	}



	char *pch = strtok(argv[optind], ":");
	if (pch != NULL) {
		if (strchr(pch, '.'))
			ip = pch;
		else
			port = atoi(pch);
	}

	pch = strtok(NULL, " \n");
	if (pch != NULL)
		port = atoi(pch);

	if (verbosity > 0)
		msg_Log("receiving from: %s:%d...", ip, port);

	uint8_t *buff;
	char ui[4] = { '-', '\\', '|', '/' };

	if (timeout) {
		// Alarm for automatic stopping
		//	    if (signal (SIGALRM, signal_handler) == SIG_IGN)
		//		signal (SIGALRM, SIG_IGN);
		signal(SIGALRM, signal_handler);
		alarm(timeout);
	}

	buff = malloc(8192);
	sock = input_init_udp(ip, port);
	if (!sock) {
		if (verbosity > 0)
			msg_Log("init socket failed");
		exit(-1);
	}


	/* read flood */
	while (!Interrupted) {

			if (activity > 0 && !(cntr % 32)) {
				fprintf(stderr, "\x08%c", ui[vertex]);
				vertex++;
				if(vertex > 3) vertex=0;
			}

			do {
				recv_size = 0;
				//wait 500ms to receive buffer
				ret = udp_read_timeout(sock, buff, &recv_size, 500);

				if(ret == -ETIMEDOUT) {
					if (continous) {
						continous=0;
						if(verbosity > 0)
							msg_Log("Stream Disappears!");
					}
				} else {
					if (!continous) {
						continous=1;
						if(verbosity > 0)
							msg_Log("Stream Appears!");
					}
				}

			} while ((recv_size <= 0 || ret == -EAGAIN) && !Interrupted);

			if (Interrupted)
				break;

			if(HandleEDIPacket(edi_p, buff, recv_size) < 0)
					errors_count++;
			cntr++;
	}
	if (verbosity > 0)
		msg_Log("Caught signal %d. ", Interrupted);

	if(outpath)
		fclose(out_fh);

	closeEDIHandle(edi_p);
	close(sock);
	sock = 0;
	free(buff);

#ifdef HAVE_ZMQ
	if(zmq_publisher)
	    zmq_close (zmq_publisher);
	if(zmq_context)
	    zmq_term (zmq_context);
#endif

	return 1;
}
