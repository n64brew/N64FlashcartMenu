#ifndef FLASHCART_ED64_PRO_RTC_H__
#define FLASHCART_ED64_PRO_RTC_H__

#include <libdragon.h>
#include "../flashcart.h"

/** @brief Install POSIX clock hooks and refresh the cached hardware time. */
flashcart_err_t ed64_pro_rtc_resync (void);
/** @brief Remove this backend's hooks and invalidate its clock cache. */
void ed64_pro_rtc_close (void);
/** @brief Return retained RTC error text without performing cartridge I/O. */
const char *ed64_pro_rtc_get_error_message (void);

#endif
