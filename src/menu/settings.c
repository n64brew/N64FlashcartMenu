#include <libdragon.h>
#include <ctype.h>
#include "ini_parser.h"

#include "settings.h"
#include "utils/fs.h"


static char *settings_path = NULL;


static void load_custom_palette(ini_t *ini) {
    const char *text = ini_get_string(ini, "custom_theme", "palette", "");
    color_t colors[THEME_COLOR_COUNT];
    if (strlen(text) != sizeof(colors) * 2) return;
    uint8_t *bytes = (uint8_t *)colors;
    for (unsigned i = 0; i < sizeof(colors); i++) {
        char hex[] = { text[i * 2], text[i * 2 + 1], 0 };
        if (!isxdigit((unsigned char)hex[0]) || !isxdigit((unsigned char)hex[1])) return;
        bytes[i] = strtoul(hex, NULL, 16);
    }
    theme_custom_set_palette(colors);
}

static void save_custom_palette(ini_t *ini) {
    color_t colors[THEME_COLOR_COUNT];
    theme_custom_get_palette(colors);
    char text[sizeof(colors) * 2 + 1];
    const uint8_t *bytes = (const uint8_t *)colors;
    for (unsigned i = 0; i < sizeof(colors); i++)
        snprintf(text + i * 2, 3, "%02x", bytes[i]);
    ini_set_string(ini, "custom_theme", "palette", text);
}

static settings_t init = {
    .schema_revision = 1,
    .first_run = true,
    .pal60_enabled = false,
    .force_progressive_scan = false,
    .show_protected_entries = false,
    .default_directory = "/",
    .boot_into_grid = false,
    .grid_directory = "/",
    .use_saves_folder = true,
    .show_saves_folder = false,
    .show_save_files = false,
    .show_cheat_files = false,
    .show_rom_configuration_files = false,
    .soundfx_enabled = false,
    .bgm_enabled = false,
    .theme = "N64",
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    .rom_autoload_enabled = false,
    .rom_autoload_path = "",
    .rom_autoload_filename = "",
    .loading_progress_bar_enabled = true,
#else
    .rom_fast_reboot_enabled = false,
#endif    
    /* Beta feature flags (should always init to default) */
    .show_browser_file_extensions = true,
    .show_browser_rom_tags = true,
    .wrap_file_list_scrolling = false,
    .rumble_enabled = false,
};


void settings_init (char *path) {
    if (settings_path) {
        free(settings_path);
    }
    settings_path = strdup(path);
}

void settings_load (settings_t *settings) {
    theme_custom_reset();
    if (!file_exists(settings_path)) {
        settings_save(&init);
    }

    ini_t *ini = ini_try_load(settings_path);

    settings->schema_revision = ini_get_int(ini, "menu", "schema_revision", init.schema_revision);
    settings->first_run = ini_get_bool(ini, "menu", "first_run", init.first_run);
    settings->pal60_enabled = ini_get_bool(ini, "menu", "pal60", init.pal60_enabled);
    settings->force_progressive_scan = ini_get_bool(ini, "menu", "force_progressive_scan", init.force_progressive_scan);
    settings->show_protected_entries = ini_get_bool(ini, "menu", "show_protected_entries", init.show_protected_entries);
    free(settings->default_directory);
    settings->default_directory = strdup(ini_get_string(ini, "menu", "default_directory", init.default_directory));
    settings->boot_into_grid = ini_get_bool(ini, "menu", "boot_into_grid", init.boot_into_grid);
    free(settings->grid_directory);
    settings->grid_directory = strdup(ini_get_string(ini, "menu", "grid_directory", settings->default_directory));
    settings->use_saves_folder = ini_get_bool(ini, "menu", "use_saves_folder", init.use_saves_folder);
    settings->show_saves_folder = ini_get_bool(ini, "menu", "show_saves_folder", init.show_saves_folder);
    settings->show_save_files = ini_get_bool(ini, "menu", "show_save_files", init.show_save_files);
    settings->show_cheat_files = ini_get_bool(ini, "menu", "show_cheat_files", init.show_cheat_files);
    settings->show_rom_configuration_files = ini_get_bool(ini, "menu", "show_rom_configuration_files", init.show_rom_configuration_files);
    settings->soundfx_enabled = ini_get_bool(ini, "menu", "soundfx_enabled", init.soundfx_enabled);
    settings->bgm_enabled = ini_get_bool(ini, "menu", "bgm_enabled", init.bgm_enabled);

    theme_custom_copy_from(theme_get_by_name(ini_get_string(ini, "custom_theme", "base", "Custom")));
    load_custom_palette(ini);
    theme_custom_set_hue_shift(ini_get_int(ini, "custom_theme", "hue_shift", 0));
    if (ini_get_bool(ini, "custom_theme", "background_image", false)) theme_custom_use_background_image();
    free(settings->theme);
    const char *theme = ini_get_string(ini, "menu", "theme", NULL);
    settings->theme = theme ? strdup(theme) : NULL;

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    settings->rom_autoload_enabled = ini_get_bool(ini, "menu", "autoload_rom_enabled", init.rom_autoload_enabled);
    free(settings->rom_autoload_path);
    settings->rom_autoload_path = strdup(ini_get_string(ini, "autoload", "rom_path", init.rom_autoload_path));
    free(settings->rom_autoload_filename);
    settings->rom_autoload_filename = strdup(ini_get_string(ini, "autoload", "rom_filename", init.rom_autoload_filename));
    settings->loading_progress_bar_enabled = ini_get_bool(ini, "menu", "loading_progress_bar_enabled", init.loading_progress_bar_enabled);
#else
    settings->rom_fast_reboot_enabled = ini_get_bool(ini, "menu", "reboot_rom_enabled", init.rom_fast_reboot_enabled);
#endif
    /* Beta feature flags, they might not be in the file */
    settings->show_browser_file_extensions = ini_get_bool(ini, "menu", "show_browser_file_extensions", init.show_browser_file_extensions);
    settings->show_browser_rom_tags = ini_get_bool(ini, "menu", "show_browser_rom_tags", init.show_browser_rom_tags);
    settings->wrap_file_list_scrolling = ini_get_bool(ini, "menu", "wrap_file_list_scrolling", init.wrap_file_list_scrolling);
    settings->rumble_enabled = ini_get_bool(ini, "menu_beta_flag", "rumble_enabled", init.rumble_enabled);

    ini_free(ini);
}

