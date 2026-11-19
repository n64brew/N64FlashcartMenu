/**
 * @file 64drive.c
 * @brief 64drive functions implementation
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
#include "64drive_ll.h"
#include "64drive.h"

#define ROM_ADDRESS                 (0x10000000)
#define SAVE_ADDRESS_DEV_A          (0x13FE0000)
#define SAVE_ADDRESS_DEV_A_PKST2    (0x11606560)
#define SAVE_ADDRESS_DEV_B          (0x1FFC0000)

#define SUPPORTED_FPGA_REVISION     (205)

static d64_device_variant_t device_variant = DEVICE_VARIANT_UNKNOWN;
static d64_save_type_t current_save_type = SAVE_TYPE_NONE;

/**
 * @brief Initialize the 64drive.
 * 
 * @return flashcart_err_t Error code.
 */
static flashcart_err_t d64_init (void) {
    uint16_t fpga_revision;
    uint32_t bootloader_version;

    if (d64_ll_enable_extended_mode(false)) {
        return FLASHCART_ERR_INT;
    }

    if (d64_ll_get_version(&device_variant, &fpga_revision, &bootloader_version)) {
        return FLASHCART_ERR_INT;
    }

    if (fpga_revision < SUPPORTED_FPGA_REVISION) {
        return FLASHCART_ERR_OUTDATED;
    }

    if (d64_ll_enable_save_writeback(false)) {
        return FLASHCART_ERR_INT;
    }

    if (d64_ll_set_save_type(SAVE_TYPE_NONE)) {
        return FLASHCART_ERR_INT;
    }

    current_save_type = SAVE_TYPE_NONE;

    if (d64_ll_enable_cartrom_writes(true)) {
        return FLASHCART_ERR_INT;
    }

    if (d64_ll_set_persistent_variable_storage(false, 0, 0)) {
        return FLASHCART_ERR_INT;
    }

    return FLASHCART_OK;
}

/**
 * @brief Deinitialize the 64drive.
 * 
 * @return flashcart_err_t Error code.
 */
static flashcart_err_t d64_deinit (void) {
    if (d64_ll_enable_cartrom_writes(false)) {
        return FLASHCART_ERR_INT;
    }

    return FLASHCART_OK;
}

/**
 * @brief Check if the 64drive has a specific feature.
 * 
 * @param feature The feature to check.
 * @return true if the feature is supported, false otherwise.
 */
static bool d64_has_feature (flashcart_features_t feature) {
    switch (feature) {
        case FLASHCART_FEATURE_64DD: return false;
        case FLASHCART_FEATURE_RTC: return true;
        case FLASHCART_FEATURE_USB: return true;
        case FLASHCART_FEATURE_AUTO_CIC: return true;
        case FLASHCART_FEATURE_AUTO_REGION: return true;
        case FLASHCART_FEATURE_SAVE_WRITEBACK: return true;
        case FLASHCART_FEATURE_ROM_REBOOT_FAST: return true;
        default: return false;
    }
}

/**
 * @brief Retrieves the firmware version of the 64drive device.
 *
 * The firmware version is returned as a flashcart_firmware_version_t structure, with each field
 * including the major, minor, and revision numbers.
 * The major version is set to 1 for 64drive variant A, and 2 for 64drive variant B.
 *
 * @return A flashcart_firmware_version_t structure containing the firmware version information.
 */
static flashcart_firmware_version_t d64_get_firmware_version (void) {
    flashcart_firmware_version_t version_info;

    d64_ll_get_version(&device_variant, &version_info.minor, &version_info.revision);

    if (device_variant == DEVICE_VARIANT_A) {
        version_info.major = 1;
    } else if (device_variant == DEVICE_VARIANT_B) {
        version_info.major = 2;
    } else {
        version_info.major = 0;
    }

    return version_info;
}

/**
 * @brief Load a ROM into the 64drive.
 * 
 * @param rom_path Path to the ROM file.
 * @param progress Progress callback function.
 * @return flashcart_err_t Error code.
 */
