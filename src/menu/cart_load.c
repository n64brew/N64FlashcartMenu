/**
 * @file cart_load.c
 * @brief Cart loading functions
 * @ingroup menu
 */

#include <string.h>
#include <libdragon.h>
#include "cart_load.h"
#include "ini_parser.h"
#include "path.h"
#include "utils/fs.h"
#include "utils/utils.h"

#ifndef DDIPL_LOCATION
#define DDIPL_LOCATION          "/menu/64ddipl"
#endif
#ifndef EMU_LOCATION
#define EMU_LOCATION            "/menu/emulators"
#endif
#ifndef EMU_CONFIG
#define EMU_CONFIG              "/menu/emulators.ini"
#endif

/**
 * @brief Check if the 64DD is connected.
 * 
 * @return true if the 64DD is connected, false otherwise.
 */
bool is_64dd_connected (void) {
    bool is_64dd_io_present = ((io_read(0x05000540) & 0x0000FFFF) == 0x0000);
    bool is_64dd_ipl_present = (io_read(0x06001010) == 0x2129FFF8);
    return (is_64dd_io_present || is_64dd_ipl_present);
}

/**
 * @brief Create the saves subdirectory.
 * 
 * @param path Pointer to the path structure.
 * @return true if an error occurred, false otherwise.
 */
static bool create_saves_subdirectory (path_t *path) {
    path_t *save_folder_path = path_clone(path);
    path_pop(save_folder_path);
    path_push(save_folder_path, SAVE_DIRECTORY_NAME);
    bool error = directory_create(path_get(save_folder_path));
    path_free(save_folder_path);
    return error;
}

/**
 * @brief Convert the ROM save type to the flashcart save type.
 * 
 * @param save_type The ROM save type.
 * @return flashcart_save_type_t The flashcart save type.
 */
static flashcart_save_type_t convert_save_type (rom_save_type_t save_type) {
    switch (save_type) {
        case SAVE_TYPE_EEPROM_4KBIT: return FLASHCART_SAVE_TYPE_EEPROM_4KBIT;
        case SAVE_TYPE_EEPROM_16KBIT: return FLASHCART_SAVE_TYPE_EEPROM_16KBIT;
        case SAVE_TYPE_SRAM_256KBIT: return FLASHCART_SAVE_TYPE_SRAM_256KBIT;
        case SAVE_TYPE_SRAM_BANKED: return FLASHCART_SAVE_TYPE_SRAM_BANKED;
        case SAVE_TYPE_SRAM_1MBIT: return FLASHCART_SAVE_TYPE_SRAM_1MBIT;
        case SAVE_TYPE_FLASHRAM_1MBIT: return FLASHCART_SAVE_TYPE_FLASHRAM_1MBIT;
        case SAVE_TYPE_FLASHRAM_PKST2: return FLASHCART_SAVE_TYPE_FLASHRAM_PKST2;
        default: return FLASHCART_SAVE_TYPE_NONE;
    }
}

/**
 * @brief Convert the cart load error code to a human-readable message.
 * 
 * @param err The cart load error code.
 * @return char* The error message.
 */
char *cart_load_convert_error_message (cart_load_err_t err) {
    switch (err) {
        case CART_LOAD_OK: return "Cart load OK";
        case CART_LOAD_ERR_ROM_LOAD_FAIL: return "Error occured during ROM loading";
        case CART_LOAD_ERR_SAVE_LOAD_FAIL: return "Error occured during save loading";
        case CART_LOAD_ERR_BOOT_MODE_FAIL: return "Error occured during boot mode setting";
        case CART_LOAD_ERR_64DD_PRESENT: return "64DD accessory is connected to the N64";
        case CART_LOAD_ERR_64DD_IPL_NOT_FOUND: return "Required 64DD IPL file was not found";
        case CART_LOAD_ERR_64DD_IPL_LOAD_FAIL: return "Error occurred during 64DD IPL loading";
        case CART_LOAD_ERR_64DD_DISK_LOAD_FAIL: return "Error occurred during 64DD disk loading";
        case CART_LOAD_ERR_EMU_NOT_FOUND: return "Required emulator file was not found";
        case CART_LOAD_ERR_EMU_LOAD_FAIL: return "Error occurred during emulator ROM loading";
        case CART_LOAD_ERR_EMU_ROM_LOAD_FAIL: return "Error occurred during emulated ROM loading";
        case CART_LOAD_ERR_CREATE_SAVES_SUBDIR_FAIL: return "Couldn't create saves subdirectory";
        case CART_LOAD_ERR_EXP_PAK_NOT_FOUND: return "Mandatory Expansion Pak accessory was not found";
        case CART_LOAD_ERR_FUNCTION_NOT_SUPPORTED: return "Your flashcart doesn't support required functionality";
        default: return "Unknown error [CART_LOAD]";
    }
}

