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
    uint32_t words[ED64_PRO_FIFO_CHUNK];
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
    uint32_t reply_intervals[8];
    uint32_t active_reply_interval;
    uint32_t next_reply_tick;
    unsigned fail_at;
    bool mailbox_stalled;
    bool mailbox_unstable;
    unsigned mailbox_reads;
    uint32_t mailbox_samples[16];
    size_t mailbox_sample_count;
    uint32_t stop_delay;
    uint32_t ready_tick;
    bool raw_frames;
    uint8_t tx_bytes[131072];
    size_t tx_count;
    uint32_t info_values[2];
    size_t info_reply_limit;
    bool info_trailing;
    unsigned info_requests;
    unsigned nresp_requests;
    uint8_t nresp_detail;
    bool nresp_missing;
    bool nresp_trailing;
    uint8_t sectors[256 * 512];
    uint8_t fci[512 * 512];
    unsigned fci_transfers;
    uint32_t fci_address;
    uint16_t flags;
    unsigned flag_commands;
    bool sysstat_busy;
    bool sysstat_unstable;
    unsigned sysstat_reads;
    bool flags_trailing;
    uint8_t dev_path_frame[ED64_PRO_FRAME_MAX];
    size_t dev_path_expected;
    size_t dev_path_received;
    char dev_paths[2][ED64_PRO_PATH_MAX + 1];
    unsigned path_commands;
    uint8_t config_bytes[64];
    unsigned config_commands;
    unsigned start_commands;
    char dev_steps[16];
    size_t dev_step_count;
    bool wre;
    bool wre_reply;
    unsigned wre_commands;
    bool transfer_active;
    bool transfer_write;
    uint32_t transfer_lba;
    size_t transfer_length;
    size_t transfer_done;
    unsigned read_acks;
    unsigned write_acks;
    unsigned stall_read_ack;
    unsigned stall_write_ack;
    bool data_trailing;
    size_t post_stop_junk;
    bool post_stop_refill;
} mock;

/**
 * @brief Append an event marker to the mock bus trace.
 */
static void event (char value) {
    TEST_ASSERT(mock.event_count + 1 < sizeof(mock.events));
    mock.events[mock.event_count++] = value;
    mock.events[mock.event_count] = '\0';
}

/**
 * @brief Model CP0 interrupt enable from the nested mock IRQ-mask state.
 */
uint32_t mock_cp0_status (void) {
    return mock.irq_depth ? mock.cp0 & ~C0_STATUS_IE : mock.cp0;
}

/**
 * @brief Return the mock clock and advance it by the configured tick step.
 */
uint32_t mock_ticks_read (void) {
    uint32_t now = mock.now;
    mock.now += mock.tick_step;
    return now;
}

/**
 * @brief Count memory barriers issued by the transport.
 */
void mock_memory_barrier (void) {
    mock.barriers++;
}

/**
 * @brief Enter IRQ exclusion and inject a queue arrival at the armed idle-to-lock race.
 */
void disable_interrupts (void) {
    if (mock.race_armed && mock.irq_depth == 0) {
        mock.race_armed = false;
        mock.busy_checks = 2;
    }
    mock.irq_depth++;
}

/**
 * @brief Release one level of mock interrupt exclusion.
 */
void enable_interrupts (void) {
    TEST_ASSERT(mock.irq_depth > 0);
    mock.irq_depth--;
}

/**
 * @brief Report uninitialized or nested IRQ state for foreground-context checks.
 */
interrupt_state_t get_interrupts_state (void) {
    if (mock.uninitialized) {
        return INTERRUPTS_UNINITIALIZED;
    }
    return mock.irq_depth ? INTERRUPTS_DISABLED : INTERRUPTS_ENABLED;
}

/**
 * @brief Expose the kernel-context fixture without starting a real kernel thread.
 */
kthread_t *kthread_current (void) {
    return mock.kernel ? (kthread_t *)&mock : NULL;
}

/**
 * @brief Model software/hardware PI busy states and arm the idle-check race injection.
 */
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

