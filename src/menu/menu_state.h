/**
 * @file menu_state.h
 * @brief Menu State
 * @ingroup menu 
 */

#ifndef MENU_STRUCT_H__
#define MENU_STRUCT_H__


#include <miniz.h>
#include <miniz_zip.h>
#include <time.h>

#include "boot/boot.h"
#include "disk_info.h"
#include "flashcart/flashcart.h"
#include "path.h"
#include "rom_info.h"
#include "settings.h"
#include "bookkeeping.h"


/** @brief Menu mode enumeration */
typedef enum {
    MENU_MODE_NONE,
    MENU_MODE_STARTUP,
    MENU_MODE_GRID,
    MENU_MODE_BROWSER,
    MENU_MODE_FILE_INFO,
    MENU_MODE_IMAGE_VIEWER,
    MENU_MODE_TEXT_VIEWER,
    MENU_MODE_MUSIC_PLAYER,
    MENU_MODE_CREDITS,
    MENU_MODE_SETTINGS,
    MENU_MODE_CONTROLLER_PAK_DUMP_INFO,
    MENU_MODE_CONTROLLER_PAK_DUMP_NOTE_INFO,
    MENU_MODE_LOAD_ROM,
    MENU_MODE_LOAD_DISK,
    MENU_MODE_LOAD_EMULATOR,
    MENU_MODE_ERROR,
    MENU_MODE_FAULT,
    MENU_MODE_BOOT,
    MENU_MODE_FAVORITE,
    MENU_MODE_HISTORY,
    MENU_MODE_DATEL_CODE_EDITOR,
    MENU_MODE_EXTRACT_FILE
} menu_mode_t;

/** @brief File entry type enumeration */
typedef enum {
    ENTRY_TYPE_DIR,
    ENTRY_TYPE_DISK,
    ENTRY_TYPE_EMULATOR,  
    ENTRY_TYPE_IMAGE,
    ENTRY_TYPE_MUSIC,
    ENTRY_TYPE_OTHER,
    ENTRY_TYPE_ROM,
    ENTRY_TYPE_ROM_CHEAT,
    ENTRY_TYPE_ROM_PATCH,
    ENTRY_TYPE_SAVE,
    ENTRY_TYPE_TEXT,
    ENTRY_TYPE_ARCHIVE,
    ENTRY_TYPE_ARCHIVED,
    ENTRY_TYPE_ROM_META
} entry_type_t;

/** @brief File Entry Structure */
typedef struct {
    char *name;
    entry_type_t type;
    int64_t size;
    int32_t index;
} entry_t;

typedef struct {
    path_t *disk_path;
    disk_info_t disk_info;
} disk_slot_entry_t;

/** @brief Disk slot structure for multi-disk 64DD games. */
typedef struct {
    disk_slot_entry_t primary; // Primary disk slot
    disk_slot_entry_t swap_slot[3]; // 3 swap slots
} disk_slot_t;

/** @brief Menu Structure */
typedef struct {
    menu_mode_t mode;
    menu_mode_t next_mode;

    const char *storage_prefix;
    /** @brief Whether menu/metadata exists; Grid and its settings are hidden without it. */
    bool grid_enabled;
    settings_t settings;
    bookkeeping_t bookkeeping;
    boot_params_t *boot_params;

    char *error_message;
    flashcart_err_t flashcart_err;

    time_t current_time;

    struct {
        bool go_up;
        bool go_down;
        bool go_left;
        bool go_right;
        bool go_fast;

        bool enter;
        bool back;
        bool options;
        bool settings;
        bool context;
        bool tab_left;
        bool tab_right;
    } actions;

    struct {
        bool valid;
        bool reload;
        bool archive;
        mz_zip_archive zip;
        path_t *directory;
        entry_t *list;
        int32_t list_capacity;
        int32_t entries;
        entry_t *entry;
        int32_t selected;
        path_t* select_file;
    } browser;

    struct {
        path_t *rom_path;
        bool from_grid;
        rom_info_t rom_info;
        disk_slot_t disk_slots;
        int32_t load_history_id;
        int32_t load_favorite_id;
        bool combined_disk_rom;
    } load;

    struct {
        bool rom_file;
        bool disk_file;
        bool emulator_file;
        bool extract_file;
    } load_pending;
} menu_t;


#endif
