#include <libdragon.h>
#include <string.h>

#include <acutest.h>

/* Include the production unit to reset ROM-lifetime state between power-on fixtures.
 * No reset or alternate transport implementation is compiled into the ROM. */
#include "../src/flashcart/ed64_pro/ed64_pro_ll.c"

volatile uint32_t mock_pi_status;

static struct {
    uint32_t now;
    uint32_t tick_step;
    uint32_t cp0;
    unsigned irq_depth;
    bool uninitialized;
    bool kernel;
    bool force_busy;
    unsigned busy_checks;
    bool race;
    bool race_armed;
    unsigned busy_rechecks;
    unsigned enqueues;
    unsigned polls;
    unsigned finish_after;
    bool never_finish;
    bool error_on_finish;
    uint64_t next_ticket;
    uint64_t ticket;
    bool active;
    bool write;
    uint32_t pi;
    size_t length;
    size_t cache_length;
    void *ram_pointer;
    uint32_t register_value;
    uint32_t ram[ED64_PRO_FIFO_CHUNK];
    char events[128];
    size_t event_count;
    unsigned barriers;
    void (*poll_hook)(void);
} mock;

static void event (char value) {
    TEST_ASSERT(mock.event_count + 1 < sizeof(mock.events));
    mock.events[mock.event_count++] = value;
    mock.events[mock.event_count] = '\0';
}

uint32_t mock_cp0_status (void) {
    return mock.irq_depth ? mock.cp0 & ~C0_STATUS_IE : mock.cp0;
}

uint32_t mock_ticks_read (void) {
    uint32_t now = mock.now;
    mock.now += mock.tick_step;
    return now;
}

void mock_memory_barrier (void) {
    mock.barriers++;
}

void disable_interrupts (void) {
    if (mock.race_armed && mock.irq_depth == 0) {
        mock.race_armed = false;
        mock.busy_checks = 2;
    }
    mock.irq_depth++;
}

void enable_interrupts (void) {
    TEST_ASSERT(mock.irq_depth > 0);
    mock.irq_depth--;
}

interrupt_state_t get_interrupts_state (void) {
    if (mock.uninitialized) {
        return INTERRUPTS_UNINITIALIZED;
    }
    return mock.irq_depth ? INTERRUPTS_DISABLED : INTERRUPTS_ENABLED;
}

kthread_t *kthread_current (void) {
    return mock.kernel ? (kthread_t *)&mock : NULL;
}

int dma_busy (void) {
    if (mock.irq_depth) {
        mock.busy_rechecks++;
    }
    if (mock.force_busy || mock.active || (mock_pi_status & (PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY))) {
        return 1;
    }
    if (mock.busy_checks) {
        mock.busy_checks--;
        return 1;
    }
    if (mock.race && mock.irq_depth == 0) {
        mock.race = false;
        mock.race_armed = true;
    }
    return 0;
}

static uint64_t submit (const void *ram, pi_addr_t pi, unsigned long length, bool write) {
    /* Trap instead of hiding the SDK's queue-full wait in the mock. */
    TEST_ASSERT(mock.irq_depth > 0);
    TEST_ASSERT(!mock.force_busy && !mock.busy_checks && !mock.active);
    TEST_ASSERT(!(mock_pi_status & (PI_STATUS_DMA_BUSY | PI_STATUS_IO_BUSY | PI_STATUS_ERROR)));
    TEST_ASSERT(ram == pro_staging);
    TEST_ASSERT(((uintptr_t)ram & 511u) == 0);
    TEST_ASSERT((pi & 3u) == 0 && length > 0 && length <= 512 && length % 4 == 0);
    mock.enqueues++;
    mock.active = true;
    mock.write = write;
    mock.pi = pi;
    mock.length = length;
    mock.ram_pointer = (void *)ram;
    mock.ticket = ++mock.next_ticket;
    mock.polls = 0;
    mock_pi_status |= PI_STATUS_DMA_BUSY;
    event(write ? 'W' : 'R');
    return mock.ticket;
}

