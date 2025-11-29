/*
 * tag_parser.c
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



bool decode_starptr(edi_handler_t *h, const uint8_t *value, uint32_t bytelength)
{
    if (bytelength != 0x40 / 8) {
    	msg_Log("EDI: Incorrect length %02x for *PTR", bytelength);
        return false;
    }
    uint32_t proto = read_32b(value);
    //fprintf(stderr, "EDI: *PTR protocol: (%08x) %c%c%c%c\n", proto, value[0],value[1],value[2],value[3]);

    uint16_t major = read_16b(value + 4);
    uint16_t minor = read_16b(value + 6);

   h->eti.is_eti = (proto == 0x44455449/*"DETI"*/ && major == 0 && minor == 0);

//    m_data_collector.update_protocol(protocol, major, minor);
    return true;
}


bool decode_deti(edi_handler_t *h, const uint8_t *value, uint32_t bytelength)
{

    /*
    uint16_t detiHeader = fct | (fcth << 8) | (rfudf << 13) | (ficf << 14) | (atstf << 15);
    packet.push_back(detiHeader >> 8);
    packet.push_back(detiHeader & 0xFF);
    */

    uint16_t detiHeader = read_16b(value);

    h->eti.m_fc.atstf = (detiHeader >> 15) & 0x1;
    h->eti.m_fc.ficf = (detiHeader >> 14) & 0x1;
    bool rfudf = (detiHeader >> 13) & 0x1;
    uint8_t fcth = (detiHeader >> 8) & 0x1F;
    uint8_t fct = detiHeader & 0xFF;

    h->eti.m_fc.dflc = fcth * 250 + fct; // modulo 5000 counter

    uint32_t etiHeader = read_32b(value + 2);

    h->eti.m_err = (etiHeader >> 24) & 0xFF;

    h->eti.m_fc.mid = (etiHeader >> 22) & 0x03;
    h->eti.m_fc.fp = (etiHeader >> 19) & 0x07;
    uint8_t rfa = (etiHeader >> 17) & 0x3;
    if (rfa != 0) {
    	msg_Log("EDI: EDI deti TAG: rfa non-zero");
    }

    bool rfu = (etiHeader >> 16) & 0x1;
    h->eti.m_mnsc = rfu ? 0xFFFF : etiHeader & 0xFFFF;

    const uint32_t fic_length_words = (h->eti.m_fc.ficf ? (h->eti.m_fc.mid == 3 ? 32 : 24) : 0);
    const uint32_t fic_length = 4 * fic_length_words;

    const uint32_t expected_length = 2 + 4 +
        (h->eti.m_fc.atstf ? 1 + 4 + 3 : 0) +
        fic_length +
        (rfudf ? 3 : 0);

    if (bytelength != expected_length) {
    	msg_Log("EDI deti: Assertion error:"
                "value.size() != expected_length: %u != %u", bytelength, expected_length);
    	return false;
    }

    size_t i = 2 + 4;

    h->eti.m_time_valid = false;
    h->eti.m_utco = 0;
    h->eti.m_seconds = 0;
    if (h->eti.m_fc.atstf) {
        uint8_t utco = value[i];
        i++;

        uint32_t seconds = read_32b(value + i);
        i += 4;

        h->eti.m_utco = utco;
        h->eti.m_seconds = seconds;

        // TODO check validity
        h->eti.m_time_valid = true;


        h->eti.m_fc.tsta = read_24b(value + i);
        i += 3;
    }
    else {
        // Null timestamp, ETSI ETS 300 799, C.2.2
    	h->eti.m_fc.tsta = 0xFFFFFF;
    }


    if (h->eti.m_fc.ficf) {
    	h->eti.fic_length = fic_length;
    	memcpy(h->eti.fic, value+i, fic_length);
        i += fic_length;
    }

    h->eti.m_rfu = 0xffff;
    if (rfudf) {
        uint32_t rfud = read_24b(value + i);

        // high 16 bits: RFU in LIDATA EOH
        // low 8 bits: RFU in TIST (not supported)
        h->eti.m_rfu = rfud >> 8;
        if ((rfud & 0xFF) != 0xFF) {
        	msg_Log("EDI: RFU in TIST not supported");
        }

        i += 3;
    }

    h->eti.m_fc_valid = false;

    if (!h->eti.m_fc.ficf) {
    	msg_Log("FIC must be present");
    }

    if (h->eti.m_fc.mid > 4) {
    	msg_Log("Invalid MID");
    }

    if (h->eti.m_fc.fp > 7) {
    	msg_Log("Invalid FP");
    }

    h->eti.m_fc_valid = true;

	return true;
}


