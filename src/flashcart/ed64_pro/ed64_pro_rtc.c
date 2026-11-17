#include <stdbool.h>
#include <stdint.h>
#include <libdragon.h>
#include <system.h>

#include "ed64_pro.h"
#include "ed64_pro_rtc.h"

#define PRO_RTC_MIN INT64_C(946684800)
#define PRO_RTC_MAX INT64_C(4102444799)

static time_t pro_rtc_time;
static uint64_t pro_rtc_ticks;
static int pro_rtc_result = RTC_ENOCLOCK;
static bool pro_rtc_hooked;
static const char *pro_rtc_error;

/* Within 2000 through 2099, every fourth year is a Gregorian leap year. */
static unsigned pro_rtc_month_days (unsigned year, unsigned month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return days[month - 1] + (month == 2 && year % 4 == 0);
}

static bool pro_rtc_decode (const uint8_t raw[ED64_PRO_RTC_SIZE], time_t *timestamp) {
    unsigned fields[7];
    for (unsigned i = 0; i < 7; i++) {
        if ((raw[i] & 15) > 9 || (raw[i] >> 4) > 9) {
            return false;
        }
        fields[i] = (raw[i] >> 4) * 10 + (raw[i] & 15);
    }
    unsigned year = 2000 + fields[0], month = fields[1], day = fields[2];
    if (month < 1 || month > 12 || day < 1 || day > pro_rtc_month_days(year, month) ||
        fields[3] > 23 || fields[4] > 59 || fields[5] > 59 || fields[6] < 1 || fields[6] > 7) {
        return false;
    }
    unsigned days = 0;
    for (unsigned y = 2000; y < year; y++) {
        days += 365 + (y % 4 == 0);
    }
    for (unsigned m = 1; m < month; m++) {
        days += pro_rtc_month_days(year, m);
    }
    days += day - 1;
    /* A contradictory weekday is not a coherent hardware calendar. */
    if (fields[6] != (days + 6) % 7 + 1) {
        return false;
    }
    *timestamp = PRO_RTC_MIN + (int64_t)days * 86400 + fields[3] * 3600 + fields[4] * 60 + fields[5];
    return true;
}

static bool pro_rtc_encode (time_t timestamp, uint8_t raw[ED64_PRO_RTC_SIZE]) {
    if (timestamp < PRO_RTC_MIN || timestamp > PRO_RTC_MAX) {
        return false;
    }
    uint64_t seconds = timestamp - PRO_RTC_MIN;
    unsigned days = seconds / 86400;
    unsigned weekday = (days + 6) % 7 + 1;
    unsigned year = 2000, month = 1;
    while (days >= 365 + (year % 4 == 0)) {
        days -= 365 + (year % 4 == 0);
        year++;
    }
    while (days >= pro_rtc_month_days(year, month)) {
        days -= pro_rtc_month_days(year, month++);
    }
    unsigned fields[] = {year - 2000, month, days + 1, seconds / 3600 % 24, seconds / 60 % 60, seconds % 60, weekday};
    for (unsigned i = 0; i < 7; i++) {
        raw[i] = (fields[i] / 10) << 4 | fields[i] % 10;
    }
    raw[7] = 0;
    return true;
}

static int pro_rtc_fail (int result, const char *message) {
    pro_rtc_result = result;
    pro_rtc_error = message;
    return result;
}

static bool pro_rtc_available (void) {
    const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
    return diagnostics->state == ED64_PRO_READY && !diagnostics->storage_failed &&
        diagnostics->storage_state == ED64_PRO_STORAGE_FATFS_OWNED;
}

static void pro_rtc_cache (time_t timestamp, uint64_t ticks) {
    pro_rtc_time = timestamp;
    pro_rtc_ticks = ticks;
    pro_rtc_result = RTC_ESUCCESS;
    pro_rtc_error = NULL;
}

static int pro_rtc_gettime (time_t *timestamp) {
    if (pro_rtc_result != RTC_ESUCCESS) {
        return pro_rtc_result;
    }
    uint64_t seconds = (get_ticks() - pro_rtc_ticks) / TICKS_PER_SECOND;
    if (seconds > (uint64_t)(PRO_RTC_MAX - pro_rtc_time)) {
        return pro_rtc_fail(RTC_EBADTIME, "EverDrive-64 Pro: RTC date exceeds 2099");
    }
    *timestamp = pro_rtc_time + seconds;
    return RTC_ESUCCESS;
}

