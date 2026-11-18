#include <stdbool.h>
#include <libdragon.h>
#include <libcart/cart.h>

#include "ed64_pro_rtc.h"

static bool pro_rtc_running;
static bool pro_rtc_failed;
static const char *pro_rtc_error;

flashcart_err_t ed64_pro_rtc_init (void) {
    if (pro_rtc_failed) return FLASHCART_ERR_INT;
    if (pro_rtc_running) return FLASHCART_OK;
    pro_rtc_error = NULL;
    uint32_t deadline = TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS);
    /* No Game Pak, save RAM or DD service is needed while FatFs owns SD.
     * OFF/zero starts only STD RTC; cartridge asset reads are qualified
     * independently on both menu boot routes (see the Pro test report). */
    const ed64_pro_dev_cfg_t config = {
        .brom_type = ED64_PRO_ROM_OFF,
        .rtc_mode = ED64_PRO_RTC_STD,
    };
    ed64_pro_ll_error_t error = ed64_pro_ll_dev_set_cfg(&config, deadline);
    if (error == ED64_PRO_LL_OK) {
        error = ed64_pro_ll_dev_start(deadline);
        if (error == ED64_PRO_LL_OK) pro_rtc_running = true;
    }
    if (error != ED64_PRO_LL_OK) {
        if (!pro_rtc_error) pro_rtc_error = "EverDrive-64 Pro: RTC services could not be started";
        ed64_pro_rtc_close();
        pro_rtc_failed = true;
        return FLASHCART_ERR_INT;
    }
    /* Exclude overlapping legacy-cart PI probes. The actual Joybus service,
     * detection, POSIX hooks, and cache all belong to ordinary libdragon. */
    cart_type = CART_MAX;
    rtc_init();
    bool available = rtc_is_source_available(RTC_SOURCE_JOYBUS);
    if (available && rtc_get_source() != RTC_SOURCE_JOYBUS) {
        /* A present RTC with an invalid calendar makes rtc_init fall back to
         * software time. Resync the genuinely detected source so native POSIX
         * reads retain its error instead of displaying a plausible fallback. */
        rtc_set_source(RTC_SOURCE_JOYBUS);
    }
    /* Clock availability is optional. Keep services running for normal menu
     * use, and STOP them before launch even when native RTC detection fails. */
    return FLASHCART_OK;
}

flashcart_err_t ed64_pro_rtc_close (void) {
    rtc_close();
    if (pro_rtc_failed) return FLASHCART_ERR_INT;
    if (!pro_rtc_running) return FLASHCART_OK;
    /* Stop before unmount/FS INIT. Never retry a failed STOP; retain the first
     * transport fault and clear native source/cache even on failure. */
    pro_rtc_running = false;
    uint32_t deadline = TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS);
    ed64_pro_ll_error_t error = ed64_pro_ll_stop(deadline);
    size_t drained;
    if (error == ED64_PRO_LL_OK) error = ed64_pro_ll_drain(&drained, deadline);
    ed64_pro_status_t status;
    if (error == ED64_PRO_LL_OK) error = ed64_pro_ll_check_status(&status, deadline);
    if (error != ED64_PRO_LL_OK) {
        if (!pro_rtc_error) pro_rtc_error = "EverDrive-64 Pro: RTC services could not be stopped";
        pro_rtc_failed = true;
        return FLASHCART_ERR_INT;
    }
    return FLASHCART_OK;
}

const char *ed64_pro_rtc_get_error_message (void) { return pro_rtc_error; }
