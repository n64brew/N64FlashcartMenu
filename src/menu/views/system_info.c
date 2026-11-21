#include <time.h>

#include "../cart_load.h"
#include "../sound.h"
#include "views.h"
#include "info_tabs.h"


static int joypad[4];
static int accessory[4];


static const char *format_accessory (int joypad) {
    switch (accessory[joypad]) {
        case JOYPAD_ACCESSORY_TYPE_RUMBLE_PAK: return "[Rumble Pak is inserted]";
        case JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK: return "[Controller Pak is inserted]";
        case JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK: return "[Transfer Pak is inserted]";
        case JOYPAD_ACCESSORY_TYPE_BIO_SENSOR: return "[BIO Sensor is inserted]";
        case JOYPAD_ACCESSORY_TYPE_SNAP_STATION: return "[Snap Station is inserted]";
        case JOYPAD_ACCESSORY_TYPE_NONE: return "";
        default: return "[unknown accessory inserted]";
    }
}

static const char *format_console_region (void) {
    switch (get_tv_type()) {
        case TV_NTSC: return "NTSC";
        case TV_PAL: return "PAL";
        case TV_MPAL: return "MPAL";
        default: return "Unknown";
    }
}

static void process (menu_t *menu) {
    JOYPAD_PORT_FOREACH (port) {
        joypad[port] = (joypad_get_style(port) != JOYPAD_STYLE_NONE);
        accessory[port] = joypad_get_accessory_type(port);
    }

    if (menu->actions.go_left) {
        sound_play_effect(SFX_SETTING);
        menu->next_mode = MENU_MODE_FLASHCART;
    } else if (menu->actions.go_right) {
        sound_play_effect(SFX_SETTING);
        menu->next_mode = MENU_MODE_CREDITS;
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();
    ui_components_tabs_draw(info_tabs, 3, 2, 162.0f);
    ui_components_layout_draw_tabbed();

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n"
        "\n"
        "Expansion PAK is %sinserted\n"
        "\n"
        "Console region: %s\n"
        "\n"
        "Joypad 1 is %sconnected %s\n"
        "Joypad 2 is %sconnected %s\n"
        "Joypad 3 is %sconnected %s\n"
        "Joypad 4 is %sconnected %s\n"
        "\n"
        "\n"
        "Physical Disk Drive attached: %s\n",
        is_memory_expanded() ? "" : "not ",
        format_console_region(),
        (joypad[0]) ? "" : "not ", format_accessory(0),
        (joypad[1]) ? "" : "not ", format_accessory(1),
        (joypad[2]) ? "" : "not ", format_accessory(2),
        (joypad[3]) ? "" : "not ", format_accessory(3),
        is_64dd_connected() ? "Yes" : "No"
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n"
        "B: Exit"
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "\n"
        "◀ Tabs ▶"
    );

    rdpq_detach_show();
}


void view_system_info_init (menu_t *menu) {
    // Nothing to initialize (yet)
}

void view_system_info_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);
}
