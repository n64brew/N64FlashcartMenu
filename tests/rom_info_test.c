#include <acutest.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Metadata tests use the real parser and host files instead of cartridge I/O. */
void debugf (const char *format, ...) {
    (void)format;
}

void *scratch_malloc (size_t size) {
    return malloc(size);
}

void scratch_free (void *pointer) {
    free(pointer);
}

/* Keep the normal extraction and sidecar paths without linking unused ZIP metadata code. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "../src/menu/rom_info.c"
#include "../src/menu/ini_parser.c"
#include "../src/menu/path.c"
#include "../src/boot/cic.c"
#pragma GCC diagnostic pop

/** @brief Extract metadata through the same header/database path used by ROM loading. */
static rom_info_t extract_header (const char *game_code, uint8_t version) {
    rom_header_t header = {.clock_rate = CLOCK_RATE_DEFAULT, .version = version};
    memcpy(header.game_code, game_code, sizeof(header.game_code));
    match_t match = find_rom_in_database(&header);
    rom_info_t info = {0};
    extract_rom_info(&match, &header, &info);
    return info;
}

/** @brief Check representative destinations and the unknown-region boundary. */
static void test_destination_tv_type (void) {
    static const struct {
        const char *game_code;
        rom_tv_type_t expected;
    } cases[] = {
        {"NSME", ROM_TV_TYPE_NTSC},
        {"NSMJ", ROM_TV_TYPE_NTSC},
        {"NSMP", ROM_TV_TYPE_PAL},
        {"NSMB", ROM_TV_TYPE_MPAL},
        {"NSM?", ROM_TV_TYPE_UNKNOWN},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TEST_CASE(cases[i].game_code);
        rom_info_t info = extract_header(cases[i].game_code, 0);
        TEST_CHECK(rom_info_get_tv_type(&info) == cases[i].expected);
        TEST_CHECK(!info.boot_override.tv && !info.boot_override.cic);
        rom_info_free_meta(&info);
    }
}

/** @brief Region-free homebrew flags do not replace destination-based automatic TV selection. */
static void test_homebrew_region_features (void) {
    rom_info_t info = extract_header("NEDP", 0x2B);
    TEST_CHECK(info.features.region_free && info.features.real_time_clock);
    TEST_CHECK(info.features.expansion_pak == EXPANSION_PAK_RECOMMENDED);
    TEST_CHECK(info.save_type == SAVE_TYPE_EEPROM_16KBIT);
    TEST_CHECK(rom_info_get_tv_type(&info) == ROM_TV_TYPE_PAL);
    rom_info_free_meta(&info);

    info = extract_header("NEDJ", 0);
    TEST_CHECK(!info.features.region_free && !info.features.real_time_clock);
    TEST_CHECK(info.features.expansion_pak == EXPANSION_PAK_UNSPECIFIED);
    TEST_CHECK(info.save_type == SAVE_TYPE_NONE);
    TEST_CHECK(rom_info_get_tv_type(&info) == ROM_TV_TYPE_NTSC);
    rom_info_free_meta(&info);

    info = extract_header("NED?", 2);
    TEST_CHECK(info.features.region_free);
    TEST_CHECK(rom_info_get_tv_type(&info) == ROM_TV_TYPE_UNKNOWN);
    rom_info_free_meta(&info);
}

/** @brief Repeated regional metadata extraction must not retain the preceding ROM's TV type. */
static void test_regional_sequence (void) {
    const char *sequence[] = {"NSMP", "NSMJ", "NSMP"};
    rom_tv_type_t expected[] = {ROM_TV_TYPE_PAL, ROM_TV_TYPE_NTSC, ROM_TV_TYPE_PAL};
    rom_info_t info = {0};
    for (size_t i = 0; i < sizeof(sequence) / sizeof(sequence[0]); i++) {
        rom_header_t header = {.clock_rate = CLOCK_RATE_DEFAULT};
        memcpy(header.game_code, sequence[i], sizeof(header.game_code));
        match_t match = find_rom_in_database(&header);
        extract_rom_info(&match, &header, &info);
        TEST_CHECK(rom_info_get_tv_type(&info) == expected[i]);
        rom_info_free_meta(&info);
    }
}

