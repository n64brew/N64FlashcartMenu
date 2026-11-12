#include <inttypes.h>
#include <libdragon.h>
#include <stdio.h>
#include <string.h>

#if defined(ED64_PRO_TEST_STORAGE) || defined(ED64_PRO_TEST_LOAD)
#include <fatfs/ff.h>
#endif
#ifdef ED64_PRO_TEST_LOAD
#include "flashcart/flashcart_utils.h"
#include <libcart/cart.h>
#endif

#include "flashcart/ed64_pro/ed64_pro.h"
#ifdef ED64_PRO_TEST_STORAGE_FAIL_WRITE
#include "menu/views/views.h"
#endif

#ifndef ED64_PRO_TEST_BUILD_ID
#error Build this ROM with tests/Makefile.n64
#endif

#if (defined(ED64_PRO_TEST_INFO) + defined(ED64_PRO_TEST_STORAGE) + defined(ED64_PRO_TEST_LOAD)) > 1
#error Select one qualification mode
#endif
#if defined(ED64_PRO_TEST_LOAD_TIMING) && !defined(ED64_PRO_TEST_LOAD)
#error Timing qualification requires load mode
#endif
#if defined(ED64_PRO_TEST_RAW_WRITE) && !defined(ED64_PRO_TEST_STORAGE)
#error Raw-write qualification requires storage mode
#endif
#if defined(ED64_PRO_TEST_STORAGE_FAIL_WRITE) && !defined(ED64_PRO_TEST_STORAGE)
#error Injected-write qualification requires storage mode
#endif
#if defined(ED64_PRO_TEST_STORAGE_FAIL_WRITE) && defined(ED64_PRO_TEST_RAW_WRITE)
#error Select raw-write qualification or injected-write qualification
#endif
#if defined(ED64_PRO_TEST_STORAGE_FAIL_WRITE) && \
    (!defined(ED64_PRO_TEST_FAIL_WRITE_N) || ED64_PRO_TEST_FAIL_WRITE_N != 1)
#error Injected-write qualification requires ED64_PRO_TEST_FAIL_WRITE_N=1
#endif

#if !defined(ED64_PRO_TEST_STORAGE) && !defined(ED64_PRO_TEST_INFO) && !defined(ED64_PRO_TEST_LOAD)
enum {
    STRESS_OPERATIONS = 100000,
    STRESS_TIMER_US = 50,
};

#define CANARY_BEFORE 0x13579BDFu
#define CANARY_AFTER 0x2468ACE0u

static struct {
    volatile uint32_t before;
    uint32_t value;
    volatile uint32_t irq_updates;
    volatile uint32_t after;
} register_line __attribute__((aligned(16)));

static struct {
    volatile uint32_t before;
    ed64_pro_status_t value;
    volatile uint32_t irq_updates;
    volatile uint32_t after;
} status_line __attribute__((aligned(16)));

_Static_assert(sizeof(register_line) == 16, "Register caller and ISR must share a cache line");
_Static_assert(sizeof(status_line) == 16, "Status caller and ISR must share a cache line");

static timer_link_t stress_timer;
static volatile bool stress_active;
static volatile uint32_t irq_dma_overlap;
static volatile uint32_t irq_canary_errors;
static volatile uint32_t irq_sink;

/**
 * @brief Validate guard words around the ISR-shared register and status outputs.
 */
static bool canaries_ok (void) {
    return register_line.before == CANARY_BEFORE && register_line.after == CANARY_AFTER &&
           status_line.before == CANARY_BEFORE && status_line.after == CANARY_AFTER;
}

/**
 * @brief Dirty caller cache lines and count PI/IRQ overlaps and canary corruption.
 *
 * This ISR never submits PI work, so it is not a competing DMA producer.
 */
static void stress_interrupt (int overflow) {
    (void)overflow;
    irq_sink ^= *(volatile uint32_t *)&register_line.value;
    volatile uint8_t *bytes = (volatile uint8_t *)&status_line.value;
    for (size_t i = 0; i < sizeof(status_line.value); i++) {
        irq_sink ^= bytes[i];
    }
    register_line.irq_updates++;
    status_line.irq_updates++;
    if (!canaries_ok()) {
        irq_canary_errors++;
    }
    if (stress_active && (*PI_STATUS & PI_STATUS_DMA_BUSY)) {
        irq_dma_overlap++;
    }
}

#endif

/**
 * @brief Label the console video standard, falling back for unknown values.
 */
static const char *tv_name (void) {
    switch (get_tv_type()) {
    case TV_PAL: return "PAL";
    case TV_NTSC: return "NTSC";
    case TV_MPAL: return "MPAL";
    default: return "unknown";
    }
}

