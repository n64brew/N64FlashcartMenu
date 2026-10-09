#include "views.h"
#include "../sound.h"
#include "../ui_components/constants.h"
#include <libcart/cart.h>

/** @brief Number of rows the flashcart pane shows. */
#define FLASHCART_ROWS  (12)

_Static_assert(FLASHCART_ROWS <= SETTINGS_ROWS, "Flashcart rows no longer fit the settings pane");

static bool show_extra_info_message = false;


static inline const char *format_boolean_type (bool bool_value) {
    return bool_value ? "Supported" : "Unsupported";
}

static const char *format_cart_type () {
    switch (cart_type) {
        case CART_CI:
            return "64drive";

        case CART_EDX:
            return "Series X EverDrive-64";

        case CART_ED:
            return "Series V EverDrive-64";

        case CART_SC:
            return "SummerCart64";

        default:        // Probably emulator
            return "Emulator?";
    }
}

static const char *format_cart_version () {
    flashcart_firmware_version_t version = flashcart_get_firmware_version();
    static char buffer[16];
    snprintf(buffer, sizeof(buffer), "%u.%u.%lu", version.major, version.minor, version.revision);
    return buffer;
}

static const char *format_button_state (void) {
    static const char *unknown = "Unknown";
    bool pressed = false;

    if (!flashcart_has_button_state()) {
        return unknown;
    }

    if (flashcart_get_button_state(&pressed) != FLASHCART_OK) {
        return unknown;
    }

    return pressed ? "Pressed" : "Released";
}

static const char *format_voltage_temperature (void) {
    static char buffer[48];
    uint16_t voltage_mv;
    int16_t temperature_deci_c;
    int temperature_abs;
    int temperature_whole;
    int temperature_frac;

    if (!flashcart_has_voltage_temperature()) {
        return "Unsupported";
    }

    if (flashcart_get_voltage_temperature(&voltage_mv, &temperature_deci_c) != FLASHCART_OK) {
        return "Unavailable";
    }

    temperature_abs = (temperature_deci_c < 0) ? -temperature_deci_c : temperature_deci_c;
    temperature_whole = (temperature_abs / 10);
    temperature_frac = (temperature_abs % 10);

    snprintf(
        buffer,
        sizeof(buffer),
        "%u mV / %s%d.%d C",
        voltage_mv,
        (temperature_deci_c < 0) ? "-" : "",
        temperature_whole,
        temperature_frac
    );
    return buffer;
}

static bool has_extra_info (void) {
    return flashcart_has_button_state() || flashcart_has_voltage_temperature();
}

static void pane_enter (menu_t *menu) {
    show_extra_info_message = false;
}

static bool pane_process (menu_t *menu) {
    if (show_extra_info_message) {
        if (menu->actions.back || menu->actions.context) {
            show_extra_info_message = false;
            sound_play_effect(SFX_EXIT);
        }
        return true;
    }

    if (menu->actions.back) {
        return false;
    } else if (menu->actions.context && has_extra_info()) {
        show_extra_info_message = true;
        sound_play_effect(SFX_SETTING);
    }

    return true;
}

static void pane_draw (menu_t *menu, bool focused) {
    static const char *labels[FLASHCART_ROWS] = {
        "Type", "Firmware", "Virtual 64DD", "Real Time Clock", "USB Debugging",
        "Automatic CIC", "Region Detection", "Save Writeback", "Auto F/W Updates",
        "Fast ROM Reboots", "Button", "Diagnostics",
    };
    const char *values[FLASHCART_ROWS] = {
        format_cart_type(),
        format_cart_version(),
        format_boolean_type(flashcart_has_feature(FLASHCART_FEATURE_64DD)),
        format_boolean_type(flashcart_has_feature(FLASHCART_FEATURE_RTC)),
        format_boolean_type(flashcart_has_feature(FLASHCART_FEATURE_USB)),
        format_boolean_type(flashcart_has_feature(FLASHCART_FEATURE_AUTO_CIC)),
        format_boolean_type(flashcart_has_feature(FLASHCART_FEATURE_AUTO_REGION)),
        format_boolean_type(flashcart_has_feature(FLASHCART_FEATURE_SAVE_WRITEBACK)),
        format_boolean_type(flashcart_has_feature(FLASHCART_FEATURE_BIOS_UPDATE_FROM_MENU)),
        format_boolean_type(flashcart_has_feature(FLASHCART_FEATURE_ROM_REBOOT_FAST)),
        format_boolean_type(flashcart_has_button_state()),
        format_boolean_type(flashcart_has_voltage_temperature()),
    };
    int y = SETTINGS_PANE_Y0 + 4;


    for (int i = 0; i < FLASHCART_ROWS; i++) {
        ui_components_settings_row_draw(y, labels[i], values[i], false);
        y += SETTINGS_ROW_HEIGHT;
    }
}

static void pane_overlay (menu_t *menu) {
    if (show_extra_info_message) {
        ui_components_messagebox_draw(
            "FLASHCART DIAGNOSTICS\n"
            "\n"
            "Diagnostics:\n"
            "  Button Realtime: %s\n"
            "  Voltage / Temp: %s\n"
            "\n"
            "Press Z or B to return.\n",
            format_button_state(),
            format_voltage_temperature()
        );
    }
}

static bool pane_blocks_tabs (menu_t *menu) {
    (void) menu;
    return show_extra_info_message;
}

static const char *pane_hint (menu_t *menu, settings_hint_t slot) {
    switch (slot) {
        case SETTINGS_HINT_LEFT: return "B: Categories\n";
        case SETTINGS_HINT_CENTER: return "◀L Tab R▶\n";
        case SETTINGS_HINT_RIGHT: return has_extra_info() ? "Z: Diagnostics\n" : NULL;
        default: return NULL;
    }
}

const settings_pane_t settings_pane_flashcart = {
    .label = "Flashcart",
    .enter = pane_enter,
    .process = pane_process,
    .draw = pane_draw,
    .overlay = pane_overlay,
    .blocks_tabs = pane_blocks_tabs,
    .hint = pane_hint,
};
