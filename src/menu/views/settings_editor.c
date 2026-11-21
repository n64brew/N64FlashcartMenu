#include <stdbool.h>
#include "../sound.h"
#include "../settings.h"
#include "views.h"
#include "../ui_components/constants.h"

#ifdef BETA_SETTINGS
#define SETTINGS_TAB_COUNT 5
#else
#define SETTINGS_TAB_COUNT 4
#endif
#define SETTINGS_MAX_ROWS 8
#define SETTINGS_ENTRY_NAME_LENGTH 256

static bool show_message_reset_settings = false;
static bool show_setting_change_confirmation;
static bool show_bgm_track_selection_menu;
static bool pending_remove_background;
static int selected_settings_tab;
static int selected_settings_rows[SETTINGS_TAB_COUNT];
static int pending_settings_tab;
static int pending_settings_row;
static int pending_setting_choice;

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static void set_loading_progress_bar_enabled_type (menu_t *menu, void *arg) {
    menu->settings.loading_progress_bar_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}
#endif

static void set_protected_entries_type (menu_t *menu, void *arg) {
    menu->settings.show_protected_entries = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);

    menu->browser.reload = true;
}

static void set_use_saves_folder_type (menu_t *menu, void *arg) {
    menu->settings.use_saves_folder = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_show_saves_folder_type (menu_t *menu, void *arg) {
    menu->settings.show_saves_folder = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);

    menu->browser.reload = true;
}

static void set_show_save_files_type (menu_t *menu, void *arg) {
    menu->settings.show_save_files = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);

    menu->browser.reload = true;
}

static void set_show_cheat_files_type (menu_t *menu, void *arg) {
    menu->settings.show_cheat_files = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);

    menu->browser.reload = true;
}
    
static void set_soundfx_enabled_type (menu_t *menu, void *arg) {
    menu->settings.soundfx_enabled = (bool)(uintptr_t)(arg);
    sound_use_sfx(menu->settings.soundfx_enabled);
    settings_save(&menu->settings);
}

static void set_bgm_enabled_type (menu_t *menu, void *arg) {
    menu->settings.bgm_enabled = (bool)(uintptr_t)(arg);
    sound_use_bgm(menu->settings.bgm_enabled);
    settings_save(&menu->settings);
}

static void set_bgm_track_type (menu_t *menu, void *arg) {
    menu->settings.bgm_track = (bgm_track_t)(uintptr_t)(arg);
    sound_set_bgm_track(menu->settings.bgm_track);
    settings_save(&menu->settings);
}

static void request_bgm_track_change (menu_t *menu, void *arg) {
    pending_settings_tab = selected_settings_tab;
    pending_settings_row = selected_settings_rows[selected_settings_tab];
    pending_setting_choice = (int)(uintptr_t)(arg);
    show_bgm_track_selection_menu = false;
    show_setting_change_confirmation = menu->settings.bgm_track != pending_setting_choice;
}

static void set_pal60_type (menu_t *menu, void *arg) {
    bool pal60_try_enable = (bool)(uintptr_t)(arg);
    tv_type_t tv_type = get_tv_type();
    // FIXME: we can check it is supported by adding a warning and setting it, with a confirmation.
    if (pal60_try_enable && (tv_type == TV_PAL)) {
        //enable it without needing to reboot the console.
        // FIXME: Add message box to press a button as confirmation. 
        // Set VI timing so it will use 60Hz signal.
        vi_set_timing_preset(&VI_TIMING_PAL60);

        // FIXME: timeout and restore to PAL 50Hz if message not shown, 
        //vi_set_timing_preset(&VI_TIMING_PAL);
        
    }
    else if (!pal60_try_enable && (tv_type == TV_PAL)){
        //disable it without needing to reboot the console.
        // Set VI timing so it will use 50Hz signal.
        vi_set_timing_preset(&VI_TIMING_PAL);
        
    }
    else {
        //not PAL, cannot enable PAL60
        pal60_try_enable = false;
    }
    
    menu->settings.pal60_enabled = pal60_try_enable;
    settings_save(&menu->settings);
}

