#include "../bookkeeping.h"
#include "../cart_load.h"
#include "../datel_codes.h"
#include "../rom_info.h"
#include "../sound.h"
#include "boot/boot.h"
#include "utils/fs.h"
#include "../ui_components/constants.h"
#include "views.h"
#include <string.h>

static bool show_extra_info_message = false;
static bool show_advanced_info_message = false;
static bool show_expansion_pak_warning = false;
static component_boxart_t *boxart;
static component_boxart_t *pending_boxart;
static char *rom_filename = NULL;

static int16_t current_metadata_image_index = 0;
static const file_image_type_t metadata_image_types[] = {
    IMAGE_BOXART_FRONT,
    IMAGE_BOXART_BACK,
    IMAGE_BOXART_LEFT,
    IMAGE_BOXART_RIGHT,
    IMAGE_BOXART_TOP,
    IMAGE_BOXART_BOTTOM,
    IMAGE_GAMEPAK_FRONT,
    IMAGE_GAMEPAK_BACK
};
#define METADATA_IMAGE_CACHE_MAX (14)
static const uint16_t metadata_image_type_count = sizeof(metadata_image_types) / sizeof(metadata_image_types[0]);
static const char *metadata_image_names[METADATA_IMAGE_CACHE_MAX];
static bool metadata_image_embedded[METADATA_IMAGE_CACHE_MAX];
static bool metadata_image_available[METADATA_IMAGE_CACHE_MAX];
static uint16_t metadata_image_count;
static bool metadata_images_scanned = false;

static void scan_metadata_images(menu_t *menu) {
    if (metadata_images_scanned) {
        return;
    }

    metadata_image_count = 0;
    if (menu->load.rom_info.meta.metadata_zip_path) {
        for (uint16_t i = 0; i < 6 && metadata_image_count < METADATA_IMAGE_CACHE_MAX; i++) {
            if (menu->load.rom_info.meta.boxart[i] && menu->load.rom_info.meta.boxart[i][0]) {
                metadata_image_names[metadata_image_count] = menu->load.rom_info.meta.boxart[i];
                metadata_image_embedded[metadata_image_count] = true;
                metadata_image_available[metadata_image_count++] = true;
            }
        }
        for (uint16_t i = 0; i < 2 && metadata_image_count < METADATA_IMAGE_CACHE_MAX; i++) {
            if (menu->load.rom_info.meta.cartart[i] && menu->load.rom_info.meta.cartart[i][0]) {
                metadata_image_names[metadata_image_count] = menu->load.rom_info.meta.cartart[i];
                metadata_image_embedded[metadata_image_count] = true;
                metadata_image_available[metadata_image_count++] = true;
            }
        }
        for (uint16_t i = 0; i < menu->load.rom_info.meta.screenshot_count && metadata_image_count < METADATA_IMAGE_CACHE_MAX; i++) {
            metadata_image_names[metadata_image_count] = menu->load.rom_info.meta.screenshots[i];
            metadata_image_embedded[metadata_image_count] = true;
            metadata_image_available[metadata_image_count++] = true;
        }
        if (metadata_image_count > 0) {
            metadata_images_scanned = true;
            return;
        }
    }

    path_t *path = path_init(menu->storage_prefix, "menu/metadata"); // should be METADATA_BASE_DIRECTORY
    char game_code_path[50];

    if (menu->load.rom_info.game_code[1] == 'E' && menu->load.rom_info.game_code[2] == 'D') {
        // This is using a homebrew ROM ID, use the title for the file name instead.
        // Create a null-terminated copy of the title for safe string operations
        char safe_title[21];  // 20 chars + null terminator
        memcpy(safe_title, menu->load.rom_info.title, 20);
        safe_title[20] = '\0';
        
        snprintf(game_code_path, sizeof(game_code_path), "homebrew/%s", safe_title); // should be HOMEBREW_ID_SUBDIRECTORY
        path_push(path, game_code_path);
    }
    else {
        snprintf(game_code_path, sizeof(game_code_path), "%c/%c/%c/%c",
            menu->load.rom_info.game_code[0],
            menu->load.rom_info.game_code[1],
            menu->load.rom_info.game_code[2],
            menu->load.rom_info.game_code[3]);
        path_push(path, game_code_path);

        if (!directory_exists(path_get(path))) { // Allow boxart to not specify the region code.
            path_pop(path);
        }
    }

    bool dir_exists = directory_exists(path_get(path));

    if (dir_exists) {
        // Filenames array matches metadata_image_types order for indexed access
        // Note: This mapping is also present in boxart.c but duplicated here
        // for efficient scanning without calling into the component layer
        char *filenames[] = {
            "boxart_front.png",
            "boxart_back.png",
            "boxart_left.png",
            "boxart_right.png",
            "boxart_top.png",
            "boxart_bottom.png",
            "gamepak_front.png",
            "gamepak_back.png"
        };

        for (uint16_t i = 0; i < metadata_image_type_count; i++) {
            path_push(path, filenames[i]);
            metadata_image_names[i] = filenames[i];
            metadata_image_embedded[i] = false;
            metadata_image_available[i] = file_exists(path_get(path));
            path_pop(path);
        }
        metadata_image_count = metadata_image_type_count;
    } else {
        // No directory exists, mark all images as unavailable
        metadata_image_count = metadata_image_type_count;
        for (uint16_t i = 0; i < metadata_image_count; i++) {
            metadata_image_available[i] = false;
        }
    }

    debugf("Metadata: Scanned metadata for ROM ID %s. \n", game_code_path);

    path_free(path);
    metadata_images_scanned = true;
}

