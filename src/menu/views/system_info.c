#include <time.h>

#include "../cart_load.h"
#include "../sound.h"
#include "../ui_components/constants.h"
#include "views.h"


const char *format_accessory_name (joypad_accessory_type_t accessory) {
    switch (accessory) {
        case JOYPAD_ACCESSORY_TYPE_RUMBLE_PAK: return "Rumble Pak";
        case JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK: return "Controller Pak";
        case JOYPAD_ACCESSORY_TYPE_TRANSFER_PAK: return "Transfer Pak";
        case JOYPAD_ACCESSORY_TYPE_BIO_SENSOR: return "BIO Sensor";
        case JOYPAD_ACCESSORY_TYPE_SNAP_STATION: return "Snap Station";
        case JOYPAD_ACCESSORY_TYPE_NONE: return "Nothing inserted";
        default: return "Unknown accessory";
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

static void pane_draw (menu_t *menu, bool focused) {
    int y = SETTINGS_PANE_Y0 + 4;


    ui_components_settings_row_draw(
        y, "Expansion Pak", is_memory_expanded() ? "Installed" : "Not installed", false
    );
    y += SETTINGS_ROW_HEIGHT;

    ui_components_settings_row_draw(y, "Console region", format_console_region(), false);
    y += SETTINGS_ROW_HEIGHT;

    ui_components_settings_row_draw(
        y, "Physical 64DD", is_64dd_connected() ? "Attached" : "Not attached", false
    );
    y += SETTINGS_ROW_HEIGHT;

    JOYPAD_PORT_FOREACH (port) {
        char label[16];
        snprintf(label, sizeof(label), "Joypad %d", port + 1);
        ui_components_settings_row_draw(y, label,
            joypad_get_style(port) == JOYPAD_STYLE_NONE ? "Disconnected" : "Connected", false);
        y += SETTINGS_ROW_HEIGHT;
        ui_components_settings_row_draw(y, "Accessory",
            format_accessory_name(joypad_get_accessory_type(port)), false);
        y += SETTINGS_ROW_HEIGHT;
    }
}

static const char *pane_hint (menu_t *menu, settings_hint_t slot) {
    switch (slot) {
        case SETTINGS_HINT_LEFT: return "B: Categories\n";
        case SETTINGS_HINT_CENTER: return "◀L Tab R▶\n";
        default: return NULL;
    }
}

const settings_pane_t settings_pane_n64 = {
    .label = "N64",
    .draw = pane_draw,
    .hint = pane_hint,
};
