
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
#include <endian.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

#include "wfficproc.h"
#include "figs.h"

#include "ni2http.h"

//#define HAVE_FEC

#ifdef HAVE_FEC
#include <fec.h>
#endif

#ifdef HAVE_ZMQ
#include <zmq.h>
#endif

int Interrupted=0;

int channel_count=0;
int channels_mapped=0;
ni2http_channel_t *channels[MAX_CHANNEL_COUNT];
ni2http_channel_t *channel_map[MAX_CU_COUNT];
ni2http_server_t ni2http_server;

void *zmq_context = NULL;

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
#if __BYTE_ORDER == __LITTLE_ENDIAN
/* Used by fig_1_4 */
		struct {
			uint16_t		Z:1;
			uint16_t		CI_flag:1;
			uint16_t		L_data:6;
			uint16_t		L1_data:6;
			uint16_t		F_PAD_type:2;
		};
#elif __BYTE_ORDER == __BIG_ENDIAN
/* Used by fig_1_4 */
		struct {
			uint16_t		F_PAD_type:2;
			uint16_t		L1_data:6;
			uint16_t		L_data:6;
			uint16_t		CI_flag:1;
			uint16_t		Z:1;
		};
#else
#error "Unknown system endian"
#endif
		uint16_t		val;
	};
} f_pad_t;

typedef struct {
	union {

#if __BYTE_ORDER == __LITTLE_ENDIAN
		/* reverse order for little-endian */
		struct {
			uint16_t		Rfa:4;
			uint16_t		Field_2:4;
			uint16_t		Field_1:4;
			uint16_t		C_flag:1;
			uint16_t		F_L:2;
			uint16_t		T:1;
		};
#elif __BYTE_ORDER == __BIG_ENDIAN
		/* reverse order for little-endian */
		struct {
			uint16_t		T:1;
			uint16_t		F_L:2;
			uint16_t		C_flag:1;
			uint16_t		Field_1:4;
			uint16_t		Field_2:4;
			uint16_t		Rfa:4;
		};
#else
#error "Unknown system endian"
#endif

		uint16_t		val;
	};
} x_pad_dls_t;


int fill_dls_pad(uint8_t *data_ptr, int len, uint8_t start_flag, ni2http_channel_t *chan)
{
	//print_bytes((char*)data_ptr, len);
	if(start_flag) {
		x_pad_dls_t dls;
		dls.val = data_ptr[len - 1] << 8 | data_ptr[len - 2];

		//WARN("DLS: T=%d, F_L=%d, C_flag=%d, Field_1=%d, Field_2=%d", dls.T, dls.F_L, dls.C_flag, dls.Field_1, dls.Field_2);

		if(chan->title_switcher == dls.T)
			return 0;

		if(dls.F_L > 1) {
			chan->pad_fillness=0;
		}

		if(!dls.C_flag && dls.Field_1 < STR_BUF_SIZE) {
			chan->pad_bytes_left = dls.Field_1 + 1;
		} else if(dls.C_flag && dls.Field_1 == 1) {
			chan->pad_data[0] = '\0';
			//shout set empty title.
			return 1;
		}

		if(dls.F_L == 1 || dls.F_L == 3) {
			chan->title_switcher = dls.T;
		}

//		chan->pad_data[chan->pad_fillness] = data_ptr[0];
//		chan->pad_fillness++;
		len-=2;
		chan->pad_last_fl = dls.F_L;
		//WARN("DLS[0]: pad_fillness=%d, pad_bytes_left=%d", chan->pad_fillness, chan->pad_bytes_left);
	}


	if (chan->pad_bytes_left && len > 0) {
		int bytes2get = chan->pad_bytes_left > len ? len : chan->pad_bytes_left;
		//WARN("DLS: bytes2get=%d", bytes2get);
		if(chan->pad_fillness + bytes2get > STR_BUF_SIZE) {
			WARN("too large pad");
			chan->pad_fillness=0;
			chan->pad_bytes_left=0;
		}

		int idx=0;
		while(bytes2get > idx) {
			//WARN("DLS: chan->pad_data[%d+(%d - %d)] = data_ptr[%d - %d + %d]", chan->pad_fillness, bytes2get, idx, len, bytes2get, idx);
			chan->pad_data[chan->pad_fillness+((bytes2get-1) - idx)] = data_ptr[len - bytes2get + idx];
			idx++;
		}

		//print_bytes((char*)chan->pad_data, chan->pad_fillness);

		chan->pad_fillness   += bytes2get;
		chan->pad_bytes_left -= bytes2get;
		if(chan->pad_bytes_left == 0 && (chan->pad_last_fl == 1 || chan->pad_last_fl == 3)) {
			chan->pad_data[chan->pad_fillness] = '\0';
			chan->pad_fillness++;
			//WARN("DLS: FULL pad_fillness=%d, pad_bytes_left=%d", chan->pad_fillness, chan->pad_bytes_left);
			//print_bytes((char*)chan->pad_data, chan->pad_fillness);
			return 1;
		}
	}
	return 0;
}



