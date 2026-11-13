/**
 * @file ed64_pro_region_probe.c
 * @brief Test-only unknown-destination ROM displaying the received boot TV type.
 */

#include <libdragon.h>
#include <stdio.h>

#ifndef ED64_PRO_REGION_PROBE_TEST
#error Build this ROM with tests/Makefile.region-probe
#endif

/** @brief Name the startup TV value without changing it. */
static const char *format_tv_type (tv_type_t tv_type) {
    switch (tv_type) {
    case TV_PAL: return "PAL";
    case TV_NTSC: return "NTSC";
    case TV_MPAL: return "MPAL";
    default: return "UNKNOWN";
    }
}

/** @brief Display startup state without filesystem, USB, RTC or controller I/O. */
int main (void) {
    tv_type_t tv_type = get_tv_type();

    console_init();
    console_set_debug(false);
    console_set_render_mode(RENDER_MANUAL);
    printf("ED64 Pro region probe (test only)\n\n");
    printf("Received boot TV: %d (%s)\n", (int)tv_type, format_tv_type(tv_type));
    printf("RDRAM: %d MiB\n\n", get_memory_size() / (1024 * 1024));
    printf("ROM destination: unknown (0x00)\n");
    printf("Automatic selection preserves console boot TV.\n\n");
    printf("Startup TV value; VI refresh is not measured.\n");
    for (;;) {
        console_render();
    }
}