uint64_t dma_read_raw_async (void *ram, pi_addr_t pi, unsigned long length) {
    return submit(ram, pi, length, false);
}

uint64_t dma_write_raw_async (const void *ram, pi_addr_t pi, unsigned long length) {
    return submit(ram, pi, length, true);
}

static void finish_pending (void) {
    TEST_ASSERT(mock.active);
    if (!mock.write) {
        mock.ram[0] = mock.register_value;
    }
    mock.active = false;
    mock_pi_status &= ~PI_STATUS_DMA_BUSY;
    if (mock.error_on_finish) {
        mock_pi_status |= PI_STATUS_ERROR;
    }
    event('F');
}

bool dma_finished (uint64_t ticket) {
    TEST_ASSERT(ticket == mock.ticket && ticket != 0);
    TEST_ASSERT(mock.irq_depth == 0);
    if (mock.poll_hook) {
        mock.poll_hook();
    }
    if (mock.active && !mock.never_finish && mock.polls++ >= mock.finish_after) {
        finish_pending();
    }
    return !mock.active;
}

static void check_cache (const volatile void *ram, unsigned long length) {
    TEST_ASSERT(ram == pro_staging);
    TEST_ASSERT(((uintptr_t)ram & 511u) == 0);
    TEST_ASSERT(length > 0 && length <= 512 && length % ED64_PRO_CACHE_LINE == 0);
    TEST_ASSERT(!mock.active);
    mock.cache_length = length;
    TEST_ASSERT(
        (uintptr_t)&pro_ll + sizeof(pro_ll) <= (uintptr_t)ram ||
        (uintptr_t)&pro_ll >= (uintptr_t)ram + sizeof(pro_staging)
    );
}

void data_cache_hit_writeback_invalidate (volatile void *ram, unsigned long length) {
    check_cache(ram, length);
    memcpy(mock.ram, (const void *)ram, length);
    /* CPU and RDRAM are deliberately different: completion alone cannot publish data. */
    memset((void *)ram, 0xA5, length);
    event('B');
}

void data_cache_hit_writeback (const volatile void *ram, unsigned long length) {
    check_cache(ram, length);
    memcpy(mock.ram, (const void *)ram, length);
    event('C');
}

void data_cache_hit_invalidate (volatile void *ram, unsigned long length) {
    check_cache(ram, length);
    memcpy((void *)ram, mock.ram, length);
    event('I');
}

static void power_on (void) {
    memset(&mock, 0, sizeof(mock));
    memset(&pro_ll, 0, sizeof(pro_ll));
    memset(pro_staging, 0, sizeof(pro_staging));
    mock_pi_status = 0;
    mock.cp0 = C0_STATUS_IE;
    mock.tick_step = 1;
    mock.register_value = 0xED640127u;
}

static uint32_t deadline_ms (uint32_t ms) {
    return mock.now + TICKS_FROM_MS(ms);
}

static void begin (ed64_pro_ll_transaction_t *transaction) {
    TEST_ASSERT(ed64_pro_ll_begin(transaction, deadline_ms(15000)) == ED64_PRO_LL_OK);
}

static void end (ed64_pro_ll_transaction_t *transaction) {
    TEST_CHECK(ed64_pro_ll_end(transaction) == ED64_PRO_LL_OK);
    TEST_CHECK(mock.irq_depth == 0);
    TEST_CHECK(pro_ll.owner == NULL);
}

