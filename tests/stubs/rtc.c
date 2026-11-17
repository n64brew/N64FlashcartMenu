#include "libdragon.h"

rtc_hooks_t test_rtc_hooks;
unsigned test_rtc_init_calls;
unsigned test_rtc_close_calls, test_rtc_set_source_calls;
int test_rtc_set_source_result;
rtc_source_t test_rtc_source = RTC_SOURCE_JOYBUS;
bool test_rtc_available = true, test_rtc_init_result = true, test_rtc_initialized;
void (*test_rtc_init_observer)(void);

uint64_t get_ticks (void) { return mock_ticks_read(); }
int hook_rtc_calls (rtc_hooks_t *hooks) { test_rtc_hooks = *hooks; return 0; }
int unhook_rtc_calls (rtc_hooks_t *hooks) {
    if (test_rtc_hooks.gettime == hooks->gettime) test_rtc_hooks.gettime = NULL;
    if (test_rtc_hooks.settime == hooks->settime) test_rtc_hooks.settime = NULL;
    return 0;
}
bool rtc_init (void) {
    if (test_rtc_init_observer) test_rtc_init_observer();
    if (test_rtc_init_calls++ == 0) {
        test_rtc_hooks = (rtc_hooks_t){0};
    }
    test_rtc_initialized = true;
    return test_rtc_init_result;
}
void rtc_close (void) {
    test_rtc_close_calls++;
    test_rtc_initialized = false;
    test_rtc_hooks = (rtc_hooks_t){0};
}
rtc_source_t rtc_get_source (void) {
    return test_rtc_initialized ? test_rtc_source : RTC_SOURCE_NONE;
}
bool rtc_is_source_available (rtc_source_t source) {
    return test_rtc_initialized && source == RTC_SOURCE_JOYBUS && test_rtc_available;
}

int rtc_set_source (rtc_source_t source) {
    test_rtc_set_source_calls++;
    if (!rtc_is_source_available(source)) return RTC_ENOCLOCK;
    test_rtc_source = source;
    return test_rtc_set_source_result;
}
