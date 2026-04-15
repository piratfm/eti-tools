/*
 * mpe2aac.c
 * Uses parts of astra-sm
 *
 * Created on: 06.06.2018
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
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

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

/*
 * UECP forwarding state
 * Reassembles UECP frames (0xFE...0xFF) from RTP header extension chunks.
 * Chunks are identified by a 4-byte ID at extension data bytes[20-23].
 * Duplicate chunk IDs are skipped, and we wait for a new 0xFE after each
 * complete frame (the final chunk is repeated until the next message starts).
 */
#define UECP_BUF_SIZE 4096

typedef struct
{
    int fd;                      /* TCP socket fd, -1 = disconnected */
    struct sockaddr_in addr;     /* forwarding destination */
    uint8_t buf[UECP_BUF_SIZE];  /* frame assembly buffer */
    uint16_t len;                /* bytes accumulated */
    bool in_frame;               /* currently assembling a frame */
    uint32_t last_chunk_id;      /* dedup: id of last accepted chunk */
} uecp_t;

typedef struct
{
    uint8_t cc;
    uint32_t crc32;

    uint16_t buffer_size;
    uint16_t buffer_skip;
    uint8_t buffer[PSI_MAX_SIZE];
    /* extra callback parameters */
    uint16_t port;
    uint32_t ip;
    bool debug;
    uecp_t *uecp;               /* NULL = UECP forwarding not configured */
} mpegts_psi_t;

/*
 * UECP TCP forwarding
 */

/* MSG_NOSIGNAL not available on macOS; use SO_NOSIGPIPE at socket creation */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static void uecp_connect(uecp_t *uecp)
{
    if (uecp->fd >= 0)
        return;
    uecp->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (uecp->fd < 0)
        return;
#ifdef SO_NOSIGPIPE
    int opt = 1;
    setsockopt(uecp->fd, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
#endif
    if (connect(uecp->fd, (struct sockaddr *)&uecp->addr, sizeof(uecp->addr)) < 0)
    {
        fprintf(stderr, "UECP connect failed: %s\n", strerror(errno));
        close(uecp->fd);
        uecp->fd = -1;
    }
}

static void uecp_send_frame(uecp_t *uecp)
{
    if (uecp->fd < 0)
        uecp_connect(uecp);
    if (uecp->fd < 0)
        return;
    ssize_t n = send(uecp->fd, uecp->buf, uecp->len, MSG_NOSIGNAL);
    if (n < 0)
    {
        fprintf(stderr, "UECP send failed: %s\n", strerror(errno));
        close(uecp->fd);
        uecp->fd = -1;
        return;
    }
    /* Drain any data the server sends back — we don't use it, but if we never
     * read it the kernel receive buffer will eventually fill and TCP flow
     * control will stall the server's sends. */
    uint8_t drain[256];
    while (recv(uecp->fd, drain, sizeof(drain), MSG_DONTWAIT) > 0)
        ;
}

/*
 * Process one RTP extension payload.
 * ext     - points to the raw extension data (after the 4-byte profile/length header)
 * ext_len - length in bytes (ext_words * 4)
 *
 * Layout within ext (0-indexed):
 *   [20-23] chunk ID (used for deduplication)
 *   [25]    chunk data length in bytes
 *   [28..]  chunk data
 */
static void uecp_process_ext(uecp_t *uecp, const uint8_t *ext, uint16_t ext_len, bool debug)
{
    /* Need header through first data byte */
    if (ext_len < 29)
        return;

    uint32_t chunk_id = ((uint32_t)ext[20] << 24) | ((uint32_t)ext[21] << 16) |
                        ((uint32_t)ext[22] << 8)  |  ext[23];
    uint8_t  chunk_len = ext[25];
    const uint8_t *chunk = ext + 28;

    if (chunk_len == 0 || 28 + chunk_len > ext_len)
        return;

    if (!uecp->in_frame)
    {
        /* Waiting for a new frame: only accept chunks that start with 0xFE */
        if (chunk[0] != 0xFE)
            return;
        uecp->len = 0;
        uecp->in_frame = true;
        uecp->last_chunk_id = chunk_id;
    }
    else
    {
        /* Building a frame: skip duplicate chunks */
        if (chunk_id == uecp->last_chunk_id)
            return;
        uecp->last_chunk_id = chunk_id;
    }

    if (uecp->len + chunk_len > UECP_BUF_SIZE)
    {
        fprintf(stderr, "UECP buffer overflow, discarding frame\n");
        uecp->in_frame = false;
        uecp->len = 0;
        return;
    }

    memcpy(uecp->buf + uecp->len, chunk, chunk_len);
    uecp->len += chunk_len;

    if (debug)
        fprintf(stderr, "UECP chunk: id=0x%08x len=%u total=%u\n",
                chunk_id, chunk_len, uecp->len);

    /* 0xFF as the last byte of the chunk marks the end of the UECP frame */
    if (chunk[chunk_len - 1] == 0xFF)
    {
        if (debug)
            fprintf(stderr, "UECP frame complete: %u bytes\n", uecp->len);
        uecp_send_frame(uecp);
        uecp->in_frame = false;
        uecp->len = 0;
    }
}

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

    uint8_t ihl = (ip[0] & 0x0F) * 4;
    unsigned char *udp = ip + ihl;
    unsigned short src_port = (udp[0] << 8) | udp[1];
    unsigned short dst_port = (udp[2] << 8) | udp[3];
    unsigned short len_udp  = (udp[4] << 8) | udp[5];
    //unsigned short chk_udp  = (udp[6] << 8) | udp[7];

    if (psi->debug)
    {
        fprintf(stderr, "UDP %d.%d.%d.%d:%d --> %d.%d.%d.%d:%d  [%d bytes payload (%zu)]\n",
            src_ip[0], src_ip[1], src_ip[2], src_ip[3], src_port,
            dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3], dst_port,
            len_udp-8, len-40);
    }

    /* maybe use dst_ip[0] + dst_ip[1] << 8 + dst_ip[2] << 16 + dst_ip[3] << 24 instead? */
    char dbuf[18];
    sprintf(dbuf, "%u.%u.%u.%u", dst_ip[0], dst_ip[1], dst_ip[2], dst_ip[3]);
    uint32_t dip;

    /* skip unknown ip or port */
    if (inet_pton (AF_INET, dbuf, &dip) != 1) return;
    if (dip != psi->ip) return;
    if (dst_port != psi->port) return;

    len_udp -= 8;
    unsigned char *rtp = udp + 8;

    /* Parse RTP header */
    if (len_udp < 12)
        return;

    if ((rtp[0] >> 6) != 2)
    {
        if (psi->debug)
            fprintf(stderr, "Not RTP (version=%d)\n", (rtp[0] >> 6));
        return;
    }

    uint8_t rtp_padding   = (rtp[0] >> 5) & 0x01;
    uint8_t rtp_extension = (rtp[0] >> 4) & 0x01;
    uint8_t rtp_cc        = rtp[0] & 0x0F;

    if (psi->debug)
    {
        uint16_t rtp_seq = (rtp[2] << 8) | rtp[3];
        uint32_t rtp_ts  = ((uint32_t)rtp[4] << 24) | ((uint32_t)rtp[5] << 16) |
                           ((uint32_t)rtp[6] << 8)  | rtp[7];
        fprintf(stderr, "RTP seq=%u ts=%u pt=%d\n", rtp_seq, rtp_ts, rtp[1] & 0x7F);
    }

    unsigned char *payload = rtp + 12;
    len_udp -= 12;

    /* skip CSRC entries */
    uint16_t csrc_len = rtp_cc * 4;
    if (len_udp < csrc_len)
        return;
    payload += csrc_len;
    len_udp -= csrc_len;

    /* skip RTP extension if present */
    if (rtp_extension)
    {
        if (len_udp < 4)
            return;
        uint16_t ext_words = (payload[2] << 8) | payload[3];
        uint16_t ext_total = 4 + ext_words * 4;
        if (len_udp < ext_total)
            return;

        if (psi->uecp)
            uecp_process_ext(psi->uecp, payload + 4, ext_words * 4, psi->debug);

        payload += ext_total;
        len_udp -= ext_total;
    }

    /* strip RTP padding if present */
    if (rtp_padding && len_udp > 0)
    {
        uint8_t pad_len = payload[len_udp - 1];
        if (pad_len > len_udp)
            return;
        len_udp -= pad_len;
    }

    /* skip leading zero bytes before the AAC header */
    while (len_udp > 0 && payload[0] == 0x00)
    {
        ++payload;
        --len_udp;
    }

    if (len_udp > 0)
        fwrite(payload, 1, len_udp, stdout);

}

