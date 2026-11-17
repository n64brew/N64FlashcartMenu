/* Test-only observer for the unmodified production menu and native RTC path.
 * It never installs clock hooks, changes RTC source, or writes the clock.
 * The coordinator creates/owns the isolated SD namespace before deployment. */
#include <errno.h>
#include <fatfs/ff.h>
#include <inttypes.h>
#include <libdragon.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "flashcart/ed64_pro/ed64_pro.h"

#ifndef RTC_MENU_NONCE
#define RTC_MENU_NONCE 0x52544D01u
#endif

#ifndef RTC_MENU_TEST_PATH
#define RTC_MENU_TEST_PATH "/tests/rtc-simplify-20261117"
#endif
#define TEST_DIRECTORY "sd:" RTC_MENU_TEST_PATH
#define TEST_FILE TEST_DIRECTORY "/coexist.bin"
enum { PACKET_SIZE = 112, FILE_SIZE = 32768, CHUNK_SIZE = 4096 };

typedef struct {
    uint8_t packet[PACKET_SIZE];
    char text[80];
} observation_snapshot_t;

/* display_show can run in an RDP interrupt. The foreground collector builds
 * the inactive snapshot and publishes it only after every byte is complete. */
static observation_snapshot_t snapshots[2];
static volatile unsigned published_snapshot;
static volatile bool snapshot_ready;
static volatile bool display_seen;
static uint8_t *packet;
static uint8_t raw_clock[ED64_PRO_RTC_SIZE];
static uint8_t chunk[CHUNK_SIZE], expected[CHUNK_SIZE];
static int raw_result = -100, workload_result = -100;
static int source_available = -100;
static uint32_t cycles, file_size, file_crc, rtc_reads, directory_found, failure_stage, asset_crc;
static uint32_t asset_rom_end, asset_high_bytes, asset_count;
static time_t file_mtime, file_clock, previous_time;
static uint64_t previous_ticks, last_poll;
static bool observed;

static uint32_t crc_update (uint32_t crc, const uint8_t *bytes, size_t size) {
    while (size--) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xEDB88320u);
        }
    }
    return crc;
}

static int read_clock (uint8_t raw[ED64_PRO_RTC_SIZE]) {
    ed64_pro_ll_transaction_t transaction;
    uint32_t deadline = TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS);
    int result = ed64_pro_ll_begin(&transaction, deadline);
    if (result == ED64_PRO_LL_OK) {
        result = ed64_pro_ll_rtc_get(&transaction, raw, deadline);
        int end = ed64_pro_ll_end(&transaction);
        if (result == ED64_PRO_LL_OK) result = end;
    }
    rtc_reads++;
    return result;
}

static int fail (unsigned stage, int code) {
    if (!failure_stage) {
        failure_stage = stage;
        workload_result = code ? code : -1;
    }
    return workload_result;
}

static void pattern (uint8_t *out, unsigned offset, unsigned size) {
    for (unsigned i = 0; i < size; i++) {
        unsigned position = offset + i;
        out[i] = position * 37u + (position >> 8) * 11u + 0x6Du;
    }
}

static int check_rom_assets (void) {
    static const char *paths[] = {
        "rom:/menu/sounds/enter.wav64",
        "rom:/menu/sprites/filetype_folder.sprite",
        "rom:/menu/fonts/Firple-Bold.font64",
        "rom:/menu/sounds/bgm.wav64",
    };
    uint32_t crc = ~0u;
    asset_rom_end = asset_high_bytes = asset_count = 0;
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        FILE *file = fopen(paths[i], "rb");
        if (!file) return fail(9, -errno);
        pi_addr_t address = dfs_rom_addr(paths[i] + 5) /* DragonFS paths omit the rom:/ mount prefix. */;
        if (address < 0x10000000u) { fclose(file); return fail(10, -EIO); }
        uint32_t offset = address - 0x10000000u;
        size_t count;
        while ((count = fread(chunk, 1, sizeof(chunk), file)) != 0) {
            crc = crc_update(crc, chunk, count);
            uint32_t end = offset + count;
            if (end > asset_rom_end) asset_rom_end = end;
            if (end > 0x100000u) asset_high_bytes += end - (offset > 0x100000u ? offset : 0x100000u);
            offset = end;
        }
        int error = ferror(file) ? -EIO : 0;
        if (fclose(file) && !error) error = -errno;
        if (error) return fail(10, error);
        asset_count++;
    }
    asset_crc = ~crc;
    return 0;
}

