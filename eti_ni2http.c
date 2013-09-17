
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

#include <math.h>
#include <fec.h>

#include "wfficproc.h"
#include "figs.h"

#include "eti_ni2http.h"


int Interrupted=0;

int channel_count=0;
int channels_mapped=0;
ni2http_channel_t *channels[MAX_CHANNEL_COUNT];
ni2http_channel_t *channel_map[MAX_CU_COUNT];
ni2http_server_t ni2http_server;


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
    fprintf(stderr, "usage: %s [--list] [--delay] [-i <inputfile>] [-c <config_file>]\n", psz);
    exit(EXIT_FAILURE);
}


static void signal_handler(int signum)
{
	if (signum != SIGPIPE) {
		Interrupted=signum;
	}
	signal(signum,signal_handler);
}




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


typedef struct {
	union {
		/* reverse order for little-endian */
		struct {
			uint16_t		Z:1;
			uint16_t		CI_flag:1;
			uint16_t		L_data:6;
			uint16_t		L1_data:6;
			uint16_t		F_PAD_type:2;
		};
		uint16_t		val;
	};
} f_pad_t;

typedef struct {
	union {
		/* reverse order for little-endian */
		struct {
			uint16_t		Rfa:4;
			uint16_t		Field_2:4;
			uint16_t		Field_1:4;
			uint16_t		C_flag:1;
			uint16_t		F_L:2;
			uint16_t		T:1;
		};
		uint16_t		val;
	};
} x_pad_dls_t;




int feed_x_pad_short(uint8_t *data_ptr, uint8_t ci_flag, ni2http_channel_t *chan)
{
	if(ci_flag) {
		if(data_ptr[0] == 0x00 && data_ptr[1] == 0x00 && data_ptr[2] == 0x00 && data_ptr[3] == 0x00)
			return 0;
		if(data_ptr[3] == 0x02) {
			x_pad_dls_t dls;
			dls.val = data_ptr[2] << 8 | data_ptr[1];

			//WARN("DLS: T=%d, F_L=%d, C_flag=%d, Field_1=%d, Field_2=%d", dls.T, dls.F_L, dls.C_flag, dls.Field_1, dls.Field_2);

			if(chan->title_switcher == dls.T)
				return 0;

			if(dls.F_L > 1) {
				chan->pad_fillness=0;
			}

			if(!dls.C_flag && dls.Field_1 < STR_BUF_SIZE) {
				chan->pad_bytes_left = dls.Field_1;
			} else if(dls.C_flag && dls.Field_1 == 1) {
				chan->pad_data[0] = '\0';
				//shout set empty title.
				return 1;
			}

			if(dls.F_L == 1 || dls.F_L == 3) {
				chan->title_switcher = dls.T;
			}

			chan->pad_data[chan->pad_fillness] = data_ptr[0];
			chan->pad_fillness++;
			chan->pad_last_fl = dls.F_L;
			//WARN("DLS[0]: pad_fillness=%d, pad_bytes_left=%d", chan->pad_fillness, chan->pad_bytes_left);
		}
	} else if (chan->pad_bytes_left) {
		int bytes2get = chan->pad_bytes_left > 4 ? 4 : chan->pad_bytes_left;
		//WARN("DLS: bytes2get=%d", bytes2get);
		if(chan->pad_fillness + bytes2get > STR_BUF_SIZE) {
			WARN("too large pad");
			chan->pad_fillness=0;
			chan->pad_bytes_left=0;
		}

		if(bytes2get > 3)
			chan->pad_data[chan->pad_fillness+3] = data_ptr[0];
		if(bytes2get > 2)
			chan->pad_data[chan->pad_fillness+2] = data_ptr[1];
		if(bytes2get > 1)
			chan->pad_data[chan->pad_fillness+1] = data_ptr[2];
		if(bytes2get)
			chan->pad_data[chan->pad_fillness] = data_ptr[3];

		chan->pad_fillness   += bytes2get;
		chan->pad_bytes_left -= bytes2get;
		//WARN("DLS: pad_fillness=%d, pad_bytes_left=%d", chan->pad_fillness, chan->pad_bytes_left);
		if(chan->pad_bytes_left == 0 && (chan->pad_last_fl == 1 || chan->pad_last_fl == 3)) {
			//print_bytes((char*)chan->pad_data, chan->pad_fillness);
			return 1;
		}
	}

	//WARN("FEED_X_PAD_SHORT: CI=%d: %02x %02x %02x %02x", ci_flag, data_ptr[0], data_ptr[1], data_ptr[2], data_ptr[3]);

	//print_bytes((char*)data_ptr, 4);
	return 0;
}


