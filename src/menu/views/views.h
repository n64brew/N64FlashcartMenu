/**
 * @file views.h
 * @brief Declarations for all menu view modules and their display/init functions.
 * @ingroup menu
 *
 * This header provides prototypes for all view initialization and display routines used in the menu system.
 */

#ifndef VIEWS_H__
#define VIEWS_H__

#include "../ui_components.h"
#include "../menu_state.h"

/**
 * @addtogroup view
 * @{
 * @brief Menu view modules and their interface functions.
 */

/**
 * @brief Initialize the startup view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_startup_init(menu_t *menu);

/**
 * @brief Display the startup view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_startup_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the Grid view, loading or refreshing its ROM index.
 *
 * @param menu Pointer to the menu structure.
 */
void view_grid_init(menu_t *menu);

/**
 * @brief Display the Grid view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_grid_display(menu_t *menu, surface_t *display);

/**
 * @brief Note that Files added or removed ROMs, so Grid syncs its index when next shown.
 */
void view_grid_library_changed(void);

/**
 * @brief Describe the Grid cache for Settings, such as "41 ROMs" or "Scan failed".
 *
 * @param menu Pointer to the menu structure.
 * @return A static string, valid until the next call.
 */
const char *view_grid_cache_summary(menu_t *menu);

/**
 * @brief Delete the Grid index, including its arrangement and view choices.
 *        Grid rebuilds it when next shown.
 *
 * @param menu Pointer to the menu structure.
 */
void view_grid_clear_cache(menu_t *menu);

/**
 * @brief Free the Grid index and artwork before leaving the menu.
 */
void view_grid_shutdown(void);

/**
 * @brief Whether Files hides this path as a system or OS metadata file unless
 *        hidden files are shown.
 *
 * @param full_path Path including the storage prefix.
 */
bool view_browser_path_is_hidden(const char *full_path);

/**
 * @brief Initialize the browser view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_browser_init(menu_t *menu);

/**
 * @brief Display the browser view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_browser_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the file info view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_file_info_init(menu_t *menu);

/**
 * @brief Display the file info view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_file_info_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the image viewer view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_image_viewer_init(menu_t *menu);

/**
 * @brief Display the image viewer view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_image_viewer_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the text viewer view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_text_viewer_init(menu_t *menu);

/**
 * @brief Display the text viewer view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_text_viewer_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the music player view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_music_player_init(menu_t *menu);

/**
 * @brief Display the music player view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_music_player_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the credits view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_credits_init(menu_t *menu);

/**
 * @brief Display the credits view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_credits_display(menu_t *menu, surface_t *display);

/**
 * @brief Action bar hint slots a settings pane can describe.
 */
typedef enum {
    SETTINGS_HINT_LEFT,     /**< Left aligned hint, conventionally A / B. */
    SETTINGS_HINT_CENTER,   /**< Center aligned hint, conventionally navigation. */
    SETTINGS_HINT_RIGHT,    /**< Right aligned hint, conventionally Z / Start. */
} settings_hint_t;

/**
 * @brief One category of the settings view.
 *
 * Each pane lives in the view module that owns the underlying feature, so the
 * settings view only arranges panes and never duplicates their logic. Every
 * callback except @ref draw is optional.
 */
typedef struct {
    /** @brief Category name shown in the rail. */
    const char *label;
    /** @brief Called when the pane takes focus. */
    void (*enter)(menu_t *menu);
    /** @brief Called when the pane loses focus. */
    void (*leave)(menu_t *menu);
    /**
     * @brief Handle input while the pane has focus.
     *
     * @return false to hand focus back to the category rail.
     */
    bool (*process)(menu_t *menu);
    /**
     * @brief Draw the pane contents.
     *
     * @param focused Whether the pane currently has focus. An unfocused pane
     *        draws a read only summary.
     */
    void (*draw)(menu_t *menu, bool focused);
    /** @brief Draw dialogs that must sit above the action bar. */
    void (*overlay)(menu_t *menu);
    /** @brief Whether the pane currently owns L / R while a modal workflow is active. */
    bool (*blocks_tabs)(menu_t *menu);
    /** @brief Action bar hint for the given slot, or NULL for none. */
    const char *(*hint)(menu_t *menu, settings_hint_t slot);
} settings_pane_t;

