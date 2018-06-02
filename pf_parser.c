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

#ifdef HAVE_FEC
#include <fec.h>
#endif

#include "edi_parser.h"
#include "crc.h"
#include "logging.h"


#include "buffer_unpack.h"


/*****************************************************************************
 * PF HandlePFPacket
 *****************************************************************************/
int HandlePFPacket(edi_handler_t *h, uint8_t *edi_pkt, size_t pktsize)
{
    if(pktsize < PFPACKET_HEADER_LEN)
        return -1;
    if(edi_pkt[0] != 'P' || edi_pkt[1] != 'F') {
        msg_Log("EDI-PF: Invalid PFT SYNC bytes '%c%c' len=%d", isprint(edi_pkt[0]) ? edi_pkt[0] : '.', isprint(edi_pkt[1]) ? edi_pkt[1] : '.', pktsize);
        return -1;
    }

    size_t index = 0;

    // Parse PFT Fragment Header (ETSI TS 102 821 V1.4.1 ch7.1)
    index += 2; // Psync

    h->pf._Pseq = read_16b(edi_pkt+index); index += 2;
    h->pf._Findex = read_24b(edi_pkt+index); index += 3;
    h->pf._Fcount = read_24b(edi_pkt+index); index += 3;
    h->pf._FEC = unpack1bit(edi_pkt[index], 0);
    h->pf._Addr = unpack1bit(edi_pkt[index], 1);
    h->pf._Plen = read_16b(edi_pkt+index) & 0x3FFF; index += 2;

    const size_t required_len = PFPACKET_HEADER_LEN + (h->pf._FEC ? 2 : 0) + (h->pf._Addr ? 4 : 0);
    if (pktsize < required_len) {
        return 0;
    }

    // Optional RS Header
    h->pf._RSk = 0;
    h->pf._RSz = 0;
    if (h->pf._FEC) {
    	h->pf._RSk = edi_pkt[index]; index += 1;
    	h->pf._RSz = edi_pkt[index]; index += 1;
    }

    // Optional transport header
    h->pf._Source = 0;
    h->pf._Dest = 0;
    if (h->pf._Addr) {
    	h->pf._Source = read_16b(edi_pkt+index); index += 2;
    	h->pf._Dest = read_16b(edi_pkt+index); index += 2;
    }

    index += 2;
    const bool crc_valid = checkCRC(edi_pkt, index);
    const bool buf_has_enough_data = (pktsize >= index + h->pf._Plen);

    if (!buf_has_enough_data) {
        return 0;
    }

    h->pf._valid = ((!h->pf._FEC) || crc_valid) && buf_has_enough_data;

    if (h->pf._valid) {
    	if(h->pf._MaxLen < h->pf._Plen) {
    		if(!h->pf._payload)
    			h->pf._payload = malloc(h->pf._Plen);
    		else
    			h->pf._payload = realloc(h->pf._payload, h->pf._Plen);
    		h->pf._MaxLen = h->pf._Plen;
    	}
    	memcpy(h->pf._payload, edi_pkt+index, h->pf._Plen);
        index += h->pf._Plen;
    }

    if (verbosity > 4)
    	msg_Log("EDI-PF: _Pseq:%u, _Findex:%u, _Fcount:%u, _FEC:%u _Addr:%u, _Plen:%u, _RSk:%u, _RSz:%u, _Source:%u, _Dest:%u, _valid:%u, len:%lu",
			h->pf._Pseq, h->pf._Findex, h->pf._Fcount, h->pf._FEC, h->pf._Addr, h->pf._Plen, h->pf._RSk, h->pf._RSz, h->pf._Source, h->pf._Dest, h->pf._valid, index);

	return index;
}