static void set_wrap_file_list_scrolling_type (menu_t *menu, void *arg) {
    menu->settings.wrap_file_list_scrolling = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_show_full_rom_descriptions_type (menu_t *menu, void *arg) {
    menu->settings.show_full_rom_descriptions = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

#ifndef FEATURE_AUTOLOAD_ROM_ENABLED
static void set_use_rom_fast_reboot_enabled_type (menu_t *menu, void *arg) {
    menu->settings.rom_fast_reboot_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}
#endif

#ifdef BETA_SETTINGS
static void set_show_browser_file_extensions_type(menu_t *menu, void *arg) {
    menu->settings.show_browser_file_extensions = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
    menu->browser.reload = true;
}

static void set_show_browser_rom_tags_type (menu_t *menu, void *arg) {
    menu->settings.show_browser_rom_tags = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

static void set_rumble_enabled_type (menu_t *menu, void *arg) {
    menu->settings.rumble_enabled = (bool)(uintptr_t)(arg);
    settings_save(&menu->settings);
}

// static void set_use_default_settings (menu_t *menu, void *arg) {
//     // FIXME: add implementation
//     menu->browser.reload = true;
// }
#endif

static void remove_background_image (menu_t *menu, void *arg) {
    (void)arg;
    (void)menu;
    ui_components_background_clear();
}

static void request_reset_settings(menu_t *menu, void *arg) {
    (void)menu;
    (void)arg;
    show_message_reset_settings = true;
}

static void request_remove_background(menu_t *menu, void *arg) {
    (void)menu;
    (void)arg;
    pending_remove_background = true;
    show_setting_change_confirmation = true;
}

static component_context_menu_t options_context_menu = { .list = {
    { .text = "Reset Settings", .action = request_reset_settings },
    { .text = "Remove Background", .action = request_remove_background },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static int get_loading_progress_bar_enabled_current_selection (menu_t *menu) {
    return menu->settings.loading_progress_bar_enabled ? 0 : 1;
}

static component_context_menu_t set_loading_progress_bar_enabled_context_menu = {
    .get_default_selection = get_loading_progress_bar_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_loading_progress_bar_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_loading_progress_bar_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

static int get_protected_entries_current_selection (menu_t *menu) {
    return menu->settings.show_protected_entries ? 0 : 1;
}

static component_context_menu_t set_protected_entries_type_context_menu = {
    .get_default_selection = get_protected_entries_current_selection,
    .list = {
        {.text = "On", .action = set_protected_entries_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_protected_entries_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_soundfx_enabled_current_selection (menu_t *menu) {
    return menu->settings.soundfx_enabled ? 0 : 1;
}

static component_context_menu_t set_soundfx_enabled_type_context_menu = {
    .get_default_selection = get_soundfx_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_soundfx_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_soundfx_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_bgm_enabled_current_selection (menu_t *menu) {
    return menu->settings.bgm_enabled ? 0 : 1;
}

static component_context_menu_t set_bgm_enabled_type_context_menu = {
    .get_default_selection = get_bgm_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_bgm_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_bgm_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_bgm_track_current_selection (menu_t *menu) {
    return menu->settings.bgm_track;
}

static component_context_menu_t set_bgm_track_type_context_menu = {
    .get_default_selection = get_bgm_track_current_selection,
    .list = {
        {.text = "Flying Dreams", .action = set_bgm_track_type, .arg = (void *)(uintptr_t)(BGM_TRACK_FLYING_DREAMS) },
        {.text = "Warm Waves", .action = set_bgm_track_type, .arg = (void *)(uintptr_t)(BGM_TRACK_WARM_WAVES) },
        {.text = "Blue Horizon", .action = set_bgm_track_type, .arg = (void *)(uintptr_t)(BGM_TRACK_BLUE_HORIZON) },
        {.text = "White Stones", .action = set_bgm_track_type, .arg = (void *)(uintptr_t)(BGM_TRACK_WHITE_STONES) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static component_context_menu_t bgm_track_selection_context_menu = { .list = {
    {.text = "Flying Dreams", .action = request_bgm_track_change, .arg = (void *)(uintptr_t)(BGM_TRACK_FLYING_DREAMS) },
    {.text = "Warm Waves", .action = request_bgm_track_change, .arg = (void *)(uintptr_t)(BGM_TRACK_WARM_WAVES) },
    {.text = "Blue Horizon", .action = request_bgm_track_change, .arg = (void *)(uintptr_t)(BGM_TRACK_BLUE_HORIZON) },
    {.text = "White Stones", .action = request_bgm_track_change, .arg = (void *)(uintptr_t)(BGM_TRACK_WHITE_STONES) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_use_saves_folder_current_selection (menu_t *menu) {
    return menu->settings.use_saves_folder ? 0 : 1;
}

static component_context_menu_t set_use_saves_folder_type_context_menu = {
    .get_default_selection = get_use_saves_folder_current_selection,
    .list = {
        {.text = "On", .action = set_use_saves_folder_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_use_saves_folder_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_saves_folder_current_selection (menu_t *menu) {
    return menu->settings.show_saves_folder ? 0 : 1;
}

static component_context_menu_t set_show_saves_folder_type_context_menu = {
    .get_default_selection = get_show_saves_folder_current_selection,
    .list = {
        {.text = "On", .action = set_show_saves_folder_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_saves_folder_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_save_files_current_selection (menu_t *menu) {
    return menu->settings.show_save_files ? 0 : 1;
}

static component_context_menu_t set_show_save_files_type_context_menu = {
    .get_default_selection = get_show_save_files_current_selection,
    .list = {
        {.text = "On", .action = set_show_save_files_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_save_files_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_cheat_files_current_selection (menu_t *menu) {
    return menu->settings.show_cheat_files ? 0 : 1;
}

static component_context_menu_t set_show_cheat_files_type_context_menu = {
    .get_default_selection = get_show_cheat_files_current_selection,
    .list = {
        {.text = "On", .action = set_show_cheat_files_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_cheat_files_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_pal60_current_selection (menu_t *menu) {
    return menu->settings.pal60_enabled ? 0 : 1;
}

static component_context_menu_t set_pal60_type_context_menu = {
    .get_default_selection = get_pal60_current_selection,
    .list = {
        {.text = "On", .action = set_pal60_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_pal60_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_wrap_file_list_scrolling_current_selection (menu_t *menu) {
    return menu->settings.wrap_file_list_scrolling ? 0 : 1;
}

static component_context_menu_t set_wrap_file_list_scrolling_context_menu = {
    .get_default_selection = get_wrap_file_list_scrolling_current_selection,
    .list = {
        {.text = "On", .action = set_wrap_file_list_scrolling_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_wrap_file_list_scrolling_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_full_rom_descriptions_current_selection (menu_t *menu) {
    return menu->settings.show_full_rom_descriptions ? 0 : 1;
}

static component_context_menu_t set_show_full_rom_descriptions_context_menu = {
    .get_default_selection = get_show_full_rom_descriptions_current_selection,
    .list = {
        {.text = "On", .action = set_show_full_rom_descriptions_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_full_rom_descriptions_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

#ifndef FEATURE_AUTOLOAD_ROM_ENABLED
static int get_use_rom_fast_reboot_current_selection (menu_t *menu) {
    return menu->settings.rom_fast_reboot_enabled ? 0 : 1;
}

static component_context_menu_t set_use_rom_fast_reboot_context_menu = {
    .get_default_selection = get_use_rom_fast_reboot_current_selection,
    .list = {
        {.text = "On", .action = set_use_rom_fast_reboot_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_use_rom_fast_reboot_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

#ifdef BETA_SETTINGS
static int get_show_browser_file_extensions_current_selection (menu_t *menu) {
    return menu->settings.show_browser_file_extensions ? 0 : 1;
}

static component_context_menu_t set_show_browser_file_extensions_context_menu = {
    .get_default_selection = get_show_browser_file_extensions_current_selection,
    .list = {
        { .text = "On", .action = set_show_browser_file_extensions_type, .arg = (void *)(uintptr_t)(true) },
        { .text = "Off", .action = set_show_browser_file_extensions_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_show_browser_rom_tags_current_selection (menu_t *menu) {
    return menu->settings.show_browser_rom_tags ? 0 : 1;
}

static component_context_menu_t set_show_browser_rom_tags_context_menu = {
    .get_default_selection = get_show_browser_rom_tags_current_selection,
    .list = {
        {.text = "On", .action = set_show_browser_rom_tags_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_show_browser_rom_tags_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};

static int get_rumble_enabled_current_selection (menu_t *menu) {
    return menu->settings.rumble_enabled ? 0 : 1;
}

static component_context_menu_t set_rumble_enabled_type_context_menu = {
    .get_default_selection = get_rumble_enabled_current_selection,
    .list = {
        {.text = "On", .action = set_rumble_enabled_type, .arg = (void *)(uintptr_t)(true) },
        {.text = "Off", .action = set_rumble_enabled_type, .arg = (void *)(uintptr_t)(false) },
    COMPONENT_CONTEXT_MENU_LIST_END,
}};
#endif

typedef struct {
    const char *label;
    component_context_menu_t *choices;
    const char *(*get_value)(menu_t *menu);
    void (*action)(menu_t *menu, void *arg);
    const char *description;
} settings_row_t;

typedef struct {
    const settings_row_t *rows;
    int row_count;
} settings_tab_t;

#define SETTINGS_ROW_COUNT(rows) (sizeof(rows) / sizeof((rows)[0]))

static const char *get_default_directory_value(menu_t *menu) {
    return menu->settings.default_directory;
}

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
static const char *get_rom_autoload_value(menu_t *menu) {
    return menu->settings.rom_autoload_enabled ? "On" : "Off";
}
#endif

static const settings_row_t general_settings[] = {
    { "Default Directory", NULL, get_default_directory_value, NULL, NULL },
    { "Use Saves Folder", &set_use_saves_folder_type_context_menu, NULL, NULL, NULL },
    { "Wrap File List", &set_wrap_file_list_scrolling_context_menu, NULL, NULL, NULL },
    { "Show Full ROM Descriptions", &set_show_full_rom_descriptions_context_menu, NULL, NULL, NULL },
};

static const settings_row_t browser_settings[] = {
    { "Show Hidden Files", &set_protected_entries_type_context_menu, NULL, NULL, NULL },
    { "Show Saves Folder", &set_show_saves_folder_type_context_menu, NULL, NULL, NULL },
    { "Show Save Files", &set_show_save_files_type_context_menu, NULL, NULL, NULL },
    { "Show Cheat Files", &set_show_cheat_files_type_context_menu, NULL, NULL, NULL },
};

static const settings_row_t audio_settings[] = {
    { "Sound Effects", &set_soundfx_enabled_type_context_menu, NULL, NULL, NULL },
    { "Background Music", &set_bgm_enabled_type_context_menu, NULL, NULL, NULL },
    { "Background Music Track", &set_bgm_track_type_context_menu, NULL, NULL, NULL },
};

#ifdef BETA_SETTINGS
static const settings_row_t beta_settings[] = {
    { "Show ROM Extensions", &set_show_browser_file_extensions_context_menu, NULL, NULL, NULL },
    { "Show ROM Tags", &set_show_browser_rom_tags_context_menu, NULL, NULL, NULL },
    { "Rumble Feedback", &set_rumble_enabled_type_context_menu, NULL, NULL, NULL },
};
#endif

static const settings_row_t system_settings[] = {
    { "PAL60 Mode", &set_pal60_type_context_menu, NULL, NULL,
      "Warning: PAL60 may darken the display; edit menu/config.ini to recover." },
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    { "Autoload ROM", NULL, get_rom_autoload_value, NULL, NULL },
    { "ROM Loading Bar", &set_loading_progress_bar_enabled_context_menu, NULL, NULL, NULL },
#else
    { "Fast Reboot ROM", &set_use_rom_fast_reboot_context_menu, NULL, NULL, NULL },
#endif
};

static const char *settings_tab_labels[SETTINGS_TAB_COUNT] = {
    "General",
    "Browser",
    "Audio",
    "System",
#ifdef BETA_SETTINGS
    "Beta",
#endif
};

static const settings_tab_t settings_tabs[SETTINGS_TAB_COUNT] = {
    { general_settings, SETTINGS_ROW_COUNT(general_settings) },
    { browser_settings, SETTINGS_ROW_COUNT(browser_settings) },
    { audio_settings, SETTINGS_ROW_COUNT(audio_settings) },
    { system_settings, SETTINGS_ROW_COUNT(system_settings) },
#ifdef BETA_SETTINGS
    { beta_settings, SETTINGS_ROW_COUNT(beta_settings) },
#endif
};

static entry_t settings_entries[SETTINGS_MAX_ROWS];
static char settings_entry_names[SETTINGS_MAX_ROWS][SETTINGS_ENTRY_NAME_LENGTH];

static const char *setting_row_value(menu_t *menu, const settings_row_t *row) {
    if (row->choices) {
        int selected = row->choices->get_default_selection(menu);
        return row->choices->list[selected].text;
    }

    if (row->get_value) {
        return row->get_value(menu);
    }

    return row->action ? "Apply" : "";
}

static void populate_settings_list(menu_t *menu, const settings_tab_t *tab) {
    for (int i = 0; i < tab->row_count; i++) {
        const settings_row_t *row = &tab->rows[i];
        snprintf(
            settings_entry_names[i],
            sizeof(settings_entry_names[i]),
            "%-28s  %s",
            row->label,
            setting_row_value(menu, row)
        );
        settings_entries[i] = (entry_t) {
            .name = settings_entry_names[i],
            .type = (row->choices || row->action) ? ENTRY_TYPE_ROM : ENTRY_TYPE_OTHER,
            .size = 0,
            .index = i,
        };
    }
}


static void process (menu_t *menu) {
    if (show_message_reset_settings) {
        if (menu->actions.enter) {
            settings_reset_to_defaults();
            menu_show_error(menu, "Reboot N64 to take effect!");
            show_message_reset_settings = false;
            sound_play_effect(SFX_ENTER);
        } else if (menu->actions.back) {
            show_message_reset_settings = false;
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    if (show_setting_change_confirmation) {
        if (menu->actions.enter) {
            if (pending_remove_background) {
                remove_background_image(menu, NULL);
            } else {
                const settings_row_t *row =
                    &settings_tabs[pending_settings_tab].rows[pending_settings_row];
                row->choices->list[pending_setting_choice].action(
                    menu,
                    row->choices->list[pending_setting_choice].arg
                );
            }
            pending_remove_background = false;
            show_setting_change_confirmation = false;
            sound_play_effect(SFX_ENTER);
        } else if (menu->actions.back) {
            pending_remove_background = false;
            show_setting_change_confirmation = false;
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    if (ui_components_context_menu_process(menu, &options_context_menu)) {
        return;
    }

    if (show_bgm_track_selection_menu) {
        ui_components_context_menu_process(menu, &bgm_track_selection_context_menu);
        if (menu->actions.back) {
            show_bgm_track_selection_menu = false;
        }
        return;
    }

    const settings_tab_t *tab = &settings_tabs[selected_settings_tab];

    if (menu->actions.go_left) {
        selected_settings_tab = (selected_settings_tab + SETTINGS_TAB_COUNT - 1) % SETTINGS_TAB_COUNT;
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.go_right) {
        selected_settings_tab = (selected_settings_tab + 1) % SETTINGS_TAB_COUNT;
        sound_play_effect(SFX_SETTING);
    } else if (menu->actions.go_up && tab->row_count > 0) {
        if (menu->settings.wrap_file_list_scrolling) {
            selected_settings_rows[selected_settings_tab] =
                (selected_settings_rows[selected_settings_tab] + tab->row_count - 1) % tab->row_count;
        } else if (selected_settings_rows[selected_settings_tab] > 0) {
            selected_settings_rows[selected_settings_tab]--;
        }
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_down && tab->row_count > 0) {
        if (menu->settings.wrap_file_list_scrolling) {
            selected_settings_rows[selected_settings_tab] =
                (selected_settings_rows[selected_settings_tab] + 1) % tab->row_count;
        } else if (selected_settings_rows[selected_settings_tab] < tab->row_count - 1) {
            selected_settings_rows[selected_settings_tab]++;
        }
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.enter && tab->row_count > 0) {
        int selected_row = selected_settings_rows[selected_settings_tab];
        const settings_row_t *row = &tab->rows[selected_row];
        if (row->choices) {
            if (row->choices == &set_bgm_track_type_context_menu) {
                ui_components_context_menu_init(&bgm_track_selection_context_menu);
                ui_components_context_menu_show(&bgm_track_selection_context_menu);
                bgm_track_selection_context_menu.row_selected = row->choices->get_default_selection(menu);
                show_bgm_track_selection_menu = true;
            } else {
                int current_value = row->choices->get_default_selection(menu);
                pending_settings_tab = selected_settings_tab;
                pending_settings_row = selected_row;
                pending_setting_choice = current_value + 1;
                if (row->choices->list[pending_setting_choice].text == NULL) {
                    pending_setting_choice = 0;
                }
                show_setting_change_confirmation = true;
            }
            sound_play_effect(SFX_SETTING);
        } else if (row->action) {
            pending_settings_tab = selected_settings_tab;
            pending_settings_row = selected_row;
            pending_setting_choice = -1;
            show_setting_change_confirmation = true;
            sound_play_effect(SFX_SETTING);
        }
    } else if (menu->actions.back) {
        menu->next_mode = MENU_MODE_BROWSER;
        sound_play_effect(SFX_EXIT);
    } else if (menu->actions.options) {
        sound_play_effect(SFX_SETTING);
        ui_components_context_menu_show(&options_context_menu);
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();
    const settings_tab_t *tab = &settings_tabs[selected_settings_tab];
    populate_settings_list(menu, tab);

    ui_components_tabs_draw(
        settings_tab_labels,
        SETTINGS_TAB_COUNT,
        selected_settings_tab,
        (VISIBLE_AREA_X1 - VISIBLE_AREA_X0) / (float)SETTINGS_TAB_COUNT
    );
    ui_components_layout_draw_tabbed();
    ui_components_file_list_draw(
        settings_entries,
        tab->row_count,
        selected_settings_rows[selected_settings_tab],
        SETTINGS_MAX_ROWS,
        false,
        "Setting                       Value",
        NULL
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "A: Change\n"
        "B: Exit"
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_RIGHT, VALIGN_TOP,
        "R: Options"
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "◀ Tabs ▶ | ▼/▲: Settings"
    );

    if (show_bgm_track_selection_menu) {
        ui_components_context_menu_draw(&bgm_track_selection_context_menu);
    } else {
        ui_components_context_menu_draw(&options_context_menu);
    }

    if (show_message_reset_settings) {
        ui_components_messagebox_draw(
            "Reset settings?\n\n"
            "A: Yes, B: Back"
        );
    } else if (show_setting_change_confirmation) {
        if (pending_remove_background) {
            ui_components_messagebox_draw(
                "Remove the background image?\n\n"
                "A: Yes        B: No"
            );
        } else {
            const settings_row_t *row = &settings_tabs[pending_settings_tab].rows[pending_settings_row];
            if (row->description) {
                int current_value = row->choices->get_default_selection(menu);
                ui_components_messagebox_draw(
                    "Change %s from %s to %s?\n\n"
                    "%s\n\n"
                    "A: Yes        B: No",
                    row->label,
                    row->choices->list[current_value].text,
                    row->choices->list[pending_setting_choice].text,
                    row->description
                );
            } else {
                int current_value = row->choices->get_default_selection(menu);
                ui_components_messagebox_draw(
                    "Change %s from %s to %s?\n\n"
                    "A: Yes        B: No",
                    row->label,
                    row->choices->list[current_value].text,
                    row->choices->list[pending_setting_choice].text
                );
            }
        }
    }

    rdpq_detach_show();
}


void view_settings_init (menu_t *menu) {
    (void)menu;
    show_message_reset_settings = false;
    show_setting_change_confirmation = false;
    pending_remove_background = false;
    selected_settings_tab = 0;
    pending_settings_tab = 0;
    pending_settings_row = 0;
    pending_setting_choice = -1;
    for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
        selected_settings_rows[i] = 0;
    }
    ui_components_context_menu_init(&options_context_menu);
}

void view_settings_display (menu_t *menu, surface_t *display) {
    process(menu);
    
    draw(menu, display);
}