/**
 * @brief Assert isolated staging and exact bus lengths, then record an IRQ-protected DMA submission.
 *
 * Fail immediately if the modeled SDK queue would block. Device effects are
 * deferred until completion rather than applied during submission.
 */
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
    for (size_t i = 0; i < ED64_PRO_FIFO_CHUNK && i < length / 4; i++) {
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

/**
 * @brief Submit a modeled cartridge-to-RAM DMA read.
 */
uint64_t dma_read_raw_async (void *ram, pi_addr_t pi, unsigned long length) {
    return submit(ram, pi, length, false);
}

/**
 * @brief Submit a modeled RAM-to-cartridge DMA write.
 */
uint64_t dma_write_raw_async (const void *ram, pi_addr_t pi, unsigned long length) {
    return submit(ram, pi, length, true);
}

/**
 * @brief Append one byte to the modeled MCU receive FIFO.
 */
static void rx_push (uint8_t byte) {
    TEST_ASSERT(mock.rx_count < sizeof(mock.rx));
    mock.rx[(mock.rx_head + mock.rx_count) % sizeof(mock.rx)] = byte;
    mock.rx_count++;
}

/**
 * @brief Release scheduled MCU bytes only when mock absolute ticks reach each byte's deadline.
 */
static void release_reply (void) {
    while (mock.pending_offset < mock.pending_length && (int32_t)(mock.now - mock.next_reply_tick) >= 0) {
        rx_push(mock.pending_reply[mock.pending_offset++]);
        mock.next_reply_tick += mock.active_reply_interval;
    }
}

/**
 * @brief Validate and record a device path frame and its launch-order marker.
 */
static void record_dev_path (const uint8_t *frame, size_t length) {
    TEST_ASSERT(
        length >= 9 && frame[0] == 0x2B && frame[1] == 0xD4 && frame[2] == 0x85 && frame[3] == 0x7A &&
        frame[4] == 0x21 && (frame[5] == 0 || frame[5] == 2)
    );
    size_t path_length = (size_t)frame[6] << 8 | frame[7];
    TEST_ASSERT(path_length <= ED64_PRO_PATH_MAX && length == path_length + 8);
    size_t slot = frame[5] == 0 ? 0 : 1;
    memcpy(mock.dev_paths[slot], frame + 8, path_length);
    mock.dev_paths[slot][path_length] = '\0';
    mock.path_commands++;
    TEST_ASSERT(mock.dev_step_count < sizeof(mock.dev_steps));
    mock.dev_steps[mock.dev_step_count++] = slot == 0 ? 'G' : 'B';
}

/**
 * @brief Complete the pending DMA with literal FIFO/mailbox protocol and RDRAM-side effects.
 *
 * Schedule status replies and post-stop cleanup without directly publishing
 * read data into CPU staging; cache invalidation remains a separate operation.
 */
static void finish_pending (void) {
    TEST_ASSERT(mock.active);
    if (mock.write && mock.pi == 0x1F800000u) {
        size_t bytes = mock.length / 4;
        TEST_ASSERT(mock.tx_count + bytes <= sizeof(mock.tx_bytes));
        for (size_t i = 0; i < bytes; i++) {
            mock.tx_bytes[mock.tx_count++] = mock.ram[i];
        }
        if (mock.raw_frames) {
            /* Framing-only fixtures deliberately bypass the simulated MCU. */
        } else if (mock.dev_path_expected) {
            TEST_ASSERT(bytes <= mock.dev_path_expected - mock.dev_path_received);
            for (size_t i = 0; i < bytes; i++) {
                mock.dev_path_frame[mock.dev_path_received++] = mock.ram[i];
            }
            if (mock.dev_path_received == mock.dev_path_expected) {
                record_dev_path(mock.dev_path_frame, mock.dev_path_expected);
                mock.dev_path_expected = 0;
                mock.dev_path_received = 0;
            }
        } else if (bytes >= 8 && mock.ram[0] == 0x2B && mock.ram[2] == 0x85 && mock.ram[4] == 0x21) {
            size_t expected = (size_t)mock.ram[6] * 256 + mock.ram[7] + 8;
            TEST_ASSERT(expected <= sizeof(mock.dev_path_frame) && bytes <= expected);
            for (size_t i = 0; i < bytes; i++) {
                mock.dev_path_frame[i] = mock.ram[i];
            }
            if (bytes < expected) {
                mock.dev_path_expected = expected;
                mock.dev_path_received = bytes;
            } else {
                record_dev_path(mock.dev_path_frame, expected);
            }
        } else if (mock.transfer_active && mock.transfer_write) {
            TEST_ASSERT(bytes <= mock.transfer_length - mock.transfer_done);
            TEST_ASSERT(mock.transfer_lba * 512 + mock.transfer_done + bytes <= sizeof(mock.sectors));
            for (size_t i = 0; i < bytes; i++) {
                mock.sectors[mock.transfer_lba * 512 + mock.transfer_done++] = mock.ram[i];
            }
            if (mock.transfer_done == mock.transfer_length) {
                mock.transfer_active = false;
                if (mock.data_trailing) {
                    rx_push(0xEF);
                }
            } else if (mock.transfer_done % ED64_PRO_ACK_BLOCK == 0) {
                mock.write_acks++;
                if (mock.write_acks != mock.stall_write_ack) {
                    rx_push(0x00);
                }
            }
        } else if (mock.transfer_active) {
            TEST_ASSERT(bytes == 1 && mock.ram[0] == 0);
            mock.read_acks++;
            size_t block = mock.transfer_length - mock.transfer_done;
            if (block > ED64_PRO_ACK_BLOCK) {
                block = ED64_PRO_ACK_BLOCK;
            }
            TEST_ASSERT(mock.transfer_lba * 512 + mock.transfer_done + block <= sizeof(mock.sectors));
            if (mock.read_acks != mock.stall_read_ack) {
                for (size_t i = 0; i < block; i++) {
                    rx_push(mock.sectors[mock.transfer_lba * 512 + mock.transfer_done + i]);
                }
            }
            mock.transfer_done += block;
            if (mock.transfer_done == mock.transfer_length) {
                mock.transfer_active = false;
                if (mock.data_trailing) {
                    rx_push(0xEF);
                }
            }
        } else {
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
                mock.active_reply_interval =
                    mock.reply_intervals[index] ? mock.reply_intervals[index] : mock.reply_interval;
                mock.next_reply_tick = mock.now + mock.active_reply_interval;
                release_reply();
                break;
            }
            case 0x13:
                TEST_ASSERT(bytes == 5 && mock.ram[4] != 0);
                mock.nresp_requests++;
                if (!mock.nresp_missing) {
                    rx_push(mock.nresp_detail);
                }
                if (mock.nresp_trailing) {
                    rx_push(0xEE);
                }
                break;
            case 0x84: {
                TEST_ASSERT(mock.ram[4] == 0x10 && mock.ram[5] >= 1 && mock.ram[5] <= 2);
                size_t count = mock.ram[5];
                TEST_ASSERT(bytes == 6 + count * 4);
                mock.info_requests++;
                for (size_t i = 0; i < count; i++) {
                    uint32_t id = mock.ram[6 + i * 4] << 24 | mock.ram[7 + i * 4] << 16 | mock.ram[8 + i * 4] << 8 |
                                  mock.ram[9 + i * 4];
                    TEST_ASSERT(id == ED64_PRO_INFS_TS_FW || id == ED64_PRO_INFS_MAX_ROM_SIZE);
                    uint32_t value = mock.info_values[id == ED64_PRO_INFS_TS_FW ? 0 : 1];
                    for (size_t j = 0; j < 4 && i * 4 + j < mock.info_reply_limit; j++) {
                        rx_push(value >> (24 - j * 8));
                    }
                }
                if (mock.info_trailing) {
                    rx_push(0xEE);
                }
                break;
            }
            case 0x81:
                if (mock.ram[4] == 0x11) {
                    TEST_ASSERT(
                        bytes == 9 && mock.ram[5] == 0xDE && mock.ram[6] == 0xAD && mock.ram[7] == 0xBE &&
                        mock.ram[8] == 0xEF
                    );
                    mock.wre_commands++;
                    mock.wre = true;
                    if (mock.wre_reply) {
                        rx_push(0xDE);
                        rx_push(0xAD);
                        rx_push(0xBE);
                        rx_push(0xEF);
                    }
                } else {
                    TEST_ASSERT(
                        bytes == 22 && mock.ram[4] == 0x10 && mock.ram[19] == 0 && mock.ram[20] == 0 &&
                        mock.ram[21] == 0
                    );
                    mock.transfer_write = mock.ram[17] == 0x11;
                    TEST_ASSERT(mock.ram[17] == (mock.transfer_write ? 0x11 : 0x19));
                    TEST_ASSERT(mock.ram[18] == (mock.transfer_write ? 0x19 : 0x11) || mock.ram[18] == 0x13);
                    size_t address_offset = mock.transfer_write ? 9 : 5;
                    mock.transfer_lba = mock.ram[address_offset] << 24 | mock.ram[address_offset + 1] << 16 |
                                        mock.ram[address_offset + 2] << 8 | mock.ram[address_offset + 3];
                    mock.transfer_length = mock.ram[13] << 24 | mock.ram[14] << 16 | mock.ram[15] << 8 | mock.ram[16];
                    mock.transfer_done = 0;
                    mock.transfer_active = true;
                    if (mock.ram[18] == 0x13) {
                        mock.fci_address = mock.ram[9] << 24 | mock.ram[10] << 16 | mock.ram[11] << 8 | mock.ram[12];
                        mock.fci_transfers++;
                        TEST_ASSERT(mock.fci_address + mock.transfer_length <= sizeof(mock.fci));
                        TEST_ASSERT(mock.transfer_lba * 512 + mock.transfer_length <= sizeof(mock.sectors));
                        for (size_t i = 0; i < mock.transfer_length; i++) {
                            size_t source = mock.transfer_lba * 512 + (mock.flags & 1 ? i ^ 1 : i);
                            mock.fci[mock.fci_address + i] = mock.sectors[source];
                        }
                        mock.transfer_done = mock.transfer_length;
                        mock.transfer_active = false;
                    } else if (mock.transfer_write) {
                        TEST_ASSERT(mock.wre);
                        mock.wre = false;
                        mock.write_acks++;
                        if (mock.write_acks != mock.stall_write_ack) {
                            rx_push(0x00);
                        }
                    }
                }
                break;
            case 0x85:
                if (mock.ram[4] == 0x20) {
                    TEST_ASSERT(bytes == 69);
                    for (size_t i = 0; i < sizeof(mock.config_bytes); i++) {
                        mock.config_bytes[i] = mock.ram[i + 5];
                    }
                    mock.config_commands++;
                    TEST_ASSERT(mock.dev_step_count < sizeof(mock.dev_steps));
                    mock.dev_steps[mock.dev_step_count++] = 'C';
                    break;
                }
                if (mock.ram[4] == 0x11) {
                    TEST_ASSERT(bytes == 5);
                    mock.start_commands++;
                    TEST_ASSERT(mock.dev_step_count < sizeof(mock.dev_steps));
                    mock.dev_steps[mock.dev_step_count++] = 'S';
                    break;
                }
                if (mock.ram[4] == 0x28) {
                    TEST_ASSERT(bytes == 9);
                    uint16_t mask = mock.ram[5] << 8 | mock.ram[6];
                    uint16_t value = mock.ram[7] << 8 | mock.ram[8];
                    TEST_ASSERT(mask == 1 && (value & ~mask) == 0);
                    mock.flags = (mock.flags & ~mask) | value;
                    mock.flag_commands++;
                    if (mock.flags_trailing) {
                        rx_push(0xEF);
                    }
                    break;
                }
                TEST_ASSERT(mock.length == 20 && mock.ram[4] == 0x10);
                mock.stop_commands++;
                mock.ready_tick = mock.now + mock.stop_delay;
                for (size_t i = 0; i < mock.post_stop_junk; i++) {
                    rx_push((uint8_t)(0x80 + i));
                }
                mock.refill = mock.post_stop_refill;
                break;
            case 0x80:
                TEST_ASSERT(mock.length == 20 && mock.ram[4] == 0x10);
                mock.sd_commands++;
                break;
            default: TEST_ASSERT(false);
            }
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
        case 0x1F800008u:
            mock.ram[0] = mock.sysstat_busy ? 1 : 0;
            if (mock.sysstat_unstable && (mock.sysstat_reads & 1)) {
                mock.ram[0] ^= 0x10;
            }
            mock.ram[0] |= (mock.sysstat_reads++ & 1) << 3;
            break;
        case 0x1F800010u:
            if (mock.stop_commands && !mock.mailbox_stalled && (int32_t)(mock.now - mock.ready_tick) >= 0) {
                mock.mailbox = 0x52445921u;
            }
            mock.ram[0] = mock.mailbox;
            if (mock.mailbox_sample_count) {
                size_t index =
                    mock.mailbox_reads < mock.mailbox_sample_count ? mock.mailbox_reads : mock.mailbox_sample_count - 1;
                mock.ram[0] = mock.mailbox_samples[index];
            } else if (mock.mailbox_unstable && (mock.mailbox_reads & 1u)) {
                mock.ram[0] ^= 1u;
            }
            mock.mailbox_reads++;
            break;
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

/**
 * @brief Poll the issued ticket with interrupts enabled while injecting hooks and completion delays.
 */
bool dma_finished (uint64_t ticket) {
    TEST_ASSERT(ticket == mock.ticket && ticket != 0);
    TEST_ASSERT(mock.irq_depth == 0);
    if (mock.poll_hook) {
        mock.poll_hook();
    }
    if (mock.active && !mock.never_finish && mock.enqueues != mock.fail_at && mock.polls++ >= mock.finish_after) {
        finish_pending();
    }
    return !mock.active;
}

/**
 * @brief Enforce staging alignment, exclusive cache-line ownership, and no active DMA during cache work.
 */
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

/**
 * @brief Write back mock staging and invalidate its CPU copy before a DMA read.
 */
void data_cache_hit_writeback_invalidate (volatile void *ram, unsigned long length) {
    check_cache(ram, length);
    memcpy(mock.ram, (const void *)ram, length);
    /* CPU and RDRAM are deliberately different: completion alone cannot publish data. */
    memset((void *)ram, 0xA5, length);
    event('B');
}

/**
 * @brief Copy CPU staging into modeled RDRAM before a DMA write.
 */
void data_cache_hit_writeback (const volatile void *ram, unsigned long length) {
    check_cache(ram, length);
    memcpy(mock.ram, (const void *)ram, length);
    event('C');
}

/**
 * @brief Publish modeled RDRAM into CPU staging after a DMA read.
 */
void data_cache_hit_invalidate (volatile void *ram, unsigned long length) {
    check_cache(ram, length);
    memcpy((void *)ram, mock.ram, length);
    event('I');
}

/**
 * @brief Reset the SDK model and ROM-lifetime driver state with well-formed default replies.
 */
static void power_on (void) {
    memset(&mock, 0, sizeof(mock));
    memset(&pro_ll, 0, sizeof(pro_ll));
    memset(&pro_diagnostics, 0, sizeof(pro_diagnostics));
    memset(pro_error_message, 0, sizeof(pro_error_message));
    pro_attempt_start = 0;
    pro_attempt_deadline = 0;
    pro_phase_start = 0;
    memset(pro_staging, 0, sizeof(pro_staging));
    mock_pi_status = 0;
    mock.cp0 = C0_STATUS_IE;
    mock.tick_step = 1;
    mock.register_value = 0xED640127u;
    mock.fifo_upper = 0xF00D0000u;
    mock.info_values[0] = 0x90555D4Eu;
    mock.info_values[1] = 0x04000000u;
    mock.info_reply_limit = 8;
    mock.nresp_detail = 0xA3;
    for (size_t i = 0; i < sizeof(mock.sectors); i++) {
        mock.sectors[i] = (uint8_t)(i * 37 + i / 512);
    }
    for (size_t i = 0; i < 8; i++) {
        mock.replies[i].length = 4;
        mock.replies[i].bytes[0] = 0x5A;
        mock.replies[i].bytes[1] = 0x39;
        mock.replies[i].bytes[2] = 0xA7;
    }
}

/**
 * @brief Compute an absolute deadline from the mock clock.
 */
static uint32_t deadline_ms (uint32_t ms) {
    return mock.now + TICKS_FROM_MS(ms);
}

/**
 * @brief Acquire a transport transaction with a 15-second test deadline.
 */
static void begin (ed64_pro_ll_transaction_t *transaction) {
    TEST_ASSERT(ed64_pro_ll_begin(transaction, deadline_ms(15000)) == ED64_PRO_LL_OK);
}

/**
 * @brief Complete passive identification and reset transfer traces for an isolated transport check.
 */
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

/**
 * @brief Release a transaction and verify ownership and interrupt state are restored.
 */
static void end (ed64_pro_ll_transaction_t *transaction) {
    TEST_CHECK(ed64_pro_ll_end(transaction) == ED64_PRO_LL_OK);
    TEST_CHECK(mock.irq_depth == 0);
    TEST_CHECK(pro_ll.owner == NULL);
}

/**
 * @brief Verify the exact EDID bus address/length and cache/barrier publication order.
 */
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

/**
 * @brief Verify the four-byte mailbox NOP uses writeback without read invalidation.
 */
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

/**
 * @brief Reject invalid ownership/register/length arguments without DMA or latched faults.
 */
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

/**
 * @brief Verify busy waiting and the IRQ-protected recheck prevent queue-full submission.
 */
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

/**
 * @brief Bound queue and hardware-busy waits across counter wrap without issuing DMA.
 */
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

/**
 * @brief Preserve caller output and staging quarantine after timeout and late DMA completion.
 */
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

/**
 * @brief Treat a deadline equal to the current tick as expired before DMA submission.
 */
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
    const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
    TEST_CHECK(failure->pi_context_valid && failure->pi_phase_limited && !failure->pi_submitted);
    TEST_CHECK(failure->pi_budget_ticks == 0 && failure->pi_elapsed_ticks == 0);
    end(&transaction);
}

/**
 * @brief Latch pre/post-submission PI faults with unchanged output and the correct poison state.
 */
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

/**
 * @brief Advance the mock clock to the configured completion deadline.
 */
static void expire_on_completion (void) {
    mock.now = finish_deadline;
}

/**
 * @brief Reject exact-deadline completion without publishing data or releasing DMA quarantine.
 */
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
    const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
    TEST_CHECK(failure->poisoned && failure->pi_context_valid && failure->pi_submitted && failure->pi_phase_limited);
    TEST_CHECK(failure->pi_budget_ticks == TICKS_FROM_MS(2) && failure->pi_elapsed_ticks == TICKS_FROM_MS(2));
    TEST_CHECK(strcmp(mock.events, "BRF") == 0);
    end(&transaction);
}

/**
 * @brief Inject a PI error while DMA is pending.
 */
static void error_during_dma (void) {
    mock_pi_status |= PI_STATUS_ERROR;
}

/**
 * @brief Keep an issued write's staging unchanged after a PI fault and late completion.
 */
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

/**
 * @brief Reject IRQ-disabled, exception, kernel, and uninitialized contexts without side effects.
 */
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

/**
 * @brief Attempt nested and interrupt-disabled entry during DMA without changing owner or progress.
 */