/**
 * @brief Initialize the settings view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_settings_init(menu_t *menu);

/**
 * @brief Request a transition from another view directly into a settings pane.
 *
 * The settings view selects and enters the pane during its next initialization.
 * An unregistered pane is ignored.
 *
 * @param menu Pointer to the menu structure.
 * @param pane Registered settings pane to select and focus.
 */
void view_settings_open_pane(menu_t *menu, const settings_pane_t *pane);

/**
 * @brief Display the settings view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_settings_display(menu_t *menu, surface_t *display);

/** @brief Menu settings pane, implemented by settings_editor.c. */
extern const settings_pane_t settings_pane_menu;
/** @brief Built-in theme picker and Custom palette editor. */
extern const settings_pane_t settings_pane_theme;
/** @brief Controller Pak pane, implemented by cpakfs_manager.c. */
extern const settings_pane_t settings_pane_controller_pak;
/** @brief Real time clock pane, implemented by rtc.c. */
extern const settings_pane_t settings_pane_time;
/** @brief Flashcart information pane, implemented by flashcart_info.c. */
extern const settings_pane_t settings_pane_flashcart;
/** @brief N64 information pane, implemented by system_info.c. */
extern const settings_pane_t settings_pane_n64;
/** @brief Menu information pane, implemented by credits.c. */
extern const settings_pane_t settings_pane_information;

/**
 * @brief Format a joypad accessory name for display.
 *
 * @param accessory The accessory type to describe.
 * @return A human readable accessory name.
 */
const char *format_accessory_name(joypad_accessory_type_t accessory);

/**
 * @brief Initialize the Controller Pak dump info view.
 * @param menu Pointer to the menu structure.
 */
void view_controller_pak_dump_info_init(menu_t *menu);
/**
 * @brief Display the Controller Pak dump info view.
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_controller_pak_dump_info_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the Controller Pak note dump info view.
 * @param menu Pointer to the menu structure.
 */
void view_controller_pak_note_dump_info_init(menu_t *menu);
/**
 * @brief Display the Controller Pak note dump info view.
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_controller_pak_note_dump_info_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the load ROM view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_load_rom_init(menu_t *menu);

/**
 * @brief Display the load ROM view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_load_rom_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the load disk view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_load_disk_init(menu_t *menu);

/**
 * @brief Display the load disk view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_load_disk_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the load emulator view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_load_emulator_init(menu_t *menu);

/**
 * @brief Display the load emulator view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_load_emulator_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the error view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_error_init(menu_t *menu);

/**
 * @brief Display the error view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_error_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the fault view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_fault_init(menu_t *menu);

/**
 * @brief Display the fault view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_fault_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the favorite view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_favorite_init(menu_t *menu);

/**
 * @brief Display the favorite view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_favorite_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the history view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_history_init(menu_t *menu);

/**
 * @brief Display the history view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_history_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the cheats editor view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_datel_code_editor_init(menu_t *menu);

/**
 * @brief Display the cheats editor view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_datel_code_editor_display(menu_t *menu, surface_t *display);

/**
 * @brief Initialize the archive browser view.
 *
 * @param menu Pointer to the menu structure.
 */
void view_extract_file_init(menu_t *menu);

/**
 * @brief Display the archive browser view.
 *
 * @param menu Pointer to the menu structure.
 * @param display Pointer to the display surface.
 */
void view_extract_file_display(menu_t *menu, surface_t *display);

/**
 * @brief Show an error message in the menu.
 *
 * @param menu Pointer to the menu structure.
 * @param error_message Error message to be displayed.
 */
void menu_show_error(menu_t *menu, char *error_message);

/** @} */ /* view */

#endif // VIEWS_H__
