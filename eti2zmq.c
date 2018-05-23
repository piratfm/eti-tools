
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

#ifdef HAVE_ZMQ
#include <zmq.h>
#endif

#define PACKED __attribute__ ((packed))
int Interrupted=0;

#define ETI_NI_FSYNC0				0xb63a07ff
#define ETI_NI_FSYNC1				0x49c5f8ff
#define ETI_NI_RAW_SIZE				6144
#define ETI_NI_FRAME_TIME			24000 //useconds



/*****************************************************************************
 * info/help/debug
 *****************************************************************************/
//#define DEBUG(format, ...) fprintf (stderr, "DEBUG: "format"\n", ## __VA_ARGS__)
#define DEBUG(format, ...)
#define  INFO(format, ...) fprintf (stderr, "INFO:  "format"\n", ## __VA_ARGS__)
#define WARN(format, ...)  fprintf (stderr, "WARN:  "format"\n", ## __VA_ARGS__)
#define ERROR(format, ...) fprintf (stderr, "ERROR: "format"\n", ## __VA_ARGS__)

extern struct ens_info einf;

static void usage(const char *psz)
{
    fprintf(stderr, "usage: %s [--delay] [-i <inputfile>] -o zmq+tcp://0.0.0.0:18082\n\n"
	    "Additional info related to arguments:\n"
	    "--input or -i <filename>	 	Provide input file for app\n"
	    "--output or -o <zeromq address>    Provide zeromq output address like zmq+tcp://[local_ip]:[port_to_listen]\n"
	    "--delay or -d                      Force pseudo-realtume streaming (add 24ms delay for each eti frame)\n"
	    "--loop or -l                       Run input file in-a-loop\n"
	    "--no-align or -L                   Disable frame alignment for ZeroMQ packing\n"
	    "--no-cut-tail or -C                Disable removing unneeded fill bytes at ETI frames tails\n"
	    "--activity or -a                   Display app's activity in console\n"
	    "--verbose or -v                    Increase app's verbosity (can be provided multiple times)\n\n",
	    psz);
    exit(EXIT_FAILURE);
}


static void signal_handler(int signum)
{
	if (signum != SIGPIPE) {
		Interrupted=signum;
	}
	signal(signum,signal_handler);
}

int verbosity=0;

#define NUM_FRAMES_PER_ZMQ_MESSAGE 4

#ifdef HAVE_ZMQ
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

int write_zmq(void *privData, void *etiData, int etiLen)
{
    int i;
    int offset = 0;
    uint8_t *etiPkt = (uint8_t *) etiData;

    if(verbosity > 1)
        INFO("write:%u bytes to zmq part: %d", etiLen, zmq_msg_ix);

    // Increment the offset by the accumulated frame offsets
    for (i = 0; i < zmq_msg_ix; i++) {
        offset += zmq_msg.buflen[i];
    }

    if (offset + etiLen > NUM_FRAMES_PER_ZMQ_MESSAGE*6144) {
    	INFO("FAULT: invalid ETI frame size!");
    	return 0;
    }

    if(etiLen < 16 || (zmq_align && zmq_msg_ix != ((etiPkt[6] >> 5) & 0x03))) {
        zmq_msg_ix=0;
        zmq_msg.buflen[0] = -1;
        zmq_msg.buflen[1] = -1;
        zmq_msg.buflen[2] = -1;
        zmq_msg.buflen[3] = -1;
        INFO("ZMQ: skip non-aligned frames: %02x != %02x", zmq_msg_ix, ((etiPkt[6] >> 5) & 0x03));
        return 0;
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
        	INFO("zmq send:%u bytes", full_length);
        zmq_send(privData, (uint8_t*)&zmq_msg, full_length, flags);

        for (i = 0; i < NUM_FRAMES_PER_ZMQ_MESSAGE; i++) {
        	zmq_msg.buflen[i] = -1;
        }
	return 1;
    } else {
    	return 0;
    }
}
#endif