static void nested_entry (void) {
    ed64_pro_ll_transaction_t nested;
    ed64_pro_ll_transaction_t *owner = pro_ll.owner;
    size_t transferred = pro_ll.transferred;
    ed64_pro_diagnostics_t startup = pro_diagnostics;
    unsigned enqueues = mock.enqueues;
    ed64_pro_status_t status;
    memset(&status, 0xCD, sizeof(status));
    TEST_CHECK(ed64_pro_ll_begin(&nested, deadline_ms(1000)) == ED64_PRO_LL_BUSY);
    TEST_CHECK(ed64_pro_ll_end(&nested) == ED64_PRO_LL_BUSY);
    TEST_CHECK(ed64_pro_ll_read_status(&nested, &status, deadline_ms(1000)) == ED64_PRO_LL_BUSY);
    TEST_CHECK(ed64_pro_probe().error == ED64_PRO_LL_BUSY);
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_BUSY);
    mock.cp0 &= ~C0_STATUS_IE;
    TEST_CHECK(ed64_pro_ll_begin(&nested, deadline_ms(1000)) == ED64_PRO_LL_CONTEXT);
    TEST_CHECK(ed64_pro_ll_read_status(owner, &status, deadline_ms(1000)) == ED64_PRO_LL_CONTEXT);
    TEST_CHECK(ed64_pro_probe().error == ED64_PRO_LL_CONTEXT);
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_CONTEXT);
    mock.cp0 |= C0_STATUS_IE;
    TEST_CHECK(pro_ll.owner == owner && mock.irq_depth == 0);
    TEST_CHECK(pro_ll.transferred == transferred && mock.enqueues == enqueues && status.key == 0xCD);
    TEST_CHECK(ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
    TEST_CHECK(memcmp(&startup.failure, &pro_diagnostics.failure, sizeof(startup.failure)) == 0);
    if (startup.state == ED64_PRO_INITIALIZING) {
        TEST_CHECK(memcmp(&startup, &pro_diagnostics, sizeof(startup)) == 0);
    }
}

/**
 * @brief Preserve the original transaction while completion hooks attempt nested transport entry.
 */
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

/**
 * @brief Model ISR updates to the caller's cache line while checking its output and canaries.
 */
static void neighboring_interrupt (void) {
    TEST_CHECK(shared_line.output == 42);
    TEST_CHECK(shared_line.before == 0x11223344u && shared_line.after == 0x55667788u);
    shared_line.irq_updates++;
}

/**
 * @brief Verify DMA publication preserves ISR updates and canaries on the caller's shared cache line.
 */
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

/**
 * @brief Match only the Pro family/device, cache the read, and forbid writes after confirmed absence.
 */
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
        TEST_CHECK(mock.cart_writes == 0);
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

/**
 * @brief Keep failed identity reads indeterminate and sticky without writes or retries.
 */
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
        TEST_CHECK(mock.cart_writes == 0);
        unsigned enqueues = mock.enqueues;
        size_t events = mock.event_count;
        TEST_CHECK(ed64_pro_probe().error == errors[i]);
        TEST_CHECK(mock.enqueues == enqueues && mock.event_count == events && mock.irq_depth == 0);
    }
}

/**
 * @brief Reject state-changing and FIFO commands until Pro identification completes.
 */
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
    TEST_CHECK(ed64_pro_ll_stop(&transaction, deadline_ms(5000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(ed64_pro_ll_init_sd(&transaction, &status, deadline_ms(5000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(mock.enqueues == 0 && mock.cart_writes == 0);
    TEST_CHECK(ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
    end(&transaction);
}

/**
 * @brief Compare selected command frames and exact DMA/cache lengths against literal protocol words.
 */
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

/**
 * @brief Preserve opaque/nonzero inherited status bytes and verify the exact status bus trace.
 */
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

/**
 * @brief Verify FIFO chunking, exact byte counts, and caller canaries across offsets and boundary lengths.
 */
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

/**
 * @brief Accept zero-length FIFO reads but reject null or oversized outputs before DMA.
 */
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

/**
 * @brief Verify empty through full-capacity handoff drains discard exactly the reported bytes.
 */
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

/**
 * @brief Bound impossible counts and continuous refill while preserving confirmed RX progress.
 */
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

/**
 * @brief Recognize phase, PI-acquisition, and PI-completion timeout results.
 */
static bool is_deadline_error (ed64_pro_ll_error_t error) {
    return error == ED64_PRO_LL_TIMEOUT || error == ED64_PRO_LL_PI_ACQUIRE_TIMEOUT || error == ED64_PRO_LL_PI_TIMEOUT;
}

/**
 * @brief Ensure continuous refill cannot restart the fixed cleanup deadline.
 */
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

/**
 * @brief Reject every incomplete status length without publishing a partial frame.
 */
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

/**
 * @brief Make malformed, trailing, and preexisting-data faults sticky without further discard.
 */
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

/**
 * @brief Preserve phase/transaction deadlines despite slow byte arrival and counter wrap.
 */
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

/**
 * @brief Inject a PI error with the second FIFO chunk pending after 128 confirmed bytes.
 */
static void fail_second_chunk (void) {
    if (mock.pi == 0x1F800000u && !mock.write && mock.rx_consumed == 128) {
        mock.never_finish = true;
        mock_pi_status |= PI_STATUS_ERROR;
    }
}

/**
 * @brief Preserve the first chunk's confirmed count and quarantine the second through late completion.
 */
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

/**
 * @brief Prevent nested transport calls from interleaving a status command and its reply.
 */
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

static const struct {
    uint32_t address;
    size_t length;
    bool write;
    ed64_pro_phase_t phase;
} init_trace[] = {
    {0x1F800014u, 4, false, ED64_PRO_PHASE_PROBE},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_INITIAL_DRAIN},
    {0x1F800000u, 12, false, ED64_PRO_PHASE_INITIAL_DRAIN},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_INITIAL_DRAIN},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_INHERITED_STATUS},
    {0x1F800000u, 16, true, ED64_PRO_PHASE_INHERITED_STATUS},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_INHERITED_STATUS},
    {0x1F800000u, 16, false, ED64_PRO_PHASE_INHERITED_STATUS},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_INHERITED_STATUS},
    {0x1F800010u, 4, true, ED64_PRO_PHASE_STOP},
    {0x1F800000u, 20, true, ED64_PRO_PHASE_STOP},
    {0x1F800010u, 4, false, ED64_PRO_PHASE_STOP},
    {0x1F800010u, 4, false, ED64_PRO_PHASE_STOP},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_POST_STOP_DRAIN},
    {0x1F800000u, 8, false, ED64_PRO_PHASE_POST_STOP_DRAIN},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_POST_STOP_DRAIN},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_STOP_STATUS},
    {0x1F800000u, 16, true, ED64_PRO_PHASE_STOP_STATUS},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_STOP_STATUS},
    {0x1F800000u, 16, false, ED64_PRO_PHASE_STOP_STATUS},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_STOP_STATUS},
    {0x1F800000u, 20, true, ED64_PRO_PHASE_SD_INIT},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_SD_INIT},
    {0x1F800000u, 16, true, ED64_PRO_PHASE_SD_INIT},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_SD_INIT},
    {0x1F800000u, 16, false, ED64_PRO_PHASE_SD_INIT},
    {0x1F800004u, 4, false, ED64_PRO_PHASE_SD_INIT},
};

/**
 * @brief Prepare inherited FIFO data, status, and mailbox state for startup tests.
 */
static void handoff_fixture (void) {
    power_on();
    rx_push(0x12);
    rx_push(0x34);
    rx_push(0x56);
    mock.post_stop_junk = 2;
    mock.replies[0].bytes[3] = 0xB0;
    mock.mailbox = 0x52445921u;
}

/**
 * @brief Verify the complete one-shot MCU handoff against its golden trace and cached-success state.
 */
static void test_init_transcript (void) {
    handoff_fixture();
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_OK);
    const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
    TEST_CHECK(diagnostics->state == ED64_PRO_READY && diagnostics->phase == ED64_PRO_PHASE_READY);
    TEST_CHECK(diagnostics->probe.id_valid && diagnostics->probe.id == 0xED640127u);
    TEST_CHECK(diagnostics->inherited_status_valid && diagnostics->inherited_status.mcu_status == 0xB0);
    TEST_CHECK(diagnostics->status_valid && diagnostics->status.mcu_status == 0);
    TEST_CHECK(diagnostics->initial_drained == 3 && diagnostics->post_stop_drained == 2);
    TEST_CHECK(diagnostics->failure.code == ED64_PRO_LL_OK);
    TEST_CHECK(mock.bus_count == sizeof(init_trace) / sizeof(init_trace[0]));
    static const uint32_t commands[] = {0x10, 0x85, 0x10, 0x80, 0x10};
    size_t command_index = 0;
    for (size_t i = 0; i < mock.bus_count; i++) {
        TEST_CHECK(
            mock.bus[i].address == init_trace[i].address && mock.bus[i].length == init_trace[i].length &&
            mock.bus[i].write == init_trace[i].write
        );
        if (mock.bus[i].write && mock.bus[i].address == 0x1F800000u) {
            TEST_ASSERT(command_index < sizeof(commands) / sizeof(commands[0]));
            TEST_CHECK(mock.bus[i].words[2] == commands[command_index++]);
        }
    }
    TEST_CHECK(command_index == 5 && mock.status_requests == 3 && mock.stop_commands == 1 && mock.sd_commands == 1);
    TEST_CHECK(mock.bus[9].words[0] == 0 && mock.mailbox_reads == 2);
    TEST_CHECK(ed64_pro_ll_get_metrics()->submissions == mock.bus_count);
    TEST_CHECK(ed64_pro_ll_get_metrics()->rx_peak == 4);
    TEST_CHECK(ed64_pro_ll_get_metrics()->max_pi_ticks > 0);
    for (ed64_pro_phase_t phase = ED64_PRO_PHASE_PROBE; phase < ED64_PRO_PHASE_READY; phase++) {
        TEST_CHECK(diagnostics->phase_ticks[phase] > 0);
    }
    TEST_CHECK(diagnostics->attempt_ticks < TICKS_FROM_MS(15000));
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "transport ready") != NULL);
    TEST_CHECK(mock.irq_depth == 0 && pro_ll.owner == NULL);

    ed64_pro_diagnostics_t before = *diagnostics;
    size_t bus_count = mock.bus_count;
    size_t events = mock.event_count;
    mock.now += TICKS_FROM_MS(20000);
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_OK);
    TEST_CHECK(mock.bus_count == bus_count && mock.event_count == events);
    TEST_CHECK(memcmp(&before, diagnostics, sizeof(before)) == 0);
}

/**
 * @brief Inject timeout at every startup DMA and verify sticky phase, evidence, and quarantine.
 *
 * Late completion must not alter staging, retained diagnostics, or retry behavior.
 */
static void test_init_failure_boundaries (void) {
    for (size_t i = 0; i < sizeof(init_trace) / sizeof(init_trace[0]); i++) {
        handoff_fixture();
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        mock.fail_at = (unsigned)i + 1;
        TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_PI_TIMEOUT);
        const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
        TEST_CHECK(diagnostics->state == ED64_PRO_POISONED && diagnostics->phase == init_trace[i].phase);
        TEST_CHECK(diagnostics->failure.code == ED64_PRO_LL_PI_TIMEOUT && diagnostics->failure.poisoned);
        TEST_CHECK(diagnostics->failure.ticket == mock.ticket && mock.active);
        TEST_CHECK(diagnostics->probe.id_valid == (i != 0));
        TEST_CHECK(diagnostics->inherited_status_valid == (i >= 9));
        TEST_CHECK(mock.bus_count == i + 1 && mock.irq_depth == 0 && pro_ll.owner == NULL);
        if (i == 0) {
            TEST_CHECK(mock.cart_writes == 0);
            TEST_CHECK(strstr(ed64_pro_get_error_message(), "identity unknown") != NULL);
        }
        if (init_trace[i].phase == ED64_PRO_PHASE_SD_INIT) {
            TEST_CHECK(!diagnostics->status_valid);
            TEST_CHECK(strstr(ed64_pro_get_error_message(), "SD readiness") != NULL);
        }
        char message[256];
        snprintf(message, sizeof(message), "%s", ed64_pro_get_error_message());
        ed64_pro_diagnostics_t before = *diagnostics;
        uint32_t staging[ED64_PRO_FIFO_CHUNK];
        memcpy(staging, pro_staging, sizeof(staging));
        TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_PI_TIMEOUT);
        TEST_CHECK(mock.bus_count == i + 1 && memcmp(staging, pro_staging, sizeof(staging)) == 0);
        finish_pending();
        TEST_CHECK(memcmp(staging, pro_staging, sizeof(staging)) == 0);
        TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_PI_TIMEOUT && mock.bus_count == i + 1);
        TEST_CHECK(memcmp(&before, diagnostics, sizeof(before)) == 0);
        TEST_CHECK(strcmp(message, ed64_pro_get_error_message()) == 0);
    }
}

