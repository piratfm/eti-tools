/*
 * fedi2eti.c
 * Uses parts of astra-sm and edi2eti.c
 *
 * Created on: 01.06.2018
 *     Author: athoik
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/*
 * edi-tools edi2eti.c
 */
#include <arpa/inet.h>
#include "edi_parser.h"
#include "network.h"
#include "logging.h"

static
void write_file(void *privData, void *etiData, int etiLen)
{
    FILE *fh = (FILE *) privData;
    fwrite(etiData, 1, etiLen, fh);
}

/*
 * mpegts/tscore.h
 */

#define TS_PACKET_SIZE 188
#define TS_HEADER_SIZE 4
#define TS_BODY_SIZE (TS_PACKET_SIZE - TS_HEADER_SIZE)

#define TS_IS_SYNC(_ts) ((_ts[0] == 0x47))
#define TS_IS_PAYLOAD(_ts) ((_ts[3] & 0x10))
#define TS_IS_PAYLOAD_START(_ts) ((TS_IS_PAYLOAD(_ts) && (_ts[1] & 0x40)))
#define TS_IS_AF(_ts) ((_ts[3] & 0x20))

#define TS_GET_PID(_ts) ((uint16_t)(((_ts[1] & 0x1F) << 8) | _ts[2]))

#define TS_GET_CC(_ts) (_ts[3] & 0x0F)

#define TS_GET_PAYLOAD(_ts) ( \
    (!TS_IS_PAYLOAD(_ts)) ? (NULL) : ( \
        (!TS_IS_AF(_ts)) ? (&_ts[TS_HEADER_SIZE]) : ( \
            (_ts[4] >= TS_BODY_SIZE - 1) ? (NULL) : (&_ts[TS_HEADER_SIZE + 1 + _ts[4]])) \
        ) \
    )

/*
 * mpegts/psi.h
 */

#define PSI_MAX_SIZE 0x00000FFF
#define PSI_HEADER_SIZE 3
#define PSI_BUFFER_GET_SIZE(_b) \
    (PSI_HEADER_SIZE + (((_b[1] & 0x0f) << 8) | _b[2]))

typedef struct
{
    uint8_t cc;
    uint32_t crc32;

    uint16_t buffer_size;
    uint16_t buffer_skip;
    uint8_t buffer[PSI_MAX_SIZE];
    /* extra callback parameters */
    edi_handler_t *edi_p;
    uint16_t port;
    uint32_t ip;
    bool debug;
} mpegts_psi_t;

/*
 * mpegts/psi.c
 */

static
void callback(mpegts_psi_t *psi);

static
void process_ts(mpegts_psi_t *psi, const uint8_t *ts)
{
    const uint8_t *payload = TS_GET_PAYLOAD(ts);
    if(!payload)
        return;

    const uint8_t cc = TS_GET_CC(ts);

    if(TS_IS_PAYLOAD_START(ts))
    {
        const uint8_t ptr_field = *payload;
        ++payload; // skip pointer field

        if(ptr_field > 0)
        { // pointer field
            if(ptr_field >= TS_BODY_SIZE)
            {
                psi->buffer_skip = 0;
                return;
            }
            if(psi->buffer_skip > 0)
            {
                if(((psi->cc + 1) & 0x0f) != cc)
                { // discontinuity error
                    psi->buffer_skip = 0;
                    return;
                }
                memcpy(&psi->buffer[psi->buffer_skip], payload, ptr_field);
                if(psi->buffer_size == 0)
                { // incomplete PSI header
                    const size_t psi_buffer_size = PSI_BUFFER_GET_SIZE(psi->buffer);
                    if(psi_buffer_size <= 3 || psi_buffer_size > PSI_MAX_SIZE)
                    {
                        psi->buffer_skip = 0;
                        return;
                    }
                    psi->buffer_size = psi_buffer_size;
                }
                if(psi->buffer_size != psi->buffer_skip + ptr_field)
                { // checking PSI length
                    psi->buffer_skip = 0;
                    return;
                }
                psi->buffer_skip = 0;
                callback(psi);
            }
            payload += ptr_field;
        }
        while(((payload - ts) < TS_PACKET_SIZE) && (payload[0] != 0xff))
        {
            psi->buffer_size = 0;

            const uint8_t remain = (ts + TS_PACKET_SIZE) - payload;
            if(remain < 3)
            {
                memcpy(psi->buffer, payload, remain);
                psi->buffer_skip = remain;
                break;
            }

            const size_t psi_buffer_size = PSI_BUFFER_GET_SIZE(payload);
            if(psi_buffer_size <= 3 || psi_buffer_size > PSI_MAX_SIZE)
                break;

            const size_t cpy_len = (ts + TS_PACKET_SIZE) - payload;
            if(cpy_len > TS_BODY_SIZE)
                break;

            psi->buffer_size = psi_buffer_size;
            if(psi_buffer_size > cpy_len)
            {
                memcpy(psi->buffer, payload, cpy_len);
                psi->buffer_skip = cpy_len;
                break;
            }
            else
            {
                memcpy(psi->buffer, payload, psi_buffer_size);
                psi->buffer_skip = 0;
                callback(psi);
                payload += psi_buffer_size;
            }
        }
    }
    else
    { // !TS_PUSI(ts)
        if(!psi->buffer_skip)
            return;
        if(((psi->cc + 1) & 0x0f) != cc)
        { // discontinuity error
            psi->buffer_skip = 0;
            return;
        }
        if(psi->buffer_size == 0)
        { // incomplete PSI header
            if(psi->buffer_skip >= 3)
            {
                psi->buffer_skip = 0;
                return;
            }
            memcpy(&psi->buffer[psi->buffer_skip], payload, 3 - psi->buffer_skip);
            const size_t psi_buffer_size = PSI_BUFFER_GET_SIZE(psi->buffer);
            if(psi_buffer_size <= 3 || psi_buffer_size > PSI_MAX_SIZE)
            {
                psi->buffer_skip = 0;
                return;
            }
            psi->buffer_size = psi_buffer_size;
        }
        const size_t remain = psi->buffer_size - psi->buffer_skip;
        if(remain <= TS_BODY_SIZE)
        {
            memcpy(&psi->buffer[psi->buffer_skip], payload, remain);
            psi->buffer_skip = 0;
            callback(psi);
        }
        else
        {
            memcpy(&psi->buffer[psi->buffer_skip], payload, TS_BODY_SIZE);
            psi->buffer_skip += TS_BODY_SIZE;
        }
    }
    psi->cc = cc;
}

