#include <acutest.h>
#include <string.h>

/* macOS system headers define these names before the project's own macros. */
#undef ALIGN
#undef MIN
#undef MAX

/* Exercise the actual facade, including its legacy unused-parameter callbacks. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../src/flashcart/flashcart.c"
#pragma GCC diagnostic pop

int cart_type;
char cart_card_byteswap;

static struct {
    bool ique;
    int bbfs_result;
    bool daisy;
    bool sdfs_ok;
    int detected_cart;
    int selected_cart;
    ed64_pro_probe_t probe;
    ed64_pro_ll_error_t transport_result;
    flashcart_err_t backend_result;
    char *message;
    unsigned probes;
    unsigned pro_inits;
    unsigned pro_getters;
    unsigned messages;
    unsigned bbfs;
    unsigned daisy_detects;
    unsigned daisy_mounts;
    unsigned getters;
    unsigned sdfs;
    unsigned isviewer;
    unsigned usb;
    unsigned callbacks;
    unsigned init_callbacks;
    unsigned file_calls;
    bool saw_byteswap;
} spy;

static flashcart_t *fallback;

bool sys_bbplayer (void) {
    return spy.ique;
}

int bbfs_init (void) {
    spy.bbfs++;
    return spy.bbfs_result;
}

/**
 * @brief Assert Pro probing precedes other cartridge/filesystem/debug work and return the fixture.
 */
ed64_pro_probe_t ed64_pro_probe (void) {
    spy.probes++;
    TEST_CHECK(spy.daisy_detects == 0 && spy.sdfs == 0 && spy.usb == 0 && spy.callbacks == 0);
    return spy.probe;
}

/**
 * @brief Model the Pro adapter's generic initialization result after identification.
 */
static flashcart_err_t pro_backend_init (void) {
    spy.pro_inits++;
    TEST_CHECK(spy.probes == 1 && spy.probe.outcome == ED64_PRO_PROBE_IDENTIFIED);
    return spy.transport_result == ED64_PRO_LL_OK ? FLASHCART_ERR_FUNCTION_NOT_SUPPORTED : FLASHCART_ERR_INT;
}

static char *pro_error_message (flashcart_err_t error) {
    spy.messages++;
    return error == FLASHCART_ERR_INT || error == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED ? spy.message : NULL;
}

static const char *pro_backend_name (void) {
    return "EverDrive-64 Pro";
}

static flashcart_t pro_backend = {
    .init = pro_backend_init,
    .get_name = pro_backend_name,
    .get_error_message = pro_error_message,
};

flashcart_t *ed64_pro_get_flashcart (void) {
    spy.pro_getters++;
    return &pro_backend;
}

bool daisydrive_detected (void) {
    spy.daisy_detects++;
    TEST_CHECK(spy.probes == 1);
    return spy.daisy;
}

/**
 * @brief Validate DaisyDrive mount arguments and return the configured SD result.
 */
bool daisydrive_init_sdfs (const char *prefix, int max_files) {
    spy.daisy_mounts++;
    TEST_CHECK(strcmp(prefix, "sd:/") == 0 && max_files == -1);
    return spy.sdfs_ok;
}

/**
 * @brief Simulate libcart detection while checking the legacy SD mount arguments.
 */
bool debug_init_sdfs (const char *prefix, int npart) {
    spy.sdfs++;
    TEST_CHECK(strcmp(prefix, "sd:/") == 0 && npart == -1);
    cart_type = spy.detected_cart;
    return spy.sdfs_ok;
}

bool debug_init_isviewer (void) {
    spy.isviewer++;
    return true;
}

bool debug_init_usblog (void) {
    spy.usb++;
    return true;
}

static flashcart_err_t backend_init (void) {
    spy.callbacks++;
    spy.init_callbacks++;
    return spy.backend_result;
}

static flashcart_err_t backend_deinit (void) {
    spy.callbacks++;
    return spy.backend_result;
}

static bool backend_has_feature (flashcart_features_t feature) {
    spy.callbacks++;
    return feature == FLASHCART_FEATURE_USB;
}

static flashcart_firmware_version_t backend_version (void) {
    spy.callbacks++;
    return (flashcart_firmware_version_t){.major = 1, .minor = 2, .revision = 3};
}