#if 0
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
			chan->pad_data[chan->pad_fillness] = '\0';
			chan->pad_fillness++;
			//print_bytes((char*)chan->pad_data, chan->pad_fillness);
			return 1;
		}
	}

	//WARN("FEED_X_PAD_SHORT: CI=%d: %02x %02x %02x %02x", ci_flag, data_ptr[0], data_ptr[1], data_ptr[2], data_ptr[3]);

	//print_bytes((char*)data_ptr, 4);
	return 0;
}
#endif

const int pad_sizes_idx[] = { 4, 6, 8, 12, 16, 24, 32, 48 };

int feed_x_pad(uint8_t *data_ptr, uint8_t ci_flag, ni2http_channel_t *chan, uint8_t x_pad_ind, int start_pos)
{
	int ret=0;
	if(x_pad_ind == 1) {
		/* short X-PAD */
		if(data_ptr[0] == 0x00 && data_ptr[1] == 0x00 && data_ptr[2] == 0x00 && data_ptr[3] == 0x00)
			return 0;
		if(fill_dls_pad(data_ptr, ci_flag ? 3 : 4, data_ptr[3] == 0x02 && ci_flag, chan)){
			INFO("chan[%d] song: %s", chan->sid, chan->pad_data);
			if(chan->shout) {
				shout_metadata_t *metadata = shout_metadata_new();
				shout_metadata_add(metadata, "song", (char *) chan->pad_data);
				shout_set_metadata(chan->shout, metadata);
				shout_metadata_free(metadata);
			}
		}
	}

	if(x_pad_ind != 2)
		return 0;

	if(ci_flag) {
		if(data_ptr[0] == 0x00 && data_ptr[1] == 0x00 && data_ptr[2] == 0x00 && data_ptr[3] == 0x00)
			return 0;

		//uint8_t subchLen = data_ptr[3] & 0x07;
		//uint8_t appTyp = data_ptr[3] >> 3;
		//WARN("FEED_X_PAD: CI=%d: %02x %02x %02x %02x", ci_flag, data_ptr[0], data_ptr[1], data_ptr[2], data_ptr[3]);
		//print_bytes((char*)data_ptr+4-32, 32);

		int data_pos = 3, subfield_idx=0;
		uint8_t sizes[4] = {0,0,0,0};
		uint8_t types[4] = {0,0,0,0};
		uint8_t appTyp, subfieldLen;
		do {
			appTyp = data_ptr[data_pos] & 0x1f;
			subfieldLen = data_ptr[data_pos] >> 5;
			data_pos--;

			if(appTyp == 0x1f) {
				appTyp = data_ptr[data_pos];
				data_pos--;
			}


			if(appTyp != 0) {
				types[subfield_idx] = appTyp;
				sizes[subfield_idx] = pad_sizes_idx[subfieldLen];
				subfield_idx++;
			}

			//WARN("FEED_X_PAD[%d|%d]: appTyp=%d, subchLen=%d", data_pos, subfield_idx, appTyp, pad_sizes_idx[subfieldLen]);

		} while (data_pos >=0 && appTyp != 0);
		//WARN("FEED_X_PAD bytes left=%d", data_pos);

		int idx;
		uint8_t *pad_data_subch_start = data_ptr + (data_pos) + 1; //data_ptr + data_pos;
		for(idx=0;idx<subfield_idx;idx++) {
			pad_data_subch_start -= sizes[idx]; //may be -1;
			if(pad_data_subch_start < data_ptr-start_pos) {
				WARN("X_PAD: BAD pad_data_subch_start: %p, data_ptr=%p, pos=%d, start==%p", pad_data_subch_start, data_ptr, start_pos, data_ptr-start_pos);
				break;
			}

			//FIXME: if next subfield is dls again, then this may not work fine.
			switch(types[idx]) {
			case 0x02:
			case 0x03:
				//WARN("FEED_X_PAD[%d]: appTyp=%d, subchLen=%d:", idx, types[idx], sizes[idx]);
				//print_bytes((char*)pad_data_subch_start, sizes[idx]);
				if(fill_dls_pad(pad_data_subch_start, sizes[idx], types[idx] == 0x02, chan)) {
					INFO("chan[%d] song: %s", chan->sid, chan->pad_data);
					if(chan->shout) {
						shout_metadata_t *metadata = shout_metadata_new();
						shout_metadata_add(metadata, "song", (char *) chan->pad_data);
						shout_set_metadata(chan->shout, metadata);
						shout_metadata_free(metadata);
					}
				}
				break;
			default:
				//WARN("FEED_X_PAD[%d]: UNUSED appTyp=%d, subchLen=%d:", idx, types[idx], sizes[idx]);
				//print_bytes((char*)pad_data_subch_start, sizes[idx]);
				break;
			}
		}


	} else {
		WARN("FEED_X_PAD: CI=%d, not implemented!", ci_flag);
	}

	//if start_of_frame = data_ptr - start_pos;
	//return 1 if have full text here.
	return ret;
}


