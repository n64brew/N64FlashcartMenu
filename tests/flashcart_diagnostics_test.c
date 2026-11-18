#include <acutest.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Supply only libdragon rendering types; the facade, menu state, presentation,
 * controls, startup decisions, and paths below use production code. */
typedef struct { uint8_t r, g, b, a; } color_t;
typedef struct { int unused; } surface_t;
typedef int rdpq_align_t;
typedef int rdpq_valign_t;
typedef int rdpq_textwrap_t;
typedef int joypad_accessory_type_t;
void rdpq_attach_clear (surface_t *display, const void *color);
void rdpq_detach_show (void);

#undef ALIGN
#undef MIN
#undef MAX
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
/* Firmware's uint32_t is unsigned long on the target, unsigned int on hosts. */
#pragma GCC diagnostic ignored "-Wformat"
#include "../src/flashcart/flashcart.c"
#include "../src/menu/views/flashcart_info.c"
#include "../src/menu/views/startup.c"
#include "../src/menu/path.c"
#pragma GCC diagnostic pop

int cart_type;
static uint16_t sample_voltage;
static int16_t sample_temperature;
static flashcart_err_t sample_error;
static unsigned voltage_calls, combined_calls, settings_saves, warnings;
static unsigned setting_sounds, exit_sounds;
static bool metadata_exists;
static char diagnostics[512], capability[32];
static flashcart_t backend;

static flashcart_err_t voltage_only (uint16_t *voltage) {
    voltage_calls++;
    TEST_ASSERT(voltage != NULL);
    if (sample_error == FLASHCART_OK) *voltage = sample_voltage;
    return sample_error;
}

static flashcart_err_t voltage_temperature (uint16_t *voltage, int16_t *temperature) {
    combined_calls++;
    TEST_ASSERT(voltage != NULL && temperature != NULL);
    if (sample_error == FLASHCART_OK) {
        *voltage = sample_voltage;
        *temperature = sample_temperature;
    }
    return sample_error;
}

static flashcart_firmware_version_t firmware (void) {
    return (flashcart_firmware_version_t){0};
}

bool directory_exists (char *path) {
    TEST_CHECK(strcmp(path, "sd:/menu/metadata") == 0);
    return metadata_exists;
}

void settings_save (settings_t *settings) {
    (void)settings;
    settings_saves++;
}

void menu_show_error (menu_t *menu, char *message) {
    warnings++;
    TEST_CHECK(strcmp(message,
        "ERROR: Low voltage detected\n\n"
        "The flashcart reports a voltage less than 3.0V.\n"
        "\n"
        "COMMON CAUSES:\n"
        "- Faulty power supply\n"
        "- Clone Flashcart modifications.") == 0);
    menu->next_mode = MENU_MODE_ERROR;
}

void sound_play_effect (sound_effect_t effect) {
    if (effect == SFX_SETTING) setting_sounds++;
    else if (effect == SFX_EXIT) exit_sounds++;
    else TEST_CHECK(false);
}

void ui_components_settings_row_draw (int y, const char *label, const char *value, bool selected) {
    (void)y;
    TEST_CHECK(!selected);
    if (strcmp(label, "Diagnostics") == 0) snprintf(capability, sizeof(capability), "%s", value);
}

void ui_components_messagebox_draw (char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(diagnostics, sizeof(diagnostics), format, args);
    va_end(args);
}

static void reset (void) {
    backend = (flashcart_t){.has_feature = dummy_has_feature, .get_firmware_version = firmware};
    flashcart = &backend;
    sample_voltage = 3300;
    sample_temperature = 235;
    sample_error = FLASHCART_OK;
    voltage_calls = combined_calls = settings_saves = warnings = 0;
    setting_sounds = exit_sounds = 0;
    metadata_exists = false;
    diagnostics[0] = capability[0] = '\0';
    show_extra_info_message = false;
}