static void screen_init (void) {
    console_init();
    console_set_debug(false);
    console_set_render_mode(RENDER_MANUAL);
}

/**
 * @brief Print build/console identity and only a successfully completed passive EDID read.
 */
static void print_identity (void) {
    const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
    printf("ED64 Pro transport qualification\nBuild: %s\n", ED64_PRO_TEST_BUILD_ID);
    printf("Console: %s, %d MiB\n", tv_name(), get_memory_size() / (1024 * 1024));
    if (diagnostics->probe.id_valid) {
        printf("EDID: %08" PRIX32 "\n", diagnostics->probe.id);
    } else {
        printf("EDID: no completed read\n");
    }
}

/**
 * @brief Report valid inherited status, cleanup counts, and measured phase/PI timing.
 */
static void print_init_metrics (void) {
    const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
    const ed64_pro_ll_metrics_t *metrics = ed64_pro_ll_get_metrics();
    if (diagnostics->inherited_status_valid) {
        const ed64_pro_status_t *status = &diagnostics->inherited_status;
        printf(
            "Inherited: %02X %02X %02X %02X\n",
            (unsigned)status->key,
            (unsigned)status->opaque[0],
            (unsigned)status->opaque[1],
            (unsigned)status->mcu_status
        );
    }
    printf(
        "Drained: %zu + %zu bytes; RX peak: %" PRIu32 "\n",
        diagnostics->initial_drained,
        diagnostics->post_stop_drained,
        metrics->rx_peak
    );
    for (ed64_pro_phase_t phase = ED64_PRO_PHASE_PROBE; phase < ED64_PRO_PHASE_READY; phase++) {
        printf("%s: %" PRIu32 " ms\n", ed64_pro_phase_name(phase), TICKS_TO_MS(diagnostics->phase_ticks[phase]));
    }
    printf(
        "Attempt: %" PRIu32 " ms; max PI: %" PRIu64 " us\n",
        TICKS_TO_MS(diagnostics->attempt_ticks),
        (uint64_t)metrics->max_pi_ticks * 1000000 / TICKS_PER_SECOND
    );
}

static void terminal (void) __attribute__((noreturn));
/**
 * @brief Keep qualification results visible without activating storage, USB, or retries.
 */
static void terminal (void) {
    printf("\nHalted; no further MCU commands or USB polling.\n");
    printf("After any failure: power cycle. Do not retry.\n");
    for (;;) {
        console_render();
    }
}

#if !defined(ED64_PRO_TEST_STORAGE) && !defined(ED64_PRO_TEST_INFO) && !defined(ED64_PRO_TEST_LOAD)
/**
 * @brief Exercise 100,000 status/EDID operations under timer-driven cache-line contention.
 *
 * Stop on the first transport or integrity failure. A passing run requires
 * observed IRQ/DMA overlap and ends on the terminal screen without storage access.
 */