static const char *format_rom_description(menu_t *menu) {
    const char *rom_description = NULL;

    if (menu->load.rom_info.meta.short_description != NULL && strlen(menu->load.rom_info.meta.short_description) > 0) {
        rom_description = menu->load.rom_info.meta.short_description;
    }

    return rom_description ? rom_description : "No description available.";
}

static char *convert_error_message (rom_err_t err) {
    switch (err) {
        case ROM_ERR_LOAD_IO: return "I/O error during loading ROM information and/or options";
        case ROM_ERR_SAVE_IO: return "I/O error during storing ROM options";
        case ROM_ERR_NO_FILE: return "Couldn't open ROM file";
        default: return "Unknown ROM info load error";
    }
}

static const char *format_rom_endianness (rom_endianness_t endianness) {
    switch (endianness) {
        case ENDIANNESS_BIG: return "Big (default)";
        case ENDIANNESS_LITTLE: return "Little (unsupported)";
        case ENDIANNESS_BYTE_SWAP: return "Byte swapped";
        default: return "Unknown";
    }
}

static const char *format_rom_media_type (rom_category_type_t media_type) {
    switch (media_type) {
        case N64_CART: return "Cartridge";
        case N64_DISK: return "Disk";
        case N64_CART_EXPANDABLE: return "Cartridge (Expandable)";
        case N64_DISK_EXPANDABLE: return "Disk (Expandable)";
        case N64_ALECK64: return "Aleck64";
        default: return "Unknown";
    }
}

static const char *format_rom_destination_market (rom_destination_type_t market_type) {
    // From https://ultra64.ca/files/documentation/online-manuals/man-v5-2/allman52/
    switch (market_type) {
        case MARKET_JAPANESE_MULTI: return "Japanese & English"; // 1080 Snowboarding JPN
        case MARKET_BRAZILIAN: return "Brazilian (Portuguese)";
        case MARKET_CHINESE: return "Chinese";
        case MARKET_GERMAN: return "German";
        case MARKET_NORTH_AMERICA: return "American English";
        case MARKET_FRENCH: return "French";
        case MARKET_DUTCH: return "Dutch";
        case MARKET_ITALIAN: return "Italian";
        case MARKET_JAPANESE: return "Japanese";
        case MARKET_KOREAN: return "Korean";
        case MARKET_CANADIAN: return "Canadaian (English & French)";
        case MARKET_SPANISH: return "Spanish";
        case MARKET_AUSTRALIAN: return "Australian (English)";
        case MARKET_SCANDINAVIAN: return "Scandinavian";
        case MARKET_GATEWAY64_NTSC: return "LodgeNet/Gateway (NTSC)";
        case MARKET_GATEWAY64_PAL: return "LodgeNet/Gateway (PAL)";
        case MARKET_EUROPEAN_BASIC: return "PAL (includes English)"; // Mostly EU but is used on some Australian ROMs
        case MARKET_OTHER_X: return "PAL (Regional)"; // AUS HSV Racing ROM's and Asia Top Gear Rally but it's PAL specific
        case MARKET_OTHER_Y: return "European (Regional)"; //Used on language variations of european ROMs
        case MARKET_OTHER_Z: return "PAL (Miscellaneous)"; //Some australian ROMs use this, but also some European ROMs.
        default: return "Unknown";
    }
}

static const char *format_rom_save_type (rom_save_type_t save_type, bool supports_cpak) {
    switch (save_type) {
        case SAVE_TYPE_NONE: return supports_cpak ? "Controller PAK" : "None";
        case SAVE_TYPE_EEPROM_4KBIT: return supports_cpak ?   "EEPROM 4kbit | Controller PAK" : "EEPROM 4kbit";
        case SAVE_TYPE_EEPROM_16KBIT: return supports_cpak ?  "EEPROM 16kbit | Controller PAK" : "EEPROM 16kbit";
        case SAVE_TYPE_SRAM_256KBIT: return supports_cpak ?   "SRAM 256kbit | Controller PAK" : "SRAM 256kbit";
        case SAVE_TYPE_SRAM_BANKED: return supports_cpak ?    "SRAM 768kbit / 3 banks | Controller PAK" : "SRAM 768kbit / 3 banks";
        case SAVE_TYPE_SRAM_1MBIT: return supports_cpak ?     "SRAM 1Mbit | Controller PAK" : "SRAM 1Mbit";
        case SAVE_TYPE_FLASHRAM_1MBIT: return supports_cpak ? "FlashRAM 1Mbit | Controller PAK" : "FlashRAM 1Mbit";
        case SAVE_TYPE_FLASHRAM_PKST2: return supports_cpak ? "FlashRAM (Pokemon Stadium 2) | Controller PAK" : "FlashRAM (Pokemon Stadium 2)";
        default: return "Unknown";
    }
}