static bool decodePFTFrags(struct afBuilders *afb, uint8_t _pseqIdx, uint32_t *errors)
{
	int erasures[afb->_cmax][256];
    struct afSingleCollector *_afSingle = &afb->pfCollectors[_pseqIdx];


	//if no FEC, then we already have our AF inside...
	if(afb->FEC) {
		size_t j,k,i,l;

		for(i=0;i<afb->_cmax;i++)
		{
			for(j=0;j<256;j++)
				erasures[i][j] = -1;
		}

		for (j = 0; j < afb->Fcount; j++) {
			if (_afSingle->packetReceived[j]) {
				k = 0;
				for (; k < afb->Plen; k++) {
					afb->rs_block[k * afb->Fcount + j] = _afSingle->pfPackets[j*afb->Plen + k];
				}
				//never happens....
				for (; k < afb->Plen; k++) {
					afb->rs_block[k * afb->Fcount + j] = 0x00;
				}
			} else {
                // fill with zeros if fragment is missing
                for (k = 0; k < afb->Plen; k++) {
                	afb->rs_block[k * afb->Fcount + j] = 0x00;

                    const size_t chunk_ix = (k * afb->Fcount + j) / (afb->RSk + 48);
                    const size_t chunk_offset = (k * afb->Fcount + j) % (afb->RSk + 48);
                    for(l=0;l<256;l++) {
                    	if(erasures[chunk_ix][l] == -1) {
                    		erasures[chunk_ix][l] = chunk_offset;
                    		break;
                    	}
                    }
                }
			}
		}


		for (i = 0; i < afb->_cmax; i++) {
			// We need to pad the chunk ourself
			//uint8_t *chunk = calloc(1, 255);
			uint8_t chunk[255];
			const uint8_t *block_begin = afb->rs_block + (afb->RSk + 48) * i;
			memcpy(chunk, block_begin, afb->RSk);
			for(j=afb->RSk; j < 207; j++) chunk[j] = 0;
			// bytes between RSk and 207 are 0x00 already
			memcpy(chunk + 207, block_begin+afb->RSk, 48);

			int erasures_cnt;
            for(erasures_cnt=0;erasures_cnt<256;erasures_cnt++)
            	if(erasures[i][erasures_cnt] == -1) break;

            *errors+=erasures_cnt;
#if 0
			if(erasures_cnt) {
				fprintf(stderr, "erasures[%d]:", erasures_cnt);
				for(k=0;k<erasures_cnt;k++)
					fprintf(stderr, " %d", erasures[i][k]);
				fprintf(stderr, "\n");
			}
#endif

#ifdef HAVE_FEC
			//int errors_corrected = fec_decode(chunk, erasures[i], cnt);
			int num_err = decode_rs_char(afb->m_rs_handler, chunk, erasures[i], erasures_cnt);
			if (num_err == -1) {
				msg_Log("Too many errors in FEC %d", erasures_cnt);
				return false;
			}
#endif

			//if(num_err) {
			//	fprintf(stderr, "corrected %d errors of %d\n", num_err, erasures_cnt);
			//}
			memcpy(afb->afPacket + i*afb->RSk, chunk, afb->RSk);
			//free(chunk);
		}

		//TODO: increase afb->bytesCollected
		_afSingle->bytesCollected = (afb->_cmax)*afb->RSk - afb->RSz;

	} else {
		//afb->afPacket = _afSingle->pfPackets;
		memcpy(afb->afPacket, _afSingle->pfPackets, afb->Fcount*afb->Plen);
	}

    // EDI specific, must have a CRC.
    if( _afSingle->bytesCollected >= 12 ) {

       //msg_Dump(afb->afPacket, _afSingle->bytesCollected);
        if(!(afb->afPacket[8] & 0x80))
            return true; //no CRC

        bool ok = checkCRC(afb->afPacket, _afSingle->bytesCollected);
        if (!ok) {
        	msg_Log("EDI-PF: CRC error to reconstruct AF");
        } else {
        	if (verbosity > 3)
        		msg_Log("EDI-PF: CRC OK!, corrected: %u/%d!!!!!!!!!!!", *errors, _afSingle->bytesCollected);

        	return true;
        }
    }

	return false;
}

static const bool checkConsistency(struct pfPkt *pf, struct afBuilders *afb)
{
    /* Consistency check, TS 102 821 Clause 7.3.2.
     *
     * Every PFT Fragment produced from a single AF or RS Packet shall have
     * the same values in all of the PFT Header fields except for the Findex,
     * Plen and HCRC fields.
     */

    return  (pf->_Fcount == afb->Fcount) &&
    		(pf->_FEC == afb->FEC) &&
			(pf->_RSk == afb->RSk) &&
			(pf->_RSz == afb->RSz) &&
			(pf->_Addr == afb->Addr) &&
			(pf->_Source == afb->Source) &&
			(pf->_Dest == afb->Dest) &&

        /* The Plen field of all fragments shall be the s for the initial f-1
         * fragments and s - (L%f) for the final fragment.
         * Note that when Reed Solomon has been used, all fragments will be of
         * length s.
         */
        (pf->_FEC ? afb->Plen == pf->_Plen : true);
}

/**********************************************************
 * Returns AF bytes that received or zero.
 **********************************************************/