/**
 * @brief Load an N64 ROM and its save file.
 * 
 * @param menu Pointer to the menu structure.
 * @param progress Progress callback function.
 * @return cart_load_err_t Error code.
 */
cart_load_err_t cart_load_n64_rom_and_save (menu_t *menu, flashcart_progress_callback_t progress, flashcart_progress_callback_t save_progress) {
    path_t *path = path_clone(menu->load.rom_path);

    bool byte_swap = (menu->load.rom_info.endianness == ENDIANNESS_BYTE_SWAP);
    flashcart_save_type_t save_type = convert_save_type(rom_info_get_save_type(&menu->load.rom_info));

    menu->flashcart_err = flashcart_load_rom(path_get(path), byte_swap, progress);
    if (menu->flashcart_err != FLASHCART_OK) {
        path_free(path);
        return CART_LOAD_ERR_ROM_LOAD_FAIL;
    }

    path_ext_replace(path, "sav");
    if (menu->settings.use_saves_folder) {
        if ((save_type != FLASHCART_SAVE_TYPE_NONE) && create_saves_subdirectory(path)) {
            path_free(path);
            return CART_LOAD_ERR_CREATE_SAVES_SUBDIR_FAIL;
        }
        path_push_subdir(path, SAVE_DIRECTORY_NAME);
    }

    if (save_progress && save_type != FLASHCART_SAVE_TYPE_NONE && !file_exists(path_get(path))) {
        save_progress(1.0f);
    }

    menu->flashcart_err = flashcart_load_save(path_get(path), save_type);
    if (menu->flashcart_err != FLASHCART_OK) {
        path_free(path);
        return CART_LOAD_ERR_SAVE_LOAD_FAIL;
    }

#ifndef FEATURE_AUTOLOAD_ROM_ENABLED
    if (menu->settings.rom_fast_reboot_enabled) {
        if (!flashcart_has_feature(FLASHCART_FEATURE_ROM_REBOOT_FAST)) {
            return CART_LOAD_ERR_FUNCTION_NOT_SUPPORTED;
        }
        menu->flashcart_err = flashcart_set_next_boot_mode(FLASHCART_REBOOT_MODE_ROM);
        if (menu->flashcart_err != FLASHCART_OK) {
            path_free(path);
            return CART_LOAD_ERR_BOOT_MODE_FAIL;
        }
    }
#endif

    path_free(path);

    return CART_LOAD_OK;
}

/**
 * @brief Load the 64DD IPL and disk.
 * 
 * @param menu Pointer to the menu structure.
 * @param progress Progress callback function.
 * @return cart_load_err_t Error code.
 */
