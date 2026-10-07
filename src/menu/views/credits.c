#include "views.h"
#include "../sound.h"
#include "../ui_components/constants.h"
#include "../../utils/utils.h"

#ifndef MENU_VERSION
#define MENU_VERSION "Unknown"
#endif

#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP "Unknown"
#endif

/** @brief Number of pixels the information pane scrolls per step. */
#define INFORMATION_SCROLL_STEP (20)

static sys_version_t sdk_version = {0};
static bool show_oss_lib_info_message = false;
static int scroll;
static int max_scroll;

/**
 * @def OSS_LIBRARIES_TEXT
 * @brief The open source libraries this menu is built on.
 */
#define OSS_LIBRARIES_TEXT \
    "\tlibdragon (UNLICENSE License)\n" \
    "\tlibspng (BSD 2-Clause License)\n" \
    "\tlibjpeg-turbo (BSD 3-Clause License)\n" \
    "\tminimp3 (CC0 1.0 Universal)\n" \
    "\tdr_flac (MIT License)\n" \
    "\tminiz (MIT License)\n"

/**
 * @brief Build the menu information text shared by the view and the settings pane.
 *
 * The project URL is wrapped by hand because word wrapping cannot break a URL,
 * which has no spaces to break on.
 *
 * @param buffer Destination for the formatted text.
 * @param size Size of @p buffer.
 * @param include_libraries Append the OSS library list. The settings pane scrolls
 *        and shows the list inline; the standalone view shows it in a dialog.
 */
static void format_information (char *buffer, size_t size, bool include_libraries) {
    snprintf(
        buffer,
        size,
        "Menu version: \t%s\n"
        "Build timestamp:  %s\n"
        "libdragon SDK: \t%s%s (%s, %.7s)\n"
        "\n"
        "Get the latest menu version:\n"
        "%s"
        "\n"
        "Authors:\n"
        "\tRobin Jones / NetworkFusion\n"
        "\tMateusz Faderewski / Polprzewodnikowy\n"
        "\tand contributors:\n"
        "\tThank you to ALL project contributors,\n"
        "\tno matter how small the commit.\n"
        "\n\nThis menu is licensed under the AGPL-3.0 License.\n"
        "%s",
        MENU_VERSION,
        BUILD_TIMESTAMP,
        sdk_version.branch, sdk_version.dirty ? "*" : "",
        sdk_version.commit_date,
        sdk_version.hash,
        include_libraries
            ? "\thttps://github.com/\n\tPolprzewodnikowy/N64FlashcartMenu\n"
            : "\thttps://github.com/Polprzewodnikowy/N64FlashcartMenu\n",
        include_libraries ? "\nOSS library software used:\n" OSS_LIBRARIES_TEXT : ""
    );
}

static void process (menu_t *menu) {
    if (menu->actions.back) {
        show_oss_lib_info_message = false;
        sound_play_effect(SFX_EXIT);
        menu->next_mode = menu->settings.boot_into_grid && menu->grid_enabled ? MENU_MODE_GRID : MENU_MODE_BROWSER;
    } else if (menu->actions.context) {
        if (show_oss_lib_info_message) {
            show_oss_lib_info_message = false;
        } else {
            show_oss_lib_info_message = true;
        }
        sound_play_effect(SFX_SETTING);
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    ui_components_layout_draw();

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "MENU INFORMATION"
    );

    char information[1024];
    format_information(information, sizeof(information), false);

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n"
        "\n"
        "%s",
        information
    );

    if (show_oss_lib_info_message) {
        ui_components_messagebox_draw("OSS library software used:\n\n" OSS_LIBRARIES_TEXT);
    }


    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n"
        "B: Exit"
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_RIGHT, VALIGN_TOP,
        "\n"
        "Z: OSS Libraries used\n"
    );

    rdpq_detach_show();
}


void view_credits_init (menu_t *menu) {
    sys_get_version(&sdk_version);
}

void view_credits_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);
}


static void pane_enter (menu_t *menu) {
    scroll = 0;
    max_scroll = 0;
}

static bool pane_process (menu_t *menu) {
    if (menu->actions.back) {
        return false;
    } else if (menu->actions.go_up) {
        scroll = MAX(scroll - INFORMATION_SCROLL_STEP, 0);
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_down) {
        scroll = MIN(scroll + INFORMATION_SCROLL_STEP, max_scroll);
        sound_play_effect(SFX_CURSOR);
    }

    return true;
}

static void pane_draw (menu_t *menu, bool focused) {
    char information[1024];
    int nbytes;
    rdpq_paragraph_t *layout;
    int content_height;


    sys_get_version(&sdk_version);
    format_information(information, sizeof(information), true);
    nbytes = strlen(information);

    layout = rdpq_paragraph_build(
        &(rdpq_textparms_t) {
            .style_id = STL_DEFAULT,
            .width = SETTINGS_PANE_X1 - SETTINGS_PANE_X0 - 36,
            .align = ALIGN_LEFT,
            .valign = VALIGN_TOP,
            .wrap = WRAP_WORD,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        information,
        &nbytes
    );

    content_height = layout->bbox.y1 - layout->bbox.y0;
    max_scroll = MAX(content_height - (SETTINGS_PANE_Y1 - SETTINGS_PANE_Y0) + 12, 0);
    scroll = MIN(scroll, max_scroll);

    rdpq_set_scissor(SETTINGS_PANE_X0 + 8, SETTINGS_PANE_Y0, SETTINGS_PANE_X1 - 8, SETTINGS_PANE_Y1);
    rdpq_paragraph_render(layout, SETTINGS_PANE_X0 + 18, SETTINGS_PANE_Y0 + 4 - layout->bbox.y0 - scroll);
    rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    rdpq_paragraph_free(layout);
}

static const char *pane_hint (menu_t *menu, settings_hint_t slot) {
    switch (slot) {
        case SETTINGS_HINT_LEFT: return "B: Categories\n";
        case SETTINGS_HINT_CENTER: return "Up / Down: Scroll\n◀L Tab R▶";
        default: return NULL;
    }
}

const settings_pane_t settings_pane_information = {
    .label = "Information",
    .enter = pane_enter,
    .process = pane_process,
    .draw = pane_draw,
    .hint = pane_hint,
};
