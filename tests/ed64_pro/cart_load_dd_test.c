#include <acutest.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Only theme storage and hardware queries need host substitutes. Menu/path/file
 * handling and all launch decisions below use the production implementation. */
typedef struct { uint8_t r, g, b, a; } color_t;
bool is_memory_expanded (void);
#undef ALIGN
#undef MIN
#undef MAX
#include "../../src/menu/cart_load.c"
#include "../../src/menu/path.c"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../../src/utils/fs.c"
#pragma GCC diagnostic pop

static bool expanded, physical_dd, supported;
static flashcart_err_t rom_result, save_result, ipl_result, disk_result;
static unsigned ipl_calls, disk_calls, multi_calls;
static char loaded_ipl[512], loaded_save[512], calls[16];
static flashcart_save_type_t loaded_save_type;
static flashcart_disk_parameters_t loaded_parameters;

bool is_memory_expanded (void) { return expanded; }
uint32_t io_read (pi_addr_t address) {
    return address == 0x05000540 && physical_dd ? 0 : UINT32_MAX;
}
bool flashcart_has_feature (flashcart_features_t feature) {
    if (feature == FLASHCART_FEATURE_64DD) return supported;
    return false;
}
flashcart_err_t flashcart_load_64dd_ipl (char *path, flashcart_progress_callback_t *progress) {
    (void)progress;
    ipl_calls++;
    snprintf(loaded_ipl, sizeof(loaded_ipl), "%s", path);
    strcat(calls, "I");
    return ipl_result;
}
flashcart_err_t flashcart_load_64dd_disk (char *path, flashcart_disk_parameters_t *parameters) {
    TEST_CHECK(strcmp(path, "/fixtures/primary.ndd") == 0);
    disk_calls++;
    loaded_parameters = *parameters;
    strcat(calls, "D");
    return disk_result;
}
flashcart_err_t flashcart_load_64dd_disks (char *path, flashcart_disk_parameters_t *parameters, char **paths, int count) {
    TEST_CHECK(count == 1);
    TEST_CHECK(strcmp(paths[0], "/fixtures/second.ndd") == 0);
    multi_calls++;
    return flashcart_load_64dd_disk(path, parameters);
}
flashcart_err_t flashcart_load_rom (char *path, bool byte_swap, flashcart_progress_callback_t *progress) {
    (void)progress;
    TEST_CHECK(strcmp(path, "/fixtures/F-Zero.z64") == 0);
    TEST_CHECK(!byte_swap);
    strcat(calls, "R");
    return rom_result;
}
flashcart_err_t flashcart_load_save (char *path, flashcart_save_type_t save_type) {
    snprintf(loaded_save, sizeof(loaded_save), "%s", path);
    loaded_save_type = save_type;
    strcat(calls, "S");
    return save_result;
}
flashcart_err_t flashcart_set_next_boot_mode (flashcart_reboot_mode_t mode) {
    (void)mode;
    return FLASHCART_OK;
}
rom_save_type_t rom_info_get_save_type (rom_info_t *info) { return info->save_type; }

