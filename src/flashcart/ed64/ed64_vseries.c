/**
 * @file ed64_vseries.c
 * @brief EverDrive-64 V-series (V1/V2/V2.5/V3) support for N64FlashcartMenu
 *
 * most V-series carts require pressing RESET after in-game saves. This driver mirrors
 * the official OS flow: load save into cart SRAM/EEPROM on boot, then write it
 * back to the SD card when the menu starts after a reset.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fatfs/ff.h>
#include <libdragon.h>

#include "utils/fs.h"
#include "utils/utils.h"

#include "../flashcart_utils.h"
#include "ed64_vseries_ll.h"
#include "ed64_vseries.h"
#include "ed64_pseudo_state.h"

#define ED64_STATE_FILE         "sd:/menu/ed64_state.ini"
#define ED64_SDRAM_SIZE         MiB(64)


typedef enum {
    ED64_V1_0 = 110,
    ED64_V2_0 = 320,
    ED64_V2_5 = 325,
    ED64_V3_0 = 330,
} ed64_vseries_device_variant_t;

static ed64_pseudo_writeback_t current_state;

static flashcart_firmware_version_t ed64_vseries_get_firmware_version (void) {
    // FIXME: get version from ll
    //ed64_vseries_ll_get_version(&version_info.major, &version_info.minor, &version_info.revision);
    flashcart_firmware_version_t version_info = {
        .major = 1,
        .minor = 1,
        .revision = 0,
    };
    return version_info;
}

static flashcart_err_t ed64_vseries_apply_save_type (flashcart_save_type_t save_type) {
    ed64_vseries_save_type_t type;

    switch (save_type) {
        case FLASHCART_SAVE_TYPE_NONE:
            type = SAVE_TYPE_NONE;
            break;
        case FLASHCART_SAVE_TYPE_EEPROM_4KBIT:
            type = SAVE_TYPE_EEPROM_4K;
            break;
        case FLASHCART_SAVE_TYPE_EEPROM_16KBIT:
            type = SAVE_TYPE_EEPROM_16K;
            break;
        case FLASHCART_SAVE_TYPE_SRAM_256KBIT:
            type = SAVE_TYPE_SRAM;
            break;
        case FLASHCART_SAVE_TYPE_SRAM_BANKED:
        case FLASHCART_SAVE_TYPE_SRAM_1MBIT:
            type = SAVE_TYPE_SRAM_128K;
            break;
        case FLASHCART_SAVE_TYPE_FLASHRAM_PKST2:
        case FLASHCART_SAVE_TYPE_FLASHRAM_1MBIT:
            type = SAVE_TYPE_FLASHRAM;
            break;
        default:
            return FLASHCART_ERR_ARGS;
    }

    ed64_vseries_ll_set_save_type(type);
    return FLASHCART_OK;
}

static flashcart_err_t ed64_writeback_save (void) {
    FIL fil;
    UINT bw;
    uint8_t cartsave_data[KiB(128)];

    if (!current_state.last_save_path || !file_exists(current_state.last_save_path)) {
        current_state.is_expecting_save_writeback = false;
        current_state.is_fram_save_type = false;
        current_state.save_type = FLASHCART_SAVE_TYPE_NONE;
        ed64_pseudo_state_save(&current_state);
        return FLASHCART_OK;
    }

    int save_size = (int) file_get_size(current_state.last_save_path);
    if (save_size <= 0 || save_size > (int) sizeof(cartsave_data)) {
        return FLASHCART_ERR_LOAD;
    }

    ed64_vseries_ll_set_sdcard_timing();
    if (ed64_vseries_apply_save_type(current_state.save_type) != FLASHCART_OK) {
        return FLASHCART_ERR_LOAD;
    }
    wait_ms(250);

    if (current_state.is_fram_save_type) {
        ed64_vseries_ll_get_fram(cartsave_data, save_size);
        current_state.is_fram_save_type = false;
    } else if (save_size > (int) KiB(2)) {
        ed64_vseries_ll_get_sram(cartsave_data, save_size);
    } else {
        ed64_vseries_ll_get_eeprom(cartsave_data, save_size);
    }

    if (f_open(&fil, strip_fs_prefix(current_state.last_save_path), FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    if (f_write(&fil, cartsave_data, save_size, &bw) != FR_OK || bw != (UINT) save_size) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    if (f_close(&fil) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    current_state.is_expecting_save_writeback = false;
    ed64_pseudo_state_save(&current_state);

    return FLASHCART_OK;
}

static flashcart_err_t ed64_vseries_init (void) {
    directory_create("sd:/menu");
    ed64_vseries_ll_set_sdcard_timing();

    ed64_pseudo_state_init(ED64_STATE_FILE);
    ed64_pseudo_state_load(&current_state);

    if (current_state.is_expecting_save_writeback) {
        flashcart_err_t err = ed64_writeback_save();
        if (err != FLASHCART_OK) {
            return err;
        }
    }

    return FLASHCART_OK;
}

static flashcart_err_t ed64_vseries_deinit (void) {
    ed64_pseudo_state_save(&current_state);
    ed64_pseudo_state_free(&current_state);
    return FLASHCART_OK;
}

static bool ed64_vseries_has_feature (flashcart_features_t feature) {
    switch (feature) {
        // case FLASHCART_FEATURE_RTC: return is_model_v3 ? true : false;
        // case FLASHCART_FEATURE_USB: return is_model_v3 ? true : false;
        // case FLASHCART_FEATURE_AUTO_CIC: return is_model_v3 ? true : false;
        case FLASHCART_FEATURE_SAVE_WRITEBACK:
            return true;
        default:
            return false;
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
    if (rom_size > ED64_SDRAM_SIZE) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    size_t chunk_size = KiB(128);
    for (size_t offset = 0; offset < rom_size; offset += chunk_size) {
        size_t block_size = MIN(rom_size - offset, chunk_size);
        if (f_read(&fil, (void *) (ROM_ADDRESS + offset), block_size, &br) != FR_OK) {
            f_close(&fil);
            return FLASHCART_ERR_LOAD;
        }
        if (progress) {
            progress(f_tell(&fil) / (float) f_size(&fil));
        }
    }

    if (f_tell(&fil) != rom_size) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    if (f_close(&fil) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    /* EverDrive V-series maps the full 64 MiB SDRAM window; pad unused tail. */
    for (size_t offset = rom_size; offset < ED64_SDRAM_SIZE; offset += chunk_size) {
        size_t block_size = MIN(ED64_SDRAM_SIZE - offset, chunk_size);
        memset((void *) (ROM_ADDRESS + offset), 0, block_size);
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
    if (file_size > (ED64_SDRAM_SIZE - rom_offset)) {
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
    FIL fil;
    UINT br;

    if (f_open(&fil, strip_fs_prefix(save_path), FA_READ) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    size_t save_size = f_size(&fil);
    if (save_size > KiB(128)) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    uint8_t cartsave_data[KiB(128)];
    if (f_read(&fil, cartsave_data, save_size, &br) != FR_OK) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }
    if (br != save_size) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }
    if (f_close(&fil) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    current_state.is_fram_save_type = false;

    ed64_vseries_save_type_t type = ed64_vseries_ll_get_save_type();
    switch (type) {
        case SAVE_TYPE_EEPROM_4K:
        case SAVE_TYPE_EEPROM_16K:
           ed64_vseries_ll_set_eeprom(cartsave_data, save_size);
            break;
        case SAVE_TYPE_SRAM:
        case SAVE_TYPE_SRAM_128K:
            ed64_vseries_ll_set_sram(cartsave_data, save_size);
            break;
        case SAVE_TYPE_FLASHRAM:
            ed64_vseries_ll_set_fram(cartsave_data, save_size);
            current_state.is_fram_save_type = true;
            break;
        default:
            break;
    }

    if (current_state.last_save_path) {
        free(current_state.last_save_path);
    }
    current_state.last_save_path = strdup(save_path);
    current_state.is_expecting_save_writeback = true;
    ed64_pseudo_state_save(&current_state);

    return FLASHCART_OK;
}

static flashcart_err_t ed64_vseries_set_save_type (flashcart_save_type_t save_type) {
    current_state.save_type = save_type;
    return ed64_vseries_apply_save_type(save_type);
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
    .get_voltage_temperature = NULL,
    .set_save_type = ed64_vseries_set_save_type,
    .set_save_writeback = NULL,
    .set_next_boot_mode = NULL,
};

flashcart_t *ed64_vseries_get_flashcart (void) {
    return &flashcart_ed64_vseries;
}
