#include <inttypes.h>
#include <libdragon.h>
#include <stdio.h>
#include <string.h>

#ifdef ED64_PRO_TEST_STORAGE
#include <fatfs/ff.h>
#endif

#include "flashcart/ed64_pro/ed64_pro.h"

#ifndef ED64_PRO_TEST_BUILD_ID
#error Build this ROM with tests/Makefile.n64
#endif

#if defined(ED64_PRO_TEST_INFO) && defined(ED64_PRO_TEST_STORAGE)
#error Select one qualification mode
#endif
#if defined(ED64_PRO_TEST_RAW_WRITE) && !defined(ED64_PRO_TEST_STORAGE)
#error Raw-write qualification requires storage mode
#endif

#if !defined(ED64_PRO_TEST_STORAGE) && !defined(ED64_PRO_TEST_INFO)
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

#if !defined(ED64_PRO_TEST_STORAGE) && !defined(ED64_PRO_TEST_INFO)
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