static flashcart_err_t backend_load_rom (char *path, flashcart_progress_callback_t *progress) {
    (void)progress;
    TEST_CHECK(strcmp(path, "sd:/test.z64") == 0);
    spy.callbacks++;
    spy.saw_byteswap = cart_card_byteswap;
    return spy.backend_result;
}

/**
 * @brief Assert file-load dispatch preserves the path and both ROM/file offsets.
 */
static flashcart_err_t backend_load_file (char *path, uint32_t rom_offset, uint32_t file_offset) {
    TEST_CHECK(strcmp(path, "sd:/test.bin") == 0 && rom_offset == 4096 && file_offset == 512);
    spy.callbacks++;
    return spy.backend_result;
}

static flashcart_err_t backend_set_save_type (flashcart_save_type_t save_type) {
    TEST_CHECK(save_type == FLASHCART_SAVE_TYPE_NONE);
    spy.callbacks++;
    return spy.backend_result;
}

static flashcart_t backend = {
    .init = backend_init,
    .deinit = backend_deinit,
    .has_feature = backend_has_feature,
    .get_firmware_version = backend_version,
    .load_rom = backend_load_rom,
    .load_file = backend_load_file,
    .set_save_type = backend_set_save_type,
};

static flashcart_t *select_backend (int type) {
    spy.getters++;
    spy.selected_cart = type;
    return &backend;
}

flashcart_t *d64_get_flashcart (void) {
    return select_backend(CART_CI);
}

flashcart_t *ed64_xseries_get_flashcart (void) {
    return select_backend(CART_EDX);
}

flashcart_t *ed64_vseries_get_flashcart (void) {
    return select_backend(CART_ED);
}

flashcart_t *sc64_get_flashcart (void) {
    return select_backend(CART_SC);
}

flashcart_t *daisydrive_get_flashcart (void) {
    return select_backend(100);
}

bool file_exists (char *path) {
    (void)path;
    spy.file_calls++;
    return true;
}

int64_t file_get_size (char *path) {
    (void)path;
    spy.file_calls++;
    return 0;
}

bool file_allocate (char *path, size_t size) {
    (void)path;
    (void)size;
    spy.file_calls++;
    return false;
}

bool file_fill (char *path, uint8_t value) {
    (void)path;
    (void)value;
    spy.file_calls++;
    return false;
}

/**
 * @brief Restore the ROM fallback and reset spies for an independent startup scenario.
 */
static void power_on (void) {
    if (!fallback) {
        fallback = flashcart;
    }
    flashcart = fallback;
    cart_type = CART_NULL;
    cart_card_byteswap = 0;
    memset(&spy, 0, sizeof(spy));
    spy.sdfs_ok = true;
    spy.detected_cart = CART_EDX;
    spy.selected_cart = -999;
    spy.probe = (ed64_pro_probe_t){.outcome = ED64_PRO_PROBE_ABSENT, .id_valid = true};
    spy.message = "EverDrive-64 Pro storage backend is not implemented.";
}

/**
 * @brief Select the Pro handler without mounting storage or invoking a previous backend.
 *
 * An indeterminate probe retains diagnostics without initializing transport.
 */
static void require_terminal_result (bool identified) {
    flashcart = &backend;
    const char *prefix = "sd:/";
    flashcart_err_t expected = identified && spy.transport_result == ED64_PRO_LL_OK
                                  ? FLASHCART_ERR_FUNCTION_NOT_SUPPORTED
                                  : FLASHCART_ERR_INT;
    TEST_CHECK(flashcart_init(&prefix) == expected);
    TEST_CHECK(prefix == NULL && flashcart == &pro_backend);
    TEST_CHECK(strcmp(flashcart_get_name(), "EverDrive-64 Pro") == 0);
    TEST_CHECK(spy.probes == 1 && spy.pro_inits == (unsigned)identified && spy.pro_getters == 1);
    TEST_CHECK(spy.daisy_detects == 0 && spy.daisy_mounts == 0 && spy.getters == 0);
    TEST_CHECK(spy.sdfs == 0 && spy.bbfs == 0 && spy.usb == 0 && spy.isviewer == 0);
    TEST_CHECK(spy.callbacks == 0 && spy.file_calls == 0);
    TEST_CHECK(flashcart_convert_error_message(expected) == spy.message);
    TEST_CHECK(spy.messages == 1);
}

