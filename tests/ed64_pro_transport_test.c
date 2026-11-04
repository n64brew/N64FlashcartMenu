#include <libdragon.h>
#include <string.h>

#include <acutest.h>

/* Include the production unit to reset ROM-lifetime state between power-on fixtures.
 * No reset or alternate transport implementation is compiled into the ROM. */
#include "../src/flashcart/ed64_pro/ed64_pro.c"
#include "../src/flashcart/ed64_pro/ed64_pro_ll.c"

volatile uint32_t mock_pi_status;

typedef struct {
    uint32_t address;
    size_t length;
    bool write;
    uint32_t words[5];
} bus_record_t;

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
    char events[262144];
    size_t event_count;
    unsigned barriers;
    void (*poll_hook)(void);
    bus_record_t bus[32768];
    size_t bus_count;
    uint8_t rx[4096];
    size_t rx_head;
    size_t rx_count;
    size_t rx_consumed;
    bool refill;
    bool count_override;
    uint32_t reported_count;
    uint32_t fifo_upper;
    uint32_t mailbox;
    unsigned cart_writes;
    unsigned status_requests;
    unsigned stop_commands;
    unsigned sd_commands;
    struct {
        uint8_t bytes[8];
        size_t length;
    } replies[8];
    uint8_t pending_reply[8];
    size_t pending_length;
    size_t pending_offset;
    uint32_t reply_interval;
    uint32_t next_reply_tick;
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
    TEST_ASSERT(mock.bus_count < sizeof(mock.bus) / sizeof(mock.bus[0]));
    bus_record_t *record = &mock.bus[mock.bus_count++];
    record->address = pi;
    record->length = length;
    record->write = write;
    for (size_t i = 0; i < 5 && i < length / 4; i++) {
        record->words[i] = mock.ram[i];
    }
    if (write) {
        mock.cart_writes++;
    }
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

static void rx_push (uint8_t byte) {
    TEST_ASSERT(mock.rx_count < sizeof(mock.rx));
    mock.rx[(mock.rx_head + mock.rx_count) % sizeof(mock.rx)] = byte;
    mock.rx_count++;
}

static void release_reply (void) {
    while (mock.pending_offset < mock.pending_length && (int32_t)(mock.now - mock.next_reply_tick) >= 0) {
        rx_push(mock.pending_reply[mock.pending_offset++]);
        mock.next_reply_tick += mock.reply_interval;
    }
}

