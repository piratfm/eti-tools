
/* Uncomment this if u want to enable Forwarded Error Correction for ETI-NA stream 
 * It will use more CPU cycles.
 **/
//#define HAVE_FEC


#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <math.h>



#ifdef HAVE_FEC
#include <fec.h>
#endif

#define E1_FRAME_LEN				32
#define FRAMES_IN_BLOCK				8
#define BLOCKS_IN_SUPERBLOCK		8
#define SUPERBLOCKS_IN_MULTIFRAME	3
#define FRAMES_IN_MULTIFRAME 	FRAMES_IN_BLOCK*BLOCKS_IN_SUPERBLOCK*SUPERBLOCKS_IN_MULTIFRAME

#define ETI_NI_FSYNC0				0xb63a07ff
#define ETI_NI_FSYNC1				0x49c5f8ff
#define ETI_NI_RAW_SIZE				6144


/*****************************************************************************
 * info/help/debug
 *****************************************************************************/
#define DEBUG(format, ...) fprintf (stderr, "DEBUG: "format"\n", ## __VA_ARGS__)
#define  INFO(format, ...) fprintf (stderr, "INFO:  "format"\n", ## __VA_ARGS__)
#define WARN(format, ...)  fprintf (stderr, "WARN:  "format"\n", ## __VA_ARGS__)
#define ERROR(format, ...) fprintf (stderr, "ERROR: "format"\n", ## __VA_ARGS__)