static void test_ready_is_not_storage (void) {
    power_on();
    spy.probe = (ed64_pro_probe_t){
        .outcome = ED64_PRO_PROBE_IDENTIFIED,
        .id = 0xED640027u,
        .id_valid = true,
    };
    spy.transport_result = ED64_PRO_LL_OK;
    spy.daisy = true;
    require_terminal_result(true);
}

/**
 * @brief Verify each identified-Pro transport failure stays terminal without storage fallback.
 */
static void test_identified_failures (void) {
    const ed64_pro_ll_error_t failures[] = {
        ED64_PRO_LL_PI_ACQUIRE_TIMEOUT,
        ED64_PRO_LL_PI_TIMEOUT,
        ED64_PRO_LL_PI_ERROR,
        ED64_PRO_LL_TIMEOUT,
        ED64_PRO_LL_FIFO_COUNT,
        ED64_PRO_LL_DRAIN_LIMIT,
        ED64_PRO_LL_FRAME,
        ED64_PRO_LL_TRAILING_DATA,
        ED64_PRO_LL_MAILBOX_TIMEOUT,
        ED64_PRO_LL_MCU_ERROR,
    };
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        power_on();
        spy.probe = (ed64_pro_probe_t){
            .outcome = ED64_PRO_PROBE_IDENTIFIED,
            .id = 0xED64FF27u,
            .id_valid = true,
        };
        spy.transport_result = failures[i];
        spy.message = "SD readiness: retained transport/MCU error.";
        spy.daisy = true;
        require_terminal_result(true);
    }
}

/**
 * @brief Verify indeterminate probes neither initialize Pro transport nor try another cartridge.
 */
static void test_failed_probe_never_falls_back (void) {
    const ed64_pro_ll_error_t failures[] = {
        ED64_PRO_LL_PI_ACQUIRE_TIMEOUT,
        ED64_PRO_LL_PI_TIMEOUT,
        ED64_PRO_LL_PI_ERROR,
        ED64_PRO_LL_BUSY,
        ED64_PRO_LL_CONTEXT,
    };
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        power_on();
        spy.probe = (ed64_pro_probe_t){.outcome = ED64_PRO_PROBE_IO_ERROR, .error = failures[i]};
        spy.message = "Cartridge identity unknown: retained PI probe failure.";
        spy.daisy = true;
        require_terminal_result(false);
    }
}

/**
 * @brief Preserve the iQue prefix and NAND errors while bypassing all cartridge probing.
 */
static void test_ique_bypass (void) {
    for (unsigned fail = 0; fail < 2; fail++) {
        power_on();
        spy.ique = true;
        spy.bbfs_result = fail;
        spy.probe.outcome = ED64_PRO_PROBE_IDENTIFIED;
        const char *prefix = NULL;
        TEST_CHECK(flashcart_init(&prefix) == (fail ? FLASHCART_ERR_BBFS : FLASHCART_OK));
        TEST_CHECK(strcmp(prefix, "bbfs:/") == 0 && flashcart == fallback);
        TEST_CHECK(flashcart_get_name() == NULL);
        TEST_CHECK(spy.bbfs == 1 && spy.probes == 0 && spy.pro_inits == 0 && spy.daisy_detects == 0);
        TEST_CHECK(spy.sdfs == 0 && spy.getters == 0 && spy.usb == 0 && spy.callbacks == 0);
    }
}

/**
 * @brief Preserve all four libcart backends' selection, initialization, and debug USB policy.
 */
