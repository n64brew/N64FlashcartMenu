#include <acutest.h>
#include <string.h>
#include <time.h>

#include "../../src/flashcart/ed64_pro/ed64_pro_rtc.c"

static struct {
    ed64_pro_diagnostics_t diagnostics;
    rtc_hooks_t hooks;
    uint64_t ticks;
    uint64_t operation_ticks;
    uint8_t raw[ED64_PRO_RTC_SIZE];
    uint8_t written[ED64_PRO_RTC_SIZE];
    ed64_pro_ll_transaction_t *owner;
    uint32_t deadline;
    ed64_pro_ll_error_t begin_error, get_error, set_error, end_error;
    unsigned begins, gets, sets, ends, hooks_installed, hooks_removed;
    bool echo_set;
    bool hook_failed;
} mock;

const ed64_pro_diagnostics_t *ed64_pro_get_diagnostics (void) {
    return &mock.diagnostics;
}

uint64_t get_ticks (void) {
    return mock.ticks;
}

uint32_t mock_ticks_read (void) {
    return mock.ticks;
}

int hook_rtc_calls (rtc_hooks_t *hooks) {
    mock.hooks_installed++;
    if (mock.hook_failed) {
        return -1;
    }
    mock.hooks = *hooks;
    return 0;
}

int unhook_rtc_calls (rtc_hooks_t *hooks) {
    mock.hooks_removed++;
    if (hooks->gettime == mock.hooks.gettime) {
        mock.hooks.gettime = NULL;
    }
    if (hooks->settime == mock.hooks.settime) {
        mock.hooks.settime = NULL;
    }
    return 0;
}

ed64_pro_ll_error_t ed64_pro_ll_begin (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    mock.begins++;
    TEST_CHECK(mock.owner == NULL);
    TEST_CHECK(deadline == (uint32_t)mock.ticks + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS));
    if (mock.begin_error == ED64_PRO_LL_OK) {
        mock.owner = transaction;
        mock.deadline = deadline;
    }
    return mock.begin_error;
}

ed64_pro_ll_error_t ed64_pro_ll_end (ed64_pro_ll_transaction_t *transaction) {
    mock.ends++;
    TEST_CHECK(mock.owner == transaction);
    mock.owner = NULL;
    return mock.end_error;
}

ed64_pro_ll_error_t ed64_pro_ll_rtc_get (
    ed64_pro_ll_transaction_t *transaction, uint8_t raw[ED64_PRO_RTC_SIZE], uint32_t deadline
) {
    mock.gets++;
    TEST_CHECK(mock.owner == transaction && deadline == mock.deadline);
    mock.ticks += mock.operation_ticks;
    if (mock.get_error == ED64_PRO_LL_OK) {
        memcpy(raw, mock.raw, ED64_PRO_RTC_SIZE);
    }
    return mock.get_error;
}

ed64_pro_ll_error_t ed64_pro_ll_rtc_set (
    ed64_pro_ll_transaction_t *transaction, const uint8_t raw[ED64_PRO_RTC_SIZE], uint32_t deadline
) {
    mock.sets++;
    TEST_CHECK(mock.owner == transaction && deadline == mock.deadline);
    mock.ticks += mock.operation_ticks;
    memcpy(mock.written, raw, ED64_PRO_RTC_SIZE);
    if (mock.echo_set && mock.set_error == ED64_PRO_LL_OK) {
        memcpy(mock.raw, raw, ED64_PRO_RTC_SIZE);
    }
    return mock.set_error;
}

static void fixture (void) {
    ed64_pro_rtc_close();
    memset(&mock, 0, sizeof(mock));
    mock.diagnostics.state = ED64_PRO_READY;
    mock.diagnostics.storage_state = ED64_PRO_STORAGE_FATFS_OWNED;
    mock.ticks = UINT64_C(0x1FFFFFFF0);
    const uint8_t initial[] = {0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x07, 0xA5};
    memcpy(mock.raw, initial, sizeof(initial));
}

static void sync_clock (void) {
    TEST_ASSERT(ed64_pro_rtc_resync() == FLASHCART_OK);
    TEST_ASSERT(mock.hooks.gettime != NULL && mock.hooks.settime != NULL);
}