int process_pad(uint8_t *data_ptr, int data_len, ni2http_channel_t *chan)
{
#if 1
	f_pad_t f_pad;
	f_pad.val = data_ptr[data_len-2] << 8 | data_ptr[data_len-1];
	//INFO("F_PAD: %02x %02x, ci=%d, type=%d:", data_ptr[data_len-2], data_ptr[data_len-1], f_pad.CI_flag, f_pad.F_PAD_type);
	//print_bytes((char*)data_ptr, data_len);
	if(f_pad.F_PAD_type==0){
		uint8_t x_pad_ind = f_pad.L1_data >> 4;
		uint8_t x_pad_L = f_pad.L1_data & 0x0f;
		//INFO("X_PAD: L1: %02x (val:%04x) ind=%d, L=%d", f_pad.L1_data, f_pad.val, x_pad_ind, x_pad_L);

		if(!x_pad_ind)
			return 0;

		//check if it x-pad or x-pad with DRC info
		if((x_pad_ind!=1 && x_pad_ind!=2) || (x_pad_L != 0 && x_pad_L != 1)) {
			print_bytes((char*)data_ptr+data_len-16, 16);
			WARN("X_PAD: L1: %02x (val:%04x) ind=%d, L=%d", f_pad.L1_data, f_pad.val, x_pad_ind, x_pad_L);
			return 0;
		}

		uint8_t *x_pad_ptr;
		int x_pad_pos;
		if(chan->is_dabplus) {
			x_pad_ptr = &data_ptr[data_len-2-4];
			x_pad_pos = data_len-2-4;
		} else {
			x_pad_ptr = &data_ptr[data_len-2-4-4];
			x_pad_pos = data_len-2-4-4;
		}

		//WARN("X_PAD: x_pad_ptr: %p, pos=%d", x_pad_ptr, x_pad_pos);
		//WARN("X_PAD: x_pad_min: %p, len=%d", data_ptr, data_len);
		feed_x_pad(x_pad_ptr, f_pad.CI_flag, chan, x_pad_ind, x_pad_pos);
		//print_bytes((char*)data_ptr+data_len-16, 16);

		//In-house information, or no information;
		//print_bytes((char*)data_ptr+data_len-16, 16);


	}
#endif
	return 1;
}

struct stream_parms {
	int rfa;
	int dac_rate;
	int sbr_flag;
	int ps_flag;
	int aac_channel_mode;
	int mpeg_surround_config;
};