int pushPFTFrag(struct pfPkt *pf, struct afBuilders *afb)
{
	int i, ret = 0;
    uint8_t _pseqIdx = pf->_Pseq % NUM_AFBUILDERS_TO_KEEP;
    struct afSingleCollector *_afSingle = &afb->pfCollectors[_pseqIdx];


    if (!checkConsistency(pf, afb)) {

    	//last packet can be smaller if no FEC, ignore it.
    	if(!pf->_FEC && pf->_Fcount > 1 && pf->_Findex+1 == pf->_Fcount)
    		return 0;

    	msg_Log("Initialise next pseq to %u", pf->_Pseq);

		afb->Fcount = pf->_Fcount;
		afb->Plen = pf->_Plen;
		afb->FEC = pf->_FEC;
		afb->Addr = pf->_Addr;
		afb->RSk = pf->_RSk;
		afb->RSz = pf->_RSz;
		afb->Source = pf->_Source;
		afb->Dest = pf->_Dest;
		afb->isInitial = true;

    	afb->Pseq_oldest = afb->Pseq_newest = pf->_Pseq;

		//if(afb->pfPackets)
		//	free(afb->pfPackets);

    	for(i=0;i<NUM_AFBUILDERS_TO_KEEP;i++){
    		if(!afb->pfCollectors[i].packetReceived)
    			afb->pfCollectors[i].packetReceived = malloc(afb->Fcount);
    		else {
    			afb->pfCollectors[i].packetReceived = realloc(afb->pfCollectors[i].packetReceived, afb->Fcount);
    		}
			memset(afb->pfCollectors[i].packetReceived, 0x00, afb->Fcount);

        	if(!afb->pfCollectors[i].pfPackets)
        		afb->pfCollectors[i].pfPackets = malloc(afb->Fcount * afb->Plen);
        	else
        		afb->pfCollectors[i].pfPackets = realloc(afb->pfCollectors[i].pfPackets, afb->Fcount * afb->Plen);
    		afb->pfCollectors[i].packetsIsProcessed=0;
    		afb->pfCollectors[i].Pseq_this=i+1; //put fake pseq's that will be rewritten later...
    	}

    	//if FEC was initialized, clean it up.
		if(afb->rs_block) {
			free(afb->rs_block);
			afb->rs_block=NULL;
		}

		if(!afb->afPacket)
			afb->afPacket = malloc(afb->Fcount * afb->Plen);
		else
			afb->afPacket = realloc(afb->afPacket, afb->Fcount * afb->Plen);

		if(afb->FEC) {
			afb->rs_block = malloc(afb->Fcount * afb->Plen);


			/* max number of RS chunks that may have been sent */
			afb->_cmax = (afb->Fcount*afb->Plen) / (afb->RSk+48);

			/* Receiving _rxmin fragments does not guarantee that decoding
			 * will succeed! */
			afb->_rxmin = afb->Fcount - (afb->_cmax*48)/afb->Plen;
#ifndef HAVE_FEC
			msg_Log("FEC is disabled in this application. Lost packets will not be recovered!");
#endif
		} else {
			afb->_rxmin = afb->Fcount;
			afb->rs_block=NULL;
		}
		_afSingle->Pseq_this = pf->_Pseq;
    }

    if(pf->_Pseq != _afSingle->Pseq_this) {

		if(!_afSingle->packetsIsProcessed) {
			if(_afSingle->fragmentsCollected >= afb->_rxmin &&
				(!afb->FEC || _afSingle->fragmentsCollected > afb->_rxmin || !_afSingle->packetReceived[afb->Fcount-1])) {
					// Calculated the minimum number of fragments necessary to apply FEC.
					// This can't be done with the last fragment that may have a
					// smaller size
					// ETSI TS 102 821 V1.4.1 ch 7.4.4
					if (verbosity > 2)
						msg_Log("pseq %u - can be processed!: rxFr:%u, rxmin:%u", _afSingle->Pseq_this, _afSingle->fragmentsCollected, afb->_rxmin);
					_afSingle->packetsIsProcessed=decodePFTFrags(afb, _pseqIdx, &afb->errorsCorrected);
					if(_afSingle->packetsIsProcessed) {
						ret=_afSingle->bytesCollected;
					} else {
						msg_Log("EDI-PF: undecodeable pseq: %u with fragments: %u/%u",
			    				_afSingle->Pseq_this, _afSingle->fragmentsCollected, afb->_rxmin);

					}
					afb->PktLost += afb->Fcount - _afSingle->fragmentsCollected;
					afb->isInitial=false;
			} else {
				if(!afb->isInitial) {
					msg_Log("EDI-PF: too far from behind/not enought packets collected pseq: %u with fragments: %u/%u",
	    				_afSingle->Pseq_this, _afSingle->fragmentsCollected, afb->_rxmin);
					afb->PktLost += afb->Fcount;
				}
			}
		} else {
			if(verbosity > 1)
				msg_Log("EDI-PF: this pseq: %u - is already processed.", _afSingle->Pseq_this);
		}

		//TODO: maybe try to decode at least what collected here?

		_afSingle->Pseq_this = pf->_Pseq;
		memset(_afSingle->packetReceived, 0x00, afb->Fcount);
		_afSingle->packetsIsProcessed=false;
		_afSingle->fragmentsCollected=0;
		_afSingle->bytesCollected=0;
	}


	if(!_afSingle->packetsIsProcessed) {
		//copy new pf packet to it's place in buffer.
		if(!_afSingle->packetReceived[pf->_Findex]) {
			//fix: last can be smaller
			memcpy(_afSingle->pfPackets + pf->_Findex*afb->Plen, pf->_payload, pf->_Plen);
			_afSingle->packetReceived[pf->_Findex]=1;
			_afSingle->bytesCollected += pf->_Plen;
			_afSingle->fragmentsCollected++;
		}
	}

	afb->PktCount++;

	return ret;
}
