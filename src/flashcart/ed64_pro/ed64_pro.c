/**
 * @file ed64_pro.c
 * @brief ed64_pro functions implementation
 * @ingroup flashcart
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fatfs/ff.h>
#include <libdragon.h>

#include "utils/fs.h"
#include "utils/utils.h"

#include "../flashcart_utils.h"
#include "ed64_pro.h"

bool ed64_pro_detected() {
    // TODO: Implementation for detecting ed64_pro
    return false;
}

bool ed64_pro_init_sdfs(const char *storage_prefix, int max_files) {
    // TODO: Implementation for initializing sdfs on ed64_pro
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