int process_dabplus_wfadts(int framelen, struct stream_parms *sp, uint8_t *header)
{
	struct adts_fixed_header {
		unsigned                     : 4;
		unsigned home                : 1;
		unsigned orig                : 1;
		unsigned channel_config      : 3;
		unsigned private_bit         : 1;
		unsigned sampling_freq_index : 4;
		unsigned profile             : 2;
		unsigned protection_absent   : 1;
		unsigned layer               : 2;
		unsigned id                  : 1;
		unsigned syncword            : 12;
	} fh;
	struct adts_variable_header {
		unsigned                            : 4;
		unsigned num_raw_data_blks_in_frame : 2;
		unsigned adts_buffer_fullness       : 11;
		unsigned frame_length               : 13;
		unsigned copyright_id_start         : 1;
		unsigned copyright_id_bit           : 1;
	} vh;
#if 0
	unsigned char header[7];
#endif
	/* 32k 16k 48k 24k */
	const unsigned short samptab[] = {0x5, 0x8, 0x3, 0x6};

	fh.syncword = 0xfff;
	fh.id = 0;
	fh.layer = 0;
	fh.protection_absent = 1;
	fh.profile = 0;
	fh.sampling_freq_index = samptab[sp->dac_rate << 1 | sp->sbr_flag];

	fh.private_bit = 0;
	switch (sp->mpeg_surround_config) {
	case 0:
		if (sp->sbr_flag && !sp->aac_channel_mode && sp->ps_flag)
			fh.channel_config = 2; /* Parametric stereo */
		else
			fh.channel_config = 1 << sp->aac_channel_mode ;
		break;
	case 1:
		fh.channel_config = 6;
		break;
	default:
		fprintf(stderr,"Unrecognized mpeg_surround_config ignored\n");
		if (sp->sbr_flag && !sp->aac_channel_mode && sp->ps_flag)
			fh.channel_config = 2; /* Parametric stereo */
		else
			fh.channel_config = 1 << sp->aac_channel_mode ;
		break;
	}

	fh.orig = 0;
	fh.home = 0;
	vh.copyright_id_bit = 0;
	vh.copyright_id_start = 0;
	vh.frame_length = framelen + 7;  /* Includes header length */
	vh.adts_buffer_fullness = 1999;
	vh.num_raw_data_blks_in_frame = 0;
#if 0
	header[0] = fh.syncword >> 4;
	header[1] = (fh.syncword & 0xf) << 4;
	header[1] |= fh.id << 3;
	header[1] |= fh.layer << 1;
	header[1] |= fh.protection_absent;
        header[2] = fh.profile << 6;
	header[2] |= fh.sampling_freq_index << 2;
	header[2] |= fh.private_bit << 1;
	header[2] |= (fh.channel_config & 0x4);
	header[3] = (fh.channel_config & 0x3) << 6;
	header[3] |= fh.orig << 5;
	header[3] |= fh.home << 4;
	header[3] |= vh.copyright_id_bit << 3;
	header[3] |= vh.copyright_id_start << 2;
	header[3] |= (vh.frame_length >> 11) & 0x3;
	header[4] = (vh.frame_length >> 3) & 0xff;
	header[5] = (vh.frame_length & 0x7) << 5;
	header[5] |= vh.adts_buffer_fullness >> 6;
	header[6] = (vh.adts_buffer_fullness & 0x3f) << 2;
	header[6] |= vh.num_raw_data_blks_in_frame;
	//fwrite(header, sizeof(header), 1, stdout);
#else
	header[0] = fh.syncword >> 4;
	header[1] = (fh.syncword & 0xf) << 4;
	header[1] |= fh.id << 3;
	header[1] |= fh.layer << 1;
	header[1] |= fh.protection_absent;
        header[2] = fh.profile << 6;
	header[2] |= fh.sampling_freq_index << 2;
	header[2] |= fh.private_bit << 1;
	header[2] |= (fh.channel_config & 0x4);
	header[3] = (fh.channel_config & 0x3) << 6;
	header[3] |= fh.orig << 5;
	header[3] |= fh.home << 4;
	header[3] |= vh.copyright_id_bit << 3;
	header[3] |= vh.copyright_id_start << 2;
	header[3] |= (vh.frame_length >> 11) & 0x3;
	header[4] = (vh.frame_length >> 3) & 0xff;
	header[5] = (vh.frame_length & 0x7) << 5;
	header[5] |= vh.adts_buffer_fullness >> 6;
	header[6] = (vh.adts_buffer_fullness & 0x3f) << 2;
	header[6] |= vh.num_raw_data_blks_in_frame;
#endif

	return 0;
}


