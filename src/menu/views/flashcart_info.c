#include "views.h"
#include "info_tabs.h"
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
    static char buffer[FLASHCART_DIAGNOSTIC_VALUE_SIZE];
    bool pressed = false;

    if (!flashcart_has_button_state()) {
        return "Unsupported";
    }

    if (flashcart_get_button_state(&pressed) != FLASHCART_OK) {
        snprintf(buffer, sizeof(buffer), "Supported / Unavailable");
    } else {
        snprintf(buffer, sizeof(buffer), " %s", pressed ? "Pressed" : "Released");
    }
    return buffer;
}

static void append_diagnostic_line (char *text, size_t text_size, size_t *used,
                                    const char *label, const char *value) {
    if (*used >= text_size) {
        return;
    }

    size_t available = text_size - *used;
    int written = snprintf(text + *used, available, "  %.19s: %.23s\n", label, value);
    if (written > 0) {
        *used += ((size_t)written < available) ? (size_t)written : available - 1;
    }
}

static void process (menu_t *menu) {
    bool has_button_support = flashcart_has_button_state();
    bool has_diagnostics_support = flashcart_has_diagnostics();
    bool has_extra_info = has_button_support || has_diagnostics_support;

    if (menu->actions.go_left) {
        sound_play_effect(SFX_SETTING);
        menu->next_mode = MENU_MODE_CREDITS;
    } else if (menu->actions.go_right) {
        sound_play_effect(SFX_SETTING);
        menu->next_mode = MENU_MODE_SYSTEM_INFO;
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    } else if (menu->actions.lz_context && has_extra_info) {
        show_extra_info_message = !show_extra_info_message;
        sound_play_effect(SFX_SETTING);
    }
}

static void draw (menu_t *menu, surface_t *d) {
    bool has_button_support = flashcart_has_button_state();
    bool has_diagnostics_support = flashcart_has_diagnostics();
    bool has_extra_info = has_button_support || has_diagnostics_support;

    rdpq_attach(d, NULL);

    ui_components_background_draw();

    ui_components_tabs_draw(info_tabs, 3, 1, 162.0f);
    ui_components_layout_draw_tabbed();

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n"
        "\n"
        "Type:\n"
        "  %s\n\n"
        "Firmware:\n"
        "  Version: %s\n"
        "  Menu updates are separate from firmware.\n"
        "\n"
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
        "B: Exit"
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "\n"
        "◀ Tabs ▶"
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
        char diagnostics_text[512];
        size_t used = (size_t)snprintf(diagnostics_text, sizeof(diagnostics_text),
                                       "FLASHCART DIAGNOSTICS\n\n");

        append_diagnostic_line(diagnostics_text, sizeof(diagnostics_text), &used,
                              "Button", format_button_state());

        if (has_diagnostics_support) {
            flashcart_diagnostic_t diagnostics[FLASHCART_DIAGNOSTIC_MAX_COUNT] = {0};
            size_t count = 0;
            flashcart_err_t err = flashcart_get_diagnostics(
                diagnostics, FLASHCART_DIAGNOSTIC_MAX_COUNT, &count);
            if (err == FLASHCART_OK) {
                if (count == 0) {
                    append_diagnostic_line(diagnostics_text, sizeof(diagnostics_text), &used,
                                          "Diagnostics", "No readings available");
                } else {
                    for (size_t i = 0; i < count; i++) {
                        append_diagnostic_line(diagnostics_text, sizeof(diagnostics_text), &used,
                                              diagnostics[i].label, diagnostics[i].value);
                    }
                }
            } else {
                append_diagnostic_line(diagnostics_text, sizeof(diagnostics_text), &used,
                                      "Diagnostics", "Unavailable");
            }
        }

        ui_components_messagebox_draw(
            "%s%sPress L|Z to return.\n\n",
            diagnostics_text,
            cart_type == CART_SC
                ? "\nTo run hardware diagnostics, hold the flashcart button while powering on.\n\n"
                : "\n\n"
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