static void run_stress (void) {
    const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
    register_line.before = status_line.before = CANARY_BEFORE;
    register_line.after = status_line.after = CANARY_AFTER;
    register_line.value = diagnostics->probe.id;
    status_line.value = diagnostics->status;
    start_timer(&stress_timer, TIMER_TICKS(STRESS_TIMER_US), TF_CONTINUOUS, stress_interrupt);

    uint32_t completed = 0;
    uint32_t max_status_ticks = 0;
    uint32_t max_register_ticks = 0;
    ed64_pro_ll_error_t error = ED64_PRO_LL_OK;
    bool integrity = true;
    while (completed < STRESS_OPERATIONS) {
        bool status_operation = (completed & 1u) == 0;
        uint32_t start = TICKS_READ();
        uint32_t deadline = start + TICKS_FROM_MS(ED64_PRO_STATUS_TIMEOUT_MS);
        ed64_pro_ll_transaction_t transaction;
        stress_active = true;
        error = ed64_pro_ll_begin(&transaction, deadline);
        if (error == ED64_PRO_LL_OK) {
            error = status_operation
                        ? ed64_pro_ll_check_status(&transaction, &status_line.value, deadline)
                        : ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &register_line.value, deadline);
            ed64_pro_ll_error_t release_error = ed64_pro_ll_end(&transaction);
            if (error == ED64_PRO_LL_OK) {
                error = release_error;
            }
        }
        stress_active = false;
        uint32_t elapsed = TICKS_READ() - start;
        uint32_t *maximum = status_operation ? &max_status_ticks : &max_register_ticks;
        if (elapsed > *maximum) {
            *maximum = elapsed;
        }
        integrity = canaries_ok() && irq_canary_errors == 0 && (register_line.value & 0xFFFF00FFu) == 0xED640027u;
        if (error != ED64_PRO_LL_OK || !integrity) {
            break;
        }
        completed++;
        if (completed % 10000 == 0) {
            console_clear();
            print_identity();
            printf("\nStatus/EDID stress: %" PRIu32 " / %u\n", completed, STRESS_OPERATIONS);
            printf(
                "Timer IRQs: %" PRIu32 "; IRQ/DMA overlap: %" PRIu32 "\n", register_line.irq_updates, irq_dma_overlap
            );
            console_render();
        }
    }
    stop_timer(&stress_timer);

    console_clear();
    print_identity();
    print_init_metrics();
    printf("\nStress: %" PRIu32 " / %u operations\n", completed, STRESS_OPERATIONS);
    printf(
        "IRQs: %" PRIu32 "; DMA overlap: %" PRIu32 "; canary errors: %" PRIu32 "\n",
        register_line.irq_updates,
        irq_dma_overlap,
        irq_canary_errors
    );
    printf(
        "Max status/register: %" PRIu64 " / %" PRIu64 " us\n",
        (uint64_t)max_status_ticks * 1000000 / TICKS_PER_SECOND,
        (uint64_t)max_register_ticks * 1000000 / TICKS_PER_SECOND
    );
    if (error != ED64_PRO_LL_OK) {
        const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
        printf(
            "FAILED: %s operation, transport error %u\n", (completed & 1u) == 0 ? "status" : "EDID", (unsigned)error
        );
        printf("Confirmed RX: %zu; reserved DMA ticket: %" PRIu64 "\n", failure->transferred, failure->ticket);
        if (error == ED64_PRO_LL_MCU_ERROR) {
            printf("Fresh MCU status: 0x%02X\n", (unsigned)status_line.value.mcu_status);
        }
    } else if (!integrity) {
        printf("FAILED: caller data/canary corruption\n");
    } else if (!irq_dma_overlap) {
        printf("UNQUALIFIED: no timer IRQ overlapped a PI DMA\n");
    } else {
        printf("Stress PASS for this launch; record all results.\n");
        printf("Other carts, launches, and memory sizes still need testing.\n");
    }
    terminal();
}

#endif

#if defined(ED64_PRO_TEST_STORAGE) || defined(ED64_PRO_TEST_LOAD)
static void print_volume (const FIL *file) {
    const FATFS *fs = file->obj.fs;
    const char *type = "unknown";
    switch (fs->fs_type) {
    case FS_FAT12: type = "FAT12"; break;
    case FS_FAT16: type = "FAT16"; break;
    case FS_FAT32: type = "FAT32"; break;
    case FS_EXFAT: type = "exFAT"; break;
    }
    printf("SD: %s, cluster %" PRIu32 " bytes\n", type, (uint32_t)fs->csize * 512u);
}
#endif

#ifdef ED64_PRO_TEST_STORAGE
#ifndef ED64_PRO_TEST_FIXTURE_PATH
#define ED64_PRO_TEST_FIXTURE_PATH "/ed64pro-test/read.bin"
#endif
#define STORAGE_FIXTURE_SIZE (1024u * 1024u)
#define SCRATCH_PATH "/ed64pro-test/scratch.bin"
#define SCRATCH_SIZE (64u * 1024u)
#define SCRATCH_SECTORS (SCRATCH_SIZE / 512u)
#define SCRATCH_PATTERN_A 0xA5u

static uint8_t storage_buffer[4096] __attribute__((aligned(16)));

