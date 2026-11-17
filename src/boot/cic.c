/**
 * @file cic.c
 * @brief CIC (Copy Protection) functions implementation
 * @ingroup boot
 */

#include "cic.h"

/**
 * @brief Get a 32-bit value from a byte array at the specified index.
 * 
 * @param p Pointer to the byte array.
 * @param index Index to get the value from.
 * @return uint32_t The 32-bit value.
 */
static inline uint32_t _get (uint8_t *p, int index) {
    int i = index * 4;
    return (p[i] << 24 | p[i + 1] << 16 | p[i + 2] << 8 | p[i + 3]);
}

/**
 * @brief Add two 32-bit values.
 * 
 * @param a1 First value.
 * @param a2 Second value.
 * @return uint32_t The result of the addition.
 */
static inline uint32_t _add (uint32_t a1, uint32_t a2) {
    return a1 + a2;
}

/**
 * @brief Subtract one 32-bit value from another.
 * 
 * @param a1 First value.
 * @param a2 Second value.
 * @return uint32_t The result of the subtraction.
 */
static inline uint32_t _sub (uint32_t a1, uint32_t a2) {
    return a1 - a2;
}

/**
 * @brief Multiply two 32-bit values.
 * 
 * @param a1 First value.
 * @param a2 Second value.
 * @return uint32_t The result of the multiplication.
 */
static inline uint32_t _mul (uint32_t a1, uint32_t a2) {
    return a1 * a2;
}

/**
 * @brief Rotate a 32-bit value left by a specified number of bits.
 * 
 * @param a The value to rotate.
 * @param s The number of bits to rotate.
 * @return uint32_t The result of the rotation.
 */
static inline uint32_t _rol (uint32_t a, uint32_t s) {
    return ((a) << (s)) | ((a) >> (-(s) & 31));
}

/**
 * @brief Rotate a 32-bit value right by a specified number of bits.
 * 
 * @param a The value to rotate.
 * @param s The number of bits to rotate.
 * @return uint32_t The result of the rotation.
 */
static inline uint32_t _ror (uint32_t a, uint32_t s) {
    return ((a) >> (s)) | ((a) << (-(s) & 31));
}

/**
 * @brief Calculate the sum of three 32-bit values.
 * 
 * @param a0 First value.
 * @param a1 Second value.
 * @param a2 Third value.
 * @return uint32_t The result of the sum.
 */
static uint32_t _sum (uint32_t a0, uint32_t a1, uint32_t a2) {
    uint64_t prod = ((uint64_t) (a0)) * (a1 == 0 ? a2 : a1);
    uint32_t hi = (prod >> 32) & 0xFFFFFFFF;
    uint32_t lo = prod & 0xFFFFFFFF;
    uint32_t diff = hi - lo;
    return (diff == 0) ? a0 : diff;
}

/**
 * @brief Calculate the IPL3 checksum for the CIC.
 * 
 * @param ipl3 Pointer to the IPL3 data.
 * @param seed The seed value.
 * @return uint64_t The calculated checksum.
 */
