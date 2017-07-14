/*
 * ts_parser.h
 *
 *  Created on: 27 янв. 2011
 *      Author: tipok
 */

#ifndef EDI_PARSER_H_
#define EDI_PARSER_H_

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

// Best if MULTIPLE OF 2!!!
#define NUM_AFBUILDERS_TO_KEEP 8

#define AFPACKET_HEADER_LEN 10
#define PFPACKET_HEADER_LEN 14

extern int verbosity;


enum {
	ETI_FMT_FRAMED = 0,
	ETI_FMT_STREAMED,
	ETI_FMT_RAW,
	ETI_FMT_ZMQ,
};





#define PACKED __attribute__ ((packed))

struct pfPkt {
	//header
	uint16_t sync;
	uint16_t _Pseq;
	uint32_t _Findex:24;
	uint32_t _Fcount:24;
	uint16_t  _FEC:1;
	uint16_t  _Addr:1;
	uint16_t  _Plen:14;
	//optional headers
	uint8_t _RSk;
	uint8_t _RSz;
	uint16_t _Source;
	uint16_t _Dest;

	bool _valid;

	uint8_t *_payload;
	uint16_t _MaxLen;
} PACKED;

struct afPkt {
	uint16_t m_last_seq;
};

struct afSingleCollector {
	uint16_t Pseq_this;

	uint32_t bytesCollected;
	uint32_t fragmentsCollected;
	uint8_t *packetReceived;
	uint8_t *pfPackets;

	//PF packets are successfully processed!
	bool packetsIsProcessed;
};

struct afBuilders {

	uint16_t Pseq_newest;
	uint16_t Pseq_oldest;

	bool     isInitial;
	uint32_t PktLost;
	uint32_t errorsCorrected;
	uint32_t PktCount;

	//this variables are shared between
	uint32_t Fcount;
	uint16_t Plen;
    bool	 FEC;
    bool 	 Addr;
    uint8_t  RSk;
    uint8_t  RSz;
    uint16_t Source;
    uint16_t Dest;


	uint32_t _rxmin;
	uint32_t _cmax;

	struct afSingleCollector pfCollectors[NUM_AFBUILDERS_TO_KEEP];

	//this vars are for single AF that has been collected and successfully processed.
	uint8_t *rs_block;
	uint8_t *afPacket;
	void* m_rs_handler;
};

struct eti_SYNC {
    uint32_t ERR:8;
    uint32_t FSYNC:24;
} PACKED;

struct eti_FC {
    uint32_t FCT:8;
    uint32_t NST:7;
    uint32_t FICF:1;
    uint32_t FL_high:3;
    uint32_t MID:2;
    uint32_t FP:3;
    uint32_t FL_low:8;
} PACKED;

struct eti_STC {
    uint32_t startAddress_high:2;
    uint32_t SCID:6;
    uint32_t startAddress_low:8;
    uint32_t STL_high:2;
    uint32_t TPL:6;
    uint32_t STL_low:8;
} PACKED;

struct eti_EOH {
    uint16_t MNSC;
    uint16_t CRC;
} PACKED;

struct eti_EOF {
    uint16_t CRC;
    uint16_t RFU;
} PACKED;

struct eti_TIST {
    uint32_t TIST;
} PACKED;

struct eti_MNSC_TIME_0 {
    uint32_t type:4;
    uint32_t identifier:4;
    uint32_t rfa:8;
} PACKED;

struct eti_MNSC_TIME_1 {
    uint32_t second_unit:4;
    uint32_t second_tens:3;
    uint32_t accuracy:1;

    uint32_t minute_unit:4;
    uint32_t minute_tens:3;
    uint32_t sync_to_frame:1;
} PACKED;

struct eti_MNSC_TIME_2 {
    uint32_t hour_unit:4;
    uint32_t hour_tens:4;

    uint32_t day_unit:4;
    uint32_t day_tens:4;
} PACKED;

struct eti_MNSC_TIME_3 {
    uint32_t month_unit:4;
    uint32_t month_tens:4;

    uint32_t year_unit:4;
    uint32_t year_tens:4;
} PACKED;

struct eti_extension_TIME {
    uint32_t TIME_SECONDS;
} PACKED;



struct eti_fc_data {
    bool atstf;
    uint32_t tsta;
    bool ficf;
    uint8_t nst;
    uint16_t dflc;
    uint8_t mid;
    uint8_t fp;

//    uint8_t fct(void) const { return dflc % 250; }
};

// Information for a subchannel available in EDI
struct eti_stc_data {
    uint8_t stream_index;
    uint8_t scid;
    uint16_t sad;
    uint8_t tpl;

    uint8_t *mst;
    uint16_t mst_size;

    // Return the length of the MST in multiples of 64 bits
//    uint16_t stl(void) const { return mst.size() / 8; }
};





struct etiBuilder {
	bool is_eti;

	uint8_t m_err;
	uint16_t m_mnsc;

    bool m_time_valid;
    uint32_t m_utco;
    uint32_t m_seconds;

    uint8_t fic_length;
	uint8_t fic[128];
	uint16_t m_rfu;


	struct eti_stc_data m_stc[64];
	uint8_t NST;

	bool m_fc_valid;
	struct eti_fc_data m_fc;
};



typedef void (* callback_t)(void *privData, void *etiData, int etiLen);

typedef struct {
	int eti_format;
	uint16_t m_last_seq;

	struct pfPkt pf;
	struct afPkt af;

	struct afBuilders afb;

	struct etiBuilder eti;

	callback_t write_cb;
	void *write_cb_priv;
} edi_handler_t;


edi_handler_t *initEDIHandle(int fmt, callback_t outCallback, void *priv_data);
int HandleEDIPacket(edi_handler_t *h, uint8_t *edi_pkt, size_t pktsize);
int HandlePFPacket(edi_handler_t *h, uint8_t *edi_pkt, size_t pktsize);
int pushPFTFrag(struct pfPkt *pf, struct afBuilders *afb);
void closeEDIHandle(edi_handler_t *h);

int decode_tagpacket(edi_handler_t *h, uint8_t *tag_pkt, uint32_t tagsize);
int AssembleETIFrame(struct etiBuilder *h, int fmt, callback_t outCallback, void *priv_data);

#endif /* EDI_PARSER_H_ */