static uint32_t fixture_crc32 (uint32_t crc, const uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

static bool read_fixture (void) {
    FIL file;
    if (f_open(&file, ED64_PRO_TEST_FIXTURE_PATH, FA_READ) != FR_OK) {
        printf("H2.1 FAILED: cannot open read.bin\n");
        return false;
    }
    print_volume(&file);
    bool valid = f_size(&file) == STORAGE_FIXTURE_SIZE;
    uint32_t crc = UINT32_MAX;
    uint32_t bytes = 0;
    uint32_t read_ticks = 0;
    while (valid && bytes < STORAGE_FIXTURE_SIZE) {
        UINT count = 0;
        uint32_t start = TICKS_READ();
        FRESULT result = f_read(&file, storage_buffer, sizeof(storage_buffer), &count);
        read_ticks += TICKS_READ() - start;
        valid = result == FR_OK && count == sizeof(storage_buffer);
        if (valid) {
            crc = fixture_crc32(crc, storage_buffer, count);
            bytes += count;
        }
    }
    if (!valid) {
        printf("H2.1 FAILED: size/read at %" PRIu32 " bytes\n", bytes);
        return false;
    }
    if (f_close(&file) != FR_OK) {
        printf("H2.1 FAILED: close\n");
        return false;
    }
    crc ^= UINT32_MAX;
    printf("H2.1: %" PRIu32 " bytes, CRC32 %08" PRIX32 "\n", bytes, crc);
    printf(
        "Read I/O: %" PRIu32 " ms, %" PRIu64 " KiB/s\n",
        TICKS_TO_MS(read_ticks),
        read_ticks ? (uint64_t)bytes * TICKS_PER_SECOND / read_ticks / 1024 : 0
    );
#ifdef ED64_PRO_TEST_FIXTURE_CRC32
    if (crc != (uint32_t)ED64_PRO_TEST_FIXTURE_CRC32) {
        printf("H2.1 FAILED: expected %08" PRIX32 "\n", (uint32_t)ED64_PRO_TEST_FIXTURE_CRC32);
        return false;
    }
    printf("H2.1 PASS for this fixture and launch.\n");
    return true;
#else
    printf("UNQUALIFIED: build needs ED64_PRO_TEST_FIXTURE_CRC32.\n");
    return false;
#endif
}

#ifdef ED64_PRO_TEST_RAW_WRITE
/* Walk only an existing, exact-size read-only file; never allocate or alter FAT metadata. */
static bool map_scratch (uint32_t sectors[SCRATCH_SECTORS]) {
    FIL file;
    if (f_open(&file, SCRATCH_PATH, FA_READ) != FR_OK) {
        printf("H2.2 FAILED: cannot open scratch.bin\n");
        return false;
    }
    FATFS *fs = file.obj.fs;
    bool valid = f_size(&file) == SCRATCH_SIZE && fs && !fs->wflag && fs->csize && fs->database <= UINT32_MAX;
    for (uint32_t index = 0; valid && index < SCRATCH_SECTORS;) {
        if (f_lseek(&file, index * 512u + 256u) != FR_OK) {
            valid = false;
            break;
        }
        uint32_t cluster = file.clust;
        if (cluster < 2 || cluster >= fs->n_fatent) {
            valid = false;
            break;
        }
        uint64_t base = (uint64_t)fs->database + (uint64_t)fs->csize * (cluster - 2u);
        uint32_t in_cluster = index % fs->csize;
        for (; valid && in_cluster < fs->csize && index < SCRATCH_SECTORS; in_cluster++, index++) {
            uint64_t lba = base + in_cluster;
            if (lba < fs->database || lba > UINT32_MAX) {
                valid = false;
                break;
            }
            for (uint32_t previous = 0; previous < index; previous++) {
                if (sectors[previous] == lba) {
                    valid = false;
                    break;
                }
            }
            sectors[index] = (uint32_t)lba;
        }
    }
    if (!valid) {
        printf("H2.2 FAILED: scratch size/cluster/LBA map\n");
        return false;
    }
    if (f_close(&file) != FR_OK) {
        printf("H2.2 FAILED: scratch close\n");
        return false;
    }
    return true;
}

static bool scratch_sector_io (uint32_t lba, bool write) {
    ed64_pro_ll_transaction_t transaction;
    uint32_t deadline = TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS);
    ed64_pro_ll_error_t error = ed64_pro_ll_begin(&transaction, deadline);
    if (error == ED64_PRO_LL_OK) {
        error = write ? ed64_pro_ll_sd_write(&transaction, storage_buffer, lba, 1, deadline)
                      : ed64_pro_ll_sd_read(&transaction, storage_buffer, lba, 1, deadline);
        ed64_pro_ll_error_t end_error = ed64_pro_ll_end(&transaction);
        if (error == ED64_PRO_LL_OK) {
            error = end_error;
        }
    }
    if (error != ED64_PRO_LL_OK) {
        printf("H2.2 FAILED: %s LBA %" PRIu32 ", LL %u\n", write ? "write" : "read", lba, (unsigned)error);
        return false;
    }
    return true;
}

static uint8_t scratch_pattern_b (uint32_t offset) {
    return (uint8_t)(offset * 29u + 0x3Cu);
}

static bool write_scratch (void) {
    uint32_t sectors[SCRATCH_SECTORS];
    if (!map_scratch(sectors)) {
        return false;
    }
    /* Verify every mapped sector is A before sending any WRE command. */
    for (uint32_t sector = 0; sector < SCRATCH_SECTORS; sector++) {
        if (!scratch_sector_io(sectors[sector], false)) {
            return false;
        }
        for (uint32_t byte = 0; byte < 512; byte++) {
            if (storage_buffer[byte] != SCRATCH_PATTERN_A) {
                printf("H2.2 REFUSED: scratch is not pattern A (A5).\n");
                return false;
            }
        }
    }
    printf("H2.2: writing B to even scratch sectors only...\n");
    console_render();
    for (uint32_t sector = 0; sector < SCRATCH_SECTORS; sector += 2) {
        for (uint32_t byte = 0; byte < 512; byte++) {
            storage_buffer[byte] = scratch_pattern_b(sector * 512u + byte);
        }
        /* An error after WRE can be an ambiguous write. Do not retry or release. */
        if (!scratch_sector_io(sectors[sector], true)) {
            return false;
        }
    }
    for (uint32_t sector = 0; sector < SCRATCH_SECTORS; sector++) {
        if (!scratch_sector_io(sectors[sector], false)) {
            return false;
        }
        for (uint32_t byte = 0; byte < 512; byte++) {
            uint8_t expected = (sector & 1u) ? SCRATCH_PATTERN_A : scratch_pattern_b(sector * 512u + byte);
            if (storage_buffer[byte] != expected) {
                printf("H2.2 FAILED: verify at %" PRIu32 "\n", sector * 512u + byte);
                return false;
            }
        }
    }
    printf("H2.2 read-back matches; host verification still required.\n");
    return true;
}
#endif