static void finish_pending (void) {
    TEST_ASSERT(mock.active);
    if (mock.write && mock.pi == 0x1F800000u) {
        TEST_ASSERT(mock.ram[0] == 0x2B && mock.ram[1] == 0xD4);
        TEST_ASSERT(mock.ram[3] == (mock.ram[2] ^ 0xFF));
        switch (mock.ram[2]) {
        case 0x10: {
            TEST_ASSERT(mock.length == 16);
            size_t index = mock.status_requests < 8 ? mock.status_requests : 7;
            mock.status_requests++;
            memcpy(mock.pending_reply, mock.replies[index].bytes, sizeof(mock.pending_reply));
            mock.pending_length = mock.replies[index].length;
            mock.pending_offset = 0;
            mock.next_reply_tick = mock.now + mock.reply_interval;
            release_reply();
            break;
        }
        case 0x85:
            TEST_ASSERT(mock.length == 20 && mock.ram[4] == 0x10);
            mock.stop_commands++;
            break;
        case 0x80:
            TEST_ASSERT(mock.length == 20 && mock.ram[4] == 0x10);
            mock.sd_commands++;
            break;
        default: TEST_ASSERT(false);
        }
    } else if (mock.write) {
        TEST_ASSERT(mock.pi == 0x1F800010u && mock.length == 4 && mock.ram[0] == 0);
        mock.mailbox = mock.ram[0];
    } else if (mock.pi == 0x1F800000u) {
        for (size_t i = 0; i < mock.length / 4; i++) {
            TEST_ASSERT(mock.rx_count > 0);
            mock.ram[i] = 0xC0FFEE00u | mock.rx[mock.rx_head];
            mock.rx_head = (mock.rx_head + 1) % sizeof(mock.rx);
            mock.rx_count--;
            mock.rx_consumed++;
            if (mock.refill) {
                rx_push(0xEE);
            }
        }
    } else {
        TEST_ASSERT(mock.length == 4);
        switch (mock.pi) {
        case 0x1F800004u:
            release_reply();
            mock.ram[0] = mock.fifo_upper | (mock.count_override ? mock.reported_count : mock.rx_count);
            break;
        case 0x1F800010u: mock.ram[0] = mock.mailbox; break;
        default: mock.ram[0] = mock.register_value; break;
        }
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
    mock.fifo_upper = 0xF00D0000u;
    for (size_t i = 0; i < 8; i++) {
        mock.replies[i].length = 4;
        mock.replies[i].bytes[0] = 0x5A;
        mock.replies[i].bytes[1] = 0x39;
        mock.replies[i].bytes[2] = 0xA7;
    }
}

static uint32_t deadline_ms (uint32_t ms) {
    return mock.now + TICKS_FROM_MS(ms);
}

static void begin (ed64_pro_ll_transaction_t *transaction) {
    TEST_ASSERT(ed64_pro_ll_begin(transaction, deadline_ms(15000)) == ED64_PRO_LL_OK);
}

static void identify (void) {
    ed64_pro_probe_t probe = ed64_pro_probe();
    TEST_ASSERT(probe.outcome == ED64_PRO_PROBE_IDENTIFIED && probe.id_valid && probe.error == ED64_PRO_LL_OK);
    mock.enqueues = 0;
    mock.bus_count = 0;
    mock.busy_rechecks = 0;
    mock.event_count = 0;
    mock.events[0] = '\0';
    mock.barriers = 0;
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
    identify();
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
    identify();
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
    size_t transferred = pro_ll.transferred;
    unsigned enqueues = mock.enqueues;
    ed64_pro_status_t status;
    memset(&status, 0xCD, sizeof(status));
    TEST_CHECK(ed64_pro_ll_begin(&nested, deadline_ms(1000)) == ED64_PRO_LL_BUSY);
    TEST_CHECK(ed64_pro_ll_end(&nested) == ED64_PRO_LL_BUSY);
    TEST_CHECK(ed64_pro_ll_read_status(&nested, &status, deadline_ms(1000)) == ED64_PRO_LL_BUSY);
    TEST_CHECK(ed64_pro_probe().error == ED64_PRO_LL_BUSY);
    mock.cp0 &= ~C0_STATUS_IE;
    TEST_CHECK(ed64_pro_ll_begin(&nested, deadline_ms(1000)) == ED64_PRO_LL_CONTEXT);
    TEST_CHECK(ed64_pro_ll_read_status(owner, &status, deadline_ms(1000)) == ED64_PRO_LL_CONTEXT);
    mock.cp0 |= C0_STATUS_IE;
    TEST_CHECK(pro_ll.owner == owner && mock.irq_depth == 0);
    TEST_CHECK(pro_ll.transferred == transferred && mock.enqueues == enqueues && status.key == 0xCD);
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

static void test_probe_identity (void) {
    const uint32_t ids[] = {
        0, UINT32_MAX, 0xABCD0027u, 0xED640026u, 0xED640000u, 0xED640027u, 0xED640127u, 0xED647F27u, 0xED64FF27u
    };
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        power_on();
        mock.register_value = ids[i];
        ed64_pro_probe_t result = ed64_pro_probe();
        bool matches = i >= 5;
        TEST_CHECK(result.outcome == (matches ? ED64_PRO_PROBE_IDENTIFIED : ED64_PRO_PROBE_ABSENT));
        TEST_CHECK(result.id_valid && result.id == ids[i] && result.error == ED64_PRO_LL_OK);
        TEST_CHECK(mock.bus_count == 1 && mock.bus[0].address == 0x1F800014u && mock.bus[0].length == 4);
        TEST_CHECK(mock.cart_writes == 0 && !ed64_pro_detected());
        TEST_CHECK(ed64_pro_probe().outcome == result.outcome && mock.bus_count == 1);
        if (!matches) {
            ed64_pro_ll_transaction_t transaction;
            begin(&transaction);
            ed64_pro_status_t status;
            TEST_CHECK(
                ed64_pro_ll_write_reg(&transaction, ED64_PRO_REG_MBX, 0, deadline_ms(1000)) ==
                ED64_PRO_LL_NOT_IDENTIFIED
            );
            TEST_CHECK(ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
            TEST_CHECK(mock.cart_writes == 0 && mock.bus_count == 1);
            end(&transaction);
        }
    }
}

static void test_probe_failures (void) {
    const ed64_pro_ll_error_t errors[] = {
        ED64_PRO_LL_PI_ACQUIRE_TIMEOUT,
        ED64_PRO_LL_PI_TIMEOUT,
        ED64_PRO_LL_PI_ERROR,
    };
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        power_on();
        mock.tick_step = TICKS_FROM_MS(1);
        mock.force_busy = i == 0;
        mock.never_finish = i == 1;
        mock_pi_status = i == 2 ? PI_STATUS_ERROR : 0;
        ed64_pro_probe_t result = ed64_pro_probe();
        TEST_CHECK(result.outcome == ED64_PRO_PROBE_IO_ERROR && result.error == errors[i] && !result.id_valid);
        TEST_CHECK(mock.cart_writes == 0 && !ed64_pro_detected());
        unsigned enqueues = mock.enqueues;
        size_t events = mock.event_count;
        TEST_CHECK(ed64_pro_probe().error == errors[i]);
        TEST_CHECK(mock.enqueues == enqueues && mock.event_count == events && mock.irq_depth == 0);
    }
}

static void test_unidentified_commands (void) {
    power_on();
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    ed64_pro_status_t status;
    size_t drained;
    TEST_CHECK(pro_send_command(&transaction, PRO_CMD_STATUS, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(pro_send_command(&transaction, PRO_CMD_STOP, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(pro_send_command(&transaction, PRO_CMD_SD_INIT, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(ed64_pro_ll_drain(&transaction, &drained, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(mock.enqueues == 0 && mock.cart_writes == 0);
    TEST_CHECK(ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
    end(&transaction);
}

static void test_command_words (void) {
    static const uint32_t frames[][5] = {
        {0x2B, 0xD4, 0x10, 0xEF, 0},
        {0x2B, 0xD4, 0x85, 0x7A, 0x10},
        {0x2B, 0xD4, 0x80, 0x7F, 0x10},
    };
    const pro_command_t commands[] = {PRO_CMD_STATUS, PRO_CMD_STOP, PRO_CMD_SD_INIT};
    for (size_t i = 0; i < 3; i++) {
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        TEST_CHECK(pro_send_command(&transaction, commands[i], deadline_ms(1000)) == ED64_PRO_LL_OK);
        size_t length = i == 0 ? 16 : 20;
        TEST_CHECK(mock.bus_count == 1 && mock.bus[0].write);
        TEST_CHECK(mock.bus[0].address == 0x1F800000u && mock.bus[0].length == length);
        TEST_CHECK(memcmp(mock.bus[0].words, frames[i], length) == 0);
        TEST_CHECK(mock.cache_length == (i == 0 ? 16 : 32));
        TEST_CHECK(pro_send_command(&transaction, (pro_command_t)0xFF, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
        TEST_CHECK(mock.bus_count == 1);
        end(&transaction);
    }
}

static void test_status_frame (void) {
    const uint8_t statuses[] = {0, 0xB0, 0xFF};
    for (size_t i = 0; i < sizeof(statuses); i++) {
        power_on();
        identify();
        mock.replies[0].bytes[1] = (uint8_t)i;
        mock.replies[0].bytes[2] = (uint8_t)(0xFF - i);
        mock.replies[0].bytes[3] = statuses[i];
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        ed64_pro_status_t status;
        TEST_CHECK(ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000)) == ED64_PRO_LL_OK);
        TEST_CHECK(status.key == 0x5A && status.opaque[0] == i && status.opaque[1] == 0xFF - i);
        TEST_CHECK(status.mcu_status == statuses[i] && mock.rx_consumed == 4 && mock.rx_count == 0);
        TEST_CHECK(mock.bus_count == 5 && mock.status_requests == 1);
        const uint32_t addresses[] = {0x1F800004u, 0x1F800000u, 0x1F800004u, 0x1F800000u, 0x1F800004u};
        const size_t lengths[] = {4, 16, 4, 16, 4};
        for (size_t n = 0; n < 5; n++) {
            TEST_CHECK(mock.bus[n].address == addresses[n] && mock.bus[n].length == lengths[n]);
            TEST_CHECK(mock.bus[n].write == (n == 1));
        }
        TEST_CHECK(ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
        end(&transaction);
    }
}

static void test_fifo_boundaries (void) {
    const size_t lengths[] = {0, 1, 2, 3, 4, 5, 15, 16, 17, 127, 128, 129, 2047, 2048};
    for (size_t offset = 0; offset < 32; offset++) {
        for (size_t n = 0; n < sizeof(lengths) / sizeof(lengths[0]); n++) {
            power_on();
            identify();
            size_t length = lengths[n];
            for (size_t i = 0; i < length; i++) {
                rx_push((uint8_t)(i * 37 + 19));
            }
            uint8_t output[2112];
            memset(output, 0xCD, sizeof(output));
            ed64_pro_ll_transaction_t transaction;
            begin(&transaction);
            TEST_CHECK(pro_fifo_read(&transaction, output + offset, length, deadline_ms(1000)) == ED64_PRO_LL_OK);
            for (size_t i = 0; i < sizeof(output); i++) {
                uint8_t expected = i >= offset && i - offset < length ? (uint8_t)((i - offset) * 37 + 19) : 0xCD;
                TEST_CHECK(output[i] == expected);
            }
            TEST_CHECK(mock.rx_count == 0 && mock.rx_consumed == length && mock.cart_writes == 0);
            size_t chunks = (length + 127) / 128;
            TEST_CHECK(mock.bus_count == chunks * 2);
            for (size_t i = 0; i < chunks; i++) {
                size_t chunk = length - i * 128 < 128 ? length - i * 128 : 128;
                TEST_CHECK(mock.bus[i * 2].address == 0x1F800004u && mock.bus[i * 2].length == 4);
                TEST_CHECK(mock.bus[i * 2 + 1].address == 0x1F800000u && mock.bus[i * 2 + 1].length == chunk * 4);
            }
            end(&transaction);
        }
    }
}

static void test_fifo_invalid_lengths (void) {
    power_on();
    identify();
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint8_t output = 42;
    TEST_CHECK(pro_fifo_read(&transaction, NULL, 0, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_CHECK(pro_fifo_read(&transaction, NULL, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(pro_fifo_read(&transaction, &output, 2049, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(pro_fifo_read(&transaction, &output, SIZE_MAX / 4 + 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(pro_fifo_read(&transaction, &output, SIZE_MAX, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_read_status(&transaction, NULL, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_drain(&transaction, NULL, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.enqueues == 0 && output == 42);
    end(&transaction);
}

static void test_fifo_drains (void) {
    const size_t lengths[] = {0, 1, 128, 129, 2048};
    for (size_t n = 0; n < sizeof(lengths) / sizeof(lengths[0]); n++) {
        power_on();
        identify();
        for (size_t i = 0; i < lengths[n]; i++) {
            rx_push((uint8_t)i);
        }
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        size_t drained = SIZE_MAX;
        TEST_CHECK(ed64_pro_ll_drain(&transaction, &drained, deadline_ms(1000)) == ED64_PRO_LL_OK);
        TEST_CHECK(drained == lengths[n] && mock.rx_count == 0 && mock.rx_consumed == lengths[n]);
        TEST_CHECK(mock.cart_writes == 0);
        end(&transaction);
    }
}

static void test_fifo_count_and_refill (void) {
    for (unsigned mode = 0; mode < 3; mode++) {
        power_on();
        identify();
        mock.count_override = mode != 2;
        mock.reported_count = mode == 0 ? 2049 : 65535;
        mock.refill = mode == 2;
        if (mock.refill) {
            for (size_t i = 0; i < 128; i++) {
                rx_push(0xEE);
            }
        }
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        size_t drained = SIZE_MAX;
        ed64_pro_ll_error_t expected = mode == 2 ? ED64_PRO_LL_DRAIN_LIMIT : ED64_PRO_LL_FIFO_COUNT;
        TEST_CHECK(ed64_pro_ll_drain(&transaction, &drained, deadline_ms(1000)) == expected);
        TEST_CHECK(mock.rx_consumed == (mode == 2 ? 2048 : 0));
        TEST_CHECK(ed64_pro_ll_get_failure()->transferred == mock.rx_consumed && drained == SIZE_MAX);
        TEST_CHECK(mock.cart_writes == 0);
        end(&transaction);
        unsigned enqueues = mock.enqueues;
        TEST_CHECK(ed64_pro_ll_begin(&transaction, deadline_ms(1000)) == expected);
        TEST_CHECK(mock.enqueues == enqueues);
    }
}

static bool is_deadline_error (ed64_pro_ll_error_t error) {
    return error == ED64_PRO_LL_TIMEOUT || error == ED64_PRO_LL_PI_ACQUIRE_TIMEOUT || error == ED64_PRO_LL_PI_TIMEOUT;
}

static void test_drain_deadline (void) {
    power_on();
    identify();
    mock.tick_step = TICKS_FROM_MS(1) / 8;
    mock.refill = true;
    rx_push(0xEE);
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint32_t start = mock.now;
    size_t drained = SIZE_MAX;
    TEST_CHECK(is_deadline_error(ed64_pro_ll_drain(&transaction, &drained, deadline_ms(1000))));
    TEST_CHECK((uint32_t)(mock.now - start) <= TICKS_FROM_MS(101));
    TEST_CHECK(mock.rx_consumed > 0 && mock.rx_consumed < 2048 && drained == SIZE_MAX);
    end(&transaction);
}

static void test_partial_status (void) {
    for (size_t length = 0; length < 4; length++) {
        power_on();
        identify();
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        mock.replies[0].length = length;
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        ed64_pro_status_t status;
        memset(&status, 0xCD, sizeof(status));
        uint32_t start = mock.now;
        TEST_CHECK(is_deadline_error(ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000))));
        TEST_CHECK((uint32_t)(mock.now - start) <= TICKS_FROM_MS(1001));
        TEST_CHECK(status.key == 0xCD && status.mcu_status == 0xCD);
        TEST_CHECK(ed64_pro_ll_get_failure()->transferred == length && mock.rx_consumed == length);
        TEST_CHECK(mock.status_requests == 1);
        end(&transaction);
    }
}

static void test_bad_status (void) {
    for (unsigned mode = 0; mode < 3; mode++) {
        power_on();
        identify();
        if (mode == 0) {
            mock.replies[0].bytes[0] = 0x5B;
        } else if (mode == 1) {
            mock.replies[0].length = 5;
            mock.replies[0].bytes[4] = 0x89;
        } else {
            rx_push(0x12);
        }
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        ed64_pro_status_t status;
        memset(&status, 0xCD, sizeof(status));
        ed64_pro_ll_error_t expected = mode == 0 ? ED64_PRO_LL_FRAME : ED64_PRO_LL_TRAILING_DATA;
        TEST_CHECK(ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000)) == expected);
        TEST_CHECK(status.key == 0xCD && status.mcu_status == 0xCD);
        TEST_CHECK(mock.rx_consumed == (mode == 2 ? 0 : 4) && mock.cart_writes == (mode == 2 ? 0 : 1));
        unsigned enqueues = mock.enqueues;
        size_t drained = SIZE_MAX;
        TEST_CHECK(ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000)) == expected);
        TEST_CHECK(ed64_pro_ll_drain(&transaction, &drained, deadline_ms(1000)) == expected);
        TEST_CHECK(mock.enqueues == enqueues && drained == SIZE_MAX);
        end(&transaction);
    }
}

static void test_slow_partial_status (void) {
    const uint32_t budgets[] = {1000, 600};
    for (size_t i = 0; i < 2; i++) {
        power_on();
        identify();
        mock.now = UINT32_MAX - TICKS_FROM_MS(250);
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        mock.reply_interval = TICKS_FROM_MS(400);
        ed64_pro_ll_transaction_t transaction;
        TEST_ASSERT(ed64_pro_ll_begin(&transaction, deadline_ms(budgets[i])) == ED64_PRO_LL_OK);
        ed64_pro_status_t status;
        memset(&status, 0xCD, sizeof(status));
        uint32_t start = mock.now;
        TEST_CHECK(is_deadline_error(ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000))));
        TEST_CHECK((uint32_t)(mock.now - start) <= TICKS_FROM_MS(budgets[i] + 1));
        TEST_CHECK(ed64_pro_ll_get_failure()->transferred == (i == 0 ? 2 : 1));
        TEST_CHECK(status.key == 0xCD && mock.status_requests == 1);
        end(&transaction);
    }
}

static void fail_second_chunk (void) {
    if (mock.pi == 0x1F800000u && !mock.write && mock.rx_consumed == 128) {
        mock.never_finish = true;
        mock_pi_status |= PI_STATUS_ERROR;
    }
}

static void test_partial_dma_failure (void) {
    power_on();
    identify();
    for (size_t i = 0; i < 129; i++) {
        rx_push((uint8_t)i);
    }
    mock.poll_hook = fail_second_chunk;
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint8_t output[129];
    memset(output, 0xCD, sizeof(output));
    TEST_CHECK(pro_fifo_read(&transaction, output, sizeof(output), deadline_ms(1000)) == ED64_PRO_LL_PI_ERROR);
    TEST_CHECK(ed64_pro_ll_get_failure()->transferred == 128 && ed64_pro_ll_get_failure()->poisoned);
    TEST_CHECK(mock.active && mock.rx_consumed == 128 && output[128] == 0xCD);
    end(&transaction);
    finish_pending();
    TEST_CHECK(output[128] == 0xCD && mock.rx_consumed == 129);
    unsigned enqueues = mock.enqueues;
    TEST_CHECK(ed64_pro_ll_begin(&transaction, deadline_ms(1000)) == ED64_PRO_LL_PI_ERROR);
    TEST_CHECK(mock.enqueues == enqueues);
}

static void test_status_serialization (void) {
    power_on();
    identify();
    mock.poll_hook = nested_entry;
    mock.finish_after = 2;
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    ed64_pro_status_t status;
    TEST_CHECK(ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_CHECK(status.key == 0x5A && mock.status_requests == 1 && mock.cart_writes == 1 && mock.bus_count == 5);
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
    {"Pro family and device identity fixtures", test_probe_identity},
    {"failed probe is indeterminate and sticky", test_probe_failures},
    {"commands require completed Pro identification", test_unidentified_commands},
    {"literal selected command word traces", test_command_words},
    {"status frame and opaque bytes", test_status_frame},
    {"FIFO exact lengths and caller offsets 0-31", test_fifo_boundaries},
    {"FIFO zero and overflow lengths", test_fifo_invalid_lengths},
    {"bounded handoff drain boundaries", test_fifo_drains},
    {"impossible count and continuous refill", test_fifo_count_and_refill},
    {"drain time budget does not reset", test_drain_deadline},
    {"partial status is never published", test_partial_status},
    {"malformed and trailing status are sticky", test_bad_status},
    {"slow partial progress preserves original deadlines", test_slow_partial_status},
    {"partial FIFO DMA failure preserves count", test_partial_dma_failure},
    {"command and reply cannot interleave", test_status_serialization},
    {NULL, NULL},
};