/** @brief Persist overrides, reload them, and restore Automatic while retaining other settings. */
static void test_override_persistence (void) {
    char directory[] = "/tmp/n64-rom-info-XXXXXX";
    TEST_ASSERT(mkdtemp(directory) != NULL);
    char rom_path[sizeof(directory) + 16];
    snprintf(rom_path, sizeof(rom_path), "%s/game.z64", directory);
    path_t *path = path_create(rom_path);
    path_t *ini_path = path_clone(path);
    path_ext_replace(ini_path, "ini");

    rom_info_t info = {.cic_type = ROM_CIC_TYPE_7102, .tv_type = ROM_TV_TYPE_PAL};
    TEST_ASSERT(rom_config_override_cic_type(path, &info, ROM_CIC_TYPE_x103) == ROM_OK);
    TEST_ASSERT(rom_config_override_tv_type(path, &info, ROM_TV_TYPE_NTSC) == ROM_OK);
    TEST_ASSERT(rom_config_setting_set_cheats(path, &info, true) == ROM_OK);

    info = (rom_info_t){.cic_type = ROM_CIC_TYPE_7102, .tv_type = ROM_TV_TYPE_PAL};
    load_rom_config_from_file(path, &info);
    TEST_CHECK(info.boot_override.cic && info.boot_override.tv);
    TEST_CHECK(rom_info_get_cic_type(&info) == ROM_CIC_TYPE_x103);
    TEST_CHECK(rom_info_get_tv_type(&info) == ROM_TV_TYPE_NTSC);
    uint8_t seed = 0;
    TEST_CHECK(!rom_info_get_cic_seed(&info, &seed) && seed == 0x78);
    TEST_CHECK(info.settings.cheats_enabled);

    TEST_ASSERT(rom_config_override_cic_type(path, &info, ROM_CIC_TYPE_AUTOMATIC) == ROM_OK);
    TEST_ASSERT(rom_config_override_tv_type(path, &info, ROM_TV_TYPE_AUTOMATIC) == ROM_OK);
    load_rom_config_from_file(path, &info);
    TEST_CHECK(!info.boot_override.cic && !info.boot_override.tv);
    TEST_CHECK(rom_info_get_cic_type(&info) == ROM_CIC_TYPE_7102);
    TEST_CHECK(rom_info_get_tv_type(&info) == ROM_TV_TYPE_PAL);
    TEST_CHECK(rom_info_get_cic_seed(&info, &seed) && seed == 0x3F);
    TEST_CHECK(info.settings.cheats_enabled);

    ini_t *persisted = ini_load(path_get(ini_path));
    TEST_ASSERT(persisted != NULL);
    TEST_CHECK(ini_get_string(persisted, "custom_boot", "cic_type", NULL) == NULL);
    TEST_CHECK(ini_get_string(persisted, "custom_boot", "tv_type", NULL) == NULL);
    TEST_CHECK(ini_get_bool(persisted, "", "cheats_enabled", false));
    ini_free(persisted);

    TEST_ASSERT(rom_config_setting_set_cheats(path, &info, false) == ROM_OK);
    errno = 0;
    TEST_CHECK(access(path_get(ini_path), F_OK) == -1 && errno == ENOENT);
    /* Missing sidecars must also clear previously loaded override flags. */
    info.boot_override.cic = info.boot_override.tv = true;
    load_rom_config_from_file(path, &info);
    TEST_CHECK(!info.boot_override.cic && !info.boot_override.tv);
    path_free(ini_path);
    path_free(path);
    TEST_CHECK(rmdir(directory) == 0);
}

TEST_LIST = {
    {"ROM destinations select NTSC PAL MPAL or unknown", test_destination_tv_type},
    {"homebrew region-free flags preserve destination TV selection", test_homebrew_region_features},
    {"PAL NTSC PAL metadata selection has no stale region", test_regional_sequence},
    {"CIC TV overrides persist and restore Automatic without losing settings", test_override_persistence},
    {NULL, NULL},
};