static void test_register_read (void) {
    power_on();
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint32_t output = 0xDEADBEEFu;
    TEST_CHECK(ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &output, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_CHECK(output == 0xED640127u);
    TEST_CHECK(mock.pi == 0x1F800014u && mock.length == 4);
    TEST_CHECK(mock.cache_length == 16);
    TEST_CHECK(mock.enqueues == 1 && mock.busy_rechecks == 1);
    TEST_CHECK(strcmp(mock.events, "BRFI") == 0);
    TEST_CHECK(mock.barriers >= 3);
    end(&transaction);
}

static void test_register_write (void) {
    power_on();
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    memset(pro_staging, 0xFF, sizeof(pro_staging));
    TEST_CHECK(ed64_pro_ll_write_reg(&transaction, ED64_PRO_REG_MBX, 0, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_CHECK(mock.pi == 0x1F800010u && mock.length == 4 && mock.ram[0] == 0);
    TEST_CHECK(mock.cache_length == 16);
    TEST_CHECK(strcmp(mock.events, "CWF") == 0);
    end(&transaction);
}

static void test_invalid_input (void) {
    power_on();
    ed64_pro_ll_transaction_t transaction;
    uint32_t output = 42;
    TEST_CHECK(ed64_pro_ll_begin(NULL, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &output, deadline_ms(1000)) == ED64_PRO_LL_BUSY);
    begin(&transaction);
    TEST_CHECK(ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, NULL, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(
        ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_FIFO, &output, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(ed64_pro_ll_write_reg(&transaction, ED64_PRO_REG_EDID, 0, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_write_reg(&transaction, ED64_PRO_REG_MBX, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    const size_t lengths[] = {0, 1, 2, 3, 513, SIZE_MAX};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        TEST_CHECK(
            pro_transfer(&transaction, ED64_PRO_REG_FIFO, lengths[i], false, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT
        );
    }
    TEST_CHECK(pro_transfer(&transaction, ED64_PRO_REG_EDID, 16, false, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.enqueues == 0 && mock.event_count == 0 && output == 42);
    TEST_CHECK(ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
    end(&transaction);
}

static void test_queue_race (void) {
    power_on();
    mock.busy_checks = 3;
    mock.race = true;
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint32_t output;
    TEST_CHECK(ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &output, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_CHECK(mock.enqueues == 1 && mock.busy_rechecks == 2);
    TEST_CHECK(output == mock.register_value);
    end(&transaction);
}

static void test_queue_timeouts (void) {
    for (unsigned mode = 0; mode < 3; mode++) {
        power_on();
        mock.now = UINT32_MAX - TICKS_FROM_MS(2);
        mock.tick_step = TICKS_FROM_MS(1);
        mock.force_busy = mode == 0;
        mock_pi_status = mode == 1 ? PI_STATUS_DMA_BUSY : mode == 2 ? PI_STATUS_IO_BUSY : 0;
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        uint32_t start = mock.now;
        uint32_t output = 42;
        TEST_CHECK(
            ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &output, deadline_ms(1000)) ==
            ED64_PRO_LL_PI_ACQUIRE_TIMEOUT
        );
        TEST_CHECK((uint32_t)(mock.now - start) <= TICKS_FROM_MS(12));
        TEST_CHECK(mock.enqueues == 0 && output == 42);
        TEST_CHECK(!ed64_pro_ll_get_failure()->poisoned);
        end(&transaction);
    }
}

static void test_completion_timeout (void) {
    power_on();
    mock.now = UINT32_MAX - TICKS_FROM_MS(2);
    mock.tick_step = TICKS_FROM_MS(1);
    mock.never_finish = true;
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint32_t start = mock.now;
    uint32_t output = 42;
    TEST_CHECK(
        ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &output, deadline_ms(1000)) == ED64_PRO_LL_PI_TIMEOUT
    );
    TEST_CHECK((uint32_t)(mock.now - start) <= TICKS_FROM_MS(12));
    TEST_CHECK(mock.enqueues == 1 && output == 42);
    TEST_CHECK(strcmp(mock.events, "BR") == 0);
    TEST_CHECK(ed64_pro_ll_get_failure()->poisoned && ed64_pro_ll_get_failure()->ticket == mock.ticket);
    end(&transaction);

    uint32_t staging_snapshot[ED64_PRO_FIFO_CHUNK];
    memcpy(staging_snapshot, pro_staging, sizeof(staging_snapshot));
    finish_pending();
    TEST_CHECK(output == 42 && memcmp(staging_snapshot, pro_staging, sizeof(staging_snapshot)) == 0);
    size_t events = mock.event_count;
    TEST_CHECK(ed64_pro_ll_begin(&transaction, deadline_ms(1000)) == ED64_PRO_LL_PI_TIMEOUT);
    TEST_CHECK(mock.event_count == events && mock.enqueues == 1);
}

static void test_exact_deadline (void) {
    power_on();
    mock.tick_step = 0;
    ed64_pro_ll_transaction_t transaction;
    TEST_ASSERT(ed64_pro_ll_begin(&transaction, mock.now) == ED64_PRO_LL_OK);
    uint32_t output = 42;
    TEST_CHECK(
        ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &output, deadline_ms(1000)) ==
        ED64_PRO_LL_PI_ACQUIRE_TIMEOUT
    );
    TEST_CHECK(mock.enqueues == 0 && output == 42);
    end(&transaction);
}

static void test_pi_errors (void) {
    for (unsigned issued = 0; issued < 2; issued++) {
        power_on();
        mock_pi_status = issued ? 0 : PI_STATUS_ERROR;
        mock.error_on_finish = issued;
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        uint32_t output = 42;
        TEST_CHECK(
            ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &output, deadline_ms(1000)) == ED64_PRO_LL_PI_ERROR
        );
        TEST_CHECK(mock.enqueues == issued && output == 42);
        TEST_CHECK(ed64_pro_ll_get_failure()->poisoned == (issued != 0));
        TEST_CHECK(strchr(mock.events, 'I') == NULL);
        end(&transaction);
        TEST_CHECK(ed64_pro_ll_begin(&transaction, deadline_ms(1000)) == ED64_PRO_LL_PI_ERROR);
        TEST_CHECK(mock.enqueues == issued && mock.irq_depth == 0);
    }
}

static uint32_t finish_deadline;

static void expire_on_completion (void) {
    mock.now = finish_deadline;
}

static void test_completion_at_deadline (void) {
    power_on();
    mock.tick_step = 0;
    finish_deadline = deadline_ms(2);
    mock.poll_hook = expire_on_completion;
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint32_t output = 42;
    TEST_CHECK(
        ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &output, finish_deadline) == ED64_PRO_LL_PI_TIMEOUT
    );
    TEST_CHECK(!mock.active && output == 42);
    TEST_CHECK(ed64_pro_ll_get_failure()->poisoned);
    TEST_CHECK(strcmp(mock.events, "BRF") == 0);
    end(&transaction);
}

static void error_during_dma (void) {
    mock_pi_status |= PI_STATUS_ERROR;
}

static void test_write_quarantine (void) {
    power_on();
    mock.never_finish = true;
    mock.poll_hook = error_during_dma;
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    TEST_CHECK(ed64_pro_ll_write_reg(&transaction, ED64_PRO_REG_MBX, 0, deadline_ms(1000)) == ED64_PRO_LL_PI_ERROR);
    TEST_CHECK(mock.active && ed64_pro_ll_get_failure()->poisoned);
    uint32_t snapshot[ED64_PRO_FIFO_CHUNK];
    memcpy(snapshot, pro_staging, sizeof(snapshot));
    TEST_CHECK(ed64_pro_ll_write_reg(&transaction, ED64_PRO_REG_MBX, 0, deadline_ms(1000)) == ED64_PRO_LL_PI_ERROR);
    TEST_CHECK(memcmp(snapshot, pro_staging, sizeof(snapshot)) == 0 && mock.enqueues == 1);
    end(&transaction);
    finish_pending();
    TEST_CHECK(memcmp(snapshot, pro_staging, sizeof(snapshot)) == 0);
    TEST_CHECK(ed64_pro_ll_begin(&transaction, deadline_ms(1000)) == ED64_PRO_LL_PI_ERROR);
}

static void test_invalid_context (void) {
    for (unsigned mode = 0; mode < 6; mode++) {
        power_on();
        mock.irq_depth = mode == 0;
        mock.cp0 = mode == 1   ? 0
                   : mode == 2 ? C0_STATUS_IE | C0_STATUS_EXL
                   : mode == 3 ? C0_STATUS_IE | C0_STATUS_ERL
                               : C0_STATUS_IE;
        mock.kernel = mode == 4;
        mock.uninitialized = mode == 5;
        unsigned depth = mock.irq_depth;
        ed64_pro_ll_transaction_t transaction;
        TEST_CHECK(ed64_pro_ll_begin(&transaction, deadline_ms(1000)) == ED64_PRO_LL_CONTEXT);
        TEST_CHECK(mock.irq_depth == depth && mock.enqueues == 0 && mock.event_count == 0);
        TEST_CHECK(pro_ll.owner == NULL && ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
    }
}

static void nested_entry (void) {
    ed64_pro_ll_transaction_t nested;
    ed64_pro_ll_transaction_t *owner = pro_ll.owner;
    TEST_CHECK(ed64_pro_ll_begin(&nested, deadline_ms(1000)) == ED64_PRO_LL_BUSY);
    TEST_CHECK(ed64_pro_ll_end(&nested) == ED64_PRO_LL_BUSY);
    mock.cp0 &= ~C0_STATUS_IE;
    TEST_CHECK(ed64_pro_ll_begin(&nested, deadline_ms(1000)) == ED64_PRO_LL_CONTEXT);
    mock.cp0 |= C0_STATUS_IE;
    TEST_CHECK(pro_ll.owner == owner && mock.irq_depth == 0);
    TEST_CHECK(ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
}

static void test_serialization (void) {
    power_on();
    mock.poll_hook = nested_entry;
    mock.finish_after = 3;
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint32_t output;
    TEST_CHECK(ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &output, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_CHECK(output == mock.register_value && mock.enqueues == 1);
    end(&transaction);
}

static struct {
    uint32_t before;
    uint32_t output;
    uint32_t after;
    uint32_t irq_updates;
} shared_line __attribute__((aligned(16)));

static void neighboring_interrupt (void) {
    TEST_CHECK(shared_line.output == 42);
    TEST_CHECK(shared_line.before == 0x11223344u && shared_line.after == 0x55667788u);
    shared_line.irq_updates++;
}

static void test_cache_sharing (void) {
    power_on();
    shared_line.before = 0x11223344u;
    shared_line.after = 0x55667788u;
    shared_line.output = 42;
    shared_line.irq_updates = 0;
    mock.poll_hook = neighboring_interrupt;
    mock.finish_after = 4;
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    TEST_CHECK(
        ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &shared_line.output, deadline_ms(1000)) == ED64_PRO_LL_OK
    );
    TEST_CHECK(shared_line.output == mock.register_value && shared_line.irq_updates == 5);
    TEST_CHECK(shared_line.before == 0x11223344u && shared_line.after == 0x55667788u);
    end(&transaction);
}

TEST_LIST = {
    {"exact register read and cache order", test_register_read},
    {"exact mailbox write and cache order", test_register_write},
    {"invalid input never submits DMA", test_invalid_input},
    {"queue acquisition and interrupt race", test_queue_race},
    {"queue and hardware busy deadlines wrap", test_queue_timeouts},
    {"DMA timeout quarantines late completion", test_completion_timeout},
    {"exact deadline is expired", test_exact_deadline},
    {"PI errors before and after submission", test_pi_errors},
    {"completion at exact deadline is not published", test_completion_at_deadline},
    {"in-flight write error keeps source quarantined", test_write_quarantine},
    {"unsupported contexts preserve IRQ state", test_invalid_context},
    {"nested transaction and ISR exclusion", test_serialization},
    {"caller cache-line interrupt sharing", test_cache_sharing},
    {NULL, NULL},
};