static const char *format_rom_tv_type (rom_tv_type_t tv_type) {
    switch (tv_type) {
        case ROM_TV_TYPE_PAL: return "PAL";
        case ROM_TV_TYPE_NTSC: return "NTSC";
        case ROM_TV_TYPE_MPAL: return "MPAL";
        default: return "Unknown";
    }
}

static const char *format_rom_expansion_pak_info (rom_expansion_pak_t expansion_pak_info) {
    switch (expansion_pak_info) {
        case EXPANSION_PAK_REQUIRED: return "Required"; // This ROM requires the Expansion Pak to run.
        case EXPANSION_PAK_RECOMMENDED: return "Recommended"; // This ROM will run without the Expansion Pak, but will have limited functionality or performance.
        case EXPANSION_PAK_UNUSED: return "Unused"; // This ROM will does not use the Expansion Pak.
        case EXPANSION_PAK_FAULTY: return "May require ROM patch";
        default: return "Unspecified"; // This ROM will probably run without the Expansion Pak, and will have full functionality.
    }
}

static const char *format_rom_pak_feature_info (bool pak_feature_info) {
    if (pak_feature_info) {
        return "Supported";
    } else {
        return "Not used";
    }
}

static const char *format_cic_type (rom_cic_type_t cic_type) {
    switch (cic_type) {
        case ROM_CIC_TYPE_5101: return "5101";
        case ROM_CIC_TYPE_5167: return "5167";
        case ROM_CIC_TYPE_6101: return "6101";
        case ROM_CIC_TYPE_7102: return "7102";
        case ROM_CIC_TYPE_x102: return "6102 / 7101";
        case ROM_CIC_TYPE_x103: return "6103 / 7103";
        case ROM_CIC_TYPE_x105: return "6105 / 7105";
        case ROM_CIC_TYPE_x106: return "6106 / 7106";
        case ROM_CIC_TYPE_8301: return "8301";
        case ROM_CIC_TYPE_8302: return "8302";
        case ROM_CIC_TYPE_8303: return "8303";
        case ROM_CIC_TYPE_8401: return "8401";
        case ROM_CIC_TYPE_8501: return "8501";
        default: return "Unknown";
    }
}

static const char *format_age_rating (uint32_t age_rating) {
    if (age_rating >= 18) {
        return "Adults Only";
    }
    else if (age_rating >= 17) {
        return "Mature";
    }
    else if (age_rating >= 13) {
        return "Teen";
    }
    else if (age_rating >= 10) {
        return "Everyone 10+";
    }
    else if (age_rating > 0) {
        return "Everyone";
    }
    else if (age_rating == 0) {
        return "None";
    }
    else {
        return "Unknown";
    }
}

static inline const char *format_boolean_type (bool bool_value) {
    return bool_value ? "On" : "Off";
}

// Forward declarations for default selection helpers (defined after context menu structs)
static int get_rom_cic_override_current_selection (menu_t *menu);
static int get_rom_save_override_current_selection (menu_t *menu);
static int get_rom_tv_override_current_selection (menu_t *menu);
static int get_rom_cheat_override_current_selection (menu_t *menu);
#ifdef FEATURE_PATCHER_GUI_ENABLED
static int get_rom_patch_override_current_selection (menu_t *menu);
#endif

static void set_cic_type (menu_t *menu, void *arg) {
    rom_cic_type_t cic_type = (rom_cic_type_t) (arg);
    rom_err_t err = rom_config_override_cic_type(menu->load.rom_path, &menu->load.rom_info, cic_type);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
    }
    menu->browser.reload = true;
}

static void set_save_type (menu_t *menu, void *arg) {
    rom_save_type_t save_type = (rom_save_type_t) (arg);
    rom_err_t err = rom_config_override_save_type(menu->load.rom_path, &menu->load.rom_info, save_type);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
    }
    menu->browser.reload = true;
}

static void set_tv_type (menu_t *menu, void *arg) {
    rom_tv_type_t tv_type = (rom_tv_type_t) (arg);
    rom_err_t err = rom_config_override_tv_type(menu->load.rom_path, &menu->load.rom_info, tv_type);
    if (err != ROM_OK) {
        menu_show_error(menu, convert_error_message(err));
    }
    menu->browser.reload = true;
}
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static void set_autoload_type (menu_t *menu, void *arg) {
    // Save the ROM this screen shows. From the History or Favorites tab, the browser's
    // directory and entry can be another file. The path is rebuilt from the storage
    // prefix first: bookkeeping loads its paths with path_create(), and path_pop() on
    // one of those would cut a ROM in the root folder back to "sd:".
    path_t *rom = path_init(menu->storage_prefix, strip_fs_prefix(path_get(menu->load.rom_path)));
    free(menu->settings.rom_autoload_filename);
    menu->settings.rom_autoload_filename = strdup(path_last_get(rom));
    path_pop(rom);
    free(menu->settings.rom_autoload_path);
    menu->settings.rom_autoload_path = strdup(strip_fs_prefix(path_get(rom)));
    path_free(rom);
    // FIXME: add a confirmation box here! (press start on reboot)
    menu->settings.rom_autoload_enabled = true;
    settings_save(&menu->settings);
    menu->browser.reload = true;
}
#endif

