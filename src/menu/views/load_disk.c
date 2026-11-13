#include "../cart_load.h"
#include "../disk_info.h"
#include "boot/boot.h"
#include "../sound.h"
#include "views.h"
#include "../bookkeeping.h"
#include <string.h>

#define DISK_SLOTS_MAX 3 // Maximum number of disk slots supported (excluding the primary disk)

static component_boxart_t *boxart;
static char *disk_filename;
static uint16_t swap_disk_count = 0;

static char *convert_disk_error_message (disk_err_t err) {
    switch (err) {
        case DISK_ERR_IO: return "I/O error during loading 64DD disk information";
        case DISK_ERR_NO_FILE: return "Couldn't open 64DD disk file";
        case DISK_ERR_INVALID: return "Invalid 64DD disk file";
        default: return "Unknown disk info load error";
    }
}

static char *convert_rom_error_message (rom_err_t err) {
    switch (err) {
        case ROM_ERR_LOAD_IO: return "I/O error during loading ROM information and/or options";
        case ROM_ERR_SAVE_IO: return "I/O error during storing ROM options";
        case ROM_ERR_NO_FILE: return "Couldn't open ROM file";
        default: return "Unknown ROM info load error";
    }
}

static char *format_disk_region (disk_region_t region) {
    switch (region) {
        case DISK_REGION_DEVELOPMENT: return "Development";
        case DISK_REGION_JAPANESE: return "Japan";
        case DISK_REGION_USA: return "USA";
        default: return "Unknown";
    }
}

static void add_favorite (menu_t *menu, void *arg) {
    bookkeeping_favorite_add(&menu->bookkeeping, menu->load.disk_slots.primary.disk_path, menu->load.rom_path, BOOKKEEPING_TYPE_DISK);
}

