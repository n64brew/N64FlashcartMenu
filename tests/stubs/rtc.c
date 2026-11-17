#include "libdragon.h"

rtc_hooks_t test_rtc_hooks;
unsigned test_rtc_init_calls;

uint64_t get_ticks (void) { return mock_ticks_read(); }
int hook_rtc_calls (rtc_hooks_t *hooks) { test_rtc_hooks = *hooks; return 0; }
int unhook_rtc_calls (rtc_hooks_t *hooks) {
    if (test_rtc_hooks.gettime == hooks->gettime) test_rtc_hooks.gettime = NULL;
    if (test_rtc_hooks.settime == hooks->settime) test_rtc_hooks.settime = NULL;
    return 0;
}
bool rtc_init (void) {
    if (test_rtc_init_calls++ == 0) {
        test_rtc_hooks = (rtc_hooks_t){0};
    }
    return false;
}
