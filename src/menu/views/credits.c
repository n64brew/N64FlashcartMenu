#include "views.h"
#include "../sound.h"

#ifndef MENU_VERSION
#define MENU_VERSION "Unknown"
#endif

#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP "Unknown"
#endif

static sys_version_t sdk_version = {0};
static bool show_oss_lib_info_message = false;

static void process (menu_t *menu) {
    if (menu->actions.back) {
        show_oss_lib_info_message = false;
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER; 
    } else if (menu->actions.lz_context) {
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

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n"
        "\n"
        "Menu version: \t%s\n"
        "Build timestamp:  %s\n"
        "libdragon SDK: \t%s%s (%s, %.7s)\n"
        "\n"
        "Get the latest menu version:\n"
        "\thttps://github.com/Polprzewodnikowy/N64FlashcartMenu\n"
        "\n"
        "Authors:\n"
        "\tRobin Jones / NetworkFusion\n"
        "\tMateusz Faderewski / Polprzewodnikowy\n"
        "\tand contributors:\n"
        "\tThank you to ALL project contributors,\n"
        "\tno matter how small the commit.\n"
        "\n\nThis menu is licensed under the AGPL-3.0 License.\n",
        MENU_VERSION,
        BUILD_TIMESTAMP,
        sdk_version.branch, sdk_version.dirty ? "*" : "",
        sdk_version.commit_date,
        sdk_version.hash
    );

    if (show_oss_lib_info_message) {
        ui_components_messagebox_draw(
            "OSS library software used:\n\n"
            "\tlibdragon (UNLICENSE License)\n"
            "\tlibspng (BSD 2-Clause License)\n"
            "\tTJpgDec (ChaN permissive license)\n"
            "\tminimp3 (CC0 1.0 Universal)\n"
            "\tminiz (MIT License)\n"
            "\tdr_flac (MIT License)\n"
        );
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
        "L|Z: OSS Libraries used\n"
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
