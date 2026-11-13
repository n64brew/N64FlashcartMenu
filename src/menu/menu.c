/**
 * @file menu.c
 * @brief Menu system implementation
 * @ingroup menu
 */

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <libdragon.h>

#include "actions.h"
#include "boot/boot.h"
#include "flashcart/flashcart.h"
#include "fonts.h"
#include "theme.h"
#include "hdmi.h"
#include "menu_state.h"
#include "menu.h"
#include "audio_player.h"
#include "jpeg_decoder.h"
#include "png_decoder.h"
#include "settings.h"
#include "sound.h"
#include "usb_comm.h"
#include "utils/fs.h"
#include "views/views.h"

#define MENU_DIRECTORY              "/menu"
#define MENU_SETTINGS_FILE          "config.ini"
#define MENU_CUSTOM_FONT_FILE       "custom.font64"
#define MENU_CUSTOM_BGM_FILE        "custom_bgm.wav64"
#define MENU_ROM_LOAD_HISTORY_FILE  "history.ini"

#define MENU_CACHE_DIRECTORY        "cache"
#define BACKGROUND_CACHE_FILE       "background.data"

#define FPS_LIMIT                   (60.0f)

static menu_t *menu;

static bool interlaced = true;

/**
 * @brief Initialize the menu system.
 * 
 * @param boot_params Pointer to the boot parameters structure.
 */
static void menu_init (boot_params_t *boot_params) {
    menu = calloc(1, sizeof(menu_t));
    assert(menu != NULL);

    menu->boot_params = boot_params;

    menu->mode = MENU_MODE_NONE;
    menu->next_mode = MENU_MODE_STARTUP;

    menu->flashcart_err = flashcart_init(&menu->storage_prefix);
    if (menu->flashcart_err != FLASHCART_OK) {
        menu->next_mode = MENU_MODE_FAULT;
    }

    // The menu runs with or without an Expansion Pak, checking is_memory_expanded()
    // where it matters. Checking before anything large is allocated also stops
    // libdragon warning that the ROM requires one.
    debugf("Expansion Pak: %s\n", is_memory_expanded() ? "installed" : "not installed");

    joypad_init();
    timer_init();
    rtc_init();
    rspq_init();
    rdpq_init();
    dfs_init(DFS_DEFAULT_LOCATION);

    actions_init();
    sound_init_default();
    sound_init_sfx();
    ui_components_sprites_init();

    hdmi_clear_game_id();

    path_t *path = path_init(menu->storage_prefix, MENU_DIRECTORY);

    directory_create(path_get(path));

    path_push(path, MENU_SETTINGS_FILE);
    settings_init(path_get(path));
    settings_load(&menu->settings);
    theme_set_by_name(menu->settings.theme);
    path_pop(path);

    path_push(path, MENU_ROM_LOAD_HISTORY_FILE);
    bookkeeping_init(path_get(path));
    bookkeeping_load(&menu->bookkeeping);
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    path_pop(path);

    // Force interlacing off in VI settings for TVs and other devices that struggle with interlaced video input.
    interlaced = !menu->settings.force_progressive_scan;

    resolution_t resolution = {
        .width = 640,
        .height = 480,
        .interlaced = interlaced ? INTERLACE_HALF : INTERLACE_OFF,
    };

    display_init(resolution, DEPTH_16_BPP, 2, GAMMA_NONE, interlaced ? FILTERS_DISABLED : FILTERS_RESAMPLE);
    
    if (menu->settings.pal60_enabled) { // it is not given that hardware VI mods understand the output
        tv_type_t tv_type = get_tv_type();
        if (tv_type == TV_PAL) {
            // Set VI timing so it will use 60Hz signal.
            vi_set_timing_preset(&VI_TIMING_PAL60);

            // FIXME: timeout and restore to PAL 50Hz if not shown, 
            // this should be added as a button confirm, or reset combo, rather than re-setting via manual edit of the INI?.
            //vi_set_timing_preset(&VI_TIMING_PAL);
        }
    }
    
    display_set_fps_limit(FPS_LIMIT);

    path_push(path, MENU_CUSTOM_FONT_FILE);
    fonts_init(path_get(path));
    path_pop(path);

    path_push(path, MENU_CUSTOM_BGM_FILE);
    sound_set_bgm_path(path_get(path));
    sound_init_bgm();
    path_pop(path);

    path_push(path, MENU_CACHE_DIRECTORY);
    directory_create(path_get(path));

    path_push(path, BACKGROUND_CACHE_FILE);
    if (!menu->settings.theme) {
        // Settings predating themes always drew a saved background image; keep showing it.
        if (file_exists(path_get(path))) theme_select_custom_image();
        menu->settings.theme = strdup(theme_get()->name);
        settings_save(&menu->settings);
    }
    ui_components_background_init(path_get(path));

    path_free(path);

    sound_use_sfx(menu->settings.soundfx_enabled);
    sound_use_bgm(menu->settings.bgm_enabled);

    menu->browser.directory = path_init(menu->storage_prefix, menu->settings.default_directory);
    if (!directory_exists(path_get(menu->browser.directory))) {
        path_free(menu->browser.directory);
        menu->browser.directory = path_init(menu->storage_prefix, "/");
    }

    debugf("N64FlashcartMenu debugging...\n");
}