static component_context_menu_t options_context_menu = { .list = {
    { .text = "Add to favorites", .action = add_favorite },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static void process (menu_t *menu) {
    if (ui_components_context_menu_process(menu, &options_context_menu)) {
        return;
    }

    if (menu->actions.enter) {
        menu->load_pending.disk_file = true;
        menu->load.combined_disk_rom = false;
    } else if (menu->actions.context && menu->load.rom_path) {
        menu->load_pending.disk_file = true;
        menu->load.combined_disk_rom = true;
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    } else if (menu->actions.options) {
        ui_components_context_menu_show(&options_context_menu);
        sound_play_effect(SFX_SETTING);
    }
}

static void scan_for_swap_disks(menu_t *menu) {
    uint16_t result;
    dir_t info;

    // Reset swap disk count
    swap_disk_count = 0;

    // Free any existing swap disk paths
    for (uint16_t i = 0; i < DISK_SLOTS_MAX; i++) {
        if (menu->load.disk_slots.swap_slot[i].disk_path) {
            path_free(menu->load.disk_slots.swap_slot[i].disk_path);
            menu->load.disk_slots.swap_slot[i].disk_path = NULL;
        }
    }

    // Get directory path from primary disk
    path_t *dir_path = path_clone(menu->load.disk_slots.primary.disk_path);
    path_pop(dir_path);

    // Scan directory using dir_findfirst/dir_findnext
    result = dir_findfirst(path_get(dir_path), &info);

    while (result == 0) {
        // Skip if we've reached maximum swap disk count
        if (swap_disk_count >= DISK_SLOTS_MAX) {
            break;
        }

        // Skip directories
        if (info.d_type == DT_DIR) {
            result = dir_findnext(path_get(dir_path), &info);
            continue;
        }

        // Check for .ndd extension
        size_t name_len = strlen(info.d_name);
        if (name_len < 4 || strcasecmp(info.d_name + name_len - 4, ".ndd") != 0) {
            result = dir_findnext(path_get(dir_path), &info);
            continue;
        }

        // Skip if this is the primary disk
        if (strcmp(info.d_name, path_last_get(menu->load.disk_slots.primary.disk_path)) == 0) {
            result = dir_findnext(path_get(dir_path), &info);
            continue;
        }

        // Construct full path for this potential swap disk
        path_t *candidate_path = path_clone_push(dir_path, info.d_name);

        // Try to load disk info to validate it's a proper disk
        disk_err_t err = disk_info_load(
            candidate_path,
            &menu->load.disk_slots.swap_slot[swap_disk_count].disk_info
        );

        if (err == DISK_OK) {
            // Valid disk found - add to swap slots
            menu->load.disk_slots.swap_slot[swap_disk_count].disk_path = candidate_path;
            swap_disk_count++;
        } else {
            // Invalid disk - free the path
            path_free(candidate_path);
        }

        result = dir_findnext(path_get(dir_path), &info);
    }

    path_free(dir_path);
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    if (menu->load_pending.disk_file) {
        ui_components_loader_draw(0.0f, NULL);
    } else {
        ui_components_layout_draw();

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "64DD disk information\n"
            "%s",
            disk_filename
        );

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n\n\n\t%.120s\n",
            "No description available."
        );

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n\n\n\n\n\n\n\n"
            "%s%s\n",
            menu->load.rom_path ? "Using Game PAK:\t" : "",
            menu->load.rom_path ? path_last_get(menu->load.rom_path) : ""
        );

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n\n\n\n\n\n\n\n\n"
            "Primary Disk:\n"
            "\tRegion:\t\t%s\n"
            "\tUnique ID:\t%.4s\n"
            "\tVersion:\t\t%hhu\n"
            "\tDisk type:\t%d\n"
            "\tSwap disks:\t%d found\n",

            format_disk_region(menu->load.disk_slots.primary.disk_info.region),
            menu->load.disk_slots.primary.disk_info.id,
            menu->load.disk_slots.primary.disk_info.version,
            menu->load.disk_slots.primary.disk_info.disk_type,
            swap_disk_count
        );

        // Display swap disk info if available
        if (swap_disk_count > 0) {
            ui_components_main_text_draw(
                STL_DEFAULT,
                ALIGN_LEFT, VALIGN_TOP,
                "\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n"
                "\t\t1: %s\n"
                "\t\t2: %s\n"
                "\t\t3: %s\n",

                swap_disk_count > 0 ? path_last_get(menu->load.disk_slots.swap_slot[0].disk_path) : "Empty",
                swap_disk_count > 1 ? path_last_get(menu->load.disk_slots.swap_slot[1].disk_path) : "Empty",
                swap_disk_count > 2 ? path_last_get(menu->load.disk_slots.swap_slot[2].disk_path) : "Empty"
            );
        }
        

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Load and run 64DD disk\n"
            "B: Exit\n"
        );

        if (menu->load.rom_path) {
            ui_components_actions_bar_text_draw(
                STL_DEFAULT,
                ALIGN_RIGHT, VALIGN_TOP,
                "Z: Load with ROM\n"
                "  R:       Options\n"
            );
        } else {
            ui_components_actions_bar_text_draw(
                STL_DEFAULT,
                ALIGN_RIGHT, VALIGN_TOP,
                "\n"
                "R:   Options\n"
            );
        }

        if (boxart != NULL) {
            ui_components_boxart_draw(boxart);
        }

        ui_components_context_menu_draw(&options_context_menu);
    }

    rdpq_detach_show();
}

static void draw_progress (float progress) {
    surface_t *d = (progress >= 1.0f) ? display_get() : display_try_get();

    if (d) {
        rdpq_attach(d, NULL);

        ui_components_background_draw();

        ui_components_loader_draw(progress, "Loading 64DD disk...");

        rdpq_detach_show();
    }
}

static void load (menu_t *menu) {
    cart_load_err_t err;

    if (menu->load.rom_path && menu->load.combined_disk_rom) {
        err = cart_load_n64_rom_and_save(menu, draw_progress, NULL);
        if (err != CART_LOAD_OK) {
            menu_show_error(menu, cart_load_convert_error_message(err));
            return;
        }
    }

    err = cart_load_64dd_ipl_and_disks(menu, draw_progress);
    if (err != CART_LOAD_OK) {
        menu_show_error(menu, cart_load_convert_error_message(err));
        return;
    }

    bookkeeping_history_add(&menu->bookkeeping, menu->load.disk_slots.primary.disk_path, menu->load.rom_path, BOOKKEEPING_TYPE_DISK);
    menu->next_mode = MENU_MODE_BOOT;

    if (menu->load.combined_disk_rom) {
        menu->boot_params->device_type = BOOT_DEVICE_TYPE_ROM;
        menu->boot_params->detect_cic_seed = rom_info_get_cic_seed(&menu->load.rom_info, &menu->boot_params->cic_seed);
        switch (rom_info_get_tv_type(&menu->load.rom_info)) {
            case ROM_TV_TYPE_PAL: menu->boot_params->tv_type = BOOT_TV_TYPE_PAL; break;
            case ROM_TV_TYPE_NTSC: menu->boot_params->tv_type = BOOT_TV_TYPE_NTSC; break;
            case ROM_TV_TYPE_MPAL: menu->boot_params->tv_type = BOOT_TV_TYPE_MPAL; break;
            default: menu->boot_params->tv_type = BOOT_TV_TYPE_PASSTHROUGH; break;
        }
        menu->boot_params->cheat_list = NULL;
        menu->boot_params->clear_rdram = false;
    } else {
        menu->boot_params->device_type = BOOT_DEVICE_TYPE_64DD;
        menu->boot_params->tv_type = BOOT_TV_TYPE_NTSC;
        menu->boot_params->detect_cic_seed = true;
        menu->boot_params->cheat_list = NULL;
        menu->boot_params->clear_rdram = false;
    }
}

