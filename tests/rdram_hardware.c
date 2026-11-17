/* Standalone CPU/RDRAM probe. No menu, filesystem, flashcart, save or USB API. */
#include <inttypes.h>
#include <libdragon.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
    PASSES = 3,
    PATTERNS = 9,
    HEADROOM = 128 * 1024,
    ALLOCATION_STEP = 64 * 1024,
    GUARD_BYTES = 64,
    PROGRESS_WORDS = 256 * 1024,
};

static const char *const pattern_names[PATTERNS] = {
    "zeros", "ones", "AAAAAAAA", "55555555", "address",
    "inverse address", "walking one", "walking zero", "PRBS mix",
};

static struct {
    unsigned pass, pattern;
    const char *phase;
    size_t completed_words;
    uint64_t checked_words, mismatches, guard_mismatches;
    uintptr_t first_address;
    uint32_t first_expected, first_actual;
    bool first_valid, finished;
} result;

static volatile uint32_t *allocation, *memory;
static size_t tested_bytes, words;
static int detected_bytes;
static uint64_t started, finished_ticks;

static void screen(void) {
    console_clear();
    printf("RDRAM CPU UNCACHED HARDWARE TEST\n\n");
    printf("Detected RAM: %d MiB (%d bytes)\n", detected_bytes / (1024 * 1024), detected_bytes);
    printf("Tested buffer: %lu KiB, 64-byte aligned\n", (unsigned long)(tested_bytes / 1024));
    if (memory) {
        printf("Uncached range: %08" PRIXPTR "-%08" PRIXPTR "\n",
               (uintptr_t)memory, (uintptr_t)memory + tested_bytes - 1);
    }
    printf("Pass %u/%d  pattern %u/%d: %s\n", result.pass, PASSES,
           result.pattern + 1, PATTERNS, pattern_names[result.pattern]);
    printf("%s: %lu/%lu words\n", result.phase,
           (unsigned long)result.completed_words, (unsigned long)words);
    printf("Verified words: %" PRIu64 "\n", result.checked_words);
    printf("Mismatches: %" PRIu64 "  guards: %" PRIu64 "\n",
           result.mismatches, result.guard_mismatches);
    if (result.first_valid) {
        printf("FIRST FAILURE at %08" PRIXPTR "\n", result.first_address);
        printf("Expected %08" PRIX32 "  actual %08" PRIX32 "\n",
               result.first_expected, result.first_actual);
    }
    uint64_t elapsed = (result.finished ? finished_ticks : get_ticks()) - started;
    printf("Elapsed: %.1f seconds\n", elapsed / (double)TICKS_PER_SECOND);
    if (result.finished) {
        printf("\nRESULT: %s\n", result.mismatches || result.guard_mismatches ? "FAIL" : "PASS");
    }
    printf("\nScope: allocated heap buffer only.\n");
    printf("Program, stack, framebuffer and other RAM\nare UNTESTED (%lu KiB outside buffer).\n",
           (unsigned long)(((size_t)detected_bytes - tested_bytes) / 1024));
    printf("No DMA, audio, menu, FS, cart or save test.\n");
    if (result.finished) printf("Persistent report; power off to exit.\n");
    console_render();
}

static void mismatch(volatile uint32_t *address, uint32_t expected, uint32_t actual, bool guard) {
    if (guard) result.guard_mismatches++;
    else result.mismatches++;
    if (!result.first_valid) {
        result.first_valid = true;
        result.first_address = (uintptr_t)address;
        result.first_expected = expected;
        result.first_actual = actual;
    }
}

