#include <stdint.h>
#include <string.h>

#include <acutest.h>

#include "../libdragon/tools/ipl3.h"
#include "boot/cic.h"

/** @brief Recognize the existing libdragon IPL3's x102 compatibility checksum. */
static void test_libdragon_ipl3 (void) {
    TEST_ASSERT(sizeof(default_ipl3) >= 0x40 + IPL3_LENGTH);
    TEST_CHECK(cic_detect(default_ipl3 + 0x40) == CIC_x102);
    TEST_CHECK(cic_get_seed(cic_detect(default_ipl3 + 0x40)) == 0x3F);
}

/** @brief Include both ends of IPL3 and avoid carrying detection across ROMs. */
static void test_changed_ipl3 (void) {
    uint8_t ipl3[IPL3_LENGTH];
    const size_t offsets[] = {0, IPL3_LENGTH - 1};

    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
        memcpy(ipl3, default_ipl3 + 0x40, sizeof(ipl3));
        ipl3[offsets[i]] ^= 0x80;
        TEST_CHECK(cic_detect(ipl3) == CIC_UNKNOWN);
        TEST_CHECK(cic_detect(default_ipl3 + 0x40) == CIC_x102);
    }
}

/** @brief Reject unknown IPL3 contents, including high-bit bytes and zero rotations. */
static void test_unknown_ipl3 (void) {
    uint8_t ipl3[IPL3_LENGTH];
    const uint8_t fills[] = {0x00, 0x01, 0x80, 0xFF};

    for (size_t i = 0; i < sizeof(fills) / sizeof(fills[0]); i++) {
        memset(ipl3, fills[i], sizeof(ipl3));
        TEST_CHECK(cic_detect(ipl3) == CIC_UNKNOWN);
        TEST_CHECK(cic_get_seed(cic_detect(ipl3)) == 0x3F);
    }
}

/** @brief IPL3 byte input has no alignment requirement. */
static void test_unaligned_ipl3 (void) {
    uint8_t storage[IPL3_LENGTH + 1];
    memcpy(storage + 1, default_ipl3 + 0x40, IPL3_LENGTH);
    TEST_CHECK(cic_detect(storage + 1) == CIC_x102);
}

/** @brief Preserve seed selection for PAL 7102, shared families and unknown inputs. */
static void test_cic_seeds (void) {
    const struct {
        cic_type_t type;
        uint8_t seed;
    } cases[] = {
        {CIC_5101, 0xAC},
        {CIC_5167, 0xDD},
        {CIC_6101, 0x3F},
        {CIC_7102, 0x3F},
        {CIC_x102, 0x3F},
        {CIC_x103, 0x78},
        {CIC_x105, 0x91},
        {CIC_x106, 0x85},
        {CIC_8301, 0xDD},
        {CIC_8302, 0xDD},
        {CIC_8303, 0xDD},
        {CIC_8401, 0xDD},
        {CIC_8501, 0xDE},
        {CIC_UNKNOWN, 0x3F},
        {(cic_type_t)-1, 0x3F},
        {(cic_type_t)255, 0x3F},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TEST_CHECK(cic_get_seed(cases[i].type) == cases[i].seed);
    }
}

TEST_LIST = {
    {"libdragon compatibility IPL3 identifies x102", test_libdragon_ipl3},
    {"changed IPL3 is unknown without stale detection", test_changed_ipl3},
    {"unknown IPL3 contents use the default seed", test_unknown_ipl3},
    {"unaligned IPL3 is detected", test_unaligned_ipl3},
    {"CIC families select their boot seeds", test_cic_seeds},
    {NULL, NULL},
};