static void reset (menu_t *menu) {
    memset(menu, 0, sizeof(*menu));
    expanded = supported = true;
    physical_dd = false;
    rom_result = save_result = ipl_result = disk_result = FLASHCART_OK;
    ipl_calls = disk_calls = multi_calls = 0;
    calls[0] = loaded_ipl[0] = loaded_save[0] = '\0';
    menu->load.disk_slots.primary.disk_path = path_create("/fixtures/primary.ndd");
    menu->next_mode = MENU_MODE_LOAD_DISK;
}
static void finish (menu_t *menu) {
    path_free(menu->load.disk_slots.primary.disk_path);
    path_free(menu->load.disk_slots.swap_slot[0].disk_path);
    path_free(menu->load.rom_path);
}
static void make_ipl (menu_t *menu, char *directory, const char *filename) {
    strcpy(directory, "/tmp/n64-dd-load-XXXXXX");
    TEST_ASSERT(mkdtemp(directory) != NULL);
    menu->storage_prefix = directory;
    path_t *path = path_init(directory, DDIPL_LOCATION);
    TEST_ASSERT(!directory_create(path_get(path)));
    path_push(path, (char *)filename);
    FILE *file = fopen(path_get(path), "wb");
    TEST_ASSERT(file != NULL);
    TEST_ASSERT(fclose(file) == 0);
    path_free(path);
}
static void remove_ipl (menu_t *menu, const char *filename) {
    path_t *path = path_init(menu->storage_prefix, DDIPL_LOCATION);
    path_push(path, (char *)filename);
    TEST_ASSERT(unlink(path_get(path)) == 0);
    path_pop(path);
    TEST_ASSERT(rmdir(path_get(path)) == 0);
    path_pop(path);
    TEST_ASSERT(rmdir(path_get(path)) == 0);
    TEST_ASSERT(rmdir(menu->storage_prefix) == 0);
    path_free(path);
}
static void test_ipl_regions (void) {
    const char *names[] = {"NDXJ0.n64", "NDDJ2.n64", "NDDE0.n64"};
    for (int region = DISK_REGION_DEVELOPMENT; region <= DISK_REGION_USA; region++) {
        TEST_CASE(names[region]);
        menu_t menu;
        reset(&menu);
        char directory[64];
        make_ipl(&menu, directory, names[region]);
        menu.load.disk_slots.primary.disk_info.region = region;
        menu.load.disk_slots.primary.disk_info.disk_type = DISK_TYPE_2;
        menu.load.disk_slots.primary.disk_info.defect_tracks[1][2] = 7;
        menu.load.disk_slots.primary.disk_info.bad_system_area_lbas[4] = true;
        TEST_CHECK(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_OK);
        TEST_CHECK(strcmp(file_basename(loaded_ipl), names[region]) == 0);
        TEST_CHECK(strcmp(calls, "ID") == 0);
        TEST_CHECK(loaded_parameters.development_drive == (region == DISK_REGION_DEVELOPMENT));
        TEST_CHECK(loaded_parameters.disk_type == DISK_TYPE_2);
        TEST_CHECK(loaded_parameters.defect_tracks[1][2] == 7);
        TEST_CHECK(loaded_parameters.bad_system_area_lbas[4]);
        remove_ipl(&menu, names[region]);
        finish(&menu);
    }
}
static void test_required_hardware_and_missing_ipl (void) {
    menu_t menu;
    reset(&menu);
    menu.storage_prefix = "/missing-dd-test-prefix";
    supported = false;
    TEST_CHECK(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_ERR_FUNCTION_NOT_SUPPORTED);
    supported = true;
    physical_dd = true;
    TEST_CHECK(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_ERR_64DD_PRESENT);
    physical_dd = false;
    expanded = false;
    TEST_CHECK(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_ERR_EXP_PAK_NOT_FOUND);
    expanded = true;
    TEST_CHECK(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_ERR_64DD_IPL_NOT_FOUND);
    TEST_CHECK(ipl_calls == 0 && disk_calls == 0);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_DISK);
    finish(&menu);
}
static void test_failed_callbacks_stop_setup (void) {
    menu_t menu;
    reset(&menu);
    char directory[64];
    make_ipl(&menu, directory, "NDXJ0.n64");
    ipl_result = FLASHCART_ERR_LOAD;
    TEST_CHECK(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_ERR_64DD_IPL_LOAD_FAIL);
    TEST_CHECK(ipl_calls == 1 && disk_calls == 0);
    TEST_CHECK(menu.flashcart_err == FLASHCART_ERR_LOAD);
    ipl_result = FLASHCART_OK;
    disk_result = FLASHCART_ERR_SD_CARD;
    TEST_CHECK(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_ERR_64DD_DISK_LOAD_FAIL);
    TEST_CHECK(menu.flashcart_err == FLASHCART_ERR_SD_CARD);
    TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_DISK);
    remove_ipl(&menu, "NDXJ0.n64");
    finish(&menu);
}
static void test_existing_swap_callback (void) {
    menu_t menu;
    reset(&menu);
    char directory[64];
    make_ipl(&menu, directory, "NDXJ0.n64");
    menu.load.disk_slots.swap_slot[0].disk_path = path_create("/fixtures/second.ndd");
    TEST_CHECK(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_OK);
    TEST_CHECK(multi_calls == 1);
    TEST_CHECK(ipl_calls == 1 && disk_calls == 1);
    remove_ipl(&menu, "NDXJ0.n64");
    finish(&menu);
}
static void test_combo_keeps_cartridge_save (void) {
    menu_t menu;
    reset(&menu);
    char directory[64];
    make_ipl(&menu, directory, "NDDJ2.n64");
    menu.load.disk_slots.primary.disk_info.region = DISK_REGION_JAPANESE;
    menu.load.combined_disk_rom = true;
    menu.load.rom_path = path_create("/fixtures/F-Zero.z64");
    menu.load.rom_info.save_type = SAVE_TYPE_SRAM_256KBIT;
    TEST_CHECK(cart_load_n64_rom_and_save(&menu, NULL, NULL) == CART_LOAD_OK);
    TEST_CHECK(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_OK);
    TEST_CHECK(strcmp(calls, "RSID") == 0);
    TEST_CHECK(strcmp(loaded_save, "/fixtures/F-Zero.sav") == 0);
    TEST_CHECK(loaded_save_type == FLASHCART_SAVE_TYPE_SRAM_256KBIT);
    remove_ipl(&menu, "NDDJ2.n64");
    finish(&menu);
}
/* The disk view returns immediately when this unchanged loader reports an error. */
static void test_combo_rom_save_failures (void) {
    for (unsigned fault = 0; fault < 2; fault++) {
        menu_t menu;
        reset(&menu);
        menu.load.combined_disk_rom = true;
        menu.load.rom_path = path_create("/fixtures/F-Zero.z64");
        menu.load.rom_info.save_type = SAVE_TYPE_SRAM_256KBIT;
        if (fault == 0) rom_result = FLASHCART_ERR_LOAD;
        else save_result = FLASHCART_ERR_SD_CARD;
        TEST_CHECK(cart_load_n64_rom_and_save(&menu, NULL, NULL) ==
            (fault == 0 ? CART_LOAD_ERR_ROM_LOAD_FAIL : CART_LOAD_ERR_SAVE_LOAD_FAIL));
        TEST_CHECK(strcmp(calls, fault == 0 ? "R" : "RS") == 0);
        TEST_CHECK(ipl_calls == 0 && disk_calls == 0);
        TEST_CHECK(menu.next_mode == MENU_MODE_LOAD_DISK);
        finish(&menu);
    }
}

TEST_LIST = {
    {"disk region selects expected IPL and parameters", test_ipl_regions},
    {"missing hardware or IPL prevents setup", test_required_hardware_and_missing_ipl},
    {"callback failures stop DD setup", test_failed_callbacks_stop_setup},
    {"existing sibling slots select the multi-disk callback", test_existing_swap_callback},
    {"combo ROM or save errors stop the shared loader", test_combo_rom_save_failures},
    {"combo loads cartridge save before IPL and disk", test_combo_keeps_cartridge_save},
    {NULL, NULL},
};