cart_load_err_t cart_load_64dd_ipl_and_disks (menu_t *menu, flashcart_progress_callback_t progress) {
    if (!flashcart_has_feature(FLASHCART_FEATURE_64DD)) {
        return CART_LOAD_ERR_FUNCTION_NOT_SUPPORTED;
    }

    if (is_64dd_connected()) {
        return CART_LOAD_ERR_64DD_PRESENT;
    }

    if (!is_memory_expanded()) {
        return CART_LOAD_ERR_EXP_PAK_NOT_FOUND;
    }

    path_t *path = path_init(menu->storage_prefix, DDIPL_LOCATION);
    flashcart_disk_parameters_t disk_parameters;

    disk_parameters.development_drive = (menu->load.disk_slots.primary.disk_info.region == DISK_REGION_DEVELOPMENT);
    disk_parameters.disk_type = menu->load.disk_slots.primary.disk_info.disk_type;
    memcpy(disk_parameters.bad_system_area_lbas, menu->load.disk_slots.primary.disk_info.bad_system_area_lbas, sizeof(disk_parameters.bad_system_area_lbas));
    memcpy(disk_parameters.defect_tracks, menu->load.disk_slots.primary.disk_info.defect_tracks, sizeof(disk_parameters.defect_tracks));

    switch (menu->load.disk_slots.primary.disk_info.region) {
        case DISK_REGION_DEVELOPMENT:
            path_push(path, "NDXJ0.n64");
            break;
        case DISK_REGION_JAPANESE:
            path_push(path, "NDDJ2.n64");
            break;
        case DISK_REGION_USA:
            path_push(path, "NDDE0.n64");
            break;
    }

    if (!file_exists(path_get(path))) {
        path_free(path);
        return CART_LOAD_ERR_64DD_IPL_NOT_FOUND;
    }

    menu->flashcart_err = flashcart_load_64dd_ipl(path_get(path), progress);
    if (menu->flashcart_err != FLASHCART_OK) {
        path_free(path);
        return CART_LOAD_ERR_64DD_IPL_LOAD_FAIL;
    }

    path_free(path);

    int swap_disk_count = 0;
    char *swap_disk_paths[3];
    for (int i = 0; i < 3; i++) {
        if (menu->load.disk_slots.swap_slot[i].disk_path) {
            swap_disk_paths[swap_disk_count++] = path_get(menu->load.disk_slots.swap_slot[i].disk_path);
        }
    }

    if (swap_disk_count > 0) {
        menu->flashcart_err = flashcart_load_64dd_disks(path_get(menu->load.disk_slots.primary.disk_path), &disk_parameters, swap_disk_paths, swap_disk_count);
    } else {
        menu->flashcart_err = flashcart_load_64dd_disk(path_get(menu->load.disk_slots.primary.disk_path), &disk_parameters);
    }
    if (menu->flashcart_err != FLASHCART_OK) {
        return CART_LOAD_ERR_64DD_DISK_LOAD_FAIL;
    }

    return CART_LOAD_OK;
}

/**
 * @brief Load an emulator and its ROM.
 * 
 * @param menu Pointer to the menu structure.
 * @param emu_type The type of emulator to load.
 * @param progress Progress callback function.
 * @return cart_load_err_t Error code.
 */