int main(int argc, const char *argv[])
{
    uint32_t pid = 0;
    uint32_t ip = 0;
    uint16_t port = 0;

    if (argc < 4)
    {
        fprintf(stderr, "usage: %s <pid> <ip> <port> [uecp_ip:port]\n", argv[0]);
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

    /* extra parameters in callback for payload extract */
    psi.ip = ip;
    psi.port = port;
    psi.debug = getenv("DEBUG") ? 1 : 0;

    /* Optional UECP forwarding: parse "ip:port" from argv[4] */
    uecp_t uecp;
    if (argc >= 5)
    {
        const char *arg = argv[4];
        const char *colon = strchr(arg, ':');
        if (!colon || colon == arg || *(colon + 1) == '\0')
        {
            fprintf(stderr, "invalid uecp address (expected ip:port): %s\n", arg);
            exit(EXIT_FAILURE);
        }

        char uecp_ip[64];
        size_t ip_len = colon - arg;
        if (ip_len >= sizeof(uecp_ip))
        {
            fprintf(stderr, "UECP IP address too long\n");
            exit(EXIT_FAILURE);
        }
        memcpy(uecp_ip, arg, ip_len);
        uecp_ip[ip_len] = '\0';

        memset(&uecp, 0, sizeof(uecp));
        uecp.fd = -1;
        uecp.addr.sin_family = AF_INET;
        uecp.addr.sin_port = htons((uint16_t)atoi(colon + 1));
        if (inet_pton(AF_INET, uecp_ip, &uecp.addr.sin_addr) != 1)
        {
            fprintf(stderr, "invalid uecp IP: %s\n", uecp_ip);
            exit(EXIT_FAILURE);
        }
        fprintf(stderr, "UECP forwarding to %s:%s\n", uecp_ip, colon + 1);
        psi.uecp = &uecp;
    }

    uint8_t ts[TS_PACKET_SIZE];
    while (fread(ts, sizeof(ts), 1, stdin) == 1)
    {
        if (TS_IS_SYNC(ts) && TS_GET_PID(ts) == pid)
            process_ts(&psi, ts);
    }

    return 0;
}