/** @brief Prefer voltage-only data without inventing a combined capability. */
static void test_voltage_only_dispatch (void) {
    reset();
    backend.get_voltage = voltage_only;
    TEST_CHECK(flashcart_has_voltage() && !flashcart_has_voltage_temperature());
    uint16_t voltage = 123;
    TEST_CHECK(flashcart_get_voltage(&voltage) == FLASHCART_OK && voltage == 3300);
    TEST_CHECK(voltage_calls == 1 && combined_calls == 0);
    backend.get_voltage_temperature = voltage_temperature;
    TEST_CHECK(flashcart_get_voltage(&voltage) == FLASHCART_OK);
    TEST_CHECK(voltage_calls == 2 && combined_calls == 0);
    int16_t temperature = 0;
    TEST_CHECK(flashcart_get_voltage_temperature(&voltage, &temperature) == FLASHCART_OK);
    TEST_CHECK(temperature == 235 && combined_calls == 1 && voltage_calls == 2);
    sample_error = FLASHCART_ERR_INT;
    voltage = 123;
    TEST_CHECK(flashcart_get_voltage(&voltage) == sample_error && voltage == 123);
    TEST_CHECK(voltage_calls == 3 && combined_calls == 1);
}

/** @brief Existing SummerCart64-style callbacks remain usable by both APIs. */
static void test_combined_dispatch (void) {
    reset();
    backend.get_voltage_temperature = voltage_temperature;
    TEST_CHECK(flashcart_has_voltage() && flashcart_has_voltage_temperature());
    uint16_t voltage = 0;
    int16_t temperature = 0;
    sample_temperature = -5;
    TEST_CHECK(flashcart_get_voltage(&voltage) == FLASHCART_OK && voltage == 3300);
    TEST_CHECK(combined_calls == 1 && voltage_calls == 0);
    TEST_CHECK(flashcart_get_voltage_temperature(&voltage, &temperature) == FLASHCART_OK);
    TEST_CHECK(temperature == -5 && combined_calls == 2);
    sample_error = FLASHCART_ERR_INT;
    TEST_CHECK(flashcart_get_voltage(&voltage) == sample_error && combined_calls == 3);
}

/** @brief Missing callbacks stay unsupported, and null outputs never reach a backend. */
static void test_unsupported_and_arguments (void) {
    reset();
    uint16_t voltage = 123;
    int16_t temperature = 456;
    TEST_CHECK(!flashcart_has_voltage() && !flashcart_has_voltage_temperature());
    TEST_CHECK(flashcart_get_voltage(&voltage) == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED);
    TEST_CHECK(voltage == 123);
    TEST_CHECK(flashcart_get_voltage(NULL) == FLASHCART_ERR_ARGS);
    TEST_CHECK(flashcart_get_voltage_temperature(NULL, &temperature) == FLASHCART_ERR_ARGS);
    TEST_CHECK(flashcart_get_voltage_temperature(&voltage, NULL) == FLASHCART_ERR_ARGS);
    TEST_CHECK(flashcart_get_voltage_temperature(&voltage, &temperature) == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED);
    backend.get_voltage = voltage_only;
    backend.get_voltage_temperature = voltage_temperature;
    TEST_CHECK(flashcart_get_voltage(NULL) == FLASHCART_ERR_ARGS);
    TEST_CHECK(voltage_calls == 0 && combined_calls == 0);
}

/** @brief Voltage-only cards expose Diagnostics without claiming temperature support. */
static void test_voltage_only_presentation (void) {
    reset();
    menu_t menu = {0};
    backend.get_voltage = voltage_only;
    pane_draw(&menu, true);
    TEST_CHECK(strcmp(capability, "Supported") == 0);
    TEST_CHECK(has_extra_info());
    TEST_CHECK(strcmp(format_voltage_temperature(), "3300 mV / Unsupported") == 0);
    sample_error = FLASHCART_ERR_INT;
    TEST_CHECK(strcmp(format_voltage_temperature(), "Unavailable / Unsupported") == 0);
    TEST_CHECK(!flashcart_has_voltage_temperature() && combined_calls == 0);
}

