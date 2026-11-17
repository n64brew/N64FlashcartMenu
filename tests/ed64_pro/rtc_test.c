#include <acutest.h>
#include <string.h>
#include <time.h>

#include "../../src/flashcart/ed64_pro/ed64_pro_rtc.c"

static struct {
    ed64_pro_dev_cfg_t config;
    ed64_pro_ll_transaction_t *owner;
    uint32_t ticks;
    bool started, native_initialized, native_available, native_result;
    rtc_source_t native_source;
    unsigned begins, ends, configs, starts, stops, drains, statuses, inits, closes, source_sets;
    int source_set_result;
    ed64_pro_ll_error_t begin_error, cfg_error, start_error, stop_error, drain_error, status_error, end_error;
    char events[128];
    unsigned event_count;
} mock;
int cart_type;

static void event (char c) {
    TEST_ASSERT(mock.event_count + 1 < sizeof(mock.events));
    mock.events[mock.event_count++] = c;
    mock.events[mock.event_count] = 0;
}
uint32_t mock_ticks_read (void) { return mock.ticks++; }
uint64_t get_ticks (void) { return mock.ticks; }
bool rtc_init (void) {
    TEST_CHECK(mock.starts == 1 && mock.started);
    event('I'); mock.inits++; mock.native_initialized = true;
    return mock.native_result;
}
void rtc_close (void) { event('X'); mock.closes++; mock.native_initialized = false; }
rtc_source_t rtc_get_source (void) { TEST_ASSERT(mock.native_initialized); return mock.native_source; }
int rtc_set_source (rtc_source_t source) {
    mock.source_sets++;
    TEST_CHECK(source == RTC_SOURCE_JOYBUS && mock.native_available);
    if (mock.source_set_result != RTC_ENOCLOCK) mock.native_source = source;
    return mock.source_set_result;
}
bool rtc_is_source_available (rtc_source_t source) {
    TEST_ASSERT(mock.native_initialized);
    TEST_CHECK(source == RTC_SOURCE_JOYBUS);
    return mock.native_available;
}
int hook_rtc_calls (rtc_hooks_t *hooks) { (void)hooks; TEST_CHECK(false); return -1; }
int unhook_rtc_calls (rtc_hooks_t *hooks) { (void)hooks; TEST_CHECK(false); return -1; }

ed64_pro_ll_error_t ed64_pro_ll_begin (ed64_pro_ll_transaction_t *t, uint32_t deadline) {
    event('B'); mock.begins++; TEST_CHECK(!mock.owner);
    if (!mock.begin_error) { mock.owner = t; t->deadline = deadline; }
    return mock.begin_error;
}
ed64_pro_ll_error_t ed64_pro_ll_end (ed64_pro_ll_transaction_t *t) {
    event('E'); mock.ends++; TEST_CHECK(mock.owner == t); mock.owner = NULL; return mock.end_error;
}
ed64_pro_ll_error_t ed64_pro_ll_dev_set_cfg (ed64_pro_ll_transaction_t *t, const ed64_pro_dev_cfg_t *cfg, uint32_t deadline) {
    event('G'); mock.configs++; TEST_CHECK(mock.owner == t && deadline <= t->deadline); mock.config = *cfg; return mock.cfg_error;
}
ed64_pro_ll_error_t ed64_pro_ll_dev_start (ed64_pro_ll_transaction_t *t, uint32_t deadline) {
    event('S'); mock.starts++; TEST_CHECK(mock.owner == t && deadline <= t->deadline); mock.started = true; return mock.start_error;
}
ed64_pro_ll_error_t ed64_pro_ll_stop (ed64_pro_ll_transaction_t *t, uint32_t deadline) {
    event('T'); mock.stops++; TEST_CHECK(mock.owner == t && deadline <= t->deadline); mock.started = false; return mock.stop_error;
}
ed64_pro_ll_error_t ed64_pro_ll_drain (ed64_pro_ll_transaction_t *t, size_t *drained, uint32_t deadline) {
    event('D'); mock.drains++; TEST_CHECK(mock.owner == t && deadline <= t->deadline); *drained = 0; return mock.drain_error;
}
ed64_pro_ll_error_t ed64_pro_ll_check_status (ed64_pro_ll_transaction_t *t, ed64_pro_status_t *status, uint32_t deadline) {
    event('C'); mock.statuses++; TEST_CHECK(mock.owner == t && deadline <= t->deadline); *status = (ed64_pro_status_t){.key = 0x5A}; return mock.status_error;
}
uint32_t ed64_pro_ll_limit_deadline (uint32_t deadline, uint32_t ms) { (void)ms; return deadline; }