#ifdef ED64_PRO_TEST_STORAGE_FAIL_WRITE
/* The standalone ROM uses the real fault view with the real Pro error callback. */
char *flashcart_convert_error_message (flashcart_err_t error) {
    return ed64_pro_get_flashcart()->get_error_message(error);
}

static void run_storage_fail_write (void) __attribute__((noreturn));
static void run_storage_fail_write (void) {
    FIL writer;
    if (f_open(&writer, SCRATCH_PATH, FA_READ | FA_WRITE) != FR_OK || f_size(&writer) != SCRATCH_SIZE) {
        printf("FAIL-WRITE REFUSED: existing scratch.bin must be 64 KiB.\n");
        terminal();
    }
    for (uint32_t offset = 0; offset < SCRATCH_SIZE; offset += sizeof(storage_buffer)) {
        UINT read = 0;
        if (f_read(&writer, storage_buffer, sizeof(storage_buffer), &read) != FR_OK || read != sizeof(storage_buffer)) {
            printf("FAIL-WRITE REFUSED: scratch read.\n");
            terminal();
        }
        for (size_t byte = 0; byte < sizeof(storage_buffer); byte++) {
            if (storage_buffer[byte] != SCRATCH_PATTERN_A) {
                printf("FAIL-WRITE REFUSED: scratch must contain A5.\n");
                terminal();
            }
        }
    }
    FIL reader;
    if (f_open(&reader, ED64_PRO_TEST_FIXTURE_PATH, FA_READ) != FR_OK || f_lseek(&writer, 0) != FR_OK ||
        f_tell(&writer) != 0 || writer.obj.fs->wflag) {
        printf("FAIL-WRITE REFUSED: handle preparation/dirty window.\n");
        terminal();
    }
    for (size_t byte = 0; byte < 1024; byte++) {
        storage_buffer[byte] = (uint8_t)(byte * 29u + 0x3Cu);
    }
    UINT written = 0;
    FRESULT write_result = f_write(&writer, storage_buffer, 1024, &written);
    const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
    if (write_result != FR_DISK_ERR || !diagnostics->storage_failed ||
        diagnostics->storage_failure.code != ED64_PRO_LL_TIMEOUT || diagnostics->storage_failure.transferred != 1 ||
        diagnostics->storage_failure.pi_context_valid || !diagnostics->storage_operation ||
        strcmp(diagnostics->storage_operation, "Write") != 0) {
        printf("FAIL-WRITE FAILED: expected latched write fault (%u, %u bytes).\n", (unsigned)write_result, written);
        terminal();
    }
    uint32_t submissions = ed64_pro_ll_get_metrics()->submissions;
    UINT read = 0;
    FRESULT read_result = f_read(&reader, storage_buffer, 512, &read);
    FRESULT reader_close = f_close(&reader);
    FRESULT writer_close = f_close(&writer);
    if (read_result != FR_INVALID_OBJECT || reader_close != FR_INVALID_OBJECT || writer_close != FR_INVALID_OBJECT ||
        read != 0 || ed64_pro_ll_get_metrics()->submissions != submissions) {
        printf("FAIL-WRITE FAILED: old handles were not inert.\n");
        terminal();
    }
    flashcart_err_t handoff_error = ed64_pro_get_flashcart()->deinit();
    if (handoff_error != FLASHCART_ERR_SD_CARD || diagnostics->storage_state != ED64_PRO_STORAGE_FAILED ||
        !diagnostics->storage_failed || ed64_pro_ll_get_metrics()->submissions != submissions) {
        printf("H5.7 FAILED: latched write did not refuse handoff without I/O.\n");
        terminal();
    }
    static menu_t fault_menu;
    fault_menu.mode = fault_menu.next_mode = MENU_MODE_FAULT;
    fault_menu.flashcart_err = handoff_error;
    console_clear();
    view_fault_init(&fault_menu);
    if (ed64_pro_ll_get_metrics()->submissions != submissions) {
        printf("H5.7 FAILED: fault initialization submitted PI I/O.\n");
        terminal();
    }
    printf("\nH5.7 hardware refusal PASS\n");
    printf("Old handles invalid; deinit refused; PI count unchanged.\n");
    printf("No START or game boot attempted.\n");
    printf("Power-cycle console and USB before host verification.\n");
    for (;;) {
        view_fault_display(&fault_menu, NULL);
    }
}
#endif