static void test_calendar (void) {
    fixture();
    const time_t minimum = INT64_C(946684800), maximum = INT64_C(4102444799);
    for (time_t timestamp = minimum; timestamp <= maximum; timestamp += 86400) {
        struct tm expected = *gmtime(&timestamp);
        uint8_t raw[ED64_PRO_RTC_SIZE];
        TEST_ASSERT(pro_rtc_encode(timestamp + 86399, raw));
        unsigned fields[] = {
            expected.tm_year - 100, expected.tm_mon + 1, expected.tm_mday,
            23, 59, 59, expected.tm_wday + 1,
        };
        for (unsigned i = 0; i < 7; i++) {
            TEST_CHECK(raw[i] == ((fields[i] / 10) << 4 | fields[i] % 10));
        }
        TEST_CHECK(raw[7] == 0);
        raw[7] = 0xFF;
        time_t decoded = -1;
        TEST_CHECK(pro_rtc_decode(raw, &decoded));
        TEST_CHECK(decoded == timestamp + 86399);
    }
    uint8_t raw[ED64_PRO_RTC_SIZE];
    TEST_CHECK(!pro_rtc_encode(minimum - 1, raw));
    TEST_CHECK(!pro_rtc_encode(maximum + 1, raw));
    TEST_CHECK(!pro_rtc_encode(-1, raw));
}

static void test_invalid_calendar (void) {
    fixture();
    const uint8_t invalid[][8] = {
        {0x01, 0x02, 0x29, 0, 0, 0, 0x05, 0},
        {0x00, 0x02, 0x30, 0, 0, 0, 0x04, 0},
        {0x00, 0x04, 0x31, 0, 0, 0, 0x02, 0},
        {0x00, 0x00, 0x01, 0, 0, 0, 0x07, 0},
        {0x00, 0x13, 0x01, 0, 0, 0, 0x07, 0},
        {0x00, 0x01, 0x00, 0, 0, 0, 0x07, 0},
        {0x00, 0x01, 0x01, 0x24, 0, 0, 0x07, 0},
        {0x00, 0x01, 0x01, 0, 0x60, 0, 0x07, 0},
        {0x00, 0x01, 0x01, 0, 0, 0x60, 0x07, 0},
        {0x00, 0x01, 0x01, 0, 0, 0, 0x00, 0},
        {0x00, 0x01, 0x01, 0, 0, 0, 0x08, 0},
        {0x00, 0x01, 0x01, 0, 0, 0, 0x01, 0},
    };
    time_t decoded = 123;
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        TEST_CHECK(!pro_rtc_decode(invalid[i], &decoded));
        TEST_CHECK(decoded == 123);
    }
    for (unsigned i = 0; i < 7; i++) {
        uint8_t raw[ED64_PRO_RTC_SIZE];
        memcpy(raw, mock.raw, sizeof(raw));
        raw[i] = 0x1A;
        TEST_CHECK(!pro_rtc_decode(raw, &decoded));
        raw[i] = 0xA1;
        TEST_CHECK(!pro_rtc_decode(raw, &decoded));
    }
}

static void test_cached_clock (void) {
    fixture();
    sync_clock();
    TEST_CHECK(ed64_pro_rtc_get_error_message() == NULL);
    time_t timestamp = -1;
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_ESUCCESS);
    TEST_CHECK(timestamp == PRO_RTC_MIN);
    mock.ticks += (uint64_t)TICKS_PER_SECOND * 86401 + TICKS_PER_SECOND / 2;
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_ESUCCESS);
    TEST_CHECK(timestamp == PRO_RTC_MIN + 86401);
    TEST_CHECK(mock.begins == 1 && mock.gets == 1 && mock.sets == 0 && mock.ends == 1);
    TEST_ASSERT(pro_rtc_encode(INT64_C(1709251199), mock.raw));
    sync_clock();
    TEST_CHECK(mock.hooks_installed == 1 && mock.gets == 2);
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_ESUCCESS && timestamp == INT64_C(1709251199));
    mock.ticks += TICKS_PER_SECOND;
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_ESUCCESS && timestamp == INT64_C(1709251200));
}

static void test_cache_range (void) {
    fixture();
    TEST_ASSERT(pro_rtc_encode(PRO_RTC_MAX, mock.raw));
    sync_clock();
    time_t timestamp = -1;
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_ESUCCESS && timestamp == PRO_RTC_MAX);
    mock.ticks += TICKS_PER_SECOND;
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_EBADTIME && timestamp == PRO_RTC_MAX);
    TEST_CHECK(ed64_pro_rtc_get_error_message() != NULL && mock.gets == 1);
}