static void set_cheat_option(menu_t *menu, void *arg) {
    debugf("Load Rom: setting cheat option to %d\n", (int)arg);
    if (!is_memory_expanded()) {
        // If the Expansion pak is not installed, we cannot use cheats, and force it to off (just incase).
        rom_config_setting_set_cheats(menu->load.rom_path, &menu->load.rom_info, false);
        menu_show_error(menu, "Datel Cheats require an Expansion Pak");
        menu->browser.reload = true;
    }
    else {
        bool enabled = (bool)arg;
        rom_config_setting_set_cheats(menu->load.rom_path, &menu->load.rom_info, enabled);
        menu->browser.reload = true;
    }
}

static void open_datel_code_editor (menu_t *menu, void *arg) {
    (void)arg;

    if (!is_memory_expanded()) {
        rom_config_setting_set_cheats(menu->load.rom_path, &menu->load.rom_info, false);
        menu_show_error(menu, "Datel Cheats require an Expansion Pak");
        menu->browser.reload = true;
        return;
    }

    menu->next_mode = MENU_MODE_DATEL_CODE_EDITOR;
}

#ifdef FEATURE_PATCHER_GUI_ENABLED
static void set_patcher_option(menu_t *menu, void *arg) {
    bool enabled = (bool)arg;
    rom_config_setting_set_patches(menu->load.rom_path, &menu->load.rom_info, enabled);
    menu->browser.reload = true;
}
#endif

static void set_clear_rdram_option(menu_t *menu, void *arg) {
    bool enabled = (bool)arg;
    rom_config_setting_set_clear_rdram(menu->load.rom_path, &menu->load.rom_info, enabled);
    menu->browser.reload = true;
}

static void add_favorite (menu_t *menu, void *arg) {
    bookkeeping_favorite_add(&menu->bookkeeping, menu->load.rom_path, NULL, BOOKKEEPING_TYPE_ROM);
}

static void iterate_metadata_image(menu_t *menu, int direction) {
    if (pending_boxart) return;
    scan_metadata_images(menu);
    if (metadata_image_count == 0) return;
    bool low_memory_mode = !is_memory_expanded();
    int16_t previous_metadata_image_index = current_metadata_image_index;

    // Transverse to next/previous available image based on direction (1 = next, -1 = previous)
    int16_t start_metadata_image_index = current_metadata_image_index;
    int16_t new_metadata_image_index = (current_metadata_image_index + direction + metadata_image_count) % metadata_image_count;

    // Find next available image from our cached list
    while (new_metadata_image_index != start_metadata_image_index) {
        if (metadata_image_available[new_metadata_image_index]) {
            if (low_memory_mode && boxart != NULL) {
                // On Jumper Pak, avoid holding old and new boxart textures at once.
                ui_components_boxart_free(boxart);
                boxart = NULL;
            }

            component_boxart_t *new_boxart = NULL;
            if (metadata_image_embedded[new_metadata_image_index]) {
                uint8_t *data = NULL;
                size_t size = 0;
                if (!is_memory_expanded()) {
                    ui_components_background_image_free_only();
                }
                if (rom_info_extract_metadata_image(
                        &menu->load.rom_info,
                        metadata_image_names[new_metadata_image_index],
                        &data, &size)) {
                    new_boxart = ui_components_boxart_init_mem(
                        metadata_image_names[new_metadata_image_index], data, size,
                        BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX
                    );
                }
                if (new_boxart == NULL) {
                    ui_components_background_reload();
                }
            } else {
                new_boxart = ui_components_boxart_init(
                    menu->storage_prefix,
                    menu->load.rom_info.game_code,
                    menu->load.rom_info.title,
                    metadata_image_types[new_metadata_image_index]
                );
            }

            if (new_boxart != NULL) {
                if (!low_memory_mode) {
                    // Keep the current art visible until the new PNG has decoded.
                    pending_boxart = new_boxart;
                } else {
                    boxart = new_boxart;
                }
                current_metadata_image_index = new_metadata_image_index;
                sound_play_effect(SFX_SETTING);
                break;
            } else if (low_memory_mode) {
                // Best effort restore of previous image after a failed low-memory swap.
                if (metadata_image_available[previous_metadata_image_index]) {
                    if (metadata_image_embedded[previous_metadata_image_index]) {
                        uint8_t *data = NULL;
                        size_t size = 0;
                        if (!is_memory_expanded()) {
                            ui_components_background_image_free_only();
                        }
                        if (rom_info_extract_metadata_image(
                                &menu->load.rom_info,
                                metadata_image_names[previous_metadata_image_index],
                                &data, &size)) {
                            boxart = ui_components_boxart_init_mem(
                                metadata_image_names[previous_metadata_image_index], data, size,
                                BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX
                            );
                        }
                        if (boxart == NULL) {
                            ui_components_background_reload();
                        }
                    } else {
                        boxart = ui_components_boxart_init(
                            menu->storage_prefix,
                            menu->load.rom_info.game_code,
                            menu->load.rom_info.title,
                            metadata_image_types[previous_metadata_image_index]
                        );
                    }
                }
                if (boxart == NULL) {
                    menu_show_error(menu, "Could not swap boxart image");
                }
                break;
            }
        }
        new_metadata_image_index = (new_metadata_image_index + direction + metadata_image_count) % metadata_image_count;
    }
}