static uint32_t expected_word(size_t index) {
    uint32_t address = (uint32_t)(uintptr_t)(memory + index);
    switch (result.pattern) {
    case 0: return 0;
    case 1: return UINT32_MAX;
    case 2: return UINT32_C(0xAAAAAAAA);
    case 3: return UINT32_C(0x55555555);
    case 4: return address;
    case 5: return ~address;
    case 6: return UINT32_C(1) << (index & 31);
    case 7: return ~(UINT32_C(1) << (index & 31));
    default: {
        uint32_t value = (uint32_t)index ^ (UINT32_C(0x9E3779B9) * result.pass);
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        return value;
    }
    }
}

static void check_guards(void) {
    const uint32_t expected = UINT32_C(0xC37A591E);
    const size_t guard_words = GUARD_BYTES / sizeof(uint32_t);
    for (size_t i = 0; i < guard_words; i++) {
        volatile uint32_t *before = allocation + i;
        volatile uint32_t *after = memory + words + i;
        uint32_t actual = *before;
        if (actual != expected) mismatch(before, expected, actual, true);
        actual = *after;
        if (actual != expected) mismatch(after, expected, actual, true);
    }
}

int main(void) {
    detected_bytes = get_memory_size();
    console_init();
    console_set_debug(false);
    console_set_render_mode(RENDER_MANUAL);
    started = get_ticks();
    result.phase = "allocating";

    heap_stats_t heap;
    sys_get_heap_stats(&heap);
    size_t available = heap.free > HEADROOM + 2 * GUARD_BYTES
        ? (size_t)heap.free - HEADROOM - 2 * GUARD_BYTES : 0;
    available -= available % ALLOCATION_STEP;
    /* Each failed attempt reduces the request; at most RAM/64 KiB attempts. */
    while (available >= ALLOCATION_STEP) {
        allocation = malloc_uncached_aligned(64, available + 2 * GUARD_BYTES);
        if (allocation) {
            sys_get_heap_stats(&heap);
            if (heap.free >= HEADROOM) break;
            free_uncached((void *)allocation);
            allocation = NULL;
        }
        available -= ALLOCATION_STEP;
    }
    if (!allocation) {
        console_clear();
        printf("RDRAM CPU UNCACHED HARDWARE TEST\n");
        printf("Detected RAM: %d bytes\n", detected_bytes);
        printf("RESULT: INCONCLUSIVE - allocation failed\n");
        printf("No test writes performed. Power off to exit.\n");
        for (;;) console_render();
    }
    tested_bytes = available;
    words = tested_bytes / sizeof(uint32_t);
    memory = allocation + GUARD_BYTES / sizeof(uint32_t);
    const size_t guard_words = GUARD_BYTES / sizeof(uint32_t);
    for (size_t i = 0; i < guard_words; i++) {
        allocation[i] = UINT32_C(0xC37A591E);
        memory[words + i] = UINT32_C(0xC37A591E);
    }

    for (result.pass = 1; result.pass <= PASSES; result.pass++) {
        for (result.pattern = 0; result.pattern < PATTERNS; result.pattern++) {
            result.phase = "writing";
            result.completed_words = 0;
            screen();
            for (size_t i = 0; i < words; i++) {
                memory[i] = expected_word(i);
                if ((i + 1) % PROGRESS_WORDS == 0) {
                    result.completed_words = i + 1;
                    screen();
                }
            }
            MEMORY_BARRIER();
            result.phase = "reading";
            result.completed_words = 0;
            screen();
            for (size_t i = 0; i < words; i++) {
                uint32_t expected = expected_word(i);
                uint32_t actual = memory[i];
                if (actual != expected) mismatch(memory + i, expected, actual, false);
                result.checked_words++;
                if ((i + 1) % PROGRESS_WORDS == 0) {
                    result.completed_words = i + 1;
                    screen();
                }
            }
            result.completed_words = words;
            check_guards();
            screen();
        }
    }
    result.pass = PASSES;
    result.pattern = PATTERNS - 1;
    result.phase = "complete";
    finished_ticks = get_ticks();
    result.finished = true;
    /* Keep the allocation and report alive; do not reuse or free tested RAM. */
    for (;;) screen();
}