static void test_writes (void) {
    fixture();
    sync_clock();
    mock.echo_set = true;
    mock.operation_ticks = TICKS_PER_SECOND / 3;
    TEST_CHECK(mock.hooks.settime(INT64_C(1709251199)) == RTC_ESUCCESS);
    const uint8_t expected[] = {0x24, 0x02, 0x29, 0x23, 0x59, 0x59, 0x05, 0};
    TEST_CHECK(memcmp(mock.written, expected, sizeof(expected)) == 0);
    TEST_CHECK(mock.begins == 2 && mock.gets == 2 && mock.sets == 1 && mock.ends == 2);
    time_t timestamp = -1;
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_ESUCCESS && timestamp == INT64_C(1709251199));
    mock.ticks += TICKS_PER_SECOND;
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_ESUCCESS && timestamp == INT64_C(1709251200));
    TEST_CHECK(mock.hooks.settime(PRO_RTC_MIN - 1) == RTC_EBADTIME);
    TEST_CHECK(mock.hooks.settime(PRO_RTC_MAX + 1) == RTC_EBADTIME);
    TEST_CHECK(mock.sets == 1);
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_ESUCCESS && timestamp == INT64_C(1709251200));
}

static void test_elapsed_readback (void) {
    for (int advance = -1; advance <= 4; advance++) {
        fixture();
        sync_clock();
        mock.operation_ticks = TICKS_PER_SECOND;
        TEST_ASSERT(pro_rtc_encode(INT64_C(1709251199) + advance, mock.raw));
        int expected = advance >= 0 && advance <= 3 ? RTC_ESUCCESS : RTC_EBADCLOCK;
        TEST_CHECK(mock.hooks.settime(INT64_C(1709251199)) == expected);
        time_t timestamp = -1;
        TEST_CHECK(mock.hooks.gettime(&timestamp) == expected);
        TEST_CHECK(expected != RTC_ESUCCESS || timestamp == INT64_C(1709251199) + advance);
        TEST_CHECK(mock.sets == 1 && mock.gets == 2 && mock.ends == 2);
    }
}

static void test_resync_failures (void) {
    for (unsigned failure = 0; failure < 5; failure++) {
        fixture();
        sync_clock();
        switch (failure) {
            case 0: mock.begin_error = ED64_PRO_LL_TIMEOUT; break;
            case 1: mock.get_error = ED64_PRO_LL_TIMEOUT; break;
            case 2: mock.end_error = ED64_PRO_LL_FRAME; break;
            case 3: mock.raw[1] = 0x13; break;
            case 4: mock.diagnostics.storage_failed = true; break;
        }
        TEST_CHECK(ed64_pro_rtc_resync() == FLASHCART_ERR_INT);
        time_t timestamp = 123;
        int expected = failure == 3 ? RTC_EBADTIME : failure == 4 ? RTC_ENOCLOCK : RTC_EBADCLOCK;
        TEST_CHECK(mock.hooks.gettime(&timestamp) == expected && timestamp == 123);
        TEST_CHECK(ed64_pro_rtc_get_error_message() != NULL);
        TEST_CHECK(mock.owner == NULL);
    }
}

static void test_write_failures (void) {
    for (unsigned failure = 0; failure < 6; failure++) {
        fixture();
        sync_clock();
        switch (failure) {
            case 0: mock.begin_error = ED64_PRO_LL_TIMEOUT; break;
            case 1: mock.set_error = ED64_PRO_LL_TIMEOUT; break;
            case 2: mock.get_error = ED64_PRO_LL_TIMEOUT; break;
            case 3: mock.end_error = ED64_PRO_LL_FRAME; break;
            case 4: mock.raw[2] = 0x32; break;
            case 5: mock.diagnostics.storage_state = ED64_PRO_STORAGE_RELEASING; break;
        }
        int expected = failure == 4 ? RTC_EBADTIME : failure == 5 ? RTC_ENOCLOCK : RTC_EBADCLOCK;
        TEST_CHECK(mock.hooks.settime(PRO_RTC_MIN) == expected);
        time_t timestamp = 123;
        TEST_CHECK(mock.hooks.gettime(&timestamp) == expected && timestamp == 123);
        TEST_CHECK(ed64_pro_rtc_get_error_message() != NULL);
        TEST_CHECK(mock.sets == (failure == 0 || failure == 5 ? 0u : 1u));
        TEST_CHECK(mock.gets == (failure == 0 || failure == 1 || failure == 5 ? 1u : 2u));
        TEST_CHECK(mock.owner == NULL);
    }
}

