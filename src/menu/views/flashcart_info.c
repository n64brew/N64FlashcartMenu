#include "views.h"
#include "../sound.h"
#include <libcart/cart.h>

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

static void process (menu_t *menu) {
    bool has_button_support = flashcart_has_button_state();
    bool has_diagnostics_support = flashcart_has_voltage_temperature();
    bool has_extra_info = has_button_support || has_diagnostics_support;

    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    } else if (menu->actions.lz_context && has_extra_info) {
        show_extra_info_message = !show_extra_info_message;
        sound_play_effect(SFX_SETTING);
    }
}

static void draw (menu_t *menu, surface_t *d) {
    bool has_button_support = flashcart_has_button_state();
    bool has_diagnostics_support = flashcart_has_voltage_temperature();
    bool has_extra_info = has_button_support || has_diagnostics_support;

    rdpq_attach(d, NULL);

    ui_components_background_draw();

    ui_components_layout_draw();

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "FLASHCART INFORMATION"
        "\n"
        "\n"
    );

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n"
        "\n"
        "Type:\n"
        "  %s\n\n"
        "Firmware:\n"
        "  Version: %s - Menu probably out of date.\n\n"
        "Features:\n"
        "  Virtual 64DD:     %s.\n"
        "  Real Time Clock:  %s.\n"
        "  USB Debugging:    %s.\n"
        "  Automatic CIC:    %s.\n"
        "  Region Detection: %s.\n"
        "  Save Writeback:   %s.\n"
        "  Auto F/W Updates: %s.\n"
        "  Fast ROM Reboots: %s.\n"
        "  Button:           %s.\n"
        "  Diagnostics:      %s.\n"
        "\n\n",
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
        format_boolean_type(has_button_support),
        format_boolean_type(has_diagnostics_support)
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n"
        "B: Back"
    );

    if (has_extra_info) {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_RIGHT, VALIGN_TOP,
            "\n"
            "L|Z: Diagnostics"
        );
    }

    if (show_extra_info_message && has_extra_info) {
        ui_components_messagebox_draw(
            "FLASHCART DIAGNOSTICS\n"
            "\n"
            "Diagnostics:\n"
            "  Button Realtime: %s\n"
            "  Voltage / Temp: %s\n"
            "\n"
            "Press L|Z to return.\n",
            format_button_state(),
            format_voltage_temperature()
        );
    }

    rdpq_detach_show();
}


void view_flashcart_info_init (menu_t *menu) {
    show_extra_info_message = false;
}

void view_flashcart_info_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}