static flashcart_err_t d64_load_rom (char *rom_path, flashcart_progress_callback_t *progress) {
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

    size_t sdram_size = MiB(64);

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
    if (f_tell(&fil) != rom_size) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    if (f_close(&fil) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    return FLASHCART_OK;
}

/**
 * @brief Load a file into the 64Drive.
 * 
 * @param file_path Path to the file.
 * @param rom_offset ROM offset.
 * @param file_offset File offset.
 * @return flashcart_err_t Error code.
 */
static flashcart_err_t d64_load_file (char *file_path, uint32_t rom_offset, uint32_t file_offset) {
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

/**
 * @brief Load a save file into the 64drive.
 * 
 * @param save_path Path to the save file.
 * @return flashcart_err_t Error code.
 */
static flashcart_err_t d64_load_save (char *save_path) {
    uint8_t eeprom_contents[2048] __attribute__((aligned(8)));
    FIL fil;
    UINT br;

    if (f_open(&fil, strip_fs_prefix(save_path), FA_READ) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    size_t save_size = f_size(&fil);

    bool is_eeprom_save = (current_save_type == SAVE_TYPE_EEPROM_4KBIT || current_save_type == SAVE_TYPE_EEPROM_16KBIT);

    void *address = (void *) (SAVE_ADDRESS_DEV_B);
    if (is_eeprom_save) {
        address = eeprom_contents;
    } else if (device_variant == DEVICE_VARIANT_A) {
        address = (void *) (SAVE_ADDRESS_DEV_A);
        if (current_save_type == SAVE_TYPE_FLASHRAM_PKST2) {
            address = (void *) (SAVE_ADDRESS_DEV_A_PKST2);
        }
    }

    if (f_read(&fil, address, save_size, &br) != FR_OK) {
        f_close(&fil);
        return FLASHCART_ERR_LOAD;
    }

    if (is_eeprom_save) {
        if (d64_ll_write_eeprom_contents(eeprom_contents)) {
            f_close(&fil);
            return FLASHCART_ERR_LOAD;
        }
    }

    if (f_close(&fil) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }

    if (br != save_size) {
        return FLASHCART_ERR_LOAD;
    }

    return FLASHCART_OK;
}

/**
 * @brief Set the save type for the 64drive.
 * 
 * @param save_type The save type.
 * @return flashcart_err_t Error code.
 */
static flashcart_err_t d64_set_save_type (flashcart_save_type_t save_type) {
    d64_save_type_t type;

    switch (save_type) {
        case FLASHCART_SAVE_TYPE_NONE:
            type = SAVE_TYPE_NONE;
            break;
        case FLASHCART_SAVE_TYPE_EEPROM_4KBIT:
            type = SAVE_TYPE_EEPROM_4KBIT;
            break;
        case FLASHCART_SAVE_TYPE_EEPROM_16KBIT:
            type = SAVE_TYPE_EEPROM_16KBIT;
            break;
        case FLASHCART_SAVE_TYPE_SRAM_256KBIT:
            type = SAVE_TYPE_SRAM_256KBIT;
            break;
        case FLASHCART_SAVE_TYPE_SRAM_BANKED:
            type = SAVE_TYPE_SRAM_BANKED;
            break;
        case FLASHCART_SAVE_TYPE_SRAM_1MBIT:
            // NOTE: 64drive doesn't support 128 kiB SRAM save type, fallback to 32 kiB SRAM save type
            type = SAVE_TYPE_SRAM_256KBIT;
            break;
        case FLASHCART_SAVE_TYPE_FLASHRAM_1MBIT:
            type = SAVE_TYPE_FLASHRAM_1MBIT;
            break;
        case FLASHCART_SAVE_TYPE_FLASHRAM_PKST2:
            type = (device_variant == DEVICE_VARIANT_A) ? SAVE_TYPE_FLASHRAM_PKST2 : SAVE_TYPE_FLASHRAM_1MBIT;
            break;
        default:
            return FLASHCART_ERR_ARGS;
    }

    if (d64_ll_enable_save_writeback(false)) {
        return FLASHCART_ERR_INT;
    }

    if (d64_ll_set_save_type(type)) {
        return FLASHCART_ERR_INT;
    }

    current_save_type = type;

    return FLASHCART_OK;
}

/**
 * @brief Set the save writeback for the 64drive.
 * 
 * @param save_path Path to the save file.
 * @return flashcart_err_t Error code.
 */
static flashcart_err_t d64_set_save_writeback (char *save_path) {
    uint32_t sectors[SAVE_WRITEBACK_MAX_SECTORS] __attribute__((aligned(8)));

    if (fatfs_get_file_sectors(save_path, sectors, ADDRESS_TYPE_MEM, SAVE_WRITEBACK_MAX_SECTORS)) {
        return FLASHCART_ERR_LOAD;
    }

    if (d64_ll_write_save_writeback_lba_list(sectors)) {
        return FLASHCART_ERR_INT;
    }

    if (d64_ll_enable_save_writeback(true)) {
        return FLASHCART_ERR_INT;
    }

    return FLASHCART_OK;
}

static flashcart_err_t d64_set_bootmode (flashcart_reboot_mode_t boot_mode) {

    if (d64_ll_set_persistent_variable_storage(true, 0, 0)) {
        return FLASHCART_ERR_INT;
    }

    return FLASHCART_OK;
}


/** @brief Flashcart structure for 64drive. */
static flashcart_t flashcart_d64 = {
    .init = d64_init,
    .deinit = d64_deinit,
    .has_feature = d64_has_feature,
    .get_firmware_version = d64_get_firmware_version,
    .load_rom = d64_load_rom,
    .load_file = d64_load_file,
    .load_save = d64_load_save,
    .load_64dd_ipl = NULL,
    .load_64dd_disk = NULL,
    .load_64dd_disks = NULL,
    .get_button_state = NULL,
    .get_input_voltage = NULL,
    .set_save_type = d64_set_save_type,
    .set_save_writeback = d64_set_save_writeback,
    .set_next_boot_mode = d64_set_bootmode,
};

/**
 * @brief Get the flashcart structure for 64drive.
 * 
 * @return flashcart_t* Pointer to the flashcart structure.
 */
flashcart_t *d64_get_flashcart (void) {
    return &flashcart_d64;
}