/**
 * @brief Distinguish malformed inherited frames from fresh STOP/SD MCU errors and retain their origin.
 */
static void test_inherited_and_fresh_errors (void) {
    for (size_t status_index = 0; status_index < 3; status_index++) {
        handoff_fixture();
        if (status_index == 0) {
            mock.replies[0].bytes[0] = 0x5B;
        } else {
            mock.replies[status_index].bytes[3] = 0xB0;
        }
        ed64_pro_ll_error_t expected = status_index == 0 ? ED64_PRO_LL_FRAME : ED64_PRO_LL_MCU_ERROR;
        TEST_CHECK(ed64_pro_init_transport() == expected);
        const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
        ed64_pro_phase_t phase = status_index == 0   ? ED64_PRO_PHASE_INHERITED_STATUS
                                 : status_index == 1 ? ED64_PRO_PHASE_STOP_STATUS
                                                     : ED64_PRO_PHASE_SD_INIT;
        TEST_CHECK(diagnostics->state == ED64_PRO_FAILED && diagnostics->phase == phase);
        TEST_CHECK(diagnostics->failure.code == expected && !diagnostics->failure.poisoned);
        TEST_CHECK(diagnostics->inherited_status_valid == (status_index != 0));
        TEST_CHECK(mock.stop_commands == (status_index == 0 ? 0 : 1));
        TEST_CHECK(mock.sd_commands == (status_index == 2 ? 1 : 0));
        if (status_index) {
            TEST_CHECK(diagnostics->status_valid && diagnostics->status.mcu_status == 0xB0);
            TEST_CHECK(strstr(ed64_pro_get_error_message(), "MCU status 0xB0") != NULL);
        }
        unsigned enqueues = mock.enqueues;
        mock.replies[status_index].bytes[0] = 0x5A;
        mock.replies[status_index].bytes[3] = 0;
        TEST_CHECK(ed64_pro_init_transport() == expected && mock.enqueues == enqueues);
    }
}

/**
 * @brief Reject every partial reply length at each handoff status phase within its original budget.
 */
static void test_init_partial_statuses (void) {
    for (size_t status_index = 0; status_index < 3; status_index++) {
        for (size_t length = 0; length < 4; length++) {
            handoff_fixture();
            mock.tick_step = TICKS_FROM_MS(1) / 8;
            mock.replies[status_index].length = length;
            TEST_CHECK(is_deadline_error(ed64_pro_init_transport()));
            const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
            ed64_pro_phase_t phase = status_index == 0   ? ED64_PRO_PHASE_INHERITED_STATUS
                                     : status_index == 1 ? ED64_PRO_PHASE_STOP_STATUS
                                                         : ED64_PRO_PHASE_SD_INIT;
            TEST_CHECK(diagnostics->phase == phase && diagnostics->failure.transferred == length);
            TEST_CHECK(diagnostics->inherited_status_valid == (status_index != 0) && !diagnostics->status_valid);
            TEST_CHECK(mock.status_requests == status_index + 1);
            uint32_t budget = status_index == 2 ? 5000 : 1000;
            TEST_CHECK(diagnostics->phase_ticks[phase] <= TICKS_FROM_MS(budget + 2));
        }
    }
}

/**
 * @brief Require two equal consecutive READY reads after clearing the inherited mailbox.
 */
static void test_mailbox_stability (void) {
    handoff_fixture();
    mock.mailbox_samples[0] = 0x52445921u;
    mock.mailbox_samples[1] = 0x12345678u;
    mock.mailbox_samples[2] = 0x52445921u;
    mock.mailbox_samples[3] = 0x52445921u;
    mock.mailbox_sample_count = 4;
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_OK);
    TEST_CHECK(mock.mailbox_reads == 4 && mock.stop_commands == 1 && mock.sd_commands == 1);
    TEST_CHECK(mock.bus[9].address == 0x1F800010u && mock.bus[9].write && mock.bus[9].words[0] == 0);
}

/**
 * @brief Bound stalled or alternating mailboxes and retain failure without sending SD_INIT.
 */
static void test_mailbox_deadlines (void) {
    for (unsigned mode = 0; mode < 2; mode++) {
        handoff_fixture();
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        mock.mailbox_stalled = mode == 0;
        mock.mailbox_unstable = mode == 1;
        ed64_pro_ll_error_t error = ed64_pro_init_transport();
        TEST_CHECK(error == ED64_PRO_LL_MAILBOX_TIMEOUT || is_deadline_error(error));
        const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
        TEST_CHECK(diagnostics->phase == ED64_PRO_PHASE_STOP && diagnostics->inherited_status_valid);
        TEST_CHECK(diagnostics->phase_ticks[ED64_PRO_PHASE_STOP] <= TICKS_FROM_MS(5002));
        TEST_CHECK(diagnostics->phase_ticks[ED64_PRO_PHASE_STOP] >= TICKS_FROM_MS(5000));
        TEST_CHECK(mock.stop_commands == 1 && mock.sd_commands == 0);
        if (mode == 0) {
            TEST_CHECK(mock.mailbox == 0);
        }
        unsigned enqueues = mock.enqueues;
        TEST_CHECK(ed64_pro_init_transport() == error && mock.enqueues == enqueues);
    }
}

/**
 * @brief Enforce the byte ceiling on both handoff cleanup phases without advancing to SD_INIT.
 */
static void test_init_cleanup_limits (void) {
    for (unsigned post_stop = 0; post_stop < 2; post_stop++) {
        power_on();
        if (post_stop) {
            mock.post_stop_junk = 128;
            mock.post_stop_refill = true;
        } else {
            for (size_t i = 0; i < 128; i++) {
                rx_push(0x12);
            }
            mock.refill = true;
        }
        TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_DRAIN_LIMIT);
        const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
        TEST_CHECK(diagnostics->phase == (post_stop ? ED64_PRO_PHASE_POST_STOP_DRAIN : ED64_PRO_PHASE_INITIAL_DRAIN));
        TEST_CHECK(diagnostics->failure.transferred == 2048 && diagnostics->state == ED64_PRO_FAILED);
        TEST_CHECK(mock.stop_commands == post_stop && mock.sd_commands == 0);
    }
}

/**
 * @brief Apply the original five-second SD budget to slow complete and incomplete replies.
 */
static void test_sd_deadline (void) {
    for (unsigned timeout = 0; timeout < 2; timeout++) {
        handoff_fixture();
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        mock.reply_intervals[2] = TICKS_FROM_MS(timeout ? 1400 : 400);
        ed64_pro_ll_error_t error = ed64_pro_init_transport();
        const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
        if (timeout) {
            TEST_CHECK(is_deadline_error(error) && !diagnostics->status_valid);
            TEST_CHECK(diagnostics->failure.transferred == 3);
            TEST_CHECK(diagnostics->phase_ticks[ED64_PRO_PHASE_SD_INIT] >= TICKS_FROM_MS(5000));
        } else {
            TEST_CHECK(error == ED64_PRO_LL_OK && diagnostics->state == ED64_PRO_READY);
            TEST_CHECK(diagnostics->phase_ticks[ED64_PRO_PHASE_SD_INIT] >= TICKS_FROM_MS(1600));
        }
        TEST_CHECK(diagnostics->phase_ticks[ED64_PRO_PHASE_SD_INIT] <= TICKS_FROM_MS(5002));
        TEST_CHECK(mock.stop_commands == 1 && mock.sd_commands == 1 && mock.status_requests == 3);
    }
}

/**
 * @brief Share the 15-second budget across probe and startup despite counter wrap or idle delay.
 */
static void test_whole_attempt_deadline (void) {
    for (unsigned expired = 0; expired < 2; expired++) {
        handoff_fixture();
        mock.now = UINT32_MAX - TICKS_FROM_MS(2000);
        TEST_ASSERT(ed64_pro_probe().outcome == ED64_PRO_PROBE_IDENTIFIED);
        mock.now += TICKS_FROM_MS(expired ? 15000 : 14000);
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        mock.stop_delay = TICKS_FROM_MS(1500);
        ed64_pro_ll_error_t error = ed64_pro_init_transport();
        TEST_CHECK(is_deadline_error(error) || error == ED64_PRO_LL_MAILBOX_TIMEOUT);
        const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
        TEST_CHECK(diagnostics->phase == (expired ? ED64_PRO_PHASE_INITIAL_DRAIN : ED64_PRO_PHASE_STOP));
        TEST_CHECK(diagnostics->attempt_ticks >= TICKS_FROM_MS(15000));
        TEST_CHECK(diagnostics->attempt_ticks <= TICKS_FROM_MS(15003));
        TEST_CHECK(mock.stop_commands == (expired ? 0 : 1) && mock.sd_commands == 0);
        if (expired) {
            TEST_CHECK(mock.enqueues == 1 && mock.cart_writes == 0);
        }
    }
}

/**
 * @brief Display cold-start ownership/context rejections without latching them or preventing retry.
 */
static void test_cold_start_rejections (void) {
    static const struct {
        const char *name;
        ed64_pro_ll_error_t error;
        const char *message;
    } cases[] = {
        {"busy", ED64_PRO_LL_BUSY, "Transport already in use"},
        {"unsupported context", ED64_PRO_LL_CONTEXT, "Unsupported interrupt/thread context"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (unsigned initialize = 0; initialize < 2; initialize++) {
            TEST_CASE_("%s via %s", cases[i].name, initialize ? "initialization" : "probe");
            power_on();
            ed64_pro_ll_transaction_t owner;
            if (cases[i].error == ED64_PRO_LL_BUSY) {
                begin(&owner);
            } else {
                mock.cp0 = 0;
            }
            ed64_pro_ll_error_t error = initialize ? ed64_pro_init_transport() : ed64_pro_probe().error;
            TEST_CHECK(error == cases[i].error);
            const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
            TEST_CHECK(diagnostics->state == ED64_PRO_COLD && diagnostics->phase == ED64_PRO_PHASE_PROBE);
            TEST_CHECK(diagnostics->probe.error == cases[i].error && !diagnostics->probe.id_valid);
            TEST_CHECK(diagnostics->failure.code == ED64_PRO_LL_OK && !diagnostics->failure.poisoned);
            TEST_CHECK(ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
            TEST_CHECK(strstr(ed64_pro_get_error_message(), cases[i].message) != NULL);
            TEST_CHECK(mock.enqueues == 0 && mock.event_count == 0);
            if (cases[i].error == ED64_PRO_LL_BUSY) {
                TEST_CHECK(pro_ll.owner == &owner);
                end(&owner);
            } else {
                mock.cp0 = C0_STATUS_IE;
            }
            TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_OK);
            TEST_CHECK(diagnostics->state == ED64_PRO_READY && diagnostics->probe.error == ED64_PRO_LL_OK);
            TEST_CHECK(diagnostics->failure.code == ED64_PRO_LL_OK);
            unsigned enqueues = mock.enqueues;
            TEST_CHECK(strstr(ed64_pro_get_error_message(), "transport ready") != NULL);
            TEST_CHECK(mock.enqueues == enqueues);
        }
    }
}

/**
 * @brief Allow context rejection without probing and cache confirmed absence without MCU writes.
 */
static void test_init_absent_and_context (void) {
    power_on();
    mock.cp0 = 0;
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_CONTEXT);
    TEST_CHECK(ed64_pro_get_diagnostics()->state == ED64_PRO_COLD && mock.enqueues == 0);
    mock.cp0 = C0_STATUS_IE;
    mock.register_value = 0xED640025u;
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(ed64_pro_get_diagnostics()->state == ED64_PRO_ABSENT && mock.enqueues == 1);
    TEST_CHECK(ed64_pro_get_diagnostics()->probe.error == ED64_PRO_LL_OK);
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "Cartridge is not an EverDrive-64 Pro") != NULL);
    TEST_CHECK(mock.enqueues == 1);
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_NOT_IDENTIFIED && mock.enqueues == 1);
    TEST_CHECK(mock.cart_writes == 0);
}

/**
 * @brief Enforce one owner through the complete handoff against nested completion hooks.
 */
