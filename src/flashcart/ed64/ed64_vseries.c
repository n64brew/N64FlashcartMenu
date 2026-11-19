#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fatfs/ff.h>
#include <libdragon.h>

#include "utils/fs.h"
#include "utils/utils.h"

#include "../flashcart_utils.h"
#include "ed64_vseries_ll.h"
#include "ed64_vseries.h"

typedef enum {
    ED64_V1_0 = 110,
    ED64_V2_0 = 320,
    ED64_V2_5 = 325,
    ED64_V3_0 = 330,
} ed64_vseries_device_variant_t;

/* ED64 save location base address  */
//#define SRAM_ADDRESS (0xA8000000)
/* ED64 ROM location base address  */
#define ROM_ADDRESS  (0xB0000000)

static flashcart_firmware_version_t ed64_vseries_get_firmware_version (void) {
    flashcart_firmware_version_t version_info;
    // FIXME: get version from ll
    version_info.major = 1;
    version_info.minor = 1;
    version_info.revision = 0;

    //ed64_ll_get_version(&version_info.major, &version_info.minor, &version_info.revision);

    return version_info;
}

static flashcart_err_t ed64_vseries_init (void) {
    return FLASHCART_OK;
}

static flashcart_err_t ed64_vseries_deinit (void) {
    return FLASHCART_OK;
}

static ed64_vseries_device_variant_t get_cart_model() {
    ed64_vseries_device_variant_t variant = ED64_V1_0; // FIXME: check cart model from ll for better feature handling.
    return variant;
}

static bool ed64_vseries_has_feature (flashcart_features_t feature) {
    bool is_model_v3 = (get_cart_model() == ED64_V3_0); 
    switch (feature) {
        case FLASHCART_FEATURE_RTC: return is_model_v3 ? true : false;
        case FLASHCART_FEATURE_USB: return is_model_v3 ? true : false;
        case FLASHCART_FEATURE_AUTO_CIC: return is_model_v3 ? true : false;
        default: return false;
    }
}

static flashcart_err_t ed64_vseries_load_rom (char *rom_path, flashcart_progress_callback_t *progress) {
    FIL fil;
    UINT br;

    if (f_open(&fil, strip_fs_prefix(rom_path), FA_READ) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    fatfs_fix_file_size(&fil);

    size_t rom_size = f_size(&fil);

    if (rom_size > MiB(64)) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    size_t sdram_size = rom_size; // (MiB(64) - KiB(128));

    size_t chunk_size = KiB(128);
    for (unsigned int offset = 0; offset < sdram_size; offset += chunk_size) {
        size_t block_size = MIN(sdram_size - offset, chunk_size);
        if (f_read(&fil, (void *) (ROM_ADDRESS + offset), block_size, &br) != FR_OK) {
            f_close(&fil);
            return FLASHCART_ERR_LOAD;
        }
        if (progress) {
            progress(f_tell(&fil) / (float) (f_size(&fil)));
        }
    }
    if (f_tell(&fil) != sdram_size) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    if (f_close(&fil) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    return FLASHCART_OK;
}

static flashcart_err_t ed64_vseries_load_file (char *file_path, uint32_t rom_offset, uint32_t file_offset) {
    FIL fil;
    UINT br;

    if (f_open(&fil, strip_fs_prefix(file_path), FA_READ) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    fatfs_fix_file_size(&fil);

    size_t file_size = f_size(&fil) - file_offset;

    if (file_size > (MiB(64) - rom_offset)) {
        f_close(&fil);
        return FLASHCART_ERR_ARGS;
    }

    if (f_lseek(&fil, file_offset) != FR_OK) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    if (f_read(&fil, (void *) (ROM_ADDRESS + rom_offset), file_size, &br) != FR_OK) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }
    if (br != file_size) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    if (f_close(&fil) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    return FLASHCART_OK;
}

static flashcart_err_t ed64_vseries_load_save (char *save_path) {
    // FIXME: the savetype will be none.
    return FLASHCART_OK;
}

static flashcart_err_t ed64_vseries_set_save_type (flashcart_save_type_t save_type) {
    // FIXME: the savetype will be none.
    return FLASHCART_OK;
}

static flashcart_t flashcart_ed64_vseries = {
    .init = ed64_vseries_init,
    .deinit = ed64_vseries_deinit,
    .has_feature = ed64_vseries_has_feature,
    .get_firmware_version = ed64_vseries_get_firmware_version,
    .load_rom = ed64_vseries_load_rom,
    .load_file = ed64_vseries_load_file,
    .load_save = ed64_vseries_load_save,
    .load_64dd_ipl = NULL,
    .load_64dd_disk = NULL,
    .load_64dd_disks = NULL,
    .get_button_state = NULL,
    .get_input_voltage = NULL,
    .set_save_type = ed64_vseries_set_save_type,
    .set_save_writeback = NULL,
    .set_next_boot_mode = NULL,
};


flashcart_t *ed64_vseries_get_flashcart (void) {
    return &flashcart_ed64_vseries;
}
