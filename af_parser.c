/*
 * af_parser.c
 *
 *  Created on: 27 янв. 2017
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

#include <fec.h>

#include "edi_parser.h"
#include "crc.h"
#include "logging.h"

#include "buffer_unpack.h"


edi_handler_t *initEDIHandle(int fmt, callback_t outCallback, void *priv_data)
{
	edi_handler_t *h = calloc(1, sizeof(edi_handler_t));
	h->eti_format = fmt;

	h->afb.m_rs_handler = init_rs_char(8, 0x11d, 1, 1, 255 - 207, ((1 << 8) - 1) - 255);
	h->write_cb = outCallback;
	h->write_cb_priv = priv_data;
	return h;
}

void closeEDIHandle(edi_handler_t *h)
{
	int i;
	msg_Log("EDI: stats: packets lost: %u/%u errors:%u/%u",
			h->afb.PktLost, h->afb.PktCount, h->afb.errorsCorrected, h->afb.PktCount*h->afb.Plen);
	if(h) {
		free_rs_char(h->afb.m_rs_handler);

		for(i=0;i<64;i++) {
			if(h->eti.m_stc[i].mst) {
				free(h->eti.m_stc[i].mst);
				h->eti.m_stc[i].mst = NULL;
				h->eti.m_stc[i].mst_size = 0;
			}
		}

		if(h->pf._payload)
			free(h->pf._payload);

		free(h);
	}
}




/*****************************************************************************
 * AF HandleAFPacket
 *****************************************************************************/
static int HandleAFPacket(edi_handler_t *h, uint8_t *edi_pkt, uint32_t pktsize)
{
	if(pktsize < AFPACKET_HEADER_LEN || edi_pkt[0] != 'A' || edi_pkt[1] != 'F') {
		msg_Log("EDI-AF: bad tag!");
		return -1;
	}

    // read length from packet
    uint32_t taglength = read_32b(edi_pkt + 2);
    uint16_t seq = read_16b(edi_pkt + 6);
    if (h->af.m_last_seq + 1 != seq) {
    	msg_Log("EDI-AF: Packet sequence error");
    }
    h->af.m_last_seq = seq;

    bool has_crc = (edi_pkt[8] & 0x80) ? true : false;
    uint8_t major_revision = (edi_pkt[8] & 0x70) >> 4;
    uint8_t minor_revision = edi_pkt[8] & 0x0F;
    if (major_revision != 1 || minor_revision != 0) {
    	msg_Log("EDI-AF: Packet has wrong revision %u.%u", major_revision, minor_revision);
    	return -1;
    }
    uint8_t pt = edi_pkt[9];
    if (pt != 'T') {
        // only support Tag
    	return 0;
    }

    const size_t crclength = 2;
    if (pktsize < AFPACKET_HEADER_LEN + taglength + crclength) {
    	msg_Log("EDI-AF: packet too small: %u < %u", pktsize, AFPACKET_HEADER_LEN + taglength + crclength);
    	return 0;
    }

    if (!has_crc) {
    	msg_Log("EDI-AF: packet not supported, has no CRC");
        return -1;
    }
#if 0
    uint16_t crc = 0xffff;
    for (i = 0; i < AFPACKET_HEADER_LEN + taglength; i++) {
        crc = crc16(crc, &edi_pkt[i], 1);
    }
    crc ^= 0xffff;

    uint16_t packet_crc = read_16b(edi_pkt + AFPACKET_HEADER_LEN + taglength);

#endif
	//msg_Dump(edi_pkt , pktsize);

    //bool ok = checkCRC(edi_pkt, AFPACKET_HEADER_LEN + taglength + crclength);
	bool ok = checkCRC(edi_pkt, AFPACKET_HEADER_LEN + taglength + crclength);

    if (!ok) {
    	msg_Log("EDI-AF: Packet crc wrong pos:%u max:%u", AFPACKET_HEADER_LEN + taglength, pktsize);
    	return -1;
    }


   	//msg_Dump(edi_pkt + AFPACKET_HEADER_LEN, taglength);
    return decode_tagpacket(h, edi_pkt + AFPACKET_HEADER_LEN, taglength);
    //utilized bytes: AFPACKET_HEADER_LEN + taglength + 2;
}


/*****************************************************************************
 * HandleEDIPacket
 *****************************************************************************/
int HandleEDIPacket(edi_handler_t *h, uint8_t *edi_pkt, size_t pktsize)
{
	int ret = 0;

	//msg_Dump((char *)edi_pkt, pktsize);

	if(pktsize < 2) {
		msg_Log("EDI: packet too small!");
		goto edi_error;
	}

	if(edi_pkt[0]=='P' && edi_pkt[1]=='F') {

		int used_bytes = 0;
		do {
			used_bytes = HandlePFPacket(h, edi_pkt+used_bytes, pktsize - used_bytes);
			if(used_bytes > 0 && h->pf._valid) {
				//returns bytes if we have AFpacked decoded.
				int afBytes = pushPFTFrag(&h->pf, &h->afb);
				if(afBytes > 0) {
					ret = HandleAFPacket(h, h->afb.afPacket, afBytes);
					if(ret > 0)
						AssembleETIFrame(&h->eti, h->eti_format, h->write_cb, h->write_cb_priv);
				}
			}
		} while (used_bytes > 0 && pktsize > used_bytes);
	} else if(edi_pkt[0]=='A' && edi_pkt[1]=='F') {
		ret = HandleAFPacket(h, edi_pkt, pktsize);
		if(ret > 0)
			AssembleETIFrame(&h->eti, h->eti_format, h->write_cb, h->write_cb_priv);
	} else {
		msg_Log("EDI: packet is unknown: %c%c", isprint(edi_pkt[0]) ? edi_pkt[0] : '.', isprint(edi_pkt[1]) ? edi_pkt[1] : '.');
	}

	return 0;
edi_error:
	return -1;
}