static component_context_menu_t set_cic_type_context_menu = {
    .get_default_selection = get_rom_cic_override_current_selection,
    .list = {
    {.text = "Automatic", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_AUTOMATIC) },
    {.text = "CIC-6101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_6101) },
    {.text = "CIC-7102", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_7102) },
    {.text = "CIC-6102 / CIC-7101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x102) },
    {.text = "CIC-6103 / CIC-7103", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x103) },
    {.text = "CIC-6105 / CIC-7105", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x105) },
    {.text = "CIC-6106 / CIC-7106", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_x106) },
    {.text = "Aleck64 CIC-5101", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_5101) },
    {.text = "64DD ROM conversion CIC-5167", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_5167) },
    {.text = "NDDJ0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8301) },
    {.text = "NDDJ1 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8302) },
    {.text = "NDDJ2 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8303) },
    {.text = "NDXJ0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8401) },
    {.text = "NDDE0 64DD IPL", .action = set_cic_type, .arg = (void *) (ROM_CIC_TYPE_8501) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_save_type_context_menu = {
    .get_default_selection = get_rom_save_override_current_selection,
    .list = {
    { .text = "Automatic", .action = set_save_type, .arg = (void *) (SAVE_TYPE_AUTOMATIC) },
    { .text = "None", .action = set_save_type, .arg = (void *) (SAVE_TYPE_NONE) },
    { .text = "EEPROM 4kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_EEPROM_4KBIT) },
    { .text = "EEPROM 16kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_EEPROM_16KBIT) },
    { .text = "SRAM 256kbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_256KBIT) },
    { .text = "SRAM 768kbit / 3 banks", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_BANKED) },
    { .text = "SRAM 1Mbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_SRAM_1MBIT) },
    { .text = "FlashRAM 1Mbit", .action = set_save_type, .arg = (void *) (SAVE_TYPE_FLASHRAM_1MBIT) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_tv_type_context_menu = {
    .get_default_selection = get_rom_tv_override_current_selection,
    .list = {
    { .text = "Automatic", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_AUTOMATIC) },
    { .text = "PAL", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_PAL) },
    { .text = "NTSC", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_NTSC) },
    { .text = "MPAL", .action = set_tv_type, .arg = (void *) (ROM_TV_TYPE_MPAL) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t set_cheat_options_menu = {
    .get_default_selection = get_rom_cheat_override_current_selection,
    .list = {
    { .text = "Enabled", .action = set_cheat_option, .arg = (void *) (true)},
    { .text = "Disabled", .action = set_cheat_option, .arg = (void *) (false)},
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

#ifdef FEATURE_PATCHER_GUI_ENABLED
static component_context_menu_t set_patcher_options_menu = {
    .get_default_selection = get_rom_patch_override_current_selection,
    .list = {
    { .text = "Enabled", .action = set_patcher_option, .arg = (void *) (true)},
    { .text = "Disabled", .action = set_patcher_option, .arg = (void *) (false)},
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

static int get_rom_clear_rdram_current_selection (menu_t *menu);

static component_context_menu_t set_clear_rdram_options_menu = {
    .get_default_selection = get_rom_clear_rdram_current_selection,
    .list = {
    { .text = "Enabled", .action = set_clear_rdram_option, .arg = (void *) (true)},
    { .text = "Disabled", .action = set_clear_rdram_option, .arg = (void *) (false)},
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t options_context_menu = { .list = {
    { .text = "Set CIC Type", .submenu = &set_cic_type_context_menu },
    { .text = "Set Save Type", .submenu = &set_save_type_context_menu },
    { .text = "Set TV Type", .submenu = &set_tv_type_context_menu },
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    { .text = "Set ROM to autoload", .action = set_autoload_type },
#endif
    { .text = "Use Cheats", .submenu = &set_cheat_options_menu },
    { .text = "Datel Code Editor", .action = open_datel_code_editor },
#ifdef FEATURE_PATCHER_GUI_ENABLED
    { .text = "Use Patches", .submenu = &set_patcher_options_menu },
#endif
    { .text = "Clear RDRAM on boot", .submenu = &set_clear_rdram_options_menu },
    { .text = "Add to favorites", .action = add_favorite },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

// Generic helper: search context menu for item matching a target argument value.
// Returns the index of the first matching item, or 0 if not found (default to first).
static int find_menu_item_index_by_arg (const component_context_menu_t *menu, void *target_arg) {
    for (int i = 0; menu->list[i].text != NULL; i++) {
        if (menu->list[i].arg == target_arg) {
            return i;
        }
    }
    return 0; // Not found; default to first item
}

// Default selection helpers: find the menu item matching the current override value
static int get_rom_cic_override_current_selection (menu_t *menu) {
    if (!menu->load.rom_info.boot_override.cic) {
        return 0;
    }
    return find_menu_item_index_by_arg(
        &set_cic_type_context_menu,
        (void *) (menu->load.rom_info.boot_override.cic_type));
}

static int get_rom_save_override_current_selection (menu_t *menu) {
    if (!menu->load.rom_info.boot_override.save) {
        return 0;
    }
    return find_menu_item_index_by_arg(
        &set_save_type_context_menu,
        (void *) (menu->load.rom_info.boot_override.save_type));
}

static int get_rom_tv_override_current_selection (menu_t *menu) {
    if (!menu->load.rom_info.boot_override.tv) {
        return 0;
    }
    return find_menu_item_index_by_arg(
        &set_tv_type_context_menu,
        (void *) (menu->load.rom_info.boot_override.tv_type));
}

static int get_rom_cheat_override_current_selection (menu_t *menu) {
    return find_menu_item_index_by_arg(
        &set_cheat_options_menu,
        (void *) (menu->load.rom_info.settings.cheats_enabled ? true : false));
}

#ifdef FEATURE_PATCHER_GUI_ENABLED
static int get_rom_patch_override_current_selection (menu_t *menu) {
    return find_menu_item_index_by_arg(
        &set_patcher_options_menu,
        (void *) (menu->load.rom_info.settings.patches_enabled ? true : false));
}
#endif

static int get_rom_clear_rdram_current_selection (menu_t *menu) {
    return find_menu_item_index_by_arg(
        &set_clear_rdram_options_menu,
        (void *) (menu->load.rom_info.settings.clear_rdram_enabled ? true : false));
}

static bool rom_requires_missing_expansion_pak (menu_t *menu) {
    return (menu->load.rom_info.features.expansion_pak == EXPANSION_PAK_REQUIRED) && !is_memory_expanded();
}

static void process (menu_t *menu) {
    if (ui_components_context_menu_process(menu, &options_context_menu)) {
        return;
    }

    if (show_expansion_pak_warning) {
        if (menu->actions.enter) {
            show_expansion_pak_warning = false;
            menu->load_pending.rom_file = true;
        } else if (menu->actions.back) {
            show_expansion_pak_warning = false;
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    if (menu->actions.enter) {
        if (rom_requires_missing_expansion_pak(menu)) {
            show_expansion_pak_warning = true;
            sound_play_effect(SFX_ERROR);
        } else {
            menu->load_pending.rom_file = true;
        }
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        if (show_advanced_info_message) {
            show_advanced_info_message = false;
        } else if (show_extra_info_message) {
            show_extra_info_message = false;
        } else {
            menu->next_mode = menu->load.from_grid ? MENU_MODE_GRID : MENU_MODE_BROWSER;
        }
    } else if (menu->actions.options) {
        ui_components_context_menu_show(&options_context_menu);
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.context) {
        if (show_extra_info_message) {
            show_extra_info_message = false;
        } else {
            show_extra_info_message = true;
        }
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.settings) { // TODO: change to go_right/go_left when those are implemented
        if (show_advanced_info_message) {
            show_advanced_info_message = false;
        } else {
            show_advanced_info_message = true;
        }
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.go_right) {
        iterate_metadata_image(menu, 1);
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_left) {
        iterate_metadata_image(menu, -1);
        sound_play_effect(SFX_CURSOR);
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (menu->load_pending.rom_file && menu->settings.loading_progress_bar_enabled) {
        ui_components_loader_draw(0.0f, NULL);
    } else {
#endif
        ui_components_layout_draw();

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "%s\n"
            "%.20s\n",
            rom_filename,
            menu->load.rom_info.title
        );

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n\n\n\t%.120s\n",
            format_rom_description(menu)
            
        );

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n\n\n\n\n\n\n\n\n\n\n"
            "Save type:\t\t%s\n"
            "TV region:\t\t%s\n"
            "\n"
            "Expansion PAK:\t%s\n"
            "Rumble PAK:\t\t%s\n"
            "Transfer PAK:\t\t%s\n"
            "\n"
            "Datel Cheats:\t\t%s\n"
            "Patches:\t\t\t%s\n"
            "Clear RDRAM:\t\t%s\n"
            ,
            
            format_rom_save_type(rom_info_get_save_type(&menu->load.rom_info), menu->load.rom_info.features.controller_pak),
            format_rom_tv_type(rom_info_get_tv_type(&menu->load.rom_info)),
            format_rom_expansion_pak_info(menu->load.rom_info.features.expansion_pak),
            format_rom_pak_feature_info(menu->load.rom_info.features.rumble_pak),
            format_rom_pak_feature_info(menu->load.rom_info.features.transfer_pak),
            format_boolean_type(menu->load.rom_info.settings.cheats_enabled),
            format_boolean_type(menu->load.rom_info.settings.patches_enabled),
            format_boolean_type(menu->load.rom_info.settings.clear_rdram_enabled)
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Launch ROM\n"
            "B: Back\n"
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "Start: Adv. Info\n"
            "◀ Change game image ▶\n"
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_RIGHT, VALIGN_TOP,
            "Z: Extra Info\n"
            "R: Adv. Options\n"
        );

        if (boxart != NULL) {
            ui_components_boxart_draw(boxart);
        }

        if (show_extra_info_message) {
            ui_components_messagebox_draw(
                "EXTRA ROM INFO\n"
                "\n"
                "Title: %.20s\n"
                "Age Rating: %s\n"
                "Players: %u\n"
                "Release Date: %s\n"
                "Author: %s\n"
                "Website: %s\n"
                "License: %s\n"
                "Game code: %c%c%c%c\n"
                "Media type: %s\n"
                "Variant: %s\n"
                "Version: %hhu\n"
                "CIC: %s\n\n\n"
                "Press B or Z to return.\n",
                menu->load.rom_info.title,
                format_age_rating(menu->load.rom_info.meta.age_rating),
                menu->load.rom_info.meta.num_players,
                menu->load.rom_info.meta.release_date,
                menu->load.rom_info.meta.author,
                menu->load.rom_info.meta.website,
                menu->load.rom_info.meta.osi_license,
                menu->load.rom_info.game_code[0], menu->load.rom_info.game_code[1], menu->load.rom_info.game_code[2], menu->load.rom_info.game_code[3],
                format_rom_media_type(menu->load.rom_info.category_code),
                format_rom_destination_market(menu->load.rom_info.destination_code),
                menu->load.rom_info.version,
                format_cic_type(rom_info_get_cic_type(&menu->load.rom_info))
            );
        }

        if (show_advanced_info_message) {
            ui_components_messagebox_draw(
                "ADVANCED ROM INFO\n"
                "\n"
                "Boot address: 0x%08lX\n"
                "SDK version: %.1f%c\n"
                "Clock Rate: %.2fMHz\n"
                "Check code: 0x%016llX\n"
                "Endianness: %s\n\n\n"
                "Press B or START to return.\n",
                menu->load.rom_info.boot_address,
                (menu->load.rom_info.libultra.version / 10.0f), menu->load.rom_info.libultra.revision,
                menu->load.rom_info.clock_rate,
                menu->load.rom_info.check_code,
                format_rom_endianness(menu->load.rom_info.endianness)
            );
        }

        if (show_expansion_pak_warning) {
            ui_components_messagebox_draw(
                "This ROM requires an Expansion Pak\n"
                "which was not detected.\n\n"
                "It may not run correctly without one.\n\n"
                "A: Continue anyway, B: Cancel\n"
            );
        }

        ui_components_context_menu_draw(&options_context_menu);
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    }
#endif

    rdpq_detach_show();
}

static void draw_progress (float progress) {
    surface_t *d = (progress >= 1.0f) ? display_get() : display_try_get();

    if (d) {
        rdpq_attach(d, NULL);

        ui_components_background_draw();

        ui_components_loader_draw(progress, "Loading ROM...");

        rdpq_detach_show();
    }
}

static void draw_creating_save (float progress) {
    surface_t *d = display_get();

    if (d) {
        rdpq_attach(d, NULL);

        ui_components_background_draw();

        ui_components_loader_draw(progress, "Creating initial save file...");

        rdpq_detach_show();
    }
}

static void load (menu_t *menu) {
    debugf("Load ROM: load function called\n");
    cart_load_err_t err;
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (!menu->settings.loading_progress_bar_enabled) {
        err = cart_load_n64_rom_and_save(menu, NULL, NULL);
    } else  {
        err = cart_load_n64_rom_and_save(menu, draw_progress, draw_creating_save);
    }
#else
    err = cart_load_n64_rom_and_save(menu, draw_progress, draw_creating_save);
#endif

    if (err != CART_LOAD_OK) {
        menu_show_error(menu, cart_load_convert_error_message(err));
        return;
    }

    bookkeeping_history_add(&menu->bookkeeping, menu->load.rom_path, NULL, BOOKKEEPING_TYPE_ROM);

    menu->next_mode = MENU_MODE_BOOT;

    menu->boot_params->device_type = BOOT_DEVICE_TYPE_ROM;
    menu->boot_params->detect_cic_seed = rom_info_get_cic_seed(&menu->load.rom_info, &menu->boot_params->cic_seed);
    switch (rom_info_get_tv_type(&menu->load.rom_info)) {
        case ROM_TV_TYPE_PAL: menu->boot_params->tv_type = BOOT_TV_TYPE_PAL; break;
        case ROM_TV_TYPE_NTSC: menu->boot_params->tv_type = BOOT_TV_TYPE_NTSC; break;
        case ROM_TV_TYPE_MPAL: menu->boot_params->tv_type = BOOT_TV_TYPE_MPAL; break;
        default: menu->boot_params->tv_type = BOOT_TV_TYPE_PASSTHROUGH; break;
    }

    // Handle cheat codes only if Expansion Pak is present and cheats are enabled
    if (is_memory_expanded() && menu->load.rom_info.settings.cheats_enabled) {
        uint32_t tmp_cheats[MAX_CHEAT_CODE_ARRAYLIST_SIZE];
        size_t cheat_item_count = generate_enabled_cheats_array(get_cheat_codes(), tmp_cheats);

        if (cheat_item_count > 2) { // account for at least one valid cheat code (address and value), excluding the last two 0s
            // Allocate memory for the cheats array
            uint32_t *cheats = malloc(cheat_item_count * sizeof(uint32_t));
            if (cheats) {
                memcpy(cheats, tmp_cheats, cheat_item_count * sizeof(uint32_t));
                for (size_t i = 0; i + 1 < cheat_item_count; i += 2) {
                    debugf("Cheat %u: Address: 0x%08lX, Value: 0x%08lX\n", i / 2, cheats[i], cheats[i + 1]);
                }
                debugf("Cheats enabled, %u cheats found\n", cheat_item_count / 2);
                menu->boot_params->cheat_list = cheats;
            } else {
                debugf("Failed to allocate memory for cheat list\n");
                menu->boot_params->cheat_list = NULL;
            }
        } else {
            debugf("Cheats enabled, but no cheats found\n");
            menu->boot_params->cheat_list = NULL;
        }
    } else {
        debugf("Cheats disabled or Expansion Pak not present\n");
        menu->boot_params->cheat_list = NULL;
    }

    menu->boot_params->clear_rdram = menu->load.rom_info.settings.clear_rdram_enabled;
}

static void deinit (void) {
    ui_components_boxart_free(pending_boxart);
    pending_boxart = NULL;
    ui_components_boxart_free(boxart);
    boxart = NULL;
    ui_components_background_reload();
    current_metadata_image_index = 0;
    metadata_images_scanned = false;
    metadata_image_count = 0;

    // Clear availability cache
    for (uint16_t i = 0; i < METADATA_IMAGE_CACHE_MAX; i++) {
        metadata_image_available[i] = false;
        metadata_image_embedded[i] = false;
        metadata_image_names[i] = NULL;
    }
}


void view_load_rom_init (menu_t *menu) {
    /* Startup autoload and Grid supply rom_path, which also survives the
     * cheat editor; otherwise resolve the current selection. */
    if (!menu->load_pending.rom_file && !menu->load.from_grid) {
        if (menu->load.rom_path) {
            rom_info_free_meta(&menu->load.rom_info);
            path_free(menu->load.rom_path);
            menu->load.rom_path = NULL;
        }

        if(menu->load.load_history_id != -1) {
            menu->load.rom_path = path_clone(menu->bookkeeping.history_items[menu->load.load_history_id].primary_path);
        } else if(menu->load.load_favorite_id != -1) {
            menu->load.rom_path = path_clone(menu->bookkeeping.favorite_items[menu->load.load_favorite_id].primary_path);
        } else {
            menu->load.rom_path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
        }

    }
    rom_filename = path_last_get(menu->load.rom_path);

    if (show_extra_info_message) {
        show_extra_info_message = false;
    }
    if (show_advanced_info_message) {
        show_advanced_info_message = false;
    }
    show_expansion_pak_warning = false;

    debugf("Load ROM: loading ROM info from %s\n", path_get(menu->load.rom_path));
    rom_err_t err = rom_config_load(menu->load.rom_path, &menu->load.rom_info);
    if (err != ROM_OK) {
        menu->load_pending.rom_file = false;
        rom_info_free_meta(&menu->load.rom_info);
        path_free(menu->load.rom_path);
        menu->load.rom_path = NULL;
        //disable the attempt at loading the favorite / history
        menu->load.load_history_id = -1;
        menu->load.load_favorite_id = -1;
        // FIXME: use bookkeeping_favorite_remove() here instead of just showing an error and leaving the broken favorite / history item in place
        menu_show_error(menu, convert_error_message(err));
        return;
    }

    if (!is_memory_expanded()) {
        menu->load.rom_info.settings.cheats_enabled = false;
    }

    /* Launching from Grid must not skip the confirmation that Details shows. */
    if (menu->load_pending.rom_file && menu->load.from_grid && rom_requires_missing_expansion_pak(menu)) {
        menu->load_pending.rom_file = false;
        show_expansion_pak_warning = true;
        sound_play_effect(SFX_ERROR);
    }

    if (menu->load.rom_info.meta.size_limit_exceeded) {
        menu_show_error(menu, "ROM metadata was skipped\nmetadata.ini exceeds size limit");
    }

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (!menu->load_pending.rom_file) {
#endif
        current_metadata_image_index = 0;
        scan_metadata_images(menu);
        if (metadata_image_count > 0 && metadata_image_available[0] && metadata_image_embedded[0]) {
            uint8_t *data = NULL;
            size_t size = 0;
            if (!is_memory_expanded()) {
                ui_components_background_image_free_only();
            }
            if (rom_info_extract_metadata_image(&menu->load.rom_info, metadata_image_names[0], &data, &size)) {
                boxart = ui_components_boxart_init_mem(
                    metadata_image_names[0], data, size,
                    BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX
                );
            }
            if (boxart == NULL) {
                ui_components_background_reload();
            }
        } else {
            boxart = ui_components_boxart_init(
                menu->storage_prefix, menu->load.rom_info.game_code,
                menu->load.rom_info.title, IMAGE_BOXART_FRONT
            );
        }
        ui_components_context_menu_init(&options_context_menu);
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    }
#endif

}

void view_load_rom_display (menu_t *menu, surface_t *display) {
    process(menu);

    if (pending_boxart && !pending_boxart->loading) {
        if (pending_boxart->image) {
            ui_components_boxart_free(boxart);
            boxart = pending_boxart;
            pending_boxart = NULL;
        } else {
            ui_components_boxart_free(pending_boxart);
            pending_boxart = NULL;
        }
    }

    if (!is_memory_expanded() && boxart != NULL && !boxart->loading && boxart->image == NULL) {
        ui_components_background_reload();
    }

    draw(menu, display);

    if (menu->load_pending.rom_file) {
        menu->load_pending.rom_file = false;
        load(menu);
    }

    if (menu->next_mode != MENU_MODE_LOAD_ROM && menu->next_mode != MENU_MODE_DATEL_CODE_EDITOR) {
        menu->load.load_history_id = -1;
        menu->load.load_favorite_id = -1;
        deinit();
        /* The error view returns to Grid and clears the flag itself. */
        if (menu->next_mode != MENU_MODE_ERROR) {
            menu->load.from_grid = false;
        }
    }
}
