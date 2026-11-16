#include "libdragon.h"

rtc_hooks_t test_rtc_hooks;
rtc_source_t test_rtc_source;
unsigned test_rtc_init_calls, test_rtc_resync_calls;
int test_rtc_sync_result;
bool test_rtc_available = true;

uint64_t get_ticks (void) { return mock_ticks_read(); }
int hook_rtc_calls (rtc_hooks_t *hooks) { test_rtc_hooks = *hooks; return 0; }
int unhook_rtc_calls (rtc_hooks_t *hooks) {
    if (test_rtc_hooks.gettime == hooks->gettime) test_rtc_hooks.gettime = NULL;
    if (test_rtc_hooks.settime == hooks->settime) test_rtc_hooks.settime = NULL;
    return 0;
}
bool rtc_init (void) {
    test_rtc_init_calls++;
    test_rtc_hooks = (rtc_hooks_t){0};
    return test_rtc_source != RTC_SOURCE_NONE;
}
rtc_source_t rtc_get_source (void) { return test_rtc_source; }
bool rtc_is_source_available (rtc_source_t source) {
    return source == test_rtc_source && test_rtc_available;
}
int rtc_set_source (rtc_source_t source) {
    test_rtc_resync_calls++;
    test_rtc_source = source;
    return test_rtc_sync_result;
}
