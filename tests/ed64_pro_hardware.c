#include <inttypes.h>
#include <libdragon.h>
#include <stdio.h>

#include "flashcart/ed64_pro/ed64_pro.h"

#ifndef ED64_PRO_TEST_BUILD_ID
#error Build this ROM with tests/Makefile.n64
#endif

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
    printf("\nNo storage backend, filesystem, or USB is active.\n");
    printf("After any failure: power cycle. Do not retry.\n");
    for (;;) {
        console_render();
    }
}

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

/**
 * @brief Run Pro identification, MCU handoff, and read-only stress without mounting storage.
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
}
