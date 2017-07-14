
#ifndef BUFFER_UNPACK_H_
#define BUFFER_UNPACK_H_

#include <stdint.h>
#include "crc.h"


/******************************************************************************
 * small helper functions
 ******************************************************************************/
static inline uint16_t read_16b(const uint8_t *buf){
    uint16_t value = ((uint16_t)buf[0]) << 8 | buf[1];
    return value;
}

static inline uint32_t read_24b(const uint8_t *buf){
    uint32_t value = ((uint32_t) buf[0]) << 16 | ((uint16_t)buf[1]) << 8 | buf[2];
    return value;
}

static inline uint32_t read_32b(const uint8_t *buf){
    uint32_t value = ((uint32_t)buf[0]) << 24 | ((uint32_t)buf[1]) << 16 | ((uint16_t)buf[2]) << 8 | buf[3];
    return value;
}

static inline uint32_t unpack1bit(const uint8_t byte, int bitpos){
    return (byte & 1 << (7-bitpos)) > (7-bitpos);
}

static inline bool checkCRC(const uint8_t *buf, size_t size)
{
    const uint16_t crc_from_packet = read_16b(buf + size - 2);
    uint16_t crc_calc = 0xffff;
    crc_calc = crc16(crc_calc, buf, size - 2);
    crc_calc ^= 0xffff;

    return crc_from_packet == crc_calc;
}

#endif /* BUFFER_UNPACK_H_ */