static int pro_rtc_settime (time_t timestamp) {
    uint8_t raw[ED64_PRO_RTC_SIZE], readback[ED64_PRO_RTC_SIZE];
    if (!pro_rtc_encode(timestamp, raw)) {
        pro_rtc_error = "EverDrive-64 Pro: RTC date must be in 2000 through 2099";
        return RTC_EBADTIME;
    }
    if (!pro_rtc_available()) {
        return pro_rtc_fail(RTC_ENOCLOCK, "EverDrive-64 Pro: RTC transport is unavailable");
    }
    uint64_t start = get_ticks();
    uint32_t deadline = TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS);
    ed64_pro_ll_transaction_t transaction;
    ed64_pro_ll_error_t error = ed64_pro_ll_begin(&transaction, deadline);
    if (error == ED64_PRO_LL_OK) {
        error = ed64_pro_ll_rtc_set(&transaction, raw, deadline);
        /* SET has no acknowledgement. A following GET proves both completion
         * and the stored value without retrying an ambiguous write. */
        if (error == ED64_PRO_LL_OK) {
            error = ed64_pro_ll_rtc_get(&transaction, readback, deadline);
        }
        ed64_pro_ll_error_t end_error = ed64_pro_ll_end(&transaction);
        if (error == ED64_PRO_LL_OK) {
            error = end_error;
        }
    }
    if (error != ED64_PRO_LL_OK) {
        return pro_rtc_fail(RTC_EBADCLOCK, "EverDrive-64 Pro: RTC write or readback failed");
    }
    time_t confirmed;
    if (!pro_rtc_decode(readback, &confirmed)) {
        return pro_rtc_fail(RTC_EBADTIME, "EverDrive-64 Pro: RTC readback date is invalid");
    }
    uint64_t now = get_ticks();
    uint64_t elapsed = (now - start) / TICKS_PER_SECOND;
    if (confirmed < timestamp || (uint64_t)(confirmed - timestamp) > elapsed + 1) {
        return pro_rtc_fail(RTC_EBADCLOCK, "EverDrive-64 Pro: RTC readback did not match");
    }
    pro_rtc_cache(confirmed, now);
    return RTC_ESUCCESS;
}

static rtc_hooks_t pro_rtc_hooks = {.gettime = pro_rtc_gettime, .settime = pro_rtc_settime};

flashcart_err_t ed64_pro_rtc_resync (void) {
    if (!pro_rtc_hooked) {
        if (hook_rtc_calls(&pro_rtc_hooks) != 0) {
            pro_rtc_fail(RTC_EBADCLOCK, "EverDrive-64 Pro: RTC clock hooks could not be installed");
            return FLASHCART_ERR_INT;
        }
        pro_rtc_hooked = true;
    }
    if (!pro_rtc_available()) {
        pro_rtc_fail(RTC_ENOCLOCK, "EverDrive-64 Pro: RTC transport is unavailable");
        return FLASHCART_ERR_INT;
    }
    uint8_t raw[ED64_PRO_RTC_SIZE];
    uint32_t deadline = TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS);
    ed64_pro_ll_transaction_t transaction;
    ed64_pro_ll_error_t error = ed64_pro_ll_begin(&transaction, deadline);
    if (error == ED64_PRO_LL_OK) {
        error = ed64_pro_ll_rtc_get(&transaction, raw, deadline);
        ed64_pro_ll_error_t end_error = ed64_pro_ll_end(&transaction);
        if (error == ED64_PRO_LL_OK) {
            error = end_error;
        }
    }
    if (error != ED64_PRO_LL_OK) {
        pro_rtc_fail(RTC_EBADCLOCK, "EverDrive-64 Pro: RTC read failed");
        return FLASHCART_ERR_INT;
    }
    time_t timestamp;
    if (!pro_rtc_decode(raw, &timestamp)) {
        pro_rtc_fail(RTC_EBADTIME, "EverDrive-64 Pro: RTC hardware date is invalid");
        return FLASHCART_ERR_INT;
    }
    pro_rtc_cache(timestamp, get_ticks());
    return FLASHCART_OK;
}

void ed64_pro_rtc_close (void) {
    if (pro_rtc_hooked) {
        unhook_rtc_calls(&pro_rtc_hooks);
    }
    pro_rtc_hooked = false;
    pro_rtc_result = RTC_ENOCLOCK;
    pro_rtc_error = NULL;
}

const char *ed64_pro_rtc_get_error_message (void) {
    return pro_rtc_error;
}