static void deinit (void) {
    ui_components_boxart_free(boxart);
}

static bool load_rom(menu_t* menu, path_t* rom_path) {
    if(path_has_value(rom_path)) {
        if (menu->load.rom_path) {
            rom_info_free_meta(&menu->load.rom_info);
            path_free(menu->load.rom_path);
            menu->load.rom_path = NULL;
        }

        menu->load.rom_path = path_clone(rom_path);

        rom_err_t err = rom_config_load(rom_path, &menu->load.rom_info);
        if (err != ROM_OK) {
            rom_info_free_meta(&menu->load.rom_info);
            path_free(menu->load.rom_path);
            menu->load.rom_path = NULL;
            menu_show_error(menu, convert_rom_error_message(err));
            return false;
        }        
    }

    return true;
}

void view_load_disk_init (menu_t *menu) {
    if (menu->load.disk_slots.primary.disk_path) {
        path_free(menu->load.disk_slots.primary.disk_path);
        menu->load.disk_slots.primary.disk_path = NULL;
    }

    menu->load_pending.disk_file = false;

    if(menu->load.load_history_id != -1 || menu->load.load_favorite_id != -1) {
        bookkeeping_item_t* items;
        int16_t item_id = -1;
        uint16_t max_count = 0;

        if(menu->load.load_history_id != -1) {
            item_id = menu->load.load_history_id;
            items = menu->bookkeeping.history_items;
            max_count = HISTORY_COUNT;
        } else if (menu->load.load_favorite_id != -1) {
            item_id = menu->load.load_favorite_id;
            items = menu->bookkeeping.favorite_items;
            max_count = FAVORITES_COUNT;
        }

        // Reset IDs
        menu->load.load_history_id = -1;
        menu->load.load_favorite_id = -1;

        // bounds validation
        if (item_id < 0 || item_id >= max_count) {
            menu_show_error(menu, "Invalid selection index");
            return;
        }

        // Check if the slot is actually populated
        if (items[item_id].bookkeeping_type == BOOKKEEPING_TYPE_EMPTY || 
            !path_has_value(items[item_id].primary_path)) {
            menu_show_error(menu, "Selected item is empty or has no disk path");
            return;
        }

        menu->load.disk_slots.primary.disk_path = path_clone(items[item_id].primary_path);
        if(!load_rom(menu, items[item_id].secondary_path)) {
            return;  // load_rom handles its own error messages
        }
    } else {
        // Existing browser path logic
        menu->load.disk_slots.primary.disk_path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
    }

    disk_filename = path_last_get(menu->load.disk_slots.primary.disk_path);
    disk_err_t err = disk_info_load(menu->load.disk_slots.primary.disk_path, &menu->load.disk_slots.primary.disk_info);
    if (err != DISK_OK) {
        menu_show_error(menu, convert_disk_error_message(err));
        return;
    }

    // Scan for swap disks in the same directory
    scan_for_swap_disks(menu);

    ui_components_context_menu_init(&options_context_menu);
    boxart = ui_components_boxart_init(menu->storage_prefix, menu->load.disk_slots.primary.disk_info.id, NULL, IMAGE_BOXART_FRONT);
}

void view_load_disk_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);

    if (menu->load_pending.disk_file) {
        menu->load_pending.disk_file = false;
        load(menu);
    }

    if (menu->next_mode != MENU_MODE_LOAD_DISK) {
        deinit();
    }
}
