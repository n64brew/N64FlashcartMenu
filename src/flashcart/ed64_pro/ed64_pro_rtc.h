#ifndef FLASHCART_ED64_PRO_RTC_H__
#define FLASHCART_ED64_PRO_RTC_H__

#include "ed64_pro.h"

/** @brief Start RTC-only OFF/zero/STD services, then initialize native libdragon RTC. */
flashcart_err_t ed64_pro_rtc_init (void);
/** @brief Close native clock state and stop menu services before storage release. */
flashcart_err_t ed64_pro_rtc_close (void);
/** @brief Return retained RTC error text without performing cartridge I/O. */
const char *ed64_pro_rtc_get_error_message (void);

#endif