/** @brief Preserve signed combined formatting, including negative fractions and int16 minimum. */
static void test_combined_presentation (void) {
    reset();
    backend.get_voltage_temperature = voltage_temperature;
    static const struct { int16_t temperature; const char *expected; } cases[] = {
        {235, "3300 mV / 23.5 C"},
        {-235, "3300 mV / -23.5 C"},
        {-5, "3300 mV / -0.5 C"},
        {0, "3300 mV / 0.0 C"},
        {INT16_MIN, "3300 mV / -3276.8 C"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        sample_temperature = cases[i].temperature;
        TEST_CHECK(strcmp(format_voltage_temperature(), cases[i].expected) == 0);
    }
    sample_error = FLASHCART_ERR_INT;
    TEST_CHECK(strcmp(format_voltage_temperature(), "Unavailable") == 0);
    backend.get_voltage_temperature = NULL;
    TEST_CHECK(strcmp(format_voltage_temperature(), "Unsupported") == 0);
    menu_t menu = {0};
    pane_draw(&menu, true);
    TEST_CHECK(strcmp(capability, "Unsupported") == 0 && !has_extra_info());
}

/** @brief Existing Z/B modal controls and action hints stay unchanged. */
static void test_diagnostics_controls (void) {
    reset();
    menu_t menu = {0};
    TEST_CHECK(pane_hint(&menu, SETTINGS_HINT_RIGHT) == NULL);
    menu.actions.context = true;
    TEST_CHECK(pane_process(&menu) && !pane_blocks_tabs(&menu));
    backend.get_voltage = voltage_only;
    TEST_CHECK(strcmp(pane_hint(&menu, SETTINGS_HINT_RIGHT), "Z: Diagnostics\n") == 0);
    pane_enter(&menu);
    TEST_CHECK(pane_process(&menu) && pane_blocks_tabs(&menu) && setting_sounds == 1);
    menu.actions.context = false;
    pane_overlay(&menu);
    TEST_CHECK(strstr(diagnostics, "3300 mV / Unsupported") != NULL);
    TEST_CHECK(strstr(diagnostics, "Press Z or B to return.") != NULL);
    menu.actions.back = true;
    TEST_CHECK(pane_process(&menu) && !pane_blocks_tabs(&menu) && exit_sounds == 1);
    TEST_CHECK(!pane_process(&menu));
    menu.actions.back = false;
    menu.actions.context = true;
    TEST_CHECK(pane_process(&menu) && pane_blocks_tabs(&menu));
    TEST_CHECK(pane_process(&menu) && !pane_blocks_tabs(&menu) && exit_sounds == 2);
}

/** @brief Run the real startup flow on each threshold and unavailable sample. */
static void test_startup_voltage (void) {
    const uint16_t values[] = {2990, 3000, 3010};
    for (unsigned combined = 0; combined < 2; combined++) {
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
            reset();
            if (combined) backend.get_voltage_temperature = voltage_temperature;
            else backend.get_voltage = voltage_only;
            sample_voltage = values[i];
            menu_t menu = {.storage_prefix = "sd:/"};
            menu.settings.first_run = true;
            view_startup_init(&menu);
            TEST_CHECK(warnings == (values[i] < 3000));
            TEST_CHECK(menu.next_mode == (values[i] < 3000 ? MENU_MODE_ERROR : MENU_MODE_CREDITS));
            TEST_CHECK(menu.settings.first_run == (values[i] < 3000));
            TEST_CHECK(settings_saves == (values[i] >= 3000));
            TEST_CHECK(voltage_calls + combined_calls == 1);
        }
    }
    for (unsigned unsupported = 0; unsupported < 2; unsupported++) {
        reset();
        if (!unsupported) backend.get_voltage = voltage_only;
        sample_error = FLASHCART_ERR_INT;
        sample_voltage = 2990;
        metadata_exists = true;
        menu_t menu = {.storage_prefix = "sd:/"};
        menu.settings.boot_into_grid = true;
        view_startup_init(&menu);
        TEST_CHECK(menu.next_mode == MENU_MODE_GRID && warnings == 0 && menu.grid_enabled);
        TEST_CHECK(settings_saves == 0);
        TEST_CHECK(voltage_calls == !unsupported);
    }
}

TEST_LIST = {
    {"voltage-only callback dispatch and preference", test_voltage_only_dispatch},
    {"combined voltage callback fallback", test_combined_dispatch},
    {"unsupported voltage and invalid arguments", test_unsupported_and_arguments},
    {"voltage-only Diagnostics presentation", test_voltage_only_presentation},
    {"combined signed Diagnostics presentation", test_combined_presentation},
    {"Diagnostics modal controls remain unchanged", test_diagnostics_controls},
    {"startup voltage threshold and unavailable data", test_startup_voltage},
    {NULL, NULL},
};