static void test_non_pro_dispatch (void) {
    const int types[] = {CART_CI, CART_EDX, CART_ED, CART_SC};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        power_on();
        spy.detected_cart = types[i];
        const char *prefix = NULL;
        TEST_CHECK(flashcart_init(&prefix) == FLASHCART_OK);
        TEST_CHECK(strcmp(prefix, "sd:/") == 0 && flashcart == &backend);
        TEST_CHECK(flashcart_get_name() == NULL);
        TEST_CHECK(spy.probes == 1 && spy.pro_inits == 0 && spy.sdfs == 1);
        TEST_CHECK(spy.daisy_detects == 1 && spy.daisy_mounts == 0 && spy.getters == 1);
        TEST_CHECK(spy.selected_cart == types[i] && spy.init_callbacks == 1 && spy.callbacks == 1);
        TEST_CHECK(cart_type == types[i]);
        TEST_CHECK(spy.file_calls == 0 && spy.isviewer == 0);
#ifdef NDEBUG
        TEST_CHECK(spy.usb == 0);
#else
        TEST_CHECK(spy.usb == 1);
#endif
    }
}

/**
 * @brief Preserve backend-error precedence over an SD initialization failure.
 */
static void test_non_pro_sd_and_init_errors (void) {
    const int types[] = {CART_CI, CART_EDX, CART_ED, CART_SC};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        for (unsigned fail_init = 0; fail_init < 2; fail_init++) {
            power_on();
            spy.detected_cart = types[i];
            spy.sdfs_ok = false;
            spy.backend_result = fail_init ? FLASHCART_ERR_OUTDATED : FLASHCART_OK;
            const char *prefix;
            TEST_CHECK(flashcart_init(&prefix) == (fail_init ? FLASHCART_ERR_OUTDATED : FLASHCART_ERR_SD_CARD));
            TEST_CHECK(strcmp(prefix, "sd:/") == 0 && spy.init_callbacks == 1);
            TEST_CHECK(spy.pro_inits == 0 && spy.selected_cart == types[i]);
        }
    }
}

/**
 * @brief Check unknown/no-cartridge ROM fallback, including the existing SD error rules.
 */
static void test_non_pro_rom_fallback (void) {
    const int types[] = {CART_NULL, 123};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        for (unsigned sd_ok = 0; sd_ok < 2; sd_ok++) {
            power_on();
            spy.detected_cart = types[i];
            spy.sdfs_ok = sd_ok;
            const char *prefix;
            TEST_CHECK(
                flashcart_init(&prefix) == (types[i] != CART_NULL && !sd_ok ? FLASHCART_ERR_SD_CARD : FLASHCART_OK)
            );
            TEST_CHECK(strcmp(prefix, "rom:/") == 0 && flashcart == fallback);
            TEST_CHECK(flashcart_get_name() == NULL);
            TEST_CHECK(spy.probes == 1 && spy.pro_inits == 0 && spy.sdfs == 1 && spy.isviewer == 1);
            TEST_CHECK(spy.getters == 0 && spy.callbacks == 0 && spy.file_calls == 0);
        }
    }
}

/**
 * @brief Preserve DaisyDrive's mount path and backend errors without libcart or USB setup.
 */
static void test_non_pro_daisy_dispatch (void) {
    for (unsigned fail = 0; fail < 2; fail++) {
        power_on();
        spy.daisy = true;
        spy.backend_result = fail ? FLASHCART_ERR_INT : FLASHCART_OK;
        const char *prefix;
        TEST_CHECK(flashcart_init(&prefix) == spy.backend_result);
        TEST_CHECK(strcmp(prefix, "sd:/") == 0 && flashcart == &backend);
        TEST_CHECK(flashcart_get_name() == NULL);
        TEST_CHECK(spy.probes == 1 && spy.pro_inits == 0 && spy.daisy_detects == 1 && spy.daisy_mounts == 1);
        TEST_CHECK(spy.getters == 1 && spy.selected_cart == 100 && spy.init_callbacks == 1);
        TEST_CHECK(spy.sdfs == 0 && spy.isviewer == 0 && spy.usb == 0);
    }
}

/**
 * @brief Verify callback forwarding, temporary byte swapping, and unsupported optional features.
 */