void settings_save (settings_t *settings) {
    ini_t *ini = ini_create();

    ini_set_int(ini, "menu", "schema_revision", settings->schema_revision);
    ini_set_bool(ini, "menu", "first_run", settings->first_run);
    ini_set_bool(ini, "menu", "pal60", settings->pal60_enabled);
    ini_set_bool(ini, "menu", "force_progressive_scan", settings->force_progressive_scan);
    ini_set_bool(ini, "menu", "show_protected_entries", settings->show_protected_entries);
    ini_set_string(ini, "menu", "default_directory", settings->default_directory);
    ini_set_bool(ini, "menu", "boot_into_grid", settings->boot_into_grid);
    ini_set_string(ini, "menu", "grid_directory", settings->grid_directory);
    ini_set_bool(ini, "menu", "use_saves_folder", settings->use_saves_folder);
    ini_set_bool(ini, "menu", "show_saves_folder", settings->show_saves_folder);
    ini_set_bool(ini, "menu", "show_save_files", settings->show_save_files);
    ini_set_bool(ini, "menu", "show_cheat_files", settings->show_cheat_files);
    ini_set_bool(ini, "menu", "show_rom_configuration_files", settings->show_rom_configuration_files);
    ini_set_bool(ini, "menu", "soundfx_enabled", settings->soundfx_enabled);
    ini_set_bool(ini, "menu", "bgm_enabled", settings->bgm_enabled);
    ini_set_string(ini, "menu", "theme", settings->theme);
    ini_set_string(ini, "custom_theme", "base", theme_custom_base_name());
    save_custom_palette(ini);
    ini_set_int(ini, "custom_theme", "hue_shift", theme_custom_hue_shift());
    ini_set_bool(ini, "custom_theme", "background_image", theme_custom_has_background_image());
#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    ini_set_bool(ini, "menu", "autoload_rom_enabled", settings->rom_autoload_enabled);
    ini_set_string(ini, "autoload", "rom_path", settings->rom_autoload_path);
    ini_set_string(ini, "autoload", "rom_filename", settings->rom_autoload_filename);
    ini_set_bool(ini, "menu", "loading_progress_bar_enabled", settings->loading_progress_bar_enabled);
#else
    ini_set_bool(ini, "menu", "reboot_rom_enabled", settings->rom_fast_reboot_enabled);
#endif

    /* Beta feature flags, they should not save until production ready! */
    // ini_set_bool(ini, "menu", "show_browser_file_extensions", settings->show_browser_file_extensions);
    // ini_set_bool(ini, "menu", "show_browser_rom_tags", settings->show_browser_rom_tags);
    ini_set_bool(ini, "menu", "wrap_file_list_scrolling", settings->wrap_file_list_scrolling);
    // ini_set_bool(ini, "menu_beta_flag", "rumble_enabled", settings->rumble_enabled);

    if (!ini_save(ini, settings_path)) {
        debugf("[SETTINGS] Failed to save settings to %s\n", settings_path);
    }

    ini_free(ini);
}

void settings_reset_to_defaults() {
    remove(settings_path);
}