/**
 * @brief Deinitialize the menu system.
 * 
 * @param menu Pointer to the menu structure.
 */
static void menu_deinit (menu_t *menu) {

    sound_deinit();
    
    ui_components_background_free();
    ui_components_file_list_free();
    rspq_wait();  // Execute deferred callbacks (e.g., display list freeing) before closing RSPQ

    hdmi_send_game_id(menu->boot_params);

    path_free(menu->load.disk_slots.primary.disk_path);
    path_free(menu->load.rom_path);
    for (int i = 0; i < menu->browser.entries; i++) {
        free(menu->browser.list[i].name);
    }
    free(menu->browser.list);
    path_free(menu->browser.directory);
    view_grid_shutdown();
    free(menu);

    display_close();

    rspq_wait();
    rdpq_close();
    rspq_close();
    rtc_close();
    timer_close();
    joypad_close();

    flashcart_deinit();
}

/**
 * @brief View structure containing initialization and display functions.
 */
typedef const struct {
    menu_mode_t id; /**< View ID */
    void (*init) (menu_t *menu); /**< Initialization function */
    void (*show) (menu_t *menu, surface_t *display); /**< Display function */
} view_t;

static view_t menu_views[] = {
    { MENU_MODE_STARTUP, view_startup_init, view_startup_display },
    { MENU_MODE_GRID, view_grid_init, view_grid_display },
    { MENU_MODE_BROWSER, view_browser_init, view_browser_display },
    { MENU_MODE_FILE_INFO, view_file_info_init, view_file_info_display },
    { MENU_MODE_IMAGE_VIEWER, view_image_viewer_init, view_image_viewer_display },
    { MENU_MODE_TEXT_VIEWER, view_text_viewer_init, view_text_viewer_display },
    { MENU_MODE_MUSIC_PLAYER, view_music_player_init, view_music_player_display },
    { MENU_MODE_CREDITS, view_credits_init, view_credits_display },
    { MENU_MODE_SETTINGS, view_settings_init, view_settings_display },
    { MENU_MODE_CONTROLLER_PAK_DUMP_INFO, view_controller_pak_dump_info_init, view_controller_pak_dump_info_display },
    { MENU_MODE_CONTROLLER_PAK_DUMP_NOTE_INFO, view_controller_pak_note_dump_info_init, view_controller_pak_note_dump_info_display },
    { MENU_MODE_LOAD_ROM, view_load_rom_init, view_load_rom_display },
    { MENU_MODE_LOAD_DISK, view_load_disk_init, view_load_disk_display },
    { MENU_MODE_LOAD_EMULATOR, view_load_emulator_init, view_load_emulator_display },
    { MENU_MODE_ERROR, view_error_init, view_error_display },
    { MENU_MODE_FAULT, view_fault_init, view_fault_display },
    { MENU_MODE_FAVORITE, view_favorite_init, view_favorite_display },
    { MENU_MODE_HISTORY, view_history_init, view_history_display },
    { MENU_MODE_DATEL_CODE_EDITOR, view_datel_code_editor_init, view_datel_code_editor_display },
    { MENU_MODE_EXTRACT_FILE, view_extract_file_init, view_extract_file_display }
};

/**
 * @brief Get the view structure for the specified menu mode.
 * 
 * @param id The menu mode ID.
 * @return view_t* Pointer to the view structure.
 */
static view_t *menu_get_view (menu_mode_t id) {
    for (int i = 0; i < sizeof(menu_views) / sizeof(view_t); i++) {
        if (menu_views[i].id == id) {
            return &menu_views[i];
        }
    }
    return NULL;
}

/**
 * @brief Run the menu system.
 * 
 * @param boot_params Pointer to the boot parameters structure.
 */
void menu_run (boot_params_t *boot_params) {
    menu_init(boot_params);

    while (true) {
        surface_t *display = display_try_get();

        if (display != NULL) {
            actions_update(menu);

            view_t *view = menu_get_view(menu->mode);
            if (view && view->show) {
                view->show(menu, display);
            } else {
                rdpq_attach_clear(display, NULL);
                rdpq_detach_wait();
                display_show(display);
            }

            if (menu->mode == MENU_MODE_BOOT) {
                // exit loop to deinit menu and boot the game
                break;
            }

            while (menu->mode != menu->next_mode) {
                menu->mode = menu->next_mode;

                view_t *next_view = menu_get_view(menu->next_mode);
                if (next_view && next_view->init) {
                    next_view->init(menu);
                }
            }

            time(&menu->current_time);
        }

        sound_poll();

        png_decoder_poll();
        jpeg_decoder_poll();

        usb_comm_poll(menu);
    }

    while (exception_reset_time() > 0) {
        // Do nothing if reset button was pressed
        // but this could be used to display a message or perform other actions while waiting for reset, 
        // like fade out the screen or show a countdown timer.
    }

    menu_deinit(menu);

    // This should now return to main.c which now boots the game.
}