static uint64_t cic_calculate_ipl3_checksum (uint8_t *ipl3, uint8_t seed) {
    const uint32_t MAGIC = 0x6C078965;

    uint32_t data, prev, next;
    data = prev = next = _get(ipl3, 0);

    uint32_t init = _add(_mul(MAGIC, seed), 1) ^ data;

    uint32_t buf[16];
    for (int i = 0; i < 16; i++) {
        buf[i] = init;
    }

    for (int i = 1; i <= 1008; i++) {
        prev = data;
        data = next;

        buf[0] = _add(buf[0], _sum(_sub(1007, i), data, i));
        buf[1] = _sum(buf[1], data, i);
        buf[2] = buf[2] ^ data;
        buf[3] = _add(buf[3], _sum(_add(data, 5), MAGIC, i));
        buf[4] = _add(buf[4], _ror(data, prev & 0x1F));
        buf[5] = _add(buf[5], _rol(data, prev >> 27));
        buf[6] = (data < buf[6]) ? (_add(buf[3], buf[6]) ^ _add(data, i)) : (_add(buf[4], data) ^ buf[6]);
        buf[7] = _sum(buf[7], _rol(data, prev & 0x1F), i);
        buf[8] = _sum(buf[8], _ror(data, prev >> 27), i);
        buf[9] = (prev < data) ? _sum(buf[9], data, i) : _add(buf[9], data);

        if (i == 1008) {
            break;
        }

        next = _get(ipl3, i);

        buf[10] = _sum(_add(buf[10], data), next, i);
        buf[11] = _sum(buf[11] ^ data, next, i);
        buf[12] = _add(buf[12], buf[8] ^ data);
        buf[13] = _add(buf[13], _add(_ror(data, data & 0x1F), _ror(next, next & 0x1F)));
        buf[14] = _sum(_sum(buf[14], _ror(data, prev & 0x1F), i), _ror(next, data & 0x1F), i);
        buf[15] = _sum(_sum(buf[15], _rol(data, prev >> 27), i), _rol(next, data >> 27), i);
    }

    uint32_t final_buf[4];
    for (int i = 0; i < 4; i++) {
        final_buf[i] = buf[0];
    }

    for (int i = 0; i < 16; i++) {
        uint32_t data = buf[i];
        final_buf[0] = _add(final_buf[0], _ror(data, data & 0x1F));
        final_buf[1] = (data < final_buf[0]) ? _add(final_buf[1], data) : _sum(final_buf[1], data, i);
        final_buf[2] = (((data & 0x02) >> 1) == (data & 0x01)) ? _add(final_buf[2], data) : _sum(final_buf[2], data, i);
        final_buf[3] = ((data & 0x01) == 0x01) ? (final_buf[3] ^ data) : _sum(final_buf[3], data, i);
    }

    uint32_t final_sum = _sum(final_buf[0], final_buf[1], 16);
    uint32_t final_xor = final_buf[3] ^ final_buf[2];

    uint64_t checksum = (((((uint64_t) (final_sum)) & 0xFFFF)) << 32) | (final_xor);

    return checksum;
}

/**
 * @brief Detect the CIC type based on the IPL3 data.
 * 
 * @param ipl3 Pointer to the IPL3 data.
 * @return cic_type_t The detected CIC type.
 */
cic_type_t cic_detect (uint8_t *ipl3) {
    switch (cic_calculate_ipl3_checksum(ipl3, 0x3F)) {
        case 0x45CC73EE317AULL: return CIC_6101;        // 6101
        case 0x44160EC5D9AFULL: return CIC_7102;        // 7102
        case 0xA536C0F1D859ULL: return CIC_x102;        // 6102 / 7101
    }
    switch (cic_calculate_ipl3_checksum(ipl3, 0x78)) {
        case 0x586FD4709867ULL: return CIC_x103;        // 6103 / 7103
    }
    switch (cic_calculate_ipl3_checksum(ipl3, 0x85)) {
        case 0x2BBAD4E6EB74ULL: return CIC_x106;        // 6106 / 7106
    }
    switch (cic_calculate_ipl3_checksum(ipl3, 0x91)) {
        case 0x8618A45BC2D3ULL: return CIC_x105;        // 6105 / 7105
    }
    switch (cic_calculate_ipl3_checksum(ipl3, 0xAC)) {
        case 0x93E983A8F152ULL: return CIC_5101;        // 5101 / Aleck64 conversion
    }
    switch (cic_calculate_ipl3_checksum(ipl3, 0xDD)) {
        case 0x6EE8D9E84970ULL: return CIC_8401;        // NDXJ0
        case 0x6C216495C8B9ULL: return CIC_8301;        // NDDJ0
        case 0xE27F43BA93ACULL: return CIC_8302;        // NDDJ1
        case 0x32B294E2AB90ULL: return CIC_8303;        // NDDJ2
        case 0x083C6C77E0B1ULL: return CIC_5167;        // 64DD Cartridge conversion
    }
    switch (cic_calculate_ipl3_checksum(ipl3, 0xDE)) {
        case 0x05BA2EF0A5F1ULL: return CIC_8501;        // NDDE0
    }

    return CIC_UNKNOWN;
}

/**
 * @brief Get the seed value for the specified CIC type.
 * 
 * @param cic_type The CIC type.
 * @return uint8_t The seed value.
 */
uint8_t cic_get_seed (cic_type_t cic_type) {
    switch (cic_type) {
        case CIC_5101: return 0xAC;
        case CIC_5167: return 0xDD;
        case CIC_6101: return 0x3F;
        case CIC_7102: return 0x3F;
        case CIC_x102: return 0x3F;
        case CIC_x103: return 0x78;
        case CIC_x105: return 0x91;
        case CIC_x106: return 0x85;
        case CIC_8301: return 0xDD;
        case CIC_8302: return 0xDD;
        case CIC_8303: return 0xDD;
        case CIC_8401: return 0xDD;
        case CIC_8501: return 0xDE;
        default: return 0x3F;
    }
}
