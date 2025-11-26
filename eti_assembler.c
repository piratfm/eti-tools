/*
 * eti_assembler.c
 *
 *  Created on: 14 лип. 2017 р.
 *      Author: tipok
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <ctype.h>

#include "edi_parser.h"
#include "logging.h"
#include "buffer_unpack.h"
#include "crc.h"

int AssembleETIFrame(struct etiBuilder *h, int fmt, callback_t outCallback, void *priv_data)
{
    uint8_t eti[6144];
    int idx=0;

	int i;
    if (!h->is_eti) {
    	msg_Log("ETI: Cannot assemble ETI before protocol");
    	return 0;
    }

    if (!h->m_fc_valid) {
    	msg_Log("ETI: Cannot assemble ETI without FC");
    	return 0;
    }

    if (!h->fic_length) {
    	msg_Log("ETI: Cannot assemble ETI without FIC data");
    	return 0;
    }

    // Accept zero subchannels, because of an edge-case that can happen
    // during reconfiguration. See ETS 300 799 Clause 5.3.3

    // TODO check time validity

    // ETS 300 799 Clause 5.3.2, but we don't support not having
    // a FIC
    if (    (h->m_fc.mid == 3 && h->fic_length != 32 * 4) ||
            (h->m_fc.mid != 3 && h->fic_length != 24 * 4) ) {
        msg_Log("ETI: Invalid FIC length %u for MID %u",h->fic_length, h->m_fc.mid);
        return 0;
    }



    eti[0] = 0xff;
    uint8_t fct = h->m_fc.dflc % 250;

    // FSYNC
    if (fct % 2 == 1) {
    	eti[1] = 0xf8;
    	eti[2] = 0xc5;
    	eti[3] = 0x49;
    } else {
    	eti[1] = 0x07;
    	eti[2] = 0x3a;
    	eti[3] = 0xb6;
    }

    // LIDATA
    // FC
    eti[4] = fct;

    const uint8_t NST = h->m_fc.nst;

    eti[5] = ((uint8_t)h->m_fc.ficf) << 7 | NST;

    // We need to pack:
    //  FP 3 bits
    //  MID 2 bits
    //  FL 11 bits

    // FL: EN 300 799 5.3.6
    uint16_t FL = NST + 1 + h->fic_length/4;
    for (i=0;i<NST;i++) {
        //fprintf(stderr,"assemble: subch.stream_index=%d, subch.scid=%d subch.sad=%d len:%d FL=%d\n",subch.stream_index, subch.scid, subch.sad, subch.mst.size(), FL);
        FL += h->m_stc[i].mst_size/4;
    }

    const uint16_t fp_mid_fl = (h->m_fc.fp << 13) | (h->m_fc.mid << 11) | FL;

    eti[6] = (uint16_t) (fp_mid_fl >> 8);
    eti[7] = (fp_mid_fl & 0xFF);

//    msg_Dump(eti, 8);


    // STC
    for (i=0;i<NST;i++) {
    	eti[8 + i*4] = (h->m_stc[i].scid << 2) | (uint16_t)((h->m_stc[i].sad & 0x300) >> 8);
        eti[8 + i*4 + 1] = h->m_stc[i].sad & 0xff;
        eti[8 + i*4 + 2] = (h->m_stc[i].tpl << 2) | (uint16_t)(((h->m_stc[i].mst_size/8) & 0x300) >> 8);
        eti[8 + i*4 + 3] = (uint16_t)(h->m_stc[i].mst_size/8) & 0xff;
//        msg_Log("STC:%u",i);
//        msg_Dump(&eti[8 + i*4], 4);
    }

    idx+=8 + NST*4;

    // EOH
    // MNSC
    eti[idx] = (((uint16_t)h->m_mnsc) >> 8);
    eti[idx+1] = (h->m_mnsc & 0xFF);

    // CRC
    // Calculate CRC from eti[4] to current position
    uint16_t eti_crc = 0xFFFF;
    eti_crc = crc16(eti_crc, &eti[4], idx - 4 + 2);
    eti_crc ^= 0xffff;

//    msg_Log("CRC of:%u bytes: 0x%04x",idx - 4 + 2, eti_crc);
//    msg_Dump(&eti[4], idx - 4 + 2);


    eti[idx+2] = (uint16_t)eti_crc >> 8;
    eti[idx+3] = eti_crc & 0xFF;
    idx+=4;

    const size_t mst_start = idx;
    // MST
    // FIC data
    memcpy(eti+idx, h->fic, h->fic_length);
    idx+=h->fic_length;

    // Data stream
    for (i=0;i<NST;i++) {
    	if(idx+h->m_stc[i].mst_size >= 6144-8) {
    		msg_Log("ETI: Invalid ETI length %u",idx+h->m_stc[i].mst_size);
    		return 0;
    	}
    	memcpy(eti+idx, h->m_stc[i].mst, h->m_stc[i].mst_size);
    	idx+=h->m_stc[i].mst_size;
    }

    // EOF
    // CRC
    uint16_t mst_crc = 0xFFFF;
    mst_crc = crc16(mst_crc, &eti[mst_start], idx - mst_start);
    mst_crc ^= 0xffff;
    eti[idx] = (uint16_t)(mst_crc >> 8);
    eti[idx+1] = (mst_crc & 0xFF);

    // RFU
    eti[idx+2] = (uint16_t)(h->m_rfu >> 8);
    eti[idx+3] = (h->m_rfu & 0xFF);

    // TIST
    eti[idx+4] = (uint32_t)(h->m_fc.tsta >> 24);
    eti[idx+5] = ((uint32_t)(h->m_fc.tsta >> 16) & 0xFF);
    eti[idx+6] = ((uint32_t)(h->m_fc.tsta >> 8) & 0xFF);
    eti[idx+7] = (h->m_fc.tsta & 0xFF);
    idx+=8;

    if(idx>6144) {
    	msg_Log("ETI: Invalid ETI length %u",idx);
    	return 0;
    }

    //raise packet if needed
    if(fmt == ETI_FMT_RAW && idx<6144) {
    	memset(eti+idx, 0x55, 6144-idx);
    	idx=6144;
    }

    if(outCallback) {
    	outCallback(priv_data, eti, idx);
    }
	return 1;
}