static void test_init_serialization (void) {
    handoff_fixture();
    mock.poll_hook = nested_entry;
    mock.finish_after = 2;
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_OK);
    TEST_CHECK(ed64_pro_get_diagnostics()->state == ED64_PRO_READY && mock.bus_count == 27);
    TEST_CHECK(mock.stop_commands == 1 && mock.sd_commands == 1 && mock.irq_depth == 0);
}

/**
 * @brief Identify submitted register-read, status-write and status-read DMA timeouts.
 */
static void test_pi_failure_context (void) {
    static const struct {
        const char *name;
        uint32_t address;
        size_t length;
        bool write;
        unsigned fail_at;
    } cases[] = {
        {"FIFO availability register", ED64_PRO_REG_FIFOSTAT, 4, false, 1},
        {"FIFO status command", ED64_PRO_REG_FIFO, 16, true, 2},
        {"FIFO four-byte status reply", ED64_PRO_REG_FIFO, 16, false, 4},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TEST_CASE(cases[i].name);
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        mock.fail_at = cases[i].fail_at;
        uint32_t value = 0x12345678;
        ed64_pro_status_t status;
        memset(&status, 0xAA, sizeof(status));
        ed64_pro_ll_error_t error;
        if (i == 0) {
            error = ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_FIFOSTAT, &value, deadline_ms(1000));
        } else {
            error = ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000));
        }
        TEST_CHECK(error == ED64_PRO_LL_PI_TIMEOUT);
        const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
        TEST_CHECK(failure->pi_context_valid && failure->pi_submitted && failure->poisoned);
        TEST_CHECK(failure->pi_address == cases[i].address && failure->pi_length == cases[i].length);
        TEST_CHECK(failure->pi_write == cases[i].write && failure->ticket == mock.ticket && failure->ticket != 0);
        TEST_CHECK(failure->transferred == 0 && value == 0x12345678 && status.key == 0xAA);
        TEST_CHECK(mock.enqueues == mock.fail_at);
        ed64_pro_ll_failure_t first;
        memcpy(&first, failure, sizeof(first));
        unsigned enqueues = mock.enqueues;
        TEST_CHECK(ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000)) == ED64_PRO_LL_PI_TIMEOUT);
        TEST_CHECK(memcmp(&first, ed64_pro_ll_get_failure(), sizeof(first)) == 0 && mock.enqueues == enqueues);
        end(&transaction);
        TEST_CHECK(ed64_pro_ll_begin(&transaction, deadline_ms(1000)) == ED64_PRO_LL_PI_TIMEOUT);
        TEST_CHECK(memcmp(&first, ed64_pro_ll_get_failure(), sizeof(first)) == 0 && mock.enqueues == enqueues);
    }
}

/**
 * @brief Identify attempted queue acquisition separately from submitted DMA.
 */
static void test_pi_attempt_context (void) {
    power_on();
    identify();
    mock.force_busy = true;
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint32_t value = 42;
    TEST_CHECK(
        ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_FIFOSTAT, &value, deadline_ms(1000)) ==
        ED64_PRO_LL_PI_ACQUIRE_TIMEOUT
    );
    const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
    TEST_CHECK(failure->pi_context_valid && !failure->pi_submitted && !failure->poisoned && failure->ticket == 0);
    TEST_CHECK(failure->pi_address == ED64_PRO_REG_FIFOSTAT && failure->pi_length == 4 && !failure->pi_write);
    TEST_CHECK(mock.enqueues == 0 && value == 42);
    end(&transaction);
}

/**
 * @brief Avoid attributing a protocol-only fault to the last successful PI access.
 */
static void test_non_pi_fault_context (void) {
    power_on();
    identify();
    mock.replies[0].bytes[0] = 0;
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    ed64_pro_status_t status;
    memset(&status, 0xAA, sizeof(status));
    TEST_CHECK(ed64_pro_ll_read_status(&transaction, &status, deadline_ms(1000)) == ED64_PRO_LL_FRAME);
    const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
    TEST_CHECK(!failure->pi_context_valid && !failure->pi_submitted && !failure->pi_write);
    TEST_CHECK(failure->pi_address == 0 && failure->pi_length == 0 && !failure->poisoned && failure->ticket == 0);
    TEST_CHECK(failure->pi_budget_ticks == 0 && failure->pi_elapsed_ticks == 0 && !failure->pi_phase_limited);
    TEST_CHECK(mock.enqueues == 4 && mock.rx_consumed == 4 && status.key == 0xAA);
    end(&transaction);
}

/**
 * @brief Distinguish a full PI cap from a deadline equal to that cap.
 */
static void test_pi_full_budget_timing (void) {
    const uint32_t phase_ms[] = {1000, ED64_PRO_PI_TIMEOUT_MS};
    for (size_t i = 0; i < sizeof(phase_ms) / sizeof(phase_ms[0]); i++) {
        TEST_CASE_("phase budget %u ms", phase_ms[i]);
        power_on();
        identify();
        mock.tick_step = TICKS_FROM_MS(1);
        mock.never_finish = true;
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        uint32_t value = 42;
        TEST_CHECK(
            ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_FIFOSTAT, &value, deadline_ms(phase_ms[i])) ==
            ED64_PRO_LL_PI_TIMEOUT
        );
        const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
        TEST_CHECK(
            failure->pi_context_valid && failure->pi_submitted && failure->poisoned && !failure->pi_phase_limited
        );
        TEST_CHECK(failure->pi_budget_ticks == TICKS_FROM_MS(ED64_PRO_PI_TIMEOUT_MS));
        TEST_CHECK(failure->pi_elapsed_ticks >= failure->pi_budget_ticks);
        TEST_CHECK(failure->pi_elapsed_ticks <= failure->pi_budget_ticks + TICKS_FROM_MS(2));
        TEST_CHECK(mock.enqueues == 1 && value == 42);
        end(&transaction);
    }
}

/**
 * @brief Preserve a phase-clipped final FIFO poll even when DMA finishes at the phase deadline.
 */
static void test_pi_final_poll_timing (void) {
    power_on();
    mock.tick_step = 0;
    identify();
    ed64_pro_ll_transaction_t transaction;
    TEST_ASSERT(ed64_pro_ll_begin(&transaction, TICKS_FROM_MS(5000)) == ED64_PRO_LL_OK);
    mock.now = TICKS_FROM_MS(4999);
    finish_deadline = TICKS_FROM_MS(5000);
    mock.poll_hook = expire_on_completion;
    uint32_t value = 42;
    TEST_CHECK(
        ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_FIFOSTAT, &value, deadline_ms(1000)) == ED64_PRO_LL_PI_TIMEOUT
    );
    const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
    TEST_CHECK(failure->pi_context_valid && failure->pi_submitted && failure->poisoned && failure->pi_phase_limited);
    TEST_CHECK(failure->pi_address == ED64_PRO_REG_FIFOSTAT && failure->pi_length == 4 && !failure->pi_write);
    TEST_CHECK(failure->pi_budget_ticks == TICKS_FROM_MS(1) && failure->pi_elapsed_ticks == TICKS_FROM_MS(1));
    TEST_CHECK(!mock.active && failure->ticket == mock.ticket && value == 42 && mock.enqueues == 1);
    ed64_pro_ll_failure_t first;
    memcpy(&first, failure, sizeof(first));
    TEST_CHECK(
        ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_FIFOSTAT, &value, deadline_ms(1000)) == ED64_PRO_LL_PI_TIMEOUT
    );
    TEST_CHECK(memcmp(&first, ed64_pro_ll_get_failure(), sizeof(first)) == 0 && mock.enqueues == 1);
    end(&transaction);
    TEST_CHECK(ed64_pro_ll_begin(&transaction, deadline_ms(1000)) == ED64_PRO_LL_PI_TIMEOUT);
    TEST_CHECK(memcmp(&first, ed64_pro_ll_get_failure(), sizeof(first)) == 0 && mock.enqueues == 1);
}

/**
 * @brief Map transport success and sticky failures through the normal flashcart adapter.
 */
static void test_adapter_initialization (void) {
    flashcart_t *adapter = ed64_pro_get_flashcart();
    for (unsigned failed = 0; failed < 2; failed++) {
        handoff_fixture();
        if (failed) {
            mock.fail_at = 2;
        }
        flashcart_err_t expected = failed ? FLASHCART_ERR_INT : FLASHCART_OK;
        TEST_CHECK(adapter->init() == expected);
        const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
        TEST_CHECK(diagnostics->state == (failed ? ED64_PRO_POISONED : ED64_PRO_READY));
        unsigned enqueues = mock.enqueues;
        TEST_CHECK(
            failed ? strstr(adapter->get_error_message(expected), "startup failed") != NULL
                   : adapter->get_error_message(expected) == NULL
        );
        TEST_CHECK(adapter->get_error_message(FLASHCART_ERR_SD_CARD) != NULL);
        TEST_CHECK(adapter->get_error_message(FLASHCART_OK) == NULL);
        TEST_CHECK(adapter->init() == expected && mock.enqueues == enqueues);
    }
}

/**
 * @brief Keep cached queries and unavailable Pro callbacks free of bus traffic even after a poisoned DMA.
 */
static void test_adapter_unavailable_operations (void) {
    for (unsigned failed = 0; failed < 2; failed++) {
        handoff_fixture();
        flashcart_t *adapter = ed64_pro_get_flashcart();
        if (failed) {
            mock.fail_at = 2;
        }
        (void)adapter->init();
        unsigned enqueues = mock.enqueues;
        for (flashcart_features_t feature = FLASHCART_FEATURE_64DD; feature <= FLASHCART_FEATURE_ROM_REBOOT_FAST;
             feature++) {
            bool supported = feature == FLASHCART_FEATURE_AUTO_CIC || feature == FLASHCART_FEATURE_AUTO_REGION
                || feature == FLASHCART_FEATURE_SAVE_WRITEBACK;
            TEST_CHECK(adapter->has_feature(feature) == supported);
        }
        flashcart_firmware_version_t version = adapter->get_firmware_version();
        TEST_CHECK(version.major == (failed ? 0 : 26) && version.minor == (failed ? 0 : 1014) && version.revision == 0);
        TEST_CHECK(adapter->load_rom("sd:/test.z64", NULL) == FLASHCART_ERR_LOAD);
        TEST_CHECK(adapter->load_file("sd:/test.bin", 0, 0) == FLASHCART_ERR_LOAD);
        TEST_CHECK(adapter->load_save("sd:/test.sra") == FLASHCART_ERR_ARGS);
        TEST_CHECK(adapter->set_save_type(FLASHCART_SAVE_TYPE_SRAM_256KBIT) == FLASHCART_ERR_LOAD);
        TEST_CHECK(adapter->deinit != NULL && adapter->load_64dd_ipl == NULL && adapter->load_64dd_disk == NULL);
        TEST_CHECK(adapter->load_64dd_disks == NULL && adapter->get_button_state == NULL);
        TEST_CHECK(adapter->get_voltage_temperature == NULL && adapter->set_save_writeback == NULL);
        TEST_CHECK(adapter->set_next_boot_mode == NULL && mock.enqueues == enqueues);
    }
}

char cart_card_byteswap;

/**
 * @brief Fail the test if transport code attempts a filesystem stat.
 */
FRESULT f_stat (const char *path, FILINFO *info) {
    (void)path;
    (void)info;
    TEST_CHECK(false);
    return FR_NO_FILE;
}

/**
 * @brief Stub file opens with a missing-file result.
 */
FRESULT f_open (FIL *file, const char *path, uint8_t mode) {
    (void)file;
    (void)path;
    (void)mode;
    return FR_NO_FILE;
}

/**
 * @brief Stub file reads with no data and a disk error.
 */
FRESULT f_read (FIL *file, void *buffer, UINT bytes, UINT *read) {
    (void)file;
    (void)buffer;
    (void)bytes;
    *read = 0;
    return FR_DISK_ERR;
}

/**
 * @brief Stub file seeks with a disk error.
 */
FRESULT f_lseek (FIL *file, FSIZE_t offset) {
    (void)file;
    (void)offset;
    return FR_DISK_ERR;
}