void print_bytes(char *bytes, int len)
{
    int i;
    int count;
    int done = 0;

    while (len > done) {
		if (len - done > 32){
			count = 32;
		} else {
			count = len - done;
		}

		fprintf(stderr, "%08x:    ", done);

		for (i=0; i<count; i++) {
	    	fprintf(stderr, "%02x ", (int)((unsigned char)bytes[done+i]));
	    	if(i==15)
	    		fprintf(stderr, "| ");
		}

		for (i=count; i<32; i++) {
    		fprintf(stderr, "   ");
	    	if(i==15)
	    		fprintf(stderr, "| ");
		}


		fprintf(stderr, "        \"");

        for (i=0; i<count; i++) {
	    	fprintf(stderr, "%c", isprint(bytes[done+i]) ? bytes[done+i] : '.');
	    	if(i==15)
	    		fprintf(stderr, "|");
        }
        fprintf(stderr, "\"\n");
    	done += count;
    }
}



int main(int i_argc, char **ppsz_argv)
{   
    int c, delay=0, loop=0, cut_tail=1;
    FILE *inputfile=stdin;
    char *outpath = NULL;
    int activity=0;
#ifdef HAVE_ZMQ
	void *zmq_context = NULL;
	void *zmq_publisher = NULL;
#endif

    static const struct option long_options[] = {
        { "input",        required_argument,       NULL, 'i' },
        { "output",       required_argument,       NULL, 'o' },
        { "delay",        no_argument,             NULL, 'd' },
	{ "loop",         no_argument,             NULL, 'l' },
        { "no-align",     no_argument,             NULL, 'L' },
	{ "no-cut-tail",  no_argument,             NULL, 'C' },
        { "activity",     no_argument,             NULL, 'a' },
        { "verbose",      no_argument,             NULL, 'v' },
        { 0, 0, 0, 0 }
    };

    while ((c = getopt_long(i_argc, ppsz_argv, "i:o:lLdhav", long_options, NULL)) != -1)
    {
        switch (c) {
        case 'i':
            inputfile = fopen(optarg, "r");
            if(!inputfile) {
                ERROR("cant open input file!");
                exit(1);
            }
            break;
#ifdef HAVE_ZMQ
	case 'L':
    	zmq_align=0;
    	break;
#endif
	case 'o':
		outpath = optarg;
		break;
        case 'd':
        	delay=1;
            break;
        case 'l':
        	loop=1;
            break;
        case 'C':
        	cut_tail=0;
            break;
        case 'a':
        	activity=1;
            break;
        case 'v':
        	verbosity++;
            break;
        case 'h':
        default:
            usage(ppsz_argv[0]);
        }
    }

	if(!outpath)
		usage(ppsz_argv[0]);
		
	if(strncmp("zmq+", outpath, 4) == 0 && strlen(outpath) > 10) {
#ifdef HAVE_ZMQ
		zmq_context = zmq_ctx_new ();
		zmq_publisher = zmq_socket (zmq_context, ZMQ_PUB);
		zmq_bind (zmq_publisher, outpath+4);
		zmq_msg.buflen[0] = -1;
		zmq_msg.buflen[1] = -1;
		zmq_msg.buflen[2] = -1;
		zmq_msg.buflen[3] = -1;
		zmq_msg.version = 1;
		zmq_msg_ix=0;
#else
		ERROR("ZEROMQ is disabled! Can't stream to specified destination: %s", outpath);
		exit(-1);
#endif
	}




    /* space for 1 ETI frame for bitwise seeking */
    int bytes_readed = 0;
    uint8_t p_ni_search_block[ETI_NI_RAW_SIZE];
    uint32_t sync_byte;
    int sync_found = 0;
    int total_readed = 0;

    unsigned long int count = 0;
    struct timeval diff1, diff2, startTV, endTV;
    if(delay)
        gettimeofday(&startTV, NULL);

    signal(SIGINT, signal_handler);
    signal(SIGKILL, signal_handler);
    signal(SIGTERM, signal_handler);


    char ui[4] = { '-', '\\', '|', '/' };
    int vertex=0;

    /* search for ETI-NI sync */
    do {
		bytes_readed=0;

		if (activity > 0 && !(count % 32)) {
			fprintf(stderr, "\x08%c", ui[vertex]);
			vertex++;
			if(vertex > 3) vertex=0;
		}

		do {
			size_t i_ret = fread(p_ni_search_block + bytes_readed, ETI_NI_RAW_SIZE - bytes_readed, 1, inputfile);
			if(i_ret != 1){
				ERROR("Can't read from file in %ld loop, total read: %d", count, total_readed);
				if(!loop) 
					exit(1);
				fseek(inputfile, 0, SEEK_SET);
                                sync_found=0;
                                bytes_readed=0;
				continue;	
			}
			total_readed += ETI_NI_RAW_SIZE - bytes_readed;
			bytes_readed = ETI_NI_RAW_SIZE;

			do {
				sync_byte = p_ni_search_block[3] << 24 | p_ni_search_block[2] << 16 | p_ni_search_block[1] << 8 | p_ni_search_block[0];
				if(sync_byte == ETI_NI_FSYNC0 || sync_byte == ETI_NI_FSYNC1) {
					sync_found=1;
				} else {
					bytes_readed--;
					memmove(p_ni_search_block, p_ni_search_block+1, bytes_readed);
				}
			} while (!sync_found && bytes_readed > 4);
		} while (!sync_found || bytes_readed != ETI_NI_RAW_SIZE);

		count++;

		DEBUG("sync=%08x at pos %d", sync_byte, total_readed);

		uint32_t fc_val = p_ni_search_block[4] << 24 | p_ni_search_block[5] << 16 | p_ni_search_block[6] << 8 | p_ni_search_block[7];
		if(fc_val == 0xffffffff)
			continue;

		DEBUG("NST=%d", p_ni_search_block[5] & 0x7f);
		//fc_t fc;
		//uint8_t fct = fc_word >> 24;
#if 0
		DEBUG("fc=%08x fct=%d, fc.fp=%d", fc.val, fc.fct, fc.fp);
		DEBUG("ficf=%d, nst=%d", fc.ficf, fc.nst);

		DEBUG("DAB MODE %d", fc.mid > 0 ? fc.mid : 4);
		DEBUG("DAB streams = %d", fc.nst);

		/* fic length in words */
		uint8_t ficl = (fc.ficf == 0) ? 0 : 24;
		if(ficl && fc.mid == 3) ficl = 32;

		DEBUG("FIC length = %d words", ficl);
		DEBUG("frame len=%d", fc.fl*4);
		DEBUG("MST len=%d", (fc.fl - (1 + 1 + fc.nst))*4);
#endif

	    	int is_written = 0;
#ifdef HAVE_ZMQ
		is_written=write_zmq(zmq_publisher, p_ni_search_block, ETI_NI_RAW_SIZE);
#endif

		if(delay && is_written) {
			gettimeofday(&endTV, NULL);
			timersub(&endTV, &startTV, &diff1);
			if(diff1.tv_sec == 0 && diff1.tv_usec < ETI_NI_FRAME_TIME*NUM_FRAMES_PER_ZMQ_MESSAGE) {
				startTV.tv_sec=0;
				startTV.tv_usec=ETI_NI_FRAME_TIME*NUM_FRAMES_PER_ZMQ_MESSAGE;
				timersub(&startTV, &diff1, &diff2);
				usleep(diff2.tv_usec);
			}

			startTV=endTV;
		}


    } while(!Interrupted);

#ifdef HAVE_ZMQ
	if(zmq_publisher)
	    zmq_close (zmq_publisher);
	if(zmq_context)
	    zmq_term (zmq_context);
#endif

    fclose(inputfile);
    return 0;
}