static void run_storage (void) {
    flashcart_t *adapter = ed64_pro_get_flashcart();
    flashcart_err_t error = adapter->init();
    print_identity();
    print_init_metrics();
    if (error != FLASHCART_ERR_FUNCTION_NOT_SUPPORTED && error != FLASHCART_OK) {
        printf("Startup failed: %s\n", ed64_pro_get_error_message());
        terminal();
    }
    flashcart_firmware_version_t firmware = adapter->get_firmware_version();
    printf("Firmware: %u.%u.%" PRIu32 "\n", firmware.major, firmware.minor, firmware.revision);
    if (!ed64_pro_init_sdfs("sd:/", 4)) {
        printf("Mount failed: %s\n", ed64_pro_get_error_message());
        terminal();
    }
    printf("Mounted; host tools must remain idle.\n");
    console_render();
    if (!read_fixture()) {
        terminal();
    }
#ifdef ED64_PRO_TEST_STORAGE_FAIL_WRITE
    run_storage_fail_write();
#endif
#ifdef ED64_PRO_TEST_RAW_WRITE
    if (!write_scratch()) {
        terminal();
    }
#else
    printf("H2.2 not run: ED64_PRO_TEST_RAW_WRITE is disabled.\n");
#endif
    error = adapter->deinit();
    if (error != FLASHCART_ERR_FUNCTION_NOT_SUPPORTED) {
        printf("Release FAILED: %s\n", ed64_pro_get_error_message());
        terminal();
    }
    printf("RELEASED: %s\n", ed64_pro_get_error_message());
    printf("Power off console before host verification.\n");
    terminal();
}
#endif

#ifdef ED64_PRO_TEST_LOAD
#ifndef ED64_PRO_TEST_LOAD_PATH
#define ED64_PRO_TEST_LOAD_PATH "/ed64pro-test/load.z64"
#endif
#ifndef ED64_PRO_TEST_V64_PATH
#define ED64_PRO_TEST_V64_PATH "/ed64pro-test/load.v64"
#endif
#ifndef ED64_PRO_TEST_AUX_PATH
#define ED64_PRO_TEST_AUX_PATH "/ed64pro-test/load-offset.bin"
#endif
#ifndef ED64_PRO_TEST_OVERSIZE_PATH
#define ED64_PRO_TEST_OVERSIZE_PATH "/ed64pro-test/oversize.z64"
#endif
#ifndef ED64_PRO_TEST_LOAD_TIMING_32_PATH
#define ED64_PRO_TEST_LOAD_TIMING_32_PATH "/ed64pro-test/boot/04-cic6105-CZLE.z64"
#endif
#ifndef ED64_PRO_TEST_LOAD_TIMING_64_PATH
#define ED64_PRO_TEST_LOAD_TIMING_64_PATH "/ed64pro-test/boot/07-cic6103-NP3E.z64"
#endif

static uint8_t source_buffer[4096] __attribute__((aligned(16)));
static uint8_t pi_buffer[4096] __attribute__((aligned(16)));
static unsigned load_progress_percent;
static bool load_progress_valid;

static void load_progress (float progress) {
    if (!(progress >= 0.0f && progress <= 1.0f)) {
        load_progress_valid = false;
        return;
    }
    unsigned percent = (unsigned)(progress * 100.0f);
    if (percent < load_progress_percent) {
        load_progress_valid = false;
    }
    if (percent / 25 > load_progress_percent / 25) {
        printf("%u%% ", percent);
        console_render();
    }
    load_progress_percent = percent;
}

static bool fixture_size (const char *path, uint32_t *size) {
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) {
        printf("FAILED: cannot open %s\n", path);
        return false;
    }
    print_volume(&file);
    FSIZE_t bytes = f_size(&file);
    if (bytes == 0 || bytes > UINT32_MAX || (bytes & 511u)) {
        printf("FAILED: fixture must have a nonzero sector-aligned size.\n");
        return false;
    }
    if (f_close(&file) != FR_OK) {
        printf("FAILED: fixture close\n");
        return false;
    }
    *size = (uint32_t)bytes;
    return true;
}