static void test_non_pro_callbacks (void) {
    power_on();
    const char *prefix;
    TEST_ASSERT(flashcart_init(&prefix) == FLASHCART_OK);
    TEST_CHECK(flashcart_has_feature(FLASHCART_FEATURE_USB));
    TEST_CHECK(!flashcart_has_feature(FLASHCART_FEATURE_64DD));
    flashcart_firmware_version_t version = flashcart_get_firmware_version();
    TEST_CHECK(version.major == 1 && version.minor == 2 && version.revision == 3);
    TEST_CHECK(flashcart_load_rom("sd:/test.z64", true, NULL) == FLASHCART_OK);
    TEST_CHECK(spy.saw_byteswap && !cart_card_byteswap);
    TEST_CHECK(flashcart_load_file("sd:/test.bin", 4096, 512) == FLASHCART_OK);
    TEST_CHECK(flashcart_load_save(NULL, FLASHCART_SAVE_TYPE_NONE) == FLASHCART_OK);
    TEST_CHECK(!flashcart_has_button_state() && !flashcart_has_voltage_temperature());
    bool pressed;
    TEST_CHECK(flashcart_get_button_state(&pressed) == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED);
    uint16_t voltage;
    int16_t temperature;
    TEST_CHECK(flashcart_get_voltage_temperature(&voltage, &temperature) == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED);
    TEST_CHECK(flashcart_load_64dd_ipl("sd:/ipl.bin", NULL) == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED);
    TEST_CHECK(flashcart_load_64dd_disk("sd:/disk.ndd", NULL) == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED);
    TEST_CHECK(flashcart_load_64dd_disks("sd:/disk.ndd", NULL, NULL, 0) == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED);
    TEST_CHECK(flashcart_set_next_boot_mode(FLASHCART_REBOOT_MODE_MENU) == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED);
    TEST_CHECK(flashcart_deinit() == FLASHCART_OK);
    TEST_CHECK(spy.file_calls == 0 && spy.pro_inits == 0);
}

/**
 * @brief Preserve existing generic error values and messages without a cartridge-specific error.
 */
static void test_error_values_preserved (void) {
    power_on();
    TEST_CHECK(FLASHCART_ERR_INT == 6 && FLASHCART_ERR_FUNCTION_NOT_SUPPORTED == 7);
    TEST_CHECK(
        strcmp(flashcart_convert_error_message(FLASHCART_ERR_SD_CARD), "Error during SD card initialization") == 0
    );
    TEST_CHECK(spy.messages == 0);
}

/**
 * @brief Specialize only supported errors, retaining the generic converter's success and fallback text.
 */
static void test_optional_error_messages (void) {
    power_on();
    flashcart = &pro_backend;
    TEST_CHECK(strcmp(flashcart_convert_error_message(FLASHCART_OK), "No error") == 0);
    TEST_CHECK(spy.messages == 0);
    TEST_CHECK(
        strcmp(flashcart_convert_error_message(FLASHCART_ERR_SD_CARD), "Error during SD card initialization") == 0
    );
    TEST_CHECK(flashcart_convert_error_message(FLASHCART_ERR_INT) == spy.message);
    TEST_CHECK(flashcart_convert_error_message(FLASHCART_ERR_FUNCTION_NOT_SUPPORTED) == spy.message);
    TEST_CHECK(strcmp(flashcart_convert_error_message((flashcart_err_t)999), "Unknown flashcart error") == 0);
    TEST_CHECK(spy.messages == 4 && spy.probes == 0 && spy.pro_inits == 0 && spy.file_calls == 0);
}

TEST_LIST = {
    {"Pro READY never activates storage", test_ready_is_not_storage},
    {"identified Pro failures never activate or fall back", test_identified_failures},
    {"indeterminate probe never selects another cartridge", test_failed_probe_never_falls_back},
    {"iQue bypass remains unchanged", test_ique_bypass},
    {"non-Pro libcart dispatch remains unchanged", test_non_pro_dispatch},
    {"non-Pro SD and initialization errors propagate", test_non_pro_sd_and_init_errors},
    {"non-Pro ROM fallback remains unchanged", test_non_pro_rom_fallback},
    {"non-Pro DaisyDrive dispatch remains unchanged", test_non_pro_daisy_dispatch},
    {"non-Pro mandatory and optional callbacks remain unchanged", test_non_pro_callbacks},
    {"public error values remain stable", test_error_values_preserved},
    {"optional error messages preserve generic fallbacks", test_optional_error_messages},
    {NULL, NULL},
};