cart_load_err_t cart_load_emulator (menu_t *menu, cart_load_emu_type_t emu_type, flashcart_progress_callback_t progress) {
    path_t *path = path_init(menu->storage_prefix, EMU_LOCATION);

    flashcart_save_type_t save_type = FLASHCART_SAVE_TYPE_NONE;
    uint32_t emulated_rom_offset = 0x200000;
    uint32_t emulated_file_offset = 0;

    const char *emu_section = NULL;
    const char *default_rom_filename = NULL;

    switch (emu_type) {
        case CART_LOAD_EMU_TYPE_NES:
            emu_section = "nes";
            default_rom_filename = "neon64bu.rom";
            // Tested against Neon 64 v1.2, v0.3 and v2
            save_type = FLASHCART_SAVE_TYPE_SRAM_1MBIT;
            break;
        case CART_LOAD_EMU_TYPE_SNES:
            emu_section = "snes";
            default_rom_filename = "sodium64.z64";
            save_type = FLASHCART_SAVE_TYPE_SRAM_256KBIT;
            break;
        case CART_LOAD_EMU_TYPE_GAMEBOY:
            emu_section = "gb";
            default_rom_filename = "gb.v64";
            // TODO: Saves might be less problematic by using the FAKE type.
            save_type = FLASHCART_SAVE_TYPE_FLASHRAM_1MBIT; //FLASHCART_SAVE_TYPE_FLASHRAM_FAKE;
            break;
        case CART_LOAD_EMU_TYPE_GAMEBOY_COLOR:
            emu_section = "gbc";
            default_rom_filename = "gbc.v64";
            // TODO: Saves might be less problematic by using the FAKE type.
            save_type = FLASHCART_SAVE_TYPE_FLASHRAM_1MBIT; //FLASHCART_SAVE_TYPE_FLASHRAM_FAKE;
            break;
        case CART_LOAD_EMU_TYPE_SEGA_GENERIC_8BIT:
            emu_section = "sega8bit";
            default_rom_filename = "smsPlus64.z64";
            save_type = FLASHCART_SAVE_TYPE_NONE;
            break;
        case CART_LOAD_EMU_TYPE_FAIRCHILD_CHANNELF:
            emu_section = "channelf";
            default_rom_filename = "Press-F.z64";
            save_type = FLASHCART_SAVE_TYPE_NONE;
            break;
        case CART_LOAD_EMU_TYPE_SINCLAIR_ZXSPECTRUM:
            emu_section = "zxspectrum";
            default_rom_filename = "zx-spectrum.z64";
            save_type = FLASHCART_SAVE_TYPE_NONE;
            break;
        case CART_LOAD_EMU_TYPE_MICROSOFT_MSX:
            emu_section = "msx";
            default_rom_filename = "msx.z64";
            save_type = FLASHCART_SAVE_TYPE_NONE;
            break;
        case CART_LOAD_EMU_TYPE_SNK_NEO_GEO:
            emu_section = "neogeo";
            default_rom_filename = "mvs64.z64";
            save_type = FLASHCART_SAVE_TYPE_NONE;
            break;
        case CART_LOAD_EMU_TYPE_DEV:
            emu_section = "dev";
            default_rom_filename = "dev_emu.z64";
            save_type = FLASHCART_SAVE_TYPE_NONE;
            break;
    }

    // Apply per-emulator overrides from sd:/menu/emulators.ini if present
    if (emu_section) {
        path_t *cfg_path = path_init(menu->storage_prefix, EMU_CONFIG);
        ini_t *cfg = ini_load(path_get(cfg_path));
        path_free(cfg_path);
        if (cfg) {
            const char *rom_override = ini_get_string(cfg, emu_section, "rom", default_rom_filename);
            char rom_filename_buf[256];
            strncpy(rom_filename_buf, rom_override, sizeof(rom_filename_buf) - 1);
            rom_filename_buf[sizeof(rom_filename_buf) - 1] = '\0';
            path_push(path, rom_filename_buf);
            int save_type_override = ini_get_int(cfg, emu_section, "save_type", (int) save_type);
            if (save_type_override >= FLASHCART_SAVE_TYPE_NONE && save_type_override < __FLASHCART_SAVE_TYPE_END) {
                save_type = (flashcart_save_type_t) save_type_override;
            }
            const char *rom_offset_str = ini_get_string(cfg, emu_section, "rom_offset", NULL);
            if (rom_offset_str) {
                char *end = NULL;
                unsigned long parsed = strtoul(rom_offset_str, &end, 0);
                // flashcart_load_file() writes to ROM_ADDRESS + rom_offset within the primary
                // 64MB SDRAM window on every flashcart (no extended-region support in load_file),
                // so reject offsets that would make (MiB(64) - rom_offset) wrap.
                if (end != rom_offset_str && *end == '\0' && parsed < MiB(64)) {
                    emulated_rom_offset = (uint32_t) parsed;
                }
            }
            ini_free(cfg);
        } else {
            char default_buf[256];
            strncpy(default_buf, default_rom_filename, sizeof(default_buf) - 1);
            default_buf[sizeof(default_buf) - 1] = '\0';
            path_push(path, default_buf);
        }
    }

    if (!file_exists(path_get(path))) {
        path_free(path);
        return CART_LOAD_ERR_EMU_NOT_FOUND;
    }

    menu->flashcart_err = flashcart_load_rom(path_get(path), false, progress);
    if (menu->flashcart_err != FLASHCART_OK) {
        path_free(path);
        return CART_LOAD_ERR_EMU_LOAD_FAIL;
    }

    path_free(path);

    path = path_clone_push(menu->browser.directory, menu->browser.entry->name);

    switch (emu_type) {
        case CART_LOAD_EMU_TYPE_SNES:
            // NOTE: The emulator expects the header to be removed from the ROM being uploaded.
            emulated_file_offset = ((file_get_size(path_get(path)) & 0x3FF) == 0x200) ? 0x200 : 0;
            break;
        default:
            break;
    }

    menu->flashcart_err = flashcart_load_file(path_get(path), emulated_rom_offset, emulated_file_offset);
    if (menu->flashcart_err != FLASHCART_OK) {
        path_free(path);
        return CART_LOAD_ERR_EMU_ROM_LOAD_FAIL;
    }

    path_ext_replace(path, "sav");
    if (menu->settings.use_saves_folder) {
        if ((save_type != FLASHCART_SAVE_TYPE_NONE) && create_saves_subdirectory(path)) {
            path_free(path);
            return CART_LOAD_ERR_CREATE_SAVES_SUBDIR_FAIL;
        }
        path_push_subdir(path, SAVE_DIRECTORY_NAME);
    }

    menu->flashcart_err = flashcart_load_save(path_get(path), save_type);
    if (menu->flashcart_err != FLASHCART_OK) {
        path_free(path);
        return CART_LOAD_ERR_SAVE_LOAD_FAIL;
    }

    path_free(path);

    return CART_LOAD_OK;
}