int process_pad(uint8_t *data_ptr, int data_len, ni2http_channel_t *chan)
{
#if 1
	f_pad_t f_pad;
	f_pad.val = data_ptr[data_len-2] << 8 | data_ptr[data_len-1];
	//INFO("F_PAD: %02x %02x, ci=%d, type=%d.", data_ptr[data_len-2], data_ptr[data_len-1], f_pad.CI_flag, f_pad.F_PAD_type);
	//print_bytes((char*)data_ptr+data_len-16, 16);
	if(f_pad.F_PAD_type==0){
		uint8_t x_pad_ind = f_pad.L1_data >> 4;
		uint8_t x_pad_L = f_pad.L1_data & 0x0f;
		//INFO("X_PAD: L1: %02x (val:%04x) ind=%d, L=%d", f_pad.L1_data, f_pad.val, x_pad_ind, x_pad_L);

		if(x_pad_ind!=1 || x_pad_L != 0) {
			print_bytes((char*)data_ptr+data_len-16, 16);
			WARN("X_PAD: L1: %02x (val:%04x) ind=%d, L=%d", f_pad.L1_data, f_pad.val, x_pad_ind, x_pad_L);
			return 0;
		}
		if(feed_x_pad_short(&data_ptr[data_len-2-4-4], f_pad.CI_flag, chan)) {
			INFO("chan[%d] song: %s", chan->sid, chan->pad_data);
			shout_metadata_t *metadata = shout_metadata_new();
			shout_metadata_add(metadata, "song", (char *) chan->pad_data);
			shout_set_metadata(chan->shout, metadata);
			shout_metadata_free(metadata);
		}
		//print_bytes((char*)data_ptr+data_len-16, 16);

		//In-house information, or no information;
		//print_bytes((char*)data_ptr+data_len-16, 16);


	}
#endif
	return 1;
}

int process_stc(uint8_t *msc_ptr, uint8_t *stc_ptr, int idx, int prev_len)
{
	sstc_t sstc;
	sstc.val = stc_ptr[4*idx] << 24 | stc_ptr[4*idx + 1] << 16 | stc_ptr[4*idx + 2] << 8 | stc_ptr[4*idx + 3];
	//DEBUG("stream[%d]: scid=%d, sad=%d, tpl=%d, stl=%d", idx, sstc.scid, sstc.sad, sstc.tpl, sstc.stl);
	ni2http_channel_t *chan = channel_map[sstc.sad];
	if(chan && chan->shout) {
		uint8_t *data_ptr = &msc_ptr[prev_len*8];
		int data_len = sstc.stl*8;

		process_pad(data_ptr, data_len, chan);

		//fwrite(data_ptr, data_len, 1, to_save);
		int result = shout_send_raw(chan->shout, data_ptr, data_len);
		if (result < 0) {
			ERROR("failed to send data to server for SID %d.\n", chan->sid);
			ERROR("  libshout: %s.\n", shout_get_error(chan->shout));
#if 0
			shout_close( chan->shout );
			shout_sync(chan->shout);
			int result = shout_open( chan->shout );
			if (result != SHOUTERR_SUCCESS) {
				fprintf(stderr,"  Failed to connect to server: %s.\n", shout_get_error(chan->shout));
			}
#endif
		}
	}

#if 0
	//print_bytes((char*)data_ptr, data_len);
	char out[256] = "";
	sprintf(out, "stream-%d.mp2", sstc.scid);

	FILE *outputfile = fopen(out, "a+");
	fclose(outputfile);
#endif

	//exit(0);
	return sstc.stl;
}