/*
 * main program
 */

static
void callback(mpegts_psi_t *psi)
{
    if (psi->buffer[0] != 0x3e)
        return;

    const uint8_t *ptr = psi->buffer;
    size_t len = psi->buffer_size;

    /* MAC address */
    unsigned char dest_mac[6];
    dest_mac[5] = psi->buffer[3];
    dest_mac[4] = psi->buffer[4];
    dest_mac[3] = psi->buffer[8];
    dest_mac[2] = psi->buffer[9];
    dest_mac[1] = psi->buffer[10];
    dest_mac[0] = psi->buffer[11];

    if (psi->debug)
	fprintf(stderr, "MAC addess: %2X:%2X:%2X:%2X:%2X:%2X\n", dest_mac[0],
            dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5]);

    /* Parse IP header */
    unsigned char *ip = psi->buffer + 12;

    /* IP version - v4 */
    char version = (ip[0] & 0xF0) >> 4;
    if(version != 4) {
        fprintf(stderr, "Not IP packet.. ver=%d\n", version);
        return;
    }

    /* Protocol number */
    char proto = ip[9];

    /* filter non-UDP packets */
    if(proto != 17)
    {
        fprintf(stderr, "Not UDP protocol %d\n", proto);
        return;
    }

    /* packet length */
    //unsigned short len_ip = ip[2] << 8 | ip[3];

    /* source IP addres */
    unsigned char src_ip[4];
    src_ip[0] = ip[12];
    src_ip[1] = ip[13];
    src_ip[2] = ip[14];
    src_ip[3] = ip[15];

    /* Destination IP address */
    unsigned char dst_ip[4];
    dst_ip[0] = ip[16];
    dst_ip[1] = ip[17];
    dst_ip[2] = ip[18];
    dst_ip[3] = ip[19];

    unsigned char *udp = ip + 20;
    unsigned short src_port = (udp[0] << 8) | udp[1];
    unsigned short dst_port = (udp[2] << 8) | udp[3];
    unsigned short len_udp  = (udp[4] << 8) | udp[5];
    //unsigned short chk_udp  = (udp[6] << 8) | udp[7];

    if (psi->debug)
    {
        fprintf(stderr, "UDP %d.%d.%d.%d:%d --> %d.%d.%d.%d:%d  [%d bytes payload (%zu) EDI packet %c%c]\n",
            src_ip[0], src_ip[1], src_ip[2], src_ip[3], src_port,
            dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], dst_port,
            len_udp-8, len-40, ptr[40], ptr[41]);
    }

    /* maybe use dst_ip[0] + dst_ip[1] << 8 + dst_ip[2] << 16 + dst_ip[3] << 24 instead? */
    char dbuf[18];
    sprintf(dbuf, "%u.%u.%u.%u", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    uint32_t dip;

    /* skip unknown ip or port */
    if (inet_pton (AF_INET, dbuf, &dip) != 1) return;
    if (dip != psi->ip) return;
    if (dst_port != psi->port) return;

    /* skip headers: MPE + IP + UDP */
    ptr += (12 + 20 + 8);
    len -= (12 + 20 + 8);

    if(HandleEDIPacket(psi->edi_p, udp+8, len_udp-8) < 0)
    {
        /* count invalid packets */
    }
}

int main(int argc, const char *argv[])
{
    uint32_t pid = 0;
    uint32_t ip = 0;
    uint16_t port = 0;

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <pid> <ip> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    pid = atoi(argv[1]);

    if (inet_pton (AF_INET, argv[2], &ip) != 1)
    {
        fprintf(stderr, "invalid ip: %s\n", argv[2]);
        exit(EXIT_FAILURE);
    }

    port = atoi(argv[3]);
    fprintf(stderr, "using pid %u ip %s port %u\n", pid, argv[2], port);

    mpegts_psi_t psi;
    memset(&psi, 0, sizeof(psi));

    /* extra parameters in callback for edi */
    psi.edi_p = initEDIHandle(ETI_FMT_RAW, write_file, stdout);
    psi.ip = ip;
    psi.port = port;
    psi.debug = getenv("DEBUG") ? 1 : 0;

    uint8_t ts[TS_PACKET_SIZE];
    while (fread(ts, sizeof(ts), 1, stdin) == 1)
    {
        if (TS_IS_SYNC(ts) && TS_GET_PID(ts) == pid)
            process_ts(&psi, ts);
    }

    closeEDIHandle(psi.edi_p);

    return 0;
}