static bool compare_loaded (const char *path, uint32_t rom_offset, uint32_t file_offset, bool normalize_v64) {
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK || f_size(&file) < file_offset) {
        printf("FAILED: comparison open/size\n");
        return false;
    }
    if (f_lseek(&file, file_offset) != FR_OK || f_tell(&file) != file_offset) {
        printf("FAILED: comparison seek\n");
        return false;
    }
    uint32_t extent = (uint32_t)(f_size(&file) - file_offset);
    for (uint32_t offset = 0; offset < extent;) {
        UINT chunk = extent - offset;
        if (chunk > sizeof(source_buffer)) {
            chunk = sizeof(source_buffer);
        }
        UINT read = 0;
        if (f_read(&file, source_buffer, chunk, &read) != FR_OK || read != chunk) {
            printf("FAILED: source read at %" PRIu32 "\n", offset);
            return false;
        }
        if (normalize_v64) {
            for (UINT byte = 0; byte < chunk; byte += 2) {
                uint8_t first = source_buffer[byte];
                source_buffer[byte] = source_buffer[byte + 1];
                source_buffer[byte + 1] = first;
            }
        }
        pi_dma_read_data((void *)(uintptr_t)(0x10000000u + rom_offset + offset), pi_buffer, chunk);
        if (memcmp(source_buffer, pi_buffer, chunk) != 0) {
            for (UINT byte = 0; byte < chunk; byte++) {
                if (source_buffer[byte] != pi_buffer[byte]) {
                    printf(
                        "FAILED: PI offset %08" PRIX32 ": %02X != %02X\n",
                        rom_offset + offset + byte,
                        pi_buffer[byte],
                        source_buffer[byte]
                    );
                    break;
                }
            }
            return false;
        }
        offset += chunk;
    }
    if (f_tell(&file) != f_size(&file) || f_close(&file) != FR_OK) {
        printf("FAILED: comparison completion/close\n");
        return false;
    }
    printf("Compared %" PRIu32 " bytes at PI +%08" PRIX32 ".\n", extent, rom_offset);
    return true;
}

static bool load_rom_fixture (flashcart_t *adapter, char *path, bool normalize_v64, bool verify_contents) {
    cart_card_byteswap = normalize_v64;
    load_progress_percent = 0;
    load_progress_valid = true;
    uint32_t submissions = ed64_pro_ll_get_metrics()->submissions;
    uint32_t start = TICKS_READ();
    flashcart_err_t error = adapter->load_rom(path, load_progress);
    uint32_t elapsed = TICKS_READ() - start;
    cart_card_byteswap = 0;
    printf(
        "\nLoad: %" PRIu32 " ms, PI submissions %" PRIu32 "\n",
        TICKS_TO_MS(elapsed),
        ed64_pro_ll_get_metrics()->submissions - submissions
    );
    if (error != FLASHCART_OK) {
        printf("Load FAILED (%u): %s\n", (unsigned)error, ed64_pro_get_error_message());
        return false;
    }
    if (!load_progress_valid || load_progress_percent != 100) {
        printf("FAILED: progress callbacks\n");
        return false;
    }
    return !verify_contents || compare_loaded(path, 0, 0, normalize_v64);
}