int process_dabplus_pad(uint8_t *data_ptr, int data_len, ni2http_channel_t *chan)
{
	uint8_t object_type = data_ptr[0] >> 5;
	if(object_type == 0x04) {
		/* HAVE DSE */
		int bytes_readed=0;
		  /* Element Instance Tag */
		//uint8_t elementInstanceTag = (data_ptr[0] >> 1) & 0x0f;
		//uint8_t dataByteAlignFlag = data_ptr[0] & 0x01;
		bytes_readed++;
		int count = data_ptr[1];
		bytes_readed++;
        if (count == 255) {
		    count += data_ptr[2]; /* EscCount */
			bytes_readed++;
		}

		//ERROR("DAB+ PAD: object_type=%d, elementInstanceTag=%d, dataByteAlignFlag=%d count=%d, start=%p\n",
		//		object_type, elementInstanceTag, dataByteAlignFlag, count, data_ptr);
		//print_bytes((char*)data_ptr + bytes_readed, count);
		process_pad(data_ptr + bytes_readed, count, chan);
	}
	return 0;
}

int process_dabplus(uint8_t *data_ptr, int data_len, ni2http_channel_t *chan)
{
	int s = chan->bitrate/8;
	int audio_super_frame_size = data_len * 5 - s * 10; /* Excludes error prot bytes */

	//ERROR("DAB+ [%d] br: %d:", data_len, chan->bitrate);
	//print_bytes((char*)data_ptr, data_len);

	if (!chan->dabplus_frame && !firecrccheck(data_ptr)) {
		ERROR("DAB+ firecrccheck: ERROR");
		return 0;
	}

	memcpy(&chan->dabplus_data[data_len * chan->dabplus_frame], data_ptr, data_len);
	chan->dabplus_frame++;

	int i,j;
	if (chan->dabplus_frame > 4) {
		chan->dabplus_frame=0;
		for (i = 0; i < s; i++) {
			uint8_t cbuf[120];
			/* Dis-interleaving is necessary prior to RS decoding */
			for (j = 0; j < 120; j++)
				cbuf[j] = chan->dabplus_data[s * j + i];
#ifdef HAVE_FEC
			int errs = decode_rs_char(chan->dabplus_rs, cbuf, (int*)NULL, 0);
			if(errs!=0) fprintf(stderr,"DAB+ errors: %d\n",errs);
#endif
			/* Write checked/corrected data back to sfbuf */
			for (j = 0; j < 110; j++)
				chan->dabplus_data[s * j + i] = cbuf[j];
		}

		const int austab[4] = {4, 2, 6, 3};
		struct stream_parms sp;
		uint16_t au_start[6] = {0,0,0,0,0,0};
		int16_t au_size[6] = {0,0,0,0,0,0};

		sp.rfa = (chan->dabplus_data[2] & 0x80) && 1;
		sp.dac_rate = (chan->dabplus_data[2] & 0x40) && 1;
		sp.sbr_flag = (chan->dabplus_data[2] & 0x20) && 1;
		sp.aac_channel_mode = (chan->dabplus_data[2] & 0x10) && 1;
		sp.ps_flag = (chan->dabplus_data[2] & 0x8) && 1;
		sp.mpeg_surround_config = chan->dabplus_data[2] & 0x7;
		int num_aus = austab[sp.dac_rate << 1 | sp.sbr_flag];
		switch (num_aus) {
		case 2:
			au_start[0] = 5;
			au_start[1] = (chan->dabplus_data[3] << 4) + ((chan->dabplus_data[4]) >> 4);
			if(au_start[1]) {
				au_size[0] = au_start[1] - au_start[0];
				au_size[1] = audio_super_frame_size - au_start[1];
			}
			break;
		case 3:
			au_start[0] = 6;
			au_start[1] = (chan->dabplus_data[3] << 4) + ((chan->dabplus_data[4]) >> 4);
			au_start[2] = ((chan->dabplus_data[4] & 0x0f) << 8) + chan->dabplus_data[5];
			if(au_start[1] && au_start[2]) {
				au_size[0] = au_start[1] - au_start[0];
				au_size[1] = au_start[2] - au_start[1];
				au_size[2] = audio_super_frame_size - au_start[2];
			}
			break;
		case 4:
			au_start[0] = 8;
			au_start[1] = (chan->dabplus_data[3] << 4) + (chan->dabplus_data[4] >> 4);
			au_start[2] = ((chan->dabplus_data[4] & 0x0f) << 8) + chan->dabplus_data[5];
			au_start[3] = (chan->dabplus_data[6] << 4) + (chan->dabplus_data[7] >> 4);
			if(au_start[1] && au_start[2] && au_start[3]) {
				au_size[0] = au_start[1] - au_start[0];
				au_size[1] = au_start[2] - au_start[1];
				au_size[2] = au_start[3] - au_start[2];
				au_size[3] = audio_super_frame_size - au_start[3];
			}
			break;
		case 6:
			au_start[0] = 11;
			au_start[1] = (chan->dabplus_data[3] << 4) + (chan->dabplus_data[4] >> 4);
			au_start[2] = ((chan->dabplus_data[4] & 0x0f) << 8) + chan->dabplus_data[5];
			au_start[3] = (chan->dabplus_data[6] << 4) + (chan->dabplus_data[7] >> 4);
			au_start[4] = ((chan->dabplus_data[7] & 0x0f) << 8) + chan->dabplus_data[8];
			au_start[5] = (chan->dabplus_data[9] << 4) + (chan->dabplus_data[10] >> 4);
			if(au_start[1] && au_start[2] && au_start[3] && au_start[4] && au_start[5]) {
				au_size[0] = au_start[1] - au_start[0];
				au_size[1] = au_start[2] - au_start[1];
				au_size[2] = au_start[3] - au_start[2];
				au_size[3] = au_start[4] - au_start[3];
				au_size[4] = au_start[5] - au_start[4];
				au_size[5] = audio_super_frame_size - au_start[5];
			}
			break;
		default: fprintf(stderr,"num_aus = %d is invalid\n",num_aus);

			break;
		}
		for (i = 0; i < num_aus; i++) {
			if(!au_size[i] || au_start[i] + au_size[i] < 2 || au_start[i] + au_size[i] > audio_super_frame_size) {
//				ERROR("Bad AU size, ignoring frame for SID %d.\n", chan->sid);
				continue;
			}
			/* Invert CRC bits */
			chan->dabplus_data[au_start[i] + au_size[i] - 2] ^= 0xff;
			chan->dabplus_data[au_start[i] + au_size[i] - 1] ^= 0xff;
			/* AUs with bad CRC are silently ignored */
			if (crccheck(chan->dabplus_data + au_start[i], au_size[i])) {
				//wfadts(au_size[i]-2, &sp);
				//fwrite(chan->dabplus_data + au_start[i], sizeof(unsigned char), au_size[i] - 2, stdout);
				if(chan->extract_pad) {
					process_dabplus_pad(chan->dabplus_data + au_start[i], au_size[i] - 2, chan);
				}
				uint8_t buff2[1024];
				process_dabplus_wfadts(au_size[i]-2, &sp, buff2);
				memcpy(buff2 + 7, chan->dabplus_data + au_start[i], au_size[i] - 2);
				//fwrite(buff2, au_size[i] - 2 + 7, 1, stdout);
				int result;
				if(chan->shout) {
					result = shout_send_raw(chan->shout, buff2, au_size[i] - 2 + 7);
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

				if(chan->file) {
					result = fwrite(buff2, au_size[i] - 2 + 7, 1, chan->file);
					if(result != 1) {
						ERROR("failed to write data to file for SID %d.\n", chan->sid);
					}
				}

			}
		}

	}

	return 1;
}


int process_mp2(uint8_t *data_ptr, int data_len, ni2http_channel_t *chan)
{
	if(chan->extract_pad && !chan->is_dabplus)
		process_pad(data_ptr, data_len, chan);

	//fwrite(data_ptr, data_len, 1, to_save);
  int result;
  if(chan->shout) {
	result = shout_send_raw(chan->shout, data_ptr, data_len);
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

	if(chan->file) {
		result = fwrite(data_ptr, data_len, 1, chan->file);
		if(result != 1) {
			ERROR("failed to write data to file for SID %d (%d): %s.\n", chan->sid, errno, strerror(errno));
		}
	}

	return 0;
}

int process_stc(uint8_t *msc_ptr, uint8_t *stc_ptr, int idx, int prev_len)
{
	uint8_t *data_ptr;
	int data_len;

	sstc_t sstc;
	sstc.val = stc_ptr[4*idx] << 24 | stc_ptr[4*idx + 1] << 16 | stc_ptr[4*idx + 2] << 8 | stc_ptr[4*idx + 3];
	//DEBUG("stream[%d]: scid=%d, sad=%d, tpl=%d, stl=%d", idx, sstc.scid, sstc.sad, sstc.tpl, sstc.stl);
	ni2http_channel_t *chan = channel_map[sstc.sad];
	if(chan && (chan->shout || chan->file)) {
		data_ptr = &msc_ptr[prev_len*8];
		data_len = sstc.stl*8;

		if(chan->is_dabplus && chan->extract_dabplus) {
			process_dabplus(data_ptr, data_len, chan);
		} else {
			process_mp2(data_ptr, data_len, chan);
		}
	}

#ifdef HAVE_ZMQ
	if(chan && chan->zmq_sock) {
		data_ptr = &msc_ptr[prev_len*8];
		data_len = sstc.stl*8;
		int frame_length = sizeof(struct zmq_frame_header) + data_len;

		struct zmq_frame_header* header = malloc(frame_length);
		uint8_t* txframe = ((uint8_t*)header) + sizeof(struct zmq_frame_header);
		header->version          = 1;
		header->encoder          = chan->is_dabplus ? ZMQ_ENCODER_FDK : ZMQ_ENCODER_TOOLAME;
		header->datasize         = data_len;
		header->audiolevel_left  = 0x1FFF; //alevel unknown
		header->audiolevel_right = 0x1FFF;

		memcpy(txframe, data_ptr, data_len);
		int send_error = zmq_send(chan->zmq_sock, header, frame_length, ZMQ_DONTWAIT);
		free(header);
		if (send_error < 0) {
			fprintf(stderr, "ZeroMQ send failed! %s\n", zmq_strerror(errno));
		}
	}
#endif


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
	shout_set_agent( shout, "ni2http" );
	shout_set_host( shout, ni2http_server.host );
	shout_set_port( shout, ni2http_server.port );
	shout_set_user( shout, ni2http_server.user );
	shout_set_password( shout, ni2http_server.password );
	shout_set_protocol( shout, ni2http_server.protocol );
	if(!chan->is_dabplus)
		shout_set_format( shout, SHOUT_FORMAT_MP3 );
	else if (chan->extract_dabplus){
#ifdef SHOUT_FORMAT_AAC
		shout_set_format( shout, SHOUT_FORMAT_AAC );
#else
		shout_set_format( shout, SHOUT_FORMAT_MP3 );
#endif
	} else {
#ifdef SHOUT_FORMAT_CUSTOM
		shout_set_format( shout, SHOUT_FORMAT_CUSTOM );
#else
		shout_set_format( shout, SHOUT_FORMAT_MP3 );
#endif
	}

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
				ERROR("Can't read from file in %ld loop, total readed: %d", count, total_readed);
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
				channels[ch]->is_dabplus = s_data->pa->dabplus;
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
						if(strlen(channels[ch]->mount) > 0) {
							init_shout_channel(channels[ch]);
							channels[ch]->title_switcher=-1;
						}

						if(strlen(channels[ch]->file_name) > 0) {
							INFO("Writing to: %s", channels[ch]->file_name);
							channels[ch]->file = fopen(channels[ch]->file_name, "a+");
							if(!channels[ch]->file) {
								INFO("sid[%d]: can't open output dump file %s.", s_data->sid, channels[ch]->file_name);
							} else {
								int fd_o = fileno(channels[ch]->file);
								if (fcntl(fd_o, F_SETFL, fcntl(fd_o, F_GETFL) | O_NONBLOCK) == -1) {
									INFO("sid[%d]: can't set non-block output for file %s.", s_data->sid, channels[ch]->file_name);
								}
							}
							channels[ch]->title_switcher=-1;
						}

						if(channels[ch]->dabplus_data) {
							free(channels[ch]->dabplus_data);
							channels[ch]->dabplus_data=NULL;
						}

						if(channels[ch]->is_dabplus) {
							channels[ch]->dabplus_data = malloc(channels[ch]->bitrate/8 * 120);
#ifdef HAVE_FEC
							channels[ch]->dabplus_rs = init_rs_char(8, 0x11d, 0, 1, 10, 135);
#endif
						}


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

#ifdef HAVE_ZMQ
		if (channels[i]->zmq_sock) {
			zmq_close(channels[i]->zmq_sock);
		}
#endif
		if(channels[i]->file) {
			fclose(channels[i]->file);
		}

		if(channels[i]->dabplus_data)
			free(channels[i]->dabplus_data);

#ifdef HAVE_FEC
		if(channels[i]->is_dabplus)
			free(channels[i]->dabplus_rs);
#endif

		free( channels[i] );
	}

#ifdef HAVE_ZMQ
	if (zmq_context)
		zmq_ctx_destroy(zmq_context);
#endif
	// Shutdown libshout
	shout_shutdown();

    fclose(inputfile);
    //fclose(outputfile);
    return 0;
}