static void coexistence_workload (void) {
    /* No failed file operation is retried. Cleanup always closes open handles. */
    FILE *file = fopen(TEST_FILE, "wb");
    if (!file) { fail(1, -errno); return; }
    file_clock = time(NULL);
    for (unsigned offset = 0; offset < FILE_SIZE && !failure_stage; offset += CHUNK_SIZE) {
        pattern(chunk, offset, sizeof(chunk));
        if (fwrite(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) {
            fail(2, -EIO);
            break;
        }
        /* The RTC command is deliberately interleaved with N64-owned FatFs. */
        raw_result = read_clock(raw_clock);
        if (raw_result != ED64_PRO_LL_OK) fail(3, raw_result);
    }
    if (fclose(file) && !failure_stage) fail(4, -errno);
    if (failure_stage) return;

    file = fopen(TEST_FILE, "rb");
    if (!file) { fail(5, -errno); return; }
    uint32_t crc = ~0u;
    file_size = 0;
    for (unsigned offset = 0; offset < FILE_SIZE && !failure_stage; offset += CHUNK_SIZE) {
        size_t size = fread(chunk, 1, sizeof(chunk), file);
        pattern(expected, offset, sizeof(expected));
        if (size != sizeof(chunk) || memcmp(chunk, expected, sizeof(chunk))) {
            fail(6, -EIO);
            break;
        }
        crc = crc_update(crc, chunk, size);
        file_size += size;
    }
    if (!failure_stage && (fgetc(file) != EOF || ferror(file))) fail(6, -EIO);
    if (fclose(file) && !failure_stage) fail(6, -errno);
    file_crc = ~crc;
    if (failure_stage) return;

    struct stat status;
    if (stat(TEST_FILE, &status) || status.st_size != FILE_SIZE) { fail(7, -EIO); return; }
    file_mtime = status.st_mtime;
    DIR directory;
    if (f_opendir(&directory, "0:" RTC_MENU_TEST_PATH) != FR_OK) { fail(8, -EIO); return; }
    directory_found = 0;
    FILINFO entry;
    FRESULT result;
    while ((result = f_readdir(&directory, &entry)) == FR_OK && entry.fname[0]) {
        if (!strcmp(entry.fname, "coexist.bin")) directory_found = 1;
    }
    FRESULT close_result = f_closedir(&directory);
    if (result != FR_OK || close_result != FR_OK || !directory_found) { fail(8, -EIO); return; }
    if (check_rom_assets()) return;
    cycles++;
    workload_result = 0;
}

static size_t cursor;
static void put (uint64_t value, unsigned size) {
    for (unsigned i = 0; i < size; i++) packet[cursor++] = value >> (8 * (size - i - 1));
}

static void collect_observation (void) {
    const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
    uint64_t ticks = get_ticks();
    time_t now = time(NULL);
    bool owned = diagnostics->storage_state == ED64_PRO_STORAGE_FATFS_OWNED && !diagnostics->storage_failed;
    /* Handoff may have closed RTC. Do not initialize it from an observer or
     * query rtc_get_source() in its closed state on a subsequent fault view. */
    int source = owned ? rtc_get_source() : -100;
    bool poll = !observed || ticks - last_poll >= TICKS_PER_SECOND;
    bool jump = observed && (now < previous_time ||
        now - previous_time > (time_t)((ticks - previous_ticks) / TICKS_PER_SECOND + 2));
    if (poll && owned) {
        raw_result = read_clock(raw_clock);
        source_available = rtc_is_source_available(RTC_SOURCE_JOYBUS);
        last_poll = ticks;
    }
    if (owned && source == RTC_SOURCE_JOYBUS && now >= 0 && !failure_stage &&
        (!cycles || jump)) {
        /* Three complete startup rounds, then one after each saved clock jump.
         * Between checks all files stay closed, allowing a settled power cut. */
        unsigned rounds = cycles ? 1 : 3;
        for (unsigned i = 0; i < rounds && !failure_stage; i++) coexistence_workload();
    }
    observed = true;
    previous_time = now;
    previous_ticks = ticks;

    /* Observation calls preserve real libdragon source selection and availability. */
    int available = owned ? source_available : -100;
    unsigned next = published_snapshot ^ 1u;
    observation_snapshot_t *snapshot = &snapshots[next];
    packet = snapshot->packet;
    memset(packet, 0, PACKET_SIZE); cursor = 0;
    put(0x52544D31u, 4); put(RTC_MENU_NONCE, 4);
    put(source, 4); put(available, 4); put(raw_result, 4);
    for (unsigned i = 0; i < sizeof(raw_clock); i++) put(raw_clock[i], 1);
    put(now, 8); put(ticks, 8); put(diagnostics->storage_state, 4);
    put(workload_result, 4); put(cycles, 4); put(file_size, 4); put(file_crc, 4);
    put(file_mtime, 8); put(file_clock, 8); put(rtc_reads, 4);
    put(directory_found, 4); put(failure_stage, 4); put(asset_crc, 4);
    put(asset_rom_end, 4); put(asset_high_bytes, 4); put(asset_count, 4);
    cursor = PACKET_SIZE - 4; put(~crc_update(~0u, packet, PACKET_SIZE - 4), 4);
    snprintf(snapshot->text, sizeof(snapshot->text),
             "RTC evidence: source %d available %d SD cycles %lu result %d",
             source, available, (unsigned long)cycles, workload_result);

    /* All hardware/filesystem calls above run with interrupts enabled. Only
     * publishing this complete double-buffered snapshot needs exclusion. */
    disable_interrupts();
    published_snapshot = next;
    snapshot_ready = true;
    enable_interrupts();
}

void __real_joypad_poll (void);
void __wrap_joypad_poll (void) {
    __real_joypad_poll();
    /* Ignore any early SDK polling before the menu finishes initializing its
     * filesystems and assets. Normal actions_update() runs after first draw. */
    if (display_seen) collect_observation();
}

void __real_display_show (surface_t *surface);
void __wrap_display_show (surface_t *surface) {
    /* Drawing only: no time, source, MCU, FatFs, or diagnostic queries here.
     * This is safe whether display_show runs in foreground or an RDP ISR. */
    display_seen = true;
    if (!snapshot_ready) {
        __real_display_show(surface);
        return;
    }
    const observation_snapshot_t *snapshot = &snapshots[published_snapshot];

    uint32_t black = graphics_make_color(0, 0, 0, 255), white = graphics_make_color(255, 255, 255, 255);
    graphics_draw_box(surface, 60, 312, 520, 140, black);
    graphics_set_color(white, black);
    graphics_draw_text(surface, 64, 316, snapshot->text);
    graphics_draw_box(surface, 60, 332, 520, 120, graphics_make_color(255, 0, 255, 255));
    for (unsigned bit = 0; bit < PACKET_SIZE * 8; bit++) {
        bool value = (snapshot->packet[bit / 8] >> (7 - bit % 8)) & 1;
        graphics_draw_box(surface, 64 + bit % 64 * 8, 336 + bit / 64 * 8, 8, 8, value ? white : black);
    }
    __real_display_show(surface);
}
