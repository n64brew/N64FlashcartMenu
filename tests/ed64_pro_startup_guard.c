#include <libcart/cart.h>
#include <libdragon.h>
#include <stdio.h>
#include <usb.h>

#include "flashcart/ed64_pro/ed64_pro.h"

/** @brief Reject legacy hardware probes after selecting the Pro backend. */
static void check_legacy_call (const char *operation) {
    if (ed64_pro_get_diagnostics()->probe.outcome == ED64_PRO_PROBE_ABSENT) {
        return;
    }
    console_init();
    console_set_debug(false);
    console_set_render_mode(RENDER_MANUAL);
    printf("ED64 Pro STARTUP GUARD FAILED\n\nForbidden legacy call:\n%s\n", operation);
    for (;;) {
        console_render();
    }
}

#define WRAP_LEGACY_VALUE(type, name, parameters, arguments) \
    type __real_##name parameters; \
    type __wrap_##name parameters { \
        check_legacy_call(#name); \
        return __real_##name arguments; \
    }

WRAP_LEGACY_VALUE(int, cart_init, (void), ())
WRAP_LEGACY_VALUE(bool, debug_init_sdfs, (const char *prefix, int npart), (prefix, npart))
WRAP_LEGACY_VALUE(bool, debug_init_usblog, (void), ())
WRAP_LEGACY_VALUE(bool, debug_init_emulog, (void), ())
/* Current libdragon aliases the old ISViewer entry point to debug_init_emulog. */
#ifndef debug_init_isviewer
WRAP_LEGACY_VALUE(bool, debug_init_isviewer, (void), ())
#endif
WRAP_LEGACY_VALUE(char, usb_initialize, (void), ())

int __real_fat_mount (const char *prefix, const fat_disk_t *disk, int flags);
/** @brief Optionally exercise the real Pro mount-failure path without SD writes. */
int __wrap_fat_mount (const char *prefix, const fat_disk_t *disk, int flags) {
#ifdef ED64_PRO_TEST_FAIL_MOUNT
    if (ed64_pro_get_diagnostics()->probe.outcome == ED64_PRO_PROBE_IDENTIFIED) {
        return -1;
    }
#endif
    return __real_fat_mount(prefix, disk, flags);
}