static void fixture (void) {
    mock.begin_error = mock.stop_error = mock.drain_error = mock.status_error = mock.end_error = ED64_PRO_LL_OK;
    ed64_pro_rtc_close();
    /* Each fixture is a new simulated boot; production failures remain latched. */
    pro_rtc_failed = pro_rtc_running = false;
    pro_rtc_error = NULL;
    memset(&mock, 0, sizeof(mock));
    mock.native_available = mock.native_result = true;
    mock.native_source = RTC_SOURCE_JOYBUS;
    mock.ticks = 0xFFFFFF00u;
    cart_type = CART_NULL;
}
static void test_native_services (void) {
    fixture();
    TEST_ASSERT(ed64_pro_rtc_init() == FLASHCART_OK);
    TEST_CHECK(mock.config.brom_type == 0); /* Vendor ROM_OFF. */
    TEST_CHECK(mock.config.gpak_size == 0);
    TEST_CHECK(mock.config.rtc_mode == ED64_PRO_RTC_STD);
    TEST_CHECK(mock.config.brm_type == ED64_PRO_BRM_OFF && mock.config.brm_size == 0);
    TEST_CHECK(mock.config.dd_en == 0 && mock.config.gpak_wren == 0 && mock.config.gpak_key == 0);
    for (unsigned i = 0; i < 8; i++) TEST_CHECK(mock.config.reserved[i] == 0);
    TEST_CHECK(mock.starts == 1 && mock.inits == 1 && cart_type == CART_MAX);
    TEST_CHECK(rtc_get_source() == RTC_SOURCE_JOYBUS && rtc_is_source_available(RTC_SOURCE_JOYBUS));
    TEST_CHECK(mock.owner == NULL && ed64_pro_rtc_get_error_message() == NULL);
    unsigned begins = mock.begins;
    TEST_CHECK(ed64_pro_rtc_init() == FLASHCART_OK && mock.begins == begins && mock.inits == 1);
}
static void test_native_gate (void) {
    for (unsigned failure = 0; failure < 2; failure++) {
        fixture(); mock.native_available = false;
        if (failure) mock.native_source = RTC_SOURCE_NONE;
        TEST_CHECK(ed64_pro_rtc_init() != FLASHCART_OK);
        TEST_CHECK(mock.starts == 1 && mock.stops == 1);
        TEST_CHECK(!mock.native_initialized && !mock.started);
        TEST_CHECK(mock.owner == NULL && ed64_pro_rtc_get_error_message() != NULL);
    }
}
static void test_invalid_native_calendar (void) {
    fixture();
    mock.native_result = false;
    mock.native_source = RTC_SOURCE_NONE;
    mock.source_set_result = RTC_EBADTIME;
    TEST_CHECK(ed64_pro_rtc_init() == FLASHCART_OK);
    TEST_CHECK(mock.source_sets == 1 && rtc_get_source() == RTC_SOURCE_JOYBUS);
    TEST_CHECK(mock.source_set_result == RTC_EBADTIME && mock.stops == 0);
}
static void test_init_faults (void) {
    for (unsigned failure = 0; failure < 4; failure++) {
        fixture();
        switch (failure) {
        case 0: mock.begin_error = ED64_PRO_LL_TIMEOUT; break;
        case 1: mock.cfg_error = ED64_PRO_LL_MCU_ERROR; break;
        case 2: mock.start_error = ED64_PRO_LL_TIMEOUT; break;
        case 3: mock.end_error = ED64_PRO_LL_FRAME; break;
        }
        TEST_CHECK(ed64_pro_rtc_init() != FLASHCART_OK);
        TEST_CHECK(mock.configs <= 1 && mock.starts <= 1 && mock.inits == 0);
        TEST_CHECK(mock.owner == NULL && ed64_pro_rtc_get_error_message() != NULL);
        unsigned begins = mock.begins, starts = mock.starts;
        const char *error = ed64_pro_rtc_get_error_message();
        TEST_CHECK(ed64_pro_rtc_init() == FLASHCART_ERR_INT);
        TEST_CHECK(ed64_pro_rtc_close() == FLASHCART_ERR_INT);
        TEST_CHECK(mock.begins == begins && mock.starts == starts && ed64_pro_rtc_get_error_message() == error);
    }
}
static void test_close_order (void) {
    fixture(); TEST_ASSERT(ed64_pro_rtc_init() == FLASHCART_OK);
    mock.event_count = 0; mock.events[0] = 0;
    TEST_CHECK(ed64_pro_rtc_close() == FLASHCART_OK);
    TEST_CHECK(mock.events[0] == 'X');
    TEST_CHECK(strstr(mock.events, "TDC") != NULL);
    TEST_CHECK(!mock.native_initialized && !mock.started && mock.owner == NULL);
    unsigned stops = mock.stops;
    TEST_CHECK(ed64_pro_rtc_close() == FLASHCART_OK && mock.stops == stops);
}
static void test_close_faults (void) {
    for (unsigned failure = 0; failure < 5; failure++) {
        fixture(); TEST_ASSERT(ed64_pro_rtc_init() == FLASHCART_OK);
        switch (failure) {
        case 0: mock.begin_error = ED64_PRO_LL_TIMEOUT; break;
        case 1: mock.stop_error = ED64_PRO_LL_TIMEOUT; break;
        case 2: mock.drain_error = ED64_PRO_LL_TIMEOUT; break;
        case 3: mock.status_error = ED64_PRO_LL_FRAME; break;
        case 4: mock.end_error = ED64_PRO_LL_FRAME; break;
        }
        TEST_CHECK(ed64_pro_rtc_close() != FLASHCART_OK);
        TEST_CHECK(!mock.native_initialized && mock.owner == NULL);
        TEST_CHECK(ed64_pro_rtc_get_error_message() != NULL);
        unsigned stops = mock.stops, begins = mock.begins;
        TEST_CHECK(ed64_pro_rtc_close() == FLASHCART_ERR_INT);
        TEST_CHECK(ed64_pro_rtc_init() == FLASHCART_ERR_INT);
        TEST_CHECK(mock.stops == stops && mock.begins == begins); /* Never retry an ambiguous shutdown. */
    }
}
TEST_LIST = {
    {"native RTC starts only compatible services before native detection", test_native_services},
    {"native source and availability failures stop services and close RTC", test_native_gate},
    {"genuine invalid-calendar RTC keeps native source and error", test_invalid_native_calendar},
    {"configuration and start failures do not publish RTC", test_init_faults},
    {"RTC closes before STOP and shutdown is idempotent", test_close_order},
    {"failed shutdown clears native state without retrying writes", test_close_faults},
    {NULL, NULL},
};