static void usage(const char *psz)
{
#ifdef HAVE_FEC
    fprintf(stderr, "usage: %s [--no-fec] [-i <inputfile>] [-o <outputfile>]\n", psz);
#else
    fprintf(stderr, "usage: %s [-i <inputfile>] [-o <outputfile>]\n", psz);
#endif
    exit(EXIT_FAILURE);
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



uint8_t e1_get_byte_in_pos(uint8_t  *inBuffer, int bitsPos)
{
	if(!(bitsPos & 0x07))
		return inBuffer[bitsPos>>3];
	else
		return inBuffer[bitsPos>>3] << (bitsPos & 0x07) | (inBuffer[(bitsPos>>3) + 1] >> (8-(bitsPos & 0x07)));
}


#define SYNC_SEARCH				8
#define E1_SEARCH_LEN			E1_FRAME_LEN*2 //even/odd
#define E1_SYNC_VAL				0x1b //u0011011
#define E1_SYNC_MASK			0x7f

int e1_sync_bitsearch(uint8_t  *inBuffer, int *is_inverted)
{
	int bit_num;
	for(bit_num=0;bit_num<8;bit_num++) {

		int byte_idx=0,frame_idx=0;
		for(byte_idx=0; byte_idx<E1_SEARCH_LEN; byte_idx++) {
			for(frame_idx=0;frame_idx<SYNC_SEARCH;frame_idx++) {
				uint8_t search_val = e1_get_byte_in_pos(inBuffer, (byte_idx + frame_idx*E1_SEARCH_LEN)*8 + bit_num) & E1_SYNC_MASK;
				//fprintf(stderr, "frame: %d: %02x %02x\n", frame_idx, search_val, E1_SYNC_MASK ^ E1_SYNC_VAL);

				if(!(search_val == E1_SYNC_VAL) &&
				   !(search_val == (E1_SYNC_VAL ^ E1_SYNC_MASK))
				) {
					break; //not a sync bits
				} else if(frame_idx==0) {
					//check inversion
					if(search_val == E1_SYNC_VAL)
						*is_inverted=0;
					else
						*is_inverted=1;
				} else {
					//inversion misconfiguration, bad frame.
					if((*is_inverted && !(search_val == (E1_SYNC_VAL ^ E1_SYNC_MASK))) ||
					   (!*is_inverted && !(search_val == E1_SYNC_VAL)))
						break;
				}
//				else
//					INFO("match: byte_idx: %d, frame_idx: %d", byte_idx, frame_idx);
			}
			if(frame_idx==SYNC_SEARCH) {
				INFO("E1 Sync found at bit: %d, inverted: %s", byte_idx*8+bit_num, *is_inverted ? "yes" : "no");
				return byte_idx*8+bit_num;
			}
		}
	}

    return -1;
}


/***************************************************************************
 * Bitwise shifting routine.
 ***************************************************************************/
/* sample:
 * seek: B:8, b:2
 * pre_offset_bits=0x19 (must be 0x64)
 * WARNING: LITTLE-ENDIAN LOGIC!
 * */
int shift_frame_mem(uint8_t *read_ptr, uint8_t *remaining_bits, uint8_t remaining_bits_num, int is_inverted, uint8_t *frame_ptr)
{
	uint8_t bit_mask = 0xff >> (8-remaining_bits_num);
	uint32_t block;
	int i;

	for(i=0;i<E1_FRAME_LEN/sizeof(uint32_t); i++) {
		int byte_idx = sizeof(uint32_t)*i;
		block = read_ptr[byte_idx] << 24 | read_ptr[byte_idx+1] << 16 | read_ptr[byte_idx+2] << 8 | read_ptr[byte_idx+3];
		uint8_t bits2save = block & bit_mask;
		block = (block >> remaining_bits_num) | ((*remaining_bits & bit_mask) << (sizeof(uint32_t)*8 - remaining_bits_num));
		if(is_inverted)
			block ^= 0xFFFFFFFF;
		//*((uint32_t *) (frame_ptr+sizeof(uint32_t)*i)) = bswap_32(block);
		frame_ptr[byte_idx] = (block >> 24) & 0xff;
		frame_ptr[byte_idx+1] = (block >> 16) & 0xff;
		frame_ptr[byte_idx+2] = (block >> 8) & 0xff;
		frame_ptr[byte_idx+3] = block & 0xff;
		*remaining_bits = bits2save;
	}

	return E1_FRAME_LEN;
}




/***************************************************************************
 * ETI Block routines
 * return number of E1 frames to skip.
 ***************************************************************************/
int eti_block_sync_search(uint8_t *eti_frames_ptr)
{
    int frame_idx=0, block_idx=0;

    for(frame_idx=0; frame_idx<FRAMES_IN_BLOCK; frame_idx++) {
    	int last_block_num=0, last_superblock_num=0;
   		for(block_idx=0;block_idx<BLOCKS_IN_SUPERBLOCK*2;block_idx++) {
   			uint8_t mgmt_byte = eti_frames_ptr[(block_idx*FRAMES_IN_BLOCK + frame_idx)*E1_FRAME_LEN+1];
//   			uint8_t supervisor_byte = eti_frames_ptr[(block_idx*FRAMES_IN_BLOCK + frame_idx)*E1_FRAME_LEN+2];

   			uint8_t block_num = (mgmt_byte >> 5) & 0x07;
   	    	uint8_t superblock_num = (mgmt_byte >> 3) & 0x03;

   	    	if(block_idx==0) {
   	    		last_block_num=block_num;
   	    		last_superblock_num=superblock_num;
   	    	} else if(
   	    			(superblock_num == last_superblock_num && ((0x07 & (last_block_num+1)) == block_num)) ||
   	    			(		(superblock_num == (0x03 & (last_superblock_num+1)) ||
   	    					(superblock_num == 0 && last_superblock_num == 2)) && block_num==0)
   	    			) {
   	    		last_block_num=block_num;
   	    		last_superblock_num=superblock_num;
//   	    		fprintf(stderr, "blockId %d: ETI %02x %02x: block:%d, superblock:%d [GOOD!]\n", block_idx,
//   	    				mgmt_byte, supervisor_byte, superblock_num, block_num);
   	    	} else {
   	    		break;
   	    	}
   		}
   		if(block_idx==BLOCKS_IN_SUPERBLOCK*2) {
   			INFO("ETI Sync found at pos: %d", frame_idx);
   			return frame_idx;
   		}
    }

	return -1;
}

/***************************************************************************
 * ETI Multiframe routines
 * return number of blocks to skip.
 ***************************************************************************/
int eti_multiframe_sync_search(uint8_t *eti_frames_ptr)
{
    int block_idx=0, superblock_idx=0;

    for(block_idx=0; block_idx<BLOCKS_IN_SUPERBLOCK; block_idx++) {
   		for(superblock_idx=0;superblock_idx<SUPERBLOCKS_IN_MULTIFRAME;superblock_idx++) {
   			uint8_t mgmt_byte = eti_frames_ptr[(superblock_idx*BLOCKS_IN_SUPERBLOCK + block_idx)*FRAMES_IN_BLOCK*E1_FRAME_LEN+1];
//   			uint8_t supervisor_byte = eti_frames_ptr[(superblock_idx*BLOCKS_IN_SUPERBLOCK + block_idx)*FRAMES_IN_BLOCK*E1_FRAME_LEN+2];

   	    	uint8_t superblock_num = (mgmt_byte >> 3) & 0x03;
   			uint8_t block_num = (mgmt_byte >> 5) & 0x07;

//    		fprintf(stderr, "blockId[%d,%d]: ETI %02x %02x: block:%d, superblock:%d\n", superblock_idx, block_idx,
//   				mgmt_byte, supervisor_byte, superblock_num, block_num);

   	    	if((superblock_num==0) && (block_num == 0)) {
//   	    		fprintf(stderr, "blockId[%d,%d]: ETI %02x %02x: block:%d, superblock:%d [GOOD]\n", superblock_idx, block_idx,
//   	   				mgmt_byte, supervisor_byte, superblock_num, block_num);
   	   			INFO("ETI Multiframe sync found at blockId: %d", superblock_idx*BLOCKS_IN_SUPERBLOCK + block_idx);
   	   			return superblock_idx*BLOCKS_IN_SUPERBLOCK + block_idx;

   	    	}
   		}
    }

	return -1;
}


/***************************************************************************
 * ETI Multiframe deinterleaving
 * return number of errors.
 ***************************************************************************/
#define INTERLEAVE_TABLE_ROWS		8
#define INTERLEAVE_TABLE_COLS		240

int eti_superblocks_deinterleave(uint8_t *eti_superblocks_ptr, uint8_t *eti_superblocks_deint, int no_fec, void **rs_handlers)
{
	int row, col;

	uint8_t *eti_superblock = eti_superblocks_ptr;
	uint8_t *eti_deint = eti_superblocks_deint;

	//eti_superblock += E1_FRAME_LEN*FRAMES_IN_BLOCK*BLOCKS_IN_SUPERBLOCK;
	uint8_t mgmt_byte_block1 = eti_superblock[E1_FRAME_LEN*FRAMES_IN_BLOCK+1];
	uint8_t superblock_num = (mgmt_byte_block1 >> 3) & 0x03;
	uint8_t block_num = (mgmt_byte_block1 >> 5) & 0x07;
	uint8_t type_bit = (mgmt_byte_block1 >> 1) & 0x01;

#if 0
			int _ptr=0;
			while(_ptr < FRAMES_IN_MULTIFRAME) {
				uint8_t mgmt_byte = eti_superblocks_ptr[_ptr*E1_FRAME_LEN + 1];

				uint8_t superblock_num = (mgmt_byte >> 3) & 0x03;
				uint8_t block_num = (mgmt_byte >> 5) & 0x07;
				DEBUG("D block[%d]:[%d.%d]:", _ptr, superblock_num, block_num);
				_ptr+=FRAMES_IN_BLOCK;
			}
			//exit(0);
	ERROR("Deinterleave: GOOD block[1]:[%d.%d]: %02x b.6=%d", superblock_num, block_num, mgmt_byte_block1, type_bit);
#endif

	if(superblock_num!=0 || block_num != 1) {
		ERROR("Deinterleave: Bad block[1]:[%d.%d]: %02x b.6=%d", superblock_num, block_num, mgmt_byte_block1, type_bit);
		exit(1);
	}

	do {
		int eti_in_ptr=0;
		for(col=0;col<INTERLEAVE_TABLE_COLS;col++) {
			for(row=0;row<INTERLEAVE_TABLE_ROWS;row++) {
				if((eti_in_ptr % 16) == 0)
					eti_in_ptr++;
				int output_ptr = col + row * INTERLEAVE_TABLE_COLS;
				eti_deint[output_ptr] = eti_superblock[eti_in_ptr];
				//if(&eti_superblock[eti_in_ptr] == &eti_superblocks_ptr[E1_FRAME_LEN*FRAMES_IN_BLOCK+1])
				//	DEBUG("Deinterleave out_ptr=%d val=0x%02x [%dx%d]", output_ptr, eti_superblock[eti_in_ptr], row, col);
				eti_in_ptr++;
			}
		}

#ifdef HAVE_FEC
		if(!no_fec) {
			for(row=0;row<INTERLEAVE_TABLE_ROWS;row++) {
				//print_bytes((char *) &eti_deint[row*INTERLEAVE_TABLE_COLS], INTERLEAVE_TABLE_COLS);
				int corrected_bytes[235];
				int err = decode_rs_char(rs_handlers[type_bit], &eti_deint[row*INTERLEAVE_TABLE_COLS], corrected_bytes, 0);
				if(err < 0) {
					WARN("Reed-Solomon uncorrectable errors at row[%d]: %d", row, err);
					return -1;
				} else if (err && err<235) {
					WARN("Reed-Solomon corrected %d errors in row[%d]:", err, row);
					int i;
					for(i=0;i < err;i++)
						fprintf(stderr, "%d ", corrected_bytes[i]);
					fprintf(stderr, "\n");
				}
			}
		}
#endif

		eti_superblock += E1_FRAME_LEN*FRAMES_IN_BLOCK*BLOCKS_IN_SUPERBLOCK;
		eti_deint += INTERLEAVE_TABLE_COLS*INTERLEAVE_TABLE_ROWS;

	} while (eti_superblock - eti_superblocks_ptr < FRAMES_IN_MULTIFRAME*E1_FRAME_LEN);

	return INTERLEAVE_TABLE_ROWS*INTERLEAVE_TABLE_COLS*SUPERBLOCKS_IN_MULTIFRAME;
}

/***************************************************************************
 * ETI Multiframe copy
 * Copies ETI-LI data into ETI-NI frame.
 ***************************************************************************/
#define M01_BYTE		30
int eti_ni_copy(uint8_t *eti_mutiframe_deint, uint8_t *eti_ni_frame, int even)
{
	uint32_t sync_flag;
	int row;

	uint8_t mgmt_byte_block1 = eti_mutiframe_deint[M01_BYTE];
	uint8_t superblock_num = (mgmt_byte_block1 >> 3) & 0x03;
	uint8_t block_num = (mgmt_byte_block1 >> 5) & 0x07;
	uint8_t type_bit = (mgmt_byte_block1 >> 1) & 0x01;

	if(superblock_num!=0 || block_num != 1) {
		ERROR("eti_ni_copy: Bad block[1]:[%d.%d]: %02x b.6=%d", superblock_num, block_num, mgmt_byte_block1, type_bit);
		exit(1);
	}


	/* Write ETI-NI frame sync */
	if(even)
		sync_flag = ETI_NI_FSYNC0;
	else
		sync_flag = ETI_NI_FSYNC1;
	eti_ni_frame[0] = sync_flag & 0xff;
	eti_ni_frame[1] = (sync_flag >> 8) & 0xff;
	eti_ni_frame[2] = (sync_flag >> 16) & 0xff;
	eti_ni_frame[3] = (sync_flag >> 24) & 0xff;
	uint8_t *eti_ni_ptr = &eti_ni_frame[4];

	/* table payload size, depending on M1,0 b6 flag */
	int max_read;
	if(type_bit)
		max_read = 226;
	else
		max_read = 235;

	for(row=0;row <INTERLEAVE_TABLE_ROWS*SUPERBLOCKS_IN_MULTIFRAME;row++) {
		//DEBUG("Row: %d bytes:", row);
		if(row % INTERLEAVE_TABLE_ROWS < 2) {
			int read_ptr = 0;
			do {
				int to_read = 29;
				if(read_ptr + to_read > max_read)
					to_read = max_read - read_ptr-1;
				//DEBUG("row[%d] copy[%d]: %d...%d", row, to_read, read_ptr+1, read_ptr + to_read);
				read_ptr++;
				memcpy(eti_ni_ptr, &eti_mutiframe_deint[row*INTERLEAVE_TABLE_COLS + read_ptr], to_read);
				eti_ni_ptr+=to_read;
				read_ptr+=to_read;
			} while (read_ptr < max_read);
		} else {
			//DEBUG("row[%d] copy[%d]: %d...%d", row, max_read, 0, max_read);
			memcpy(eti_ni_ptr, &eti_mutiframe_deint[row*INTERLEAVE_TABLE_COLS], max_read);
			eti_ni_ptr+=max_read;
		}
	}

	memset(eti_ni_ptr, 0x55, ETI_NI_RAW_SIZE+eti_ni_frame - eti_ni_ptr);
	return ETI_NI_RAW_SIZE;
}





int main(int i_argc, char **ppsz_argv)
{   
    int c, no_fec=0;
    FILE *inputfile=stdin;
    FILE *outputfile=stdout;
    
    static const struct option long_options[] = {
        { "input",         required_argument,       NULL, 'i' },
        { "output",        required_argument,       NULL, 'o' },
        { "no-fec",        no_argument,       		NULL, 'f' },
        { 0, 0, 0, 0 }
    };
    
    while ((c = getopt_long(i_argc, ppsz_argv, "p:s:i:o:hf", long_options, NULL)) != -1)
    {
        switch (c) {
        case 'i':
            inputfile = fopen(optarg, "r");
            if(!inputfile) {
                ERROR("cant open input file!");
                exit(1);
            }
            break;

        case 'o':
            outputfile = fopen(optarg, "w");
            if(!outputfile) {
                ERROR("cant open output file!");
                exit(1);
            }
            break;

        case 'f':
        	no_fec=1;
            break;
        case 'h':
        default:
            usage(ppsz_argv[0]);
        }
    }

#ifdef HAVE_FEC
	WARN("Forwarded error correction %s", no_fec ? "disabled" : "enabled");
#else
	WARN("Forwarded error correction disabled (NOT COMPILED)");
#endif
	/* space for 2 ETI frames for bitwise seeking */
    uint8_t p_e1_search_block[E1_FRAME_LEN*FRAMES_IN_BLOCK*2];
    size_t i_ret = fread(p_e1_search_block, E1_FRAME_LEN*FRAMES_IN_BLOCK, 2, inputfile);
    if(i_ret != 2){
    	ERROR("Can't read from file");
    	exit(1);
    }

    int is_inverted = 0;
    //int e1_sync_pos = e1_sync_bitsearch(&hBitBuffer, &is_inverted);
    int e1_sync_pos = e1_sync_bitsearch(p_e1_search_block, &is_inverted);

    if(e1_sync_pos < 0) {
    	ERROR("Can't find sync");
    	exit(1);
    }


    int offset_bytes_num = e1_sync_pos >> 3;
    int offset_bits_num  = e1_sync_pos & 0x07;
    DEBUG("seek: B:%d, b:%d", offset_bytes_num, offset_bits_num);
    //print_bytes((char*)p_e1_search_block + offset_bytes_num, E1_FRAME_LEN*2);

    uint8_t *read_ptr = p_e1_search_block + offset_bytes_num + 1;

    uint8_t pre_offset_bits = p_e1_search_block[offset_bytes_num];
    //got E1 frame bytes

    //add 1 block for sync search
    uint8_t eti_multiframe[E1_FRAME_LEN*(FRAMES_IN_MULTIFRAME + FRAMES_IN_BLOCK + E1_FRAME_LEN)];
//    DEBUG("input: (pre-offset:%02x)", pre_offset_bits);
//    print_bytes((char*)read_ptr, E1_FRAME_LEN*2);
    uint8_t *eti_frames_ptr = &eti_multiframe[0];

    /* fill by already readed E1 frames (2blocks - 1frame_max). */
	int multiframe_filling=0;

	do {
    	int readed_bytes = shift_frame_mem(read_ptr, &pre_offset_bits, 8-offset_bits_num, is_inverted, &eti_frames_ptr[E1_FRAME_LEN*multiframe_filling]);
    	if(readed_bytes != E1_FRAME_LEN) {
    		ERROR("Readed too small amount of bytes: %d", readed_bytes);
    		exit(1);
    	}
    	read_ptr+=E1_FRAME_LEN;
    	multiframe_filling++;
    } while (read_ptr + E1_FRAME_LEN < p_e1_search_block + E1_FRAME_LEN*FRAMES_IN_BLOCK*2);

    int not_readed = p_e1_search_block + E1_FRAME_LEN*FRAMES_IN_BLOCK*2 - read_ptr;
    DEBUG("pre-readed output %d frames (%d bytes left):", multiframe_filling, not_readed);
    //print_bytes((char*)eti_multiframe, E1_FRAME_LEN*frame_filling);

    /* move last bytes to the beginning and read whole ETI block */
    if(not_readed)
    	memmove(p_e1_search_block, read_ptr, not_readed);

	while (multiframe_filling < FRAMES_IN_MULTIFRAME + FRAMES_IN_BLOCK) {
		int i;

		/* if not_readed > 0, that means we have some bytes in buffer, just fill buffer to align by E1_FRAME_LEN */
		if(fread(p_e1_search_block + not_readed, E1_FRAME_LEN*FRAMES_IN_BLOCK - not_readed, 1, inputfile) != 1){
			ERROR("Can't read from file");
			exit(1);
		}

		if(not_readed)
			not_readed=0;

		read_ptr = p_e1_search_block;

		// at zero cycle - read frame
		for(i=0;i<FRAMES_IN_BLOCK;i++) {
			int readed_bytes = shift_frame_mem(read_ptr, &pre_offset_bits, 8-offset_bits_num, is_inverted,
					&eti_frames_ptr[E1_FRAME_LEN*multiframe_filling]);
			if(readed_bytes != E1_FRAME_LEN) {
				ERROR("Readed too small amount of bytes: %d", readed_bytes);
				exit(1);
			}
			read_ptr+=E1_FRAME_LEN;
			multiframe_filling++;
		};
	}

	DEBUG("MULTIFRAME FILLING: %d", multiframe_filling);

	int eti_skip_frames = eti_block_sync_search(eti_frames_ptr);
	assert(eti_skip_frames>=0 && eti_skip_frames < FRAMES_IN_BLOCK);
	int eti_skip_blocks = eti_multiframe_sync_search(&eti_frames_ptr[eti_skip_frames*E1_FRAME_LEN]);
	eti_skip_frames += eti_skip_blocks*FRAMES_IN_BLOCK;
	memmove(eti_frames_ptr, &eti_frames_ptr[eti_skip_frames*E1_FRAME_LEN], 	E1_FRAME_LEN*(multiframe_filling - eti_skip_frames));
	multiframe_filling -= eti_skip_frames;
	assert(multiframe_filling >= 0 && multiframe_filling < FRAMES_IN_MULTIFRAME + E1_FRAME_LEN);


	/* Initialize a Reed-Solomon codec
	 * symsize = symbol size, bits
	 * gfpoly = Field generator polynomial coefficients
	 * fcr = first root of RS code generator polynomial, index form
	 * prim = primitive element to generate polynomial roots
	 * nroots = RS code generator polynomial degree (number of roots)
	 * pad = padding bytes at front of shortened block
	 *
	 * R = 5 resulting in an RS(235,240) code.
	 * R = 14 resulting in an RS(226,240) code.
	 */
	void *rs_handlers[2];
#ifdef HAVE_FEC
	if(!no_fec) {

		rs_handlers[0] = init_rs_char(8, 0x187, 120, 1, 5, 15);
		rs_handlers[1] = init_rs_char(8, 0x187, 120, 1, 14, 15);

		if(rs_handlers[0] == (void*)NULL || rs_handlers[1] == (void*)NULL) {
			DEBUG("Can't init Reed-Solomon decoder!");
			exit(1);
		}
	}
#endif

	uint8_t eti_mutiframe_deint[INTERLEAVE_TABLE_ROWS*INTERLEAVE_TABLE_COLS*SUPERBLOCKS_IN_MULTIFRAME];
	uint8_t eti_ni_frame[ETI_NI_RAW_SIZE];
	int even=1;
	unsigned long multiframes_readed=0;

read_again:
	while(multiframe_filling < FRAMES_IN_MULTIFRAME) {
		if(fread(p_e1_search_block, E1_FRAME_LEN, 1, inputfile) != 1){
			fprintf(stderr, "\n");
			ERROR("Can't read from file");
			break;
		}

		int readed_bytes = shift_frame_mem(p_e1_search_block, &pre_offset_bits, 8-offset_bits_num, is_inverted,
				&eti_frames_ptr[E1_FRAME_LEN*multiframe_filling]);
		if(readed_bytes != E1_FRAME_LEN) {
			ERROR("Readed too small amount of bytes: %d", readed_bytes);
			break;
		}

		multiframe_filling++;
	}

	if(multiframe_filling >= FRAMES_IN_MULTIFRAME) {

		if(!(multiframes_readed % 100))
				fprintf(stderr, ".");

		eti_superblocks_deinterleave(eti_frames_ptr, eti_mutiframe_deint, no_fec, rs_handlers);
		//WARN("Got %d deinterleaved bytes", deinterleaved);
		eti_ni_copy(eti_mutiframe_deint, eti_ni_frame, even);
		even=!even;
		//WARN("Copied %d ETI-LI bytes", copied);

		size_t o_ret = fwrite(eti_ni_frame, ETI_NI_RAW_SIZE, 1, outputfile);
	    if (o_ret != 1) {
	    	WARN("Can't write output ETI");
	    	exit(1);
	    }

	    multiframe_filling -= FRAMES_IN_MULTIFRAME;
		multiframes_readed++;
		goto read_again;
	}

	INFO("Readed %ld ETI-frames", multiframes_readed);

    fclose(inputfile);
    fclose(outputfile);

#ifdef HAVE_FEC
    if(!no_fec) {
    	free_rs_char(rs_handlers[0]);
    	free_rs_char(rs_handlers[1]);
    }
#endif

    return 0;
}

