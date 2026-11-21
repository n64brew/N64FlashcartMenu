/**
 * @file settings.h
 * @brief Menu Settings
 * @ingroup menu 
 */

#ifndef SETTINGS_H__
#define SETTINGS_H__

/* Reserve space for up to four built-in tracks. */
#define BGM_TRACK_CAPACITY 4

typedef enum {
    BGM_TRACK_FLYING_DREAMS,
    BGM_TRACK_WARM_WAVES,
    BGM_TRACK_BLUE_HORIZON,
    BGM_TRACK_WHITE_STONES,
    BGM_TRACK_AVAILABLE_COUNT,
} bgm_track_t;

/** @brief Settings Structure */
typedef struct {
    /** @brief Settings version */
    int schema_revision;

    /** @brief First run of the menu */
    bool first_run;

    /** @brief Use 60 Hz refresh rate on a PAL console */
    bool pal60_enabled;

    /** @brief Direct the VI to force progressive scan output at 240p. Meant for TVs and other devices which struggle to display interlaced video. */
    bool force_progressive_scan;

    /** @brief Show files/directories that are filtered in the browser */
    bool show_protected_entries;

    /** @brief Default directory to navigate to when menu loads */
    char *default_directory;

    /** @brief Put saves into separate directory */
    bool use_saves_folder;

    /** @brief Show saves folder in file browser */ 
    bool show_saves_folder;

    /** @brief Show save files in file browser */
    bool show_save_files;

    /** @brief Show cheat files in file browser */
    bool show_cheat_files;

    /** @brief Show rom configuration files in file browser */
    bool show_rom_configuration_files;

    /** @brief Show rom file extensions in browser */    
    bool show_browser_file_extensions;

    /** @brief Show rom tags in browser */  
    bool show_browser_rom_tags;

    /** @brief Wrap file list scrolling (infinite/circular navigation) */
    bool wrap_file_list_scrolling;

    /** @brief Enable Background music */
    bool bgm_enabled;

    /** @brief Selected built-in background music track */
    bgm_track_t bgm_track;

    /** @brief Enable Sound effects within the menu */
    bool soundfx_enabled;

    /** @brief Enable rumble feedback within the menu */
    bool rumble_enabled;

    /** @brief Enable viewing the full ROM description (when available) via a dedicated scrollable screen */
    bool show_full_rom_descriptions;

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    /** @brief Enable the ability to bypass the menu and instantly load a ROM on power and reset button */
    bool rom_autoload_enabled;

    /** @brief A path to the autoloaded ROM */
    char *rom_autoload_path;

    /** @brief A filename of the autoloaded ROM */
    char *rom_autoload_filename;
    
    /** @brief Show progress bar when loading a ROM */
    bool loading_progress_bar_enabled;
#else
    /** @brief Enable the ability to bypass the menu and instantly load a ROM on reset button */
    bool rom_fast_reboot_enabled;
#endif

} settings_t;


/** @brief Init settings path */
void settings_init (char *path);
/** @brief The settings to load */
void settings_load (settings_t *settings);
/** @brief The settings to save */
void settings_save (settings_t *settings);
/** @brief Reset settings to defaults */
void settings_reset_to_defaults();

#endif