static int init_shout_channel( ni2http_channel_t *chan )
{
	shout_t *shout;
	char string[STR_BUF_SIZE];
	int result;

	// Don't connect?
	if (strlen(ni2http_server.host)==0) {
		return 0;
	} else {
		shout = shout_new();
	}


	// Set server parameters
	shout_set_agent( shout, "eti_ni2http" );
	shout_set_host( shout, ni2http_server.host );
	shout_set_port( shout, ni2http_server.port );
	shout_set_user( shout, ni2http_server.user );
	shout_set_password( shout, ni2http_server.password );
	shout_set_protocol( shout, ni2http_server.protocol );
	shout_set_format( shout, SHOUT_FORMAT_MP3 );

	shout_set_name( shout, chan->name );
	shout_set_mount( shout, chan->mount );
	shout_set_genre( shout, chan->genre );
	shout_set_description( shout, chan->description );
	shout_set_url( shout, chan->url );


	// Add information about the audio format
	snprintf(string, STR_BUF_SIZE, "%d", chan->bitrate );
	shout_set_audio_info( shout, SHOUT_AI_BITRATE, string);
#if 0
	snprintf(string, STR_BUF_SIZE, "%d", chan->mpah.samplerate );
	shout_set_audio_info( shout, SHOUT_AI_SAMPLERATE, string);

	snprintf(string, STR_BUF_SIZE, "%d", chan->mpah.channels );
	shout_set_audio_info( shout, SHOUT_AI_CHANNELS, string);
#endif

	//shout_set_nonblocking(shout, 1);


	// Connect!
	INFO("connecting to: http://%s:%d%s", shout_get_host( shout ), shout_get_port( shout ), shout_get_mount( shout ));

	result = shout_open( shout );
	chan->shout = shout;

	if (result != SHOUTERR_SUCCESS) {
		ERROR("Failed to connect to server: %s.", shout_get_error(shout));
		return 0;
	}

	chan->title_switcher=-1;

	//INFO("shout=%p", chan->shout);
	return 1;
}


