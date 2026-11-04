/**
 * @file ed64_pro.c
 * @brief ed64_pro functions implementation
 * @ingroup flashcart
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libdragon.h>

#include "ed64_pro.h"

ed64_pro_probe_t ed64_pro_probe (void) {
    return ed64_pro_ll_probe(TICKS_READ() + TICKS_FROM_MS(ED64_PRO_ATTEMPT_TIMEOUT_MS));
}

bool ed64_pro_detected () {
    /* Production selection stays inactive until the hardware qualification gate. */
    return false;
}

bool ed64_pro_init_sdfs (const char *storage_prefix, int max_files) {
    (void)storage_prefix;
    (void)max_files;
    return false;
}

/** @brief Flashcart structure for ed64_pro. */
static flashcart_t flashcart_ed64_pro = {
    .init = NULL,
    .deinit = NULL,
    .has_feature = NULL,
    .get_firmware_version = NULL,
    .load_rom = NULL,
    .load_file = NULL,
    .load_save = NULL,
    .load_64dd_ipl = NULL,
    .load_64dd_disk = NULL,
    .load_64dd_disks = NULL,
    .get_button_state = NULL,
    .get_voltage_temperature = NULL,
    .set_save_type = NULL,
    .set_save_writeback = NULL,
    .set_next_boot_mode = NULL,
};

/**
 * @brief Get the flashcart structure for ed64_pro.
 *
 * @return flashcart_t* Pointer to the flashcart structure.
 */
flashcart_t *ed64_pro_get_flashcart (void) {
    return &flashcart_ed64_pro;
}