bool decode_estn(edi_handler_t *h, const uint8_t *value, uint32_t bytelength, uint8_t n)
{
    uint32_t sstc = read_24b(value);

    if(n>64 || n==0) {
    	msg_Log("EDI: number of streams is invalid:%u", n);
    	return false;
    }

    struct eti_stc_data *stc = &h->eti.m_stc[n-1];

    uint32_t old_size = stc->mst_size;

    stc->stream_index = n - 1; // n is 1-indexed
    stc->scid = (sstc >> 18) & 0x3F;
    stc->sad = (sstc >> 8) & 0x3FF;
    stc->tpl = (sstc >> 2) & 0x3F;
    uint8_t rfa = sstc & 0x3;
    if (rfa != 0) {
    	msg_Log("EDI: rfa field in ESTn tag non-null");
    }

    if(bytelength < 3) {
    	msg_Log("EDI: est%d bad length", n);
    }

    stc->mst_size = bytelength - 3;
    if(verbosity > 1)
    	msg_Log("EDI: est%d length:%u", n, stc->mst_size);
    if(!stc->mst)
    	stc->mst=malloc(stc->mst_size);
    else if(stc->mst_size != old_size)
    	stc->mst=realloc(stc->mst, stc->mst_size);

    memcpy(stc->mst, value+3, stc->mst_size);
    h->eti.m_fc.nst++;


	return true;
}

bool decode_stardmy(edi_handler_t *h, uint8_t *tag_value, uint32_t taglength)
{
	return true;
}

/*****************************************************************************
 * AF decode_tagpacket
 *****************************************************************************/
int decode_tagpacket(edi_handler_t *h, uint8_t *tag_pkt, uint32_t tagsize)
{
    size_t i, length = 0;
    bool success = true;
    h->eti.m_fc.nst=0;
    for (i = 0; i + 8 < tagsize; i += 8 + length) {
        uint32_t tagId = read_32b(tag_pkt + i);
        uint32_t taglength = read_32b(tag_pkt + i + 4);
        if(verbosity > 2)
        	msg_Log("EDI: tag %08x (%c%c%c%c) len:%u",
        			tagId, tag_pkt[i], tag_pkt[i+1], tag_pkt[i+2],
					isprint(tag_pkt[i+3]) ? tag_pkt[i+3] : (isprint(tag_pkt[i+3]+0x30) ? (tag_pkt[i+3]+0x30) : '.'),
					taglength);


        if (taglength % 8 != 0) {
        	msg_Log("EDI: Invalid tag length!");
            break;
        }
        taglength /= 8;

        length = taglength;

        uint8_t *tag_value = tag_pkt + i+8;

        bool tagsuccess = false;

        if (tagId == 0x2a707472 /*"*ptr"*/) {
            tagsuccess = decode_starptr(h, tag_value, taglength);
        }
        else if (tagId == 0x64657469 /*"deti"*/) {
            tagsuccess = decode_deti(h, tag_value, taglength);
        }
        else if ((tagId & 0xFFFFFF00) == 0x65737400/*"est"*/) {
            uint8_t n = tagId & 0x000000FF;
            tagsuccess = decode_estn(h, tag_value, taglength, n);
        }
        else if (tagId == 0x2a646d79 /*"*dmy"*/) {
            tagsuccess = decode_stardmy(h, tag_value, taglength);
        }
        else if (tagId == 0x46707474 /*"Fptt"*/) {
            // at least register this tag, but do nothing, prevents ""[date and time] EDI: Unknown TAG Fptt" message
            tagsuccess = true;
        }
        else if (tagId == 0x46736964 /*"Fsid"*/) {
            // at least register this tag, but do nothing, prevents ""[date and time] EDI: Unknown TAG Fsid" message
            tagsuccess = true;
        }
        else if (tagId == 0x46737374 /*"Fsst"*/) {
            // at least register this tag, but do nothing, prevents ""[date and time] EDI: Unknown TAG Fsst" message
            tagsuccess = true;
        }
        else if (tagId == 0x61676d74 /*"avtm"*/) {
            // at least register this tag, but do nothing, prevents ""[date and time] EDI: Unknown TAG avtm" message
            tagsuccess = true;
        }
        else {
        	msg_Log("EDI: Unknown TAG %c%c%c%c", tag_pkt[i], tag_pkt[i+1], tag_pkt[i+2], tag_pkt[i+3]);
            break;
        }

        if (!tagsuccess) {
        	msg_Log("EDI: Error decoding TAG %c%c%c%c",tag_pkt[i], tag_pkt[i+1], tag_pkt[i+2], tag_pkt[i+3]);
            success = tagsuccess;
            break;
        }
    }

    return success;
}