static void run_load (void) {
    flashcart_t *adapter = ed64_pro_get_flashcart();
    flashcart_err_t error = adapter->init();
    print_identity();
    print_init_metrics();
    if (error != FLASHCART_OK || !ed64_pro_init_sdfs("sd:/", 4)) {
        printf("Startup/mount FAILED: %s\n", ed64_pro_get_error_message());
        terminal();
    }
#ifdef ED64_PRO_TEST_LOAD_TIMING
    static const struct {
        char *path;
        uint32_t size;
    } fixtures[] = {
        {ED64_PRO_TEST_LOAD_PATH, 8u * 1024u * 1024u},
        {ED64_PRO_TEST_LOAD_TIMING_32_PATH, 32u * 1024u * 1024u},
        {ED64_PRO_TEST_LOAD_TIMING_64_PATH, 64u * 1024u * 1024u},
    };
    for (size_t fixture = 0; fixture < sizeof(fixtures) / sizeof(fixtures[0]); fixture++) {
        uint32_t size;
        if (!fixture_size(fixtures[fixture].path, &size) || size != fixtures[fixture].size) {
            printf("TIMING REFUSED: fixture size mismatch.\n");
            terminal();
        }
        printf("Timing %" PRIu32 " MiB, comparison disabled:\n", size / (1024u * 1024u));
        console_render();
        if (!load_rom_fixture(adapter, fixtures[fixture].path, false, false)) {
            terminal();
        }
    }
    printf("Timing-only loads complete; no content-verification claim.\n");
    printf("No release or START. Power off before host access.\n");
    terminal();
#endif
#if defined(ED64_PRO_TEST_OVERSIZE)
    uint32_t oversize_size;
    const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
    if (!fixture_size(ED64_PRO_TEST_OVERSIZE_PATH, &oversize_size) || oversize_size <= diagnostics->rom_capacity) {
        printf("H3.3 REFUSED: fixture must exceed reported capacity.\n");
        terminal();
    }
    error = adapter->load_rom(ED64_PRO_TEST_OVERSIZE_PATH, NULL);
    if (error != FLASHCART_ERR_LOAD || diagnostics->storage_failed || diagnostics->state != ED64_PRO_READY ||
        diagnostics->storage_state != ED64_PRO_STORAGE_FATFS_OWNED ||
        ed64_pro_ll_get_failure()->code != ED64_PRO_LL_OK) {
        printf("H3.3 FAILED: oversized ROM refusal (%u).\n", (unsigned)error);
        terminal();
    }
    printf("H3.3 PASS: oversized ROM refused, storage healthy.\n");
    console_render();
#endif
    uint32_t rom_size;
    uint32_t auxiliary_size;
    if (!fixture_size(ED64_PRO_TEST_LOAD_PATH, &rom_size) || !fixture_size(ED64_PRO_TEST_AUX_PATH, &auxiliary_size) ||
        auxiliary_size <= 512u) {
        terminal();
    }
    printf("H3.1: z64 (%" PRIu32 " bytes)\n", rom_size);
    console_render();
    if (!load_rom_fixture(adapter, ED64_PRO_TEST_LOAD_PATH, false, true)) {
        terminal();
    }
    uint32_t start = TICKS_READ();
    error = adapter->load_file(ED64_PRO_TEST_AUX_PATH, rom_size, 512);
    printf("Auxiliary: %" PRIu32 " ms, skip512, ROM +%08" PRIX32 "\n", TICKS_TO_MS(TICKS_READ() - start), rom_size);
    if (error != FLASHCART_OK || !compare_loaded(ED64_PRO_TEST_AUX_PATH, rom_size, 512, false)) {
        printf("H3.1 auxiliary FAILED (%u)\n", (unsigned)error);
        terminal();
    }
    printf("H3.1 PASS for these ROM/auxiliary fixtures.\n");
    console_render();
    uint32_t v64_size;
    if (!fixture_size(ED64_PRO_TEST_V64_PATH, &v64_size) || v64_size != rom_size) {
        printf("H3.2 REFUSED: v64 size must match z64.\n");
        terminal();
    }
    printf("H3.2: v64 (%" PRIu32 " bytes)\n", v64_size);
    console_render();
    if (!load_rom_fixture(adapter, ED64_PRO_TEST_V64_PATH, true, true)) {
        terminal();
    }
    printf("H3.2 PASS: PI read-back matches normalized v64.\n");
    printf("No release or START attempted. Power off before host access.\n");
    terminal();
}
#endif

/**
 * @brief Run the selected Pro qualification without starting USB polling or game services.
 *
 * Initialize the clock before deadlines, bypass iQue, and stop permanently on
 * startup failure rather than retrying or selecting another cartridge backend.
 */
int main (void) {
    /* timer_init resets CP0 COUNT; it must precede the probe/attempt deadline. */
    timer_init();
    if (sys_bbplayer()) {
        screen_init();
        printf("iQue: Pro probe intentionally bypassed.\n");
        terminal();
    }
#ifdef ED64_PRO_TEST_INFO
    screen_init();
    flashcart_t *adapter = ed64_pro_get_flashcart();
    flashcart_err_t result = adapter->init();
    print_identity();
    print_init_metrics();
    flashcart_firmware_version_t firmware = adapter->get_firmware_version();
    printf("\nH1: %s\n", adapter->get_name());
    printf("Firmware: %u.%u.%" PRIu32 " (minimum 26.1014)\n", firmware.major, firmware.minor, firmware.revision);
    printf("Init result: %u\n%s\n", (unsigned)result, ed64_pro_get_error_message());
    terminal();
#elif defined(ED64_PRO_TEST_STORAGE)
    screen_init();
    run_storage();
#elif defined(ED64_PRO_TEST_LOAD)
    screen_init();
    run_load();
#else
    ed64_pro_ll_error_t error = ed64_pro_init_transport();
    screen_init();
    print_identity();
#ifdef ED64_PRO_TEST_FORCE_TIMEOUT
    printf("Software PI-completion timeout injection build\n");
#endif
    if (error != ED64_PRO_LL_OK) {
        printf("\n%s\n", ed64_pro_get_error_message());
        print_init_metrics();
        terminal();
    }
    printf("\nTransport/SD ready; storage remains inactive.\n");
    print_init_metrics();
    printf("\nStarting 100,000 status/EDID operations...\n");
    console_render();
    run_stress();
#endif
}