/**
 * @brief Stub file closes with a successful result.
 */
FRESULT f_close (FIL *file) {
    (void)file;
    return FR_OK;
}

/**
 * @brief Round the mocked file size up to a whole sector.
 */
void fatfs_fix_file_size (FIL *file) {
    file->obj.objsize = (file->obj.objsize + 511) & ~UINT64_C(511);
}

/**
 * @brief Remove a filesystem prefix while retaining the leading slash.
 */
char *strip_fs_prefix (char *path) {
    char *prefix = strstr(path, ":/");
    return prefix ? prefix + 1 : path;
}

/**
 * @brief Fail the test if transport code attempts to mount a filesystem.
 */
int fat_mount (const char *prefix, const fat_disk_t *disk, int flags) {
    (void)prefix;
    (void)disk;
    (void)flags;
    TEST_CHECK(false);
    return -1;
}

/**
 * @brief Fail the test if transport code attempts to unmount a filesystem.
 */
int fat_unmount (int volume_id) {
    (void)volume_id;
    TEST_CHECK(false);
    return -1;
}

/**
 * @brief Verify frame bounds, chunked encoding, and rejection before DMA.
 */
static void test_frame_limits (void) {
    power_on();
    identify();
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    mock.raw_frames = true;
    uint8_t payload[ED64_PRO_FRAME_MAX];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 19);
    }
    TEST_CHECK(
        pro_send_frame(&transaction, PRO_CMD_SYS, 0x10, payload, ED64_PRO_FRAME_MAX - 4, deadline_ms(1000)) ==
        ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(pro_send_frame(&transaction, PRO_CMD_SYS, 0x11, payload, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(pro_send_frame(&transaction, PRO_CMD_SYS, 0x10, NULL, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.enqueues == 0);
    TEST_CHECK(
        pro_send_frame(&transaction, PRO_CMD_SYS, 0x10, payload, ED64_PRO_FRAME_MAX - 5, deadline_ms(1000)) ==
        ED64_PRO_LL_OK
    );
    TEST_CHECK(mock.tx_count == ED64_PRO_FRAME_MAX && mock.bus_count == ED64_PRO_FRAME_MAX / ED64_PRO_FIFO_CHUNK);
    const uint8_t header[] = {0x2B, 0xD4, 0x84, 0x7B, 0x10};
    TEST_CHECK(memcmp(mock.tx_bytes, header, sizeof(header)) == 0);
    TEST_CHECK(memcmp(mock.tx_bytes + sizeof(header), payload, ED64_PRO_FRAME_MAX - sizeof(header)) == 0);
    for (size_t i = 0; i < mock.bus_count; i++) {
        TEST_CHECK(mock.bus[i].length == ED64_PRO_FIFO_CHUNK * 4 && mock.bus[i].write);
    }
    end(&transaction);
}

/**
 * @brief Verify byte-exact system-info requests and decoded values.
 */
static void test_sys_info (void) {
    for (size_t count = 1; count <= 2; count++) {
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        const uint32_t ids[] = {ED64_PRO_INFS_TS_FW, ED64_PRO_INFS_MAX_ROM_SIZE};
        uint32_t values[2] = {0, 0xDEADBEEF};
        TEST_CHECK(ed64_pro_ll_get_sys_info(&transaction, ids, values, count, deadline_ms(1000)) == ED64_PRO_LL_OK);
        TEST_CHECK(values[0] == 0x90555D4E);
        TEST_CHECK(values[1] == (count == 2 ? 0x04000000 : 0xDEADBEEF));
        const uint8_t frame[] = {0x2B, 0xD4, 0x84, 0x7B, 0x10, count, 0, 0, 0, 6, 0, 0, 0, 9};
        TEST_CHECK(mock.tx_count == 6 + count * 4 && memcmp(mock.tx_bytes, frame, mock.tx_count) == 0);
        TEST_CHECK(mock.info_requests == 1 && mock.rx_count == 0 && mock.status_requests == 0);
        end(&transaction);
    }
}

/**
 * @brief Reject invalid system-info and NRESP arguments before DMA.
 */
static void test_sys_info_invalid (void) {
    power_on();
    identify();
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint32_t ids[] = {ED64_PRO_INFS_TS_FW, 8};
    uint32_t values[2] = {0};
    TEST_CHECK(ed64_pro_ll_get_sys_info(&transaction, NULL, values, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_get_sys_info(&transaction, ids, NULL, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_get_sys_info(&transaction, ids, values, 0, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_get_sys_info(&transaction, ids, values, 3, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_get_sys_info(&transaction, ids, values, 2, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    uint8_t detail;
    TEST_CHECK(ed64_pro_ll_read_nresp(&transaction, 0xB0, &detail, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.enqueues == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
    end(&transaction);
}

/**
 * @brief Keep outputs unchanged and latch truncated or trailing system-info replies.
 */
static void test_sys_info_faults (void) {
    for (unsigned trailing = 0; trailing < 2; trailing++) {
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        const uint32_t ids[] = {ED64_PRO_INFS_TS_FW, ED64_PRO_INFS_MAX_ROM_SIZE};
        uint32_t values[] = {0xDEADBEEF, 0xDEADBEEF};
        mock.info_reply_limit = trailing ? 8 : 7;
        mock.info_trailing = trailing;
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        ed64_pro_ll_error_t error = ed64_pro_ll_get_sys_info(&transaction, ids, values, 2, deadline_ms(1000));
        TEST_CHECK(trailing ? error == ED64_PRO_LL_TRAILING_DATA : is_deadline_error(error));
        TEST_CHECK(values[0] == 0xDEADBEEF && values[1] == 0xDEADBEEF);
        TEST_CHECK(mock.info_requests == 1 && mock.nresp_requests == 0);
        unsigned enqueues = mock.enqueues;
        TEST_CHECK(ed64_pro_ll_get_sys_info(&transaction, ids, values, 2, deadline_ms(1000)) == error);
        TEST_CHECK(mock.enqueues == enqueues);
        end(&transaction);
    }
}

/**
 * @brief Verify MCU error details and sticky malformed, missing, or trailing replies.
 */
static void test_nresp (void) {
    for (unsigned fault = 0; fault < 4; fault++) {
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        mock.replies[0].bytes[3] = 0xB0;
        mock.replies[0].bytes[0] = fault == 1 ? 0xEE : 0x5A;
        mock.nresp_missing = fault == 2;
        mock.nresp_trailing = fault == 3;
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        ed64_pro_status_t status;
        ed64_pro_ll_error_t error = ed64_pro_ll_check_status(&transaction, &status, deadline_ms(1000));
        TEST_CHECK(
            fault == 0   ? error == ED64_PRO_LL_MCU_ERROR
            : fault == 1 ? error == ED64_PRO_LL_FRAME
            : fault == 2 ? is_deadline_error(error)
                         : error == ED64_PRO_LL_TRAILING_DATA
        );
        TEST_CHECK(mock.nresp_requests == (fault == 1 ? 0 : 1));
        const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
        TEST_CHECK(failure->nresp_valid == (fault == 0));
        if (fault == 0) {
            TEST_CHECK(failure->status_valid && failure->status == 0xB0 && failure->nresp == 0xA3);
            const uint8_t wire[] = {0x2B, 0xD4, 0x10, 0xEF, 0x2B, 0xD4, 0x13, 0xEC, 0xB0};
            TEST_CHECK(mock.tx_count == sizeof(wire) && memcmp(mock.tx_bytes, wire, sizeof(wire)) == 0);
        }
        unsigned enqueues = mock.enqueues;
        TEST_CHECK(ed64_pro_ll_check_status(&transaction, &status, deadline_ms(1000)) == error);
        TEST_CHECK(mock.enqueues == enqueues);
        end(&transaction);
    }
}

/**
 * @brief Verify exact FIFO payloads and ACK counts across transfer lengths.
 */
static void test_ack_helpers (void) {
    const size_t lengths[] = {512, 1024, 1536, 4096};
    uint8_t buffer[4096];
    for (unsigned write = 0; write < 2; write++) {
        for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
            power_on();
            identify();
            ed64_pro_ll_transaction_t transaction;
            begin(&transaction);
            mock.transfer_active = true;
            mock.transfer_write = write;
            mock.transfer_lba = 3;
            mock.transfer_length = lengths[i];
            if (write) {
                mock.write_acks = 1;
                rx_push(0);
            }
            memset(buffer, 0xC6, sizeof(buffer));
            ed64_pro_ll_error_t error =
                write ? pro_fifo_write_ack(&transaction, buffer, lengths[i], false, deadline_ms(5000))
                      : pro_fifo_read_ack(&transaction, buffer, lengths[i], deadline_ms(5000));
            TEST_CHECK(error == ED64_PRO_LL_OK);
            TEST_CHECK(memcmp(buffer, mock.sectors + 3 * 512, lengths[i]) == 0);
            TEST_CHECK((write ? mock.write_acks : mock.read_acks) == (lengths[i] + 1023) / 1024);
            TEST_CHECK(!mock.transfer_active && mock.rx_count == 0 && mock.status_requests == 0);
            end(&transaction);
        }
    }
}

/**
 * @brief Verify SD read/write frames, payloads, and block acknowledgements.
 */
static void test_sd_read_write (void) {
    const uint32_t counts[] = {1, 2, 3, 8, 64};
    uint8_t buffer[64 * 512];
    for (unsigned write = 0; write < 2; write++) {
        for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
            power_on();
            identify();
            ed64_pro_ll_transaction_t transaction;
            begin(&transaction);
            uint32_t count = counts[i];
            size_t length = count * 512;
            memset(buffer, 0xA5, sizeof(buffer));
            ed64_pro_ll_error_t error = write ? ed64_pro_ll_sd_write(&transaction, buffer, 3, count, deadline_ms(5000))
                                              : ed64_pro_ll_sd_read(&transaction, buffer, 3, count, deadline_ms(5000));
            TEST_CHECK(error == ED64_PRO_LL_OK);
            TEST_CHECK(memcmp(buffer, mock.sectors + 3 * 512, length) == 0);
            TEST_CHECK(mock.sectors[3 * 512 - 1] == (uint8_t)((3 * 512 - 1) * 37 + 2));
            TEST_CHECK(mock.sectors[(3 + count) * 512] == (uint8_t)((3 + count) * 512 * 37 + 3 + count));
            TEST_CHECK(mock.status_requests == 1 && mock.wre_commands == write);
            TEST_CHECK((write ? mock.write_acks : mock.read_acks) == (length + 1023) / 1024);
            size_t frame_offset = write ? 9 : 0;
            const uint8_t read_frame[] = {0x2B, 0xD4, 0x81, 0x7E, 0x10, 0, 0, 0, 3, 0, 0, 0, 0};
            const uint8_t write_frame[] = {0x2B, 0xD4, 0x81, 0x7E, 0x10, 0, 0, 0, 0, 0, 0, 0, 3};
            TEST_CHECK(memcmp(mock.tx_bytes + frame_offset, write ? write_frame : read_frame, 13) == 0);
            TEST_CHECK(mock.tx_bytes[frame_offset + 13] == (uint8_t)(length >> 24));
            TEST_CHECK(mock.tx_bytes[frame_offset + 14] == (uint8_t)(length >> 16));
            TEST_CHECK(mock.tx_bytes[frame_offset + 15] == (uint8_t)(length >> 8));
            TEST_CHECK(mock.tx_bytes[frame_offset + 16] == (uint8_t)length);
            TEST_CHECK(mock.tx_bytes[frame_offset + 17] == (write ? 0x11 : 0x19));
            TEST_CHECK(mock.tx_bytes[frame_offset + 18] == (write ? 0x19 : 0x11));
            TEST_CHECK(
                mock.tx_bytes[frame_offset + 19] == 0 && mock.tx_bytes[frame_offset + 20] == 0 &&
                mock.tx_bytes[frame_offset + 21] == 0
            );
            TEST_CHECK(mock.rx_count == 0 && !mock.transfer_active);
            end(&transaction);
        }
    }
}

/**
 * @brief Reject invalid SD buffers, sector counts, and LBA ranges before DMA.
 */
static void test_sd_invalid (void) {
    power_on();
    identify();
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    uint8_t byte;
    for (unsigned write = 0; write < 2; write++) {
        const struct {
            void *buffer;
            uint32_t lba;
            uint32_t count;
        } cases[] = {{NULL, 0, 1}, {&byte, 0, 0}, {&byte, 0, 65}, {&byte, UINT32_MAX, 2}, {&byte, UINT32_MAX - 62, 64}};
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            ed64_pro_ll_error_t error =
                write ? ed64_pro_ll_sd_write(
                            &transaction, cases[i].buffer, cases[i].lba, cases[i].count, deadline_ms(5000)
                        )
                      : ed64_pro_ll_sd_read(
                            &transaction, cases[i].buffer, cases[i].lba, cases[i].count, deadline_ms(5000)
                        );
            TEST_CHECK(error == ED64_PRO_LL_ARGUMENT);
        }
    }
    TEST_CHECK(mock.enqueues == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
    end(&transaction);
}

/**
 * @brief Bound each SD block ACK wait and retain the timeout without further DMA.
 */
static void test_sd_ack_timeouts (void) {
    uint8_t buffer[4096];
    for (unsigned write = 0; write < 2; write++) {
        for (unsigned block = 1; block <= 4; block++) {
            power_on();
            identify();
            ed64_pro_ll_transaction_t transaction;
            begin(&transaction);
            mock.stall_read_ack = write ? 0 : block;
            mock.stall_write_ack = write ? block : 0;
            mock.tick_step = TICKS_FROM_MS(1) / 64;
            uint32_t deadline = deadline_ms(1000);
            ed64_pro_ll_error_t error = write ? ed64_pro_ll_sd_write(&transaction, buffer, 0, 8, deadline)
                                              : ed64_pro_ll_sd_read(&transaction, buffer, 0, 8, deadline);
            TEST_CHECK(is_deadline_error(error));
            TEST_CHECK((int32_t)(mock.now - deadline) >= 0 && mock.now - deadline < TICKS_FROM_MS(2));
            TEST_CHECK((write ? mock.write_acks : mock.read_acks) == block);
            TEST_CHECK(mock.status_requests == 0);
            unsigned enqueues = mock.enqueues;
            TEST_CHECK(ed64_pro_ll_sd_read(&transaction, buffer, 0, 1, deadline_ms(5000)) == error);
            TEST_CHECK(mock.enqueues == enqueues);
            end(&transaction);
        }
    }
}

/**
 * @brief Latch SD write framing and status faults without retrying.
 */
static void test_sd_faults (void) {
    uint8_t buffer[1536] = {0};
    for (unsigned fault = 0; fault < 5; fault++) {
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        mock.wre_reply = fault == 0;
        mock.data_trailing = fault == 1;
        mock.replies[0].bytes[0] = fault == 2 ? 0xEE : 0x5A;
        mock.replies[0].bytes[3] = fault == 3 ? 0xB0 : 0;
        mock.replies[0].length = fault == 4 ? 3 : 4;
        mock.tick_step = TICKS_FROM_MS(1) / 64;
        ed64_pro_ll_error_t error = ed64_pro_ll_sd_write(&transaction, buffer, 0, 3, deadline_ms(1000));
        TEST_CHECK(
            fault <= 1   ? error == ED64_PRO_LL_TRAILING_DATA
            : fault == 2 ? error == ED64_PRO_LL_FRAME
            : fault == 3 ? error == ED64_PRO_LL_MCU_ERROR
                         : is_deadline_error(error)
        );
        TEST_CHECK(mock.status_requests == (fault <= 1 ? 0 : 1));
        TEST_CHECK(mock.nresp_requests == (fault == 3 ? 1 : 0));
        TEST_CHECK(mock.transfer_done == (fault == 0 ? 0 : sizeof(buffer)));
        unsigned enqueues = mock.enqueues;
        TEST_CHECK(ed64_pro_ll_sd_write(&transaction, buffer, 0, 3, deadline_ms(5000)) == error);
        TEST_CHECK(mock.enqueues == enqueues);
        end(&transaction);
    }
}

/**
 * @brief Verify direct SD-to-FCI frames, payloads, and byte-swap flag bracketing.
 */
static void test_fci_transfers (void) {
    const uint32_t counts[] = {1, 3, 256};
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        for (unsigned swap = 0; swap < 2; swap++) {
            power_on();
            identify();
            ed64_pro_ll_transaction_t transaction;
            begin(&transaction);
            uint32_t count = counts[i];
            uint32_t fci = 0x10000;
            TEST_ASSERT(
                ed64_pro_ll_set_flags(&transaction, ED64_PRO_FLAG_WRSW, swap, deadline_ms(5000)) == ED64_PRO_LL_OK
            );
            size_t frame_start = mock.tx_count;
            TEST_CHECK(ed64_pro_ll_sd_read_fci(&transaction, fci, 0, count, deadline_ms(5000)) == ED64_PRO_LL_OK);
            TEST_CHECK(mock.fci_transfers == 1 && mock.fci_address == fci && mock.transfer_done == count * 512);
            const uint8_t header[] = {0x2B, 0xD4, 0x81, 0x7E, 0x10, 0, 0, 0, 0, 0, 1, 0, 0};
            TEST_CHECK(memcmp(mock.tx_bytes + frame_start, header, sizeof(header)) == 0);
            TEST_CHECK(mock.tx_bytes[frame_start + 13] == 0 && mock.tx_bytes[frame_start + 14] == (count * 512 >> 16));
            TEST_CHECK(
                mock.tx_bytes[frame_start + 15] == (uint8_t)(count * 512 >> 8) && mock.tx_bytes[frame_start + 16] == 0
            );
            TEST_CHECK(mock.tx_bytes[frame_start + 17] == 0x19 && mock.tx_bytes[frame_start + 18] == 0x13);
            TEST_CHECK(
                mock.tx_bytes[frame_start + 19] == 0 && mock.tx_bytes[frame_start + 20] == 0 &&
                mock.tx_bytes[frame_start + 21] == 0
            );
            for (size_t j = 0; j < count * 512; j++) {
                TEST_ASSERT(mock.fci[fci + j] == mock.sectors[swap ? j ^ 1 : j]);
            }
            TEST_CHECK(mock.read_acks == 0 && mock.write_acks == 0 && mock.status_requests == 1);
            TEST_ASSERT(
                ed64_pro_ll_set_flags(&transaction, ED64_PRO_FLAG_WRSW, 0, deadline_ms(5000)) == ED64_PRO_LL_OK
            );
            TEST_CHECK(mock.flags == 0 && mock.flag_commands == 2 && mock.status_requests == 1);
            end(&transaction);
        }
    }
}

/**
 * @brief Reject invalid FCI ranges and flag arguments before DMA.
 */
static void test_fci_invalid (void) {
    power_on();
    identify();
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    const struct {
        uint32_t fci;
        uint32_t lba;
        uint32_t count;
    } cases[] = {
        {0, 0, 0},
        {0, 0, 257},
        {1, 0, 1},
        {0, UINT32_MAX, 2},
        {ED64_PRO_FCI_ROM_LIMIT, 0, 1},
        {ED64_PRO_FCI_ROM_LIMIT - 512, 0, 2},
        {UINT32_MAX - 511, 0, 1}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        TEST_CHECK(
            ed64_pro_ll_sd_read_fci(&transaction, cases[i].fci, cases[i].lba, cases[i].count, deadline_ms(5000)) ==
            ED64_PRO_LL_ARGUMENT
        );
    }
    TEST_CHECK(ed64_pro_ll_set_flags(&transaction, 0, 0, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_set_flags(&transaction, 2, 2, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_set_flags(&transaction, 1, 2, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.enqueues == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
    end(&transaction);
}

/**
 * @brief Verify flag frames, stable completion, and sticky wait or trailing-data faults.
 */
static void test_flag_frames_and_faults (void) {
    for (unsigned fault = 0; fault < 4; fault++) {
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        mock.sysstat_busy = fault == 1;
        mock.sysstat_unstable = fault == 2;
        mock.flags_trailing = fault == 3;
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        ed64_pro_ll_error_t error = ed64_pro_ll_set_flags(&transaction, 1, 1, deadline_ms(1000));
        TEST_CHECK(
            fault == 0   ? error == ED64_PRO_LL_OK
            : fault == 3 ? error == ED64_PRO_LL_TRAILING_DATA
                         : (error == ED64_PRO_LL_TIMEOUT || is_deadline_error(error))
        );
        const uint8_t frame[] = {0x2B, 0xD4, 0x85, 0x7A, 0x28, 0, 1, 0, 1};
        TEST_CHECK(mock.tx_count == sizeof(frame) && memcmp(mock.tx_bytes, frame, sizeof(frame)) == 0);
        TEST_CHECK(mock.flag_commands == 1 && mock.status_requests == 0 && mock.sysstat_reads >= 2);
        if (fault) {
            unsigned enqueues = mock.enqueues;
            TEST_CHECK(ed64_pro_ll_set_flags(&transaction, 1, 0, deadline_ms(1000)) == error);
            TEST_CHECK(mock.enqueues == enqueues);
        }
        end(&transaction);
    }
}

/**
 * @brief Latch malformed and MCU-error status replies after FCI transfers.
 */
static void test_fci_status_failure (void) {
    for (unsigned malformed = 0; malformed < 2; malformed++) {
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        mock.replies[0].bytes[0] = malformed ? 0xEE : 0x5A;
        mock.replies[0].bytes[3] = 0xB0;
        ed64_pro_ll_error_t error = ed64_pro_ll_sd_read_fci(&transaction, 0, 0, 1, deadline_ms(5000));
        TEST_CHECK(error == (malformed ? ED64_PRO_LL_FRAME : ED64_PRO_LL_MCU_ERROR));
        TEST_CHECK(mock.nresp_requests == (malformed ? 0 : 1));
        unsigned enqueues = mock.enqueues;
        TEST_CHECK(ed64_pro_ll_sd_read_fci(&transaction, 0, 0, 1, deadline_ms(5000)) == error);
        TEST_CHECK(mock.enqueues == enqueues && mock.fci_transfers == 1);
        end(&transaction);
    }
}

/**
 * @brief Return a valid ROM, EEPROM, and RTC launch configuration.
 */
static ed64_pro_dev_cfg_t launch_config (void) {
    return (ed64_pro_dev_cfg_t){
        .brom_type = ED64_PRO_ROM_GPAK,
        .gpak_size = 0x01000200,
        .brm_size = 512,
        .brm_type = ED64_PRO_BRM_EEP4K,
        .rtc_mode = ED64_PRO_RTC_RDO,
    };
}

/**
 * @brief Verify device path encoding for UTF-8 bytes and maximum-length paths.
 */
static void test_dev_path_frames (void) {
    const char *paths[] = {"/game.z64", "/saves/caf\xC3\xA9.sav"};
    for (unsigned type = 0; type < 2; type++) {
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        ed64_pro_path_t path_type = type ? ED64_PRO_PATH_BRM : ED64_PRO_PATH_GPAK;
        TEST_CHECK(ed64_pro_ll_dev_set_path(&transaction, path_type, paths[type], deadline_ms(5000)) == ED64_PRO_LL_OK);
        const uint8_t header[] = {0x2B, 0xD4, 0x85, 0x7A, 0x21, type ? 2 : 0, 0, strlen(paths[type])};
        TEST_CHECK(memcmp(mock.tx_bytes, header, sizeof(header)) == 0);
        TEST_CHECK(memcmp(mock.tx_bytes + sizeof(header), paths[type], strlen(paths[type])) == 0);
        const uint8_t status[] = {0x2B, 0xD4, 0x10, 0xEF};
        TEST_CHECK(memcmp(mock.tx_bytes + sizeof(header) + strlen(paths[type]), status, sizeof(status)) == 0);
        TEST_CHECK(mock.tx_count == sizeof(header) + strlen(paths[type]) + sizeof(status));
        TEST_CHECK(
            strcmp(mock.dev_paths[type], paths[type]) == 0 && mock.path_commands == 1 && mock.status_requests == 1
        );
        end(&transaction);
    }
    power_on();
    identify();
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    char path[ED64_PRO_PATH_MAX + 1];
    memset(path, 'a', sizeof(path));
    path[0] = '/';
    path[ED64_PRO_PATH_MAX] = '\0';
    TEST_CHECK(ed64_pro_ll_dev_set_path(&transaction, ED64_PRO_PATH_GPAK, path, deadline_ms(5000)) == ED64_PRO_LL_OK);
    TEST_CHECK(strcmp(mock.dev_paths[0], path) == 0 && mock.tx_count == ED64_PRO_PATH_MAX + 12);
    size_t frame_bytes = 0;
    unsigned frame_dmas = 0;
    for (size_t i = 0; i < mock.bus_count && frame_bytes < ED64_PRO_PATH_MAX + 8; i++) {
        if (mock.bus[i].write && mock.bus[i].address == ED64_PRO_REG_FIFO) {
            TEST_CHECK(mock.bus[i].length <= 512);
            frame_bytes += mock.bus[i].length / 4;
            frame_dmas++;
        }
    }
    TEST_CHECK(frame_bytes == ED64_PRO_PATH_MAX + 8 && frame_dmas == 9);
    end(&transaction);
}

/**
 * @brief Reject invalid device paths and launch configurations before DMA.
 */
static void test_dev_invalid_arguments (void) {
    power_on();
    identify();
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    const char *paths[] = {NULL, "", "relative/file", "sd:/game.z64"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        TEST_CHECK(
            ed64_pro_ll_dev_set_path(&transaction, ED64_PRO_PATH_GPAK, paths[i], deadline_ms(5000)) ==
            ED64_PRO_LL_ARGUMENT
        );
    }
    char path[ED64_PRO_PATH_MAX + 2];
    memset(path, 'a', sizeof(path));
    path[0] = '/';
    path[sizeof(path) - 1] = '\0';
    TEST_CHECK(
        ed64_pro_ll_dev_set_path(&transaction, ED64_PRO_PATH_GPAK, path, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(
        ed64_pro_ll_dev_set_path(&transaction, (ed64_pro_path_t)1, "/file", deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(
        ed64_pro_ll_dev_set_path(&transaction, (ed64_pro_path_t)3, "/file", deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(
        ed64_pro_ll_dev_set_path(&transaction, (ed64_pro_path_t)4, "/file", deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(ed64_pro_ll_dev_set_cfg(&transaction, NULL, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    for (unsigned invalid = 0; invalid < 12; invalid++) {
        ed64_pro_dev_cfg_t config = launch_config();
        switch (invalid) {
        case 0: config.brom_type = 1; break;
        case 1: config.gpak_size = 0; break;
        case 2: config.gpak_size = 1; break;
        case 3: config.gpak_size = ED64_PRO_FCI_ROM_LIMIT + 512; break;
        case 4: config.brm_type = 7; break;
        case 5: config.brm_size = 0; break;
        case 6: config.rtc_mode = 1; break;
        case 7: config.dd_en = 1; break;
        case 8: config.gpak_wren = 1; break;
        case 9: config.gpak_key = 1; break;
        case 10: config.reserved[0] = 1; break;
        case 11: config.reserved[7] = 1; break;
        }
        TEST_CHECK(ed64_pro_ll_dev_set_cfg(&transaction, &config, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    }
    TEST_CHECK(mock.enqueues == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
    end(&transaction);
}

/**
 * @brief Verify the device configuration frame for every supported save type.
 */
static void test_dev_config_frame (void) {
    const uint32_t sizes[] = {0, 512, 2048, 32768, 98304, 131072, 131072};
    for (unsigned type = 0; type < sizeof(sizes) / sizeof(sizes[0]); type++) {
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        ed64_pro_dev_cfg_t config = launch_config();
        config.brm_type = type;
        config.brm_size = sizes[type];
        TEST_CHECK(ed64_pro_ll_dev_set_cfg(&transaction, &config, deadline_ms(5000)) == ED64_PRO_LL_OK);
        const uint8_t header[] = {0x2B, 0xD4, 0x85, 0x7A, 0x20, 0, 0, 0, 2, 1, 0, 2, 0};
        TEST_CHECK(memcmp(mock.tx_bytes, header, sizeof(header)) == 0 && mock.tx_count == 73);
        TEST_CHECK(
            mock.config_bytes[8] == 0 && mock.config_bytes[9] == (sizes[type] >> 16) &&
            mock.config_bytes[10] == (uint8_t)(sizes[type] >> 8) && mock.config_bytes[11] == 0
        );
        TEST_CHECK(
            mock.config_bytes[12] == 0 && mock.config_bytes[13] == 0 && mock.config_bytes[14] == 0 &&
            mock.config_bytes[15] == type
        );
        TEST_CHECK(
            mock.config_bytes[16] == 0 && mock.config_bytes[17] == 0 && mock.config_bytes[18] == 0 &&
            mock.config_bytes[19] == 2
        );
        for (size_t i = 20; i < sizeof(mock.config_bytes); i++) {
            TEST_CHECK(mock.config_bytes[i] == 0);
        }
        TEST_CHECK(mock.config_commands == 1 && mock.status_requests == 1);
        end(&transaction);
    }
}

/**
 * @brief Verify launch command order and stop at the first failed step.
 */
static void test_dev_order_and_faults (void) {
    for (unsigned failed_step = 0; failed_step <= 4; failed_step++) {
        power_on();
        identify();
        ed64_pro_ll_transaction_t transaction;
        begin(&transaction);
        if (failed_step < 4) {
            mock.replies[failed_step].bytes[3] = 0xB0;
        }
        ed64_pro_dev_cfg_t config = launch_config();
        ed64_pro_ll_error_t error =
            ed64_pro_ll_dev_set_path(&transaction, ED64_PRO_PATH_GPAK, "/game.z64", deadline_ms(5000));
        if (error == ED64_PRO_LL_OK) {
            error = ed64_pro_ll_dev_set_path(&transaction, ED64_PRO_PATH_BRM, "/saves/game.sav", deadline_ms(5000));
        }
        if (error == ED64_PRO_LL_OK) {
            error = ed64_pro_ll_dev_set_cfg(&transaction, &config, deadline_ms(5000));
        }
        if (error == ED64_PRO_LL_OK) {
            error = ed64_pro_ll_dev_start(&transaction, deadline_ms(5000));
        }
        TEST_CHECK(error == (failed_step == 4 ? ED64_PRO_LL_OK : ED64_PRO_LL_MCU_ERROR));
        size_t steps = failed_step == 4 ? 4 : failed_step + 1;
        TEST_CHECK(mock.dev_step_count == steps && memcmp(mock.dev_steps, "GBCS", steps) == 0);
        TEST_CHECK(mock.status_requests == steps && mock.nresp_requests == (failed_step == 4 ? 0 : 1));
        TEST_CHECK(mock.start_commands == (failed_step >= 3 ? 1 : 0));
        if (failed_step == 4) {
            const uint8_t start_frame[] = {0x2B, 0xD4, 0x85, 0x7A, 0x11, 0x2B, 0xD4, 0x10, 0xEF};
            TEST_CHECK(
                memcmp(mock.tx_bytes + mock.tx_count - sizeof(start_frame), start_frame, sizeof(start_frame)) == 0
            );
        } else {
            unsigned enqueues = mock.enqueues;
            TEST_CHECK(ed64_pro_ll_dev_start(&transaction, deadline_ms(5000)) == error);
            TEST_CHECK(mock.enqueues == enqueues);
        }
        end(&transaction);
    }
}

#ifdef ED64_PRO_TEST_FAIL_WRITE_N
/**
 * @brief Verify the injected write timeout preserves partial data and blocks further DMA.
 */
static void test_sd_injected_write_failure (void) {
    power_on();
    identify();
    uint8_t buffer[1536];
    memset(buffer, 0x69, sizeof(buffer));
    ed64_pro_ll_transaction_t transaction;
    begin(&transaction);
    for (unsigned i = 1; i < ED64_PRO_TEST_FAIL_WRITE_N; i++) {
        TEST_ASSERT(ed64_pro_ll_sd_write(&transaction, buffer, 0, 3, deadline_ms(5000)) == ED64_PRO_LL_OK);
    }
    uint8_t original[512];
    memcpy(original, mock.sectors + 1024, sizeof(original));
    memset(buffer, 0xA6, sizeof(buffer));
    TEST_CHECK(ed64_pro_ll_sd_write(&transaction, buffer, 0, 3, deadline_ms(5000)) == ED64_PRO_LL_TIMEOUT);
    TEST_CHECK(memcmp(mock.sectors, buffer, 1024) == 0);
    TEST_CHECK(memcmp(mock.sectors + 1024, original, sizeof(original)) == 0);
    TEST_CHECK(mock.transfer_done == 1024 && mock.status_requests == ED64_PRO_TEST_FAIL_WRITE_N - 1);
    unsigned enqueues = mock.enqueues;
    TEST_CHECK(ed64_pro_ll_sd_read(&transaction, buffer, 0, 1, deadline_ms(5000)) == ED64_PRO_LL_TIMEOUT);
    TEST_CHECK(mock.enqueues == enqueues);
    end(&transaction);
}
#endif

TEST_LIST = {
    {"device paths encode lengths and long UTF8 bytes", test_dev_path_frames},
    {"device paths and configuration whitelist reject before I/O", test_dev_invalid_arguments},
    {"device configuration encodes sixteen BE words", test_dev_config_frame},
    {"device handoff commands stop at first failed step", test_dev_order_and_faults},
    {"SD to FCI byte-exact frames and swap bracketing", test_fci_transfers},
    {"FCI bounds and flags reject without I/O", test_fci_invalid},
    {"flag apply waits are bounded and have no STATUS", test_flag_frames_and_faults},
    {"FCI MCU and malformed status faults latch", test_fci_status_failure},
#ifdef ED64_PRO_TEST_FAIL_WRITE_N
    {"injected nth write fails after first ACK block", test_sd_injected_write_failure},
#endif
    {"bounded frame chunks and zero-I/O rejection", test_frame_limits},
    {"byte-exact whitelisted system info", test_sys_info},
    {"system info and NRESP invalid arguments", test_sys_info_invalid},
    {"system info partial and trailing replies latch", test_sys_info_faults},
    {"NRESP only follows well-formed nonzero status", test_nresp},
    {"ACK helper read and write block boundaries", test_ack_helpers},
    {"raw sectors and ACK boundary transcripts", test_sd_read_write},
    {"raw-sector arguments never submit DMA", test_sd_invalid},
    {"each read/write ACK uses the original deadline", test_sd_ack_timeouts},
    {"ambiguous write faults never retry", test_sd_faults},
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
    {"Pro adapter maps startup through shared errors", test_adapter_initialization},
    {"Pro adapter operations remain unavailable without bus traffic", test_adapter_unavailable_operations},
    {"golden one-shot MCU initialization transcript", test_init_transcript},
    {"sticky failure at every initialization DMA boundary", test_init_failure_boundaries},
    {"inherited versus fresh MCU errors", test_inherited_and_fresh_errors},
    {"partial replies at every initialization status", test_init_partial_statuses},
    {"mailbox requires matching READY samples", test_mailbox_stability},
    {"mailbox stall and instability are bounded", test_mailbox_deadlines},
    {"both handoff cleanup phases are bounded", test_init_cleanup_limits},
    {"SD initialization uses its original five-second budget", test_sd_deadline},
    {"whole attempt budget spans probe and initialization", test_whole_attempt_deadline},
    {"cold-start rejections retain diagnostics and remain retryable", test_cold_start_rejections},
    {"absent and unsupported context never initialize", test_init_absent_and_context},
    {"initialization guard covers the complete handoff", test_init_serialization},
    {"PI timeout retains submitted access context", test_pi_failure_context},
    {"PI acquisition failure retains attempted context", test_pi_attempt_context},
    {"protocol fault has no active PI context", test_non_pi_fault_context},
    {"PI cap timing records full effective budget", test_pi_full_budget_timing},
    {"final FIFO poll timing records clipped phase budget", test_pi_final_poll_timing},
    {NULL, NULL},
};