int main(int i_argc, char **ppsz_argv)
{   
    int i, c, list=0, config_parsed=0, delay=0;
    FILE *inputfile=stdin;
    
	for (i=0;i<MAX_CU_COUNT;i++) channel_map[i]=NULL;
	for (i=0;i<MAX_CHANNEL_COUNT;i++) channels[i]=NULL;
	memset( &ni2http_server, 0, sizeof(ni2http_server_t) );

	// Default server settings
	ni2http_server.port = SERVER_PORT_DEFAULT;
	strcpy(ni2http_server.user, SERVER_USER_DEFAULT);
	strcpy(ni2http_server.password, SERVER_PASSWORD_DEFAULT);
	ni2http_server.protocol = SERVER_PROTOCOL_DEFAULT;

    static const struct option long_options[] = {
        { "input",         required_argument,       NULL, 'i' },
        { "config",        required_argument,       NULL, 'c' },
        { "delay",        no_argument,               NULL, 'd' },
        { "list",        no_argument,       		NULL, 'l' },
        { 0, 0, 0, 0 }
    };
    
    while ((c = getopt_long(i_argc, ppsz_argv, "i:c:ldh", long_options, NULL)) != -1)
    {
        switch (c) {
        case 'i':
            inputfile = fopen(optarg, "r");
            if(!inputfile) {
                ERROR("cant open input file!");
                exit(1);
            }
            break;

        case 'c':
        	parse_config(optarg);
        	config_parsed=1;
            break;

        case 'l':
        	list=1;
            break;
        case 'd':
        	delay=1;
            break;
        case 'h':
        default:
            usage(ppsz_argv[0]);
        }
    }

	/* space for 1 ETI frame for bitwise seeking */
    int bytes_readed = 0;
    uint8_t p_ni_search_block[ETI_NI_RAW_SIZE];
    uint32_t sync_byte;
    int sync_found = 0;
    int total_readed = 0;

    fc_t fc;
	unsigned long int count = 0;

	// Initialise libshout
	shout_init();

	ficinit(&einf);
	struct timeval diff1, diff2, startTV, endTV;
	if(delay)
		gettimeofday(&startTV, NULL);

    /* search for ETI-NI sync */
    do {
		bytes_readed=0;

		do {
			size_t i_ret = fread(p_ni_search_block + bytes_readed, ETI_NI_RAW_SIZE - bytes_readed, 1, inputfile);
			if(i_ret != 1){
				ERROR("Can't read from file in %d loop, total readed: %d", count, total_readed);
				exit(1);
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

		fc.val = p_ni_search_block[4] << 24 | p_ni_search_block[5] << 16 | p_ni_search_block[6] << 8 | p_ni_search_block[7];
		if(fc.val == 0xffffffff)
			continue;

		DEBUG("NST=%d", p_ni_search_block[5] & 0x7f);
		//fc_t fc;
		//uint8_t fct = fc_word >> 24;
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


		//process_msc(&p_ni_search_block[4+4 + fc.nst*4 + 4], ficl, fc.nst);
		uint8_t *stc_ptr = &p_ni_search_block[(1 + 1)*4];
		uint8_t *msc_ptr = &stc_ptr[(fc.nst + 1 + ficl)*4];
		//print_bytes((char*)&p_ni_search_block[(1 + 1 + fc.nst + 1)*4], ficl*4);
		//exit(0);

		int idx;
		if(!channels_mapped) {
			/*search for zero phase */
			//print_bytes((char*)&stc_ptr[(fc.nst + 1)*4], ficl*4);
			process_fic2(&stc_ptr[(fc.nst + 1)*4], ficl*4, fc.mid);
			if(!labelled(&einf))
				continue;

			if(list || !config_parsed) {
				DEBUG("Ensemble list:");
				disp_ensemble(&einf);
				if(!config_parsed) {
					fclose(inputfile);
					exit(0);
				}
			}


			int ch;
			INFO("channels: %d", channel_count);
			for (ch=0; ch <channel_count; ch++) {
				DEBUG("channels[%d] sid=%d", channel_count, channels[ch]->sid);
				struct service *s_data = find_service(&einf, channels[ch]->sid);
				if(!s_data) {
					ERROR("service for channel %d - not found", channels[ch]->sid);
					continue;
				}
				channels[ch]->bitrate = s_data->pa->bitrate;
				if(channels[ch]->name[0] == '\0')
					strncpy(channels[ch]->name, s_data->label, sizeof(s_data->label));

				sstc_t sstc;
				int idx_found = 0;
				for (idx=0; idx < fc.nst; idx++) {
					sstc.val = stc_ptr[4*idx] << 24 | stc_ptr[4*idx + 1] << 16 | stc_ptr[4*idx + 2] << 8 | stc_ptr[4*idx + 3];
					if(sstc.sad == s_data->pa->startaddr){
						channel_map[s_data->pa->startaddr] = channels[ch];
						INFO("sid[%d]: channel_map[%d] = channel[%d]", s_data->sid, s_data->pa->startaddr, idx);
						idx_found=1;
						channels_mapped++;
						init_shout_channel(channels[ch]);
						break;
					}
				}

				if(!idx_found) {
					ERROR("index for service %d - not found", channels[ch]->sid);
					continue;
				}
			}

			if(!channels_mapped){
				INFO("no channels to map, exiting");
				exit(1);
			}
			INFO("%d channels will be streamed", channels_mapped);

			// Setup signal handlers
			if (signal(SIGHUP, signal_handler) == SIG_IGN) signal(SIGHUP, SIG_IGN);
			if (signal(SIGINT, signal_handler) == SIG_IGN) signal(SIGINT, SIG_IGN);
			if (signal(SIGTERM, signal_handler) == SIG_IGN) signal(SIGTERM, SIG_IGN);

		} else {
			int len = 0; //len in 8-bit intervals of previous frame
			for (idx=0; idx < fc.nst; idx++) {
				len += process_stc(msc_ptr, stc_ptr, idx, len);
			}


			if(delay) {
				gettimeofday(&endTV, NULL);
				timersub(&endTV, &startTV, &diff1);
				if(diff1.tv_sec == 0 && diff1.tv_usec < ETI_NI_FRAME_TIME) {
					startTV.tv_sec=0;
					startTV.tv_usec=ETI_NI_FRAME_TIME;
					timersub(&startTV, &diff1, &diff2);
					usleep(diff2.tv_usec);
				}

				startTV=endTV;
			}
		}


    } while(!Interrupted);


	// Clean up
	for (i=0;i<channel_count;i++) {
		if (channels[i]->shout) {
			shout_close( channels[i]->shout );
			shout_free( channels[i]->shout );
		}

		free( channels[i] );
	}

	// Shutdown libshout
	shout_shutdown();

    fclose(inputfile);
    //fclose(outputfile);
    return 0;
}