static void test_invalid_repair (void) {
    fixture();
    mock.raw[1] = 0;
    TEST_CHECK(ed64_pro_rtc_resync() == FLASHCART_ERR_INT);
    TEST_ASSERT(mock.hooks.gettime && mock.hooks.settime);
    time_t timestamp = 123;
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_EBADTIME && timestamp == 123);
    mock.echo_set = true;
    TEST_CHECK(mock.hooks.settime(INT64_C(1794844800)) == RTC_ESUCCESS);
    TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_ESUCCESS && timestamp == INT64_C(1794844800));
    TEST_CHECK(ed64_pro_rtc_get_error_message() == NULL);
}

static void test_ownership (void) {
    for (unsigned failure = 0; failure < 4; failure++) {
        fixture();
        switch (failure) {
            case 0: mock.diagnostics.state = ED64_PRO_COLD; break;
            case 1: mock.diagnostics.state = ED64_PRO_FAILED; break;
            case 2: mock.diagnostics.storage_failed = true; break;
            case 3: mock.diagnostics.storage_state = ED64_PRO_STORAGE_MCU_OWNED; break;
        }
        TEST_CHECK(ed64_pro_rtc_resync() == FLASHCART_ERR_INT);
        TEST_ASSERT(mock.hooks.gettime && mock.hooks.settime);
        time_t timestamp = 123;
        TEST_CHECK(mock.hooks.gettime(&timestamp) == RTC_ENOCLOCK && timestamp == 123);
        TEST_CHECK(mock.hooks.settime(PRO_RTC_MIN) == RTC_ENOCLOCK);
        TEST_CHECK(mock.begins == 0 && mock.gets == 0 && mock.sets == 0);
    }
}

static void test_write_range (void) {
    fixture();
    time_t minimum = 0, maximum = 0;
    TEST_CHECK(!ed64_pro_rtc_get_write_range(&minimum, &maximum));
    mock.raw[1] = 0;
    TEST_CHECK(ed64_pro_rtc_resync() == FLASHCART_ERR_INT);
    TEST_CHECK(ed64_pro_rtc_get_write_range(&minimum, &maximum));
    TEST_CHECK(minimum == PRO_RTC_MIN && maximum == PRO_RTC_MAX);
    TEST_CHECK(mock.gets == 1 && mock.sets == 0);
    mock.diagnostics.storage_state = ED64_PRO_STORAGE_MCU_OWNED;
    TEST_CHECK(!ed64_pro_rtc_get_write_range(&minimum, &maximum));
    ed64_pro_rtc_close();
    TEST_CHECK(!ed64_pro_rtc_get_write_range(&minimum, &maximum));
}

static void test_lifecycle (void) {
    fixture();
    mock.hook_failed = true;
    TEST_CHECK(ed64_pro_rtc_resync() == FLASHCART_ERR_INT && mock.begins == 0);
    TEST_CHECK(ed64_pro_rtc_get_error_message() != NULL);
    mock.hook_failed = false;
    sync_clock();
    rtc_hooks_t saved = mock.hooks;
    ed64_pro_rtc_close();
    TEST_CHECK(mock.hooks.gettime == NULL && mock.hooks.settime == NULL);
    time_t timestamp = 123;
    TEST_CHECK(saved.gettime(&timestamp) == RTC_ENOCLOCK && timestamp == 123);
    TEST_CHECK(mock.hooks_removed == 1 && ed64_pro_rtc_get_error_message() == NULL);
    ed64_pro_rtc_close();
    TEST_CHECK(mock.hooks_removed == 1);
    sync_clock();
    TEST_CHECK(mock.hooks_installed == 3 && mock.gets == 2);
}

TEST_LIST = {
    {"RTC Gregorian calendar roundtrip every day in 2000 through 2099", test_calendar},
    {"RTC rejects malformed BCD and impossible calendars", test_invalid_calendar},
    {"RTC cached time advances without MCU traffic and resyncs", test_cached_clock},
    {"RTC cache stops at supported year boundary", test_cache_range},
    {"RTC writes encoded UTC date and preserves cache on invalid arguments", test_writes},
    {"RTC SET readback allows only bounded elapsed seconds", test_elapsed_readback},
    {"RTC resync failures invalidate cache without fallback", test_resync_failures},
    {"RTC failed writes never retry or publish success", test_write_failures},
    {"RTC invalid hardware date can be repaired", test_invalid_repair},
    {"RTC hardware operations require menu ownership", test_ownership},
    {"RTC write range requires hooks and ownership but permits invalid dates", test_write_range},
    {"RTC hooks close and reinstall without stale cached time", test_lifecycle},
    {NULL, NULL},
};
