#include <libdragon.h>
#include <string.h>

#include <acutest.h>
#include "host_compat.h"

/* Include the production unit to reset ROM-lifetime state between power-on fixtures.
 * No reset or alternate transport implementation is compiled into the ROM. */
#include "../../src/flashcart/ed64_pro/ed64_pro.c"
#include "../../src/flashcart/ed64_pro/ed64_pro_ll.c"

/** @brief Treat an unexpected production boot halt as a transport test failure. */
void debug_assert_func_f (const char *file, int line, const char *func, const char *expression, const char *format, ...) {
    (void)file; (void)line; (void)func; (void)expression; (void)format;
    TEST_ASSERT(false);
    abort();
}

int get_memory_size (void) {
    return 8 * 1024 * 1024;
}

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
    unsigned accesses;
    unsigned dma_reads;
    unsigned dma_writes;
    unsigned io_reads;
    unsigned io_writes;
    bool mailbox_write_pending;
    unsigned waits;
    bool active;
    bool write;
    uint32_t pi;
    size_t length;
    size_t cache_length;
    uint32_t register_value;
    uint32_t ram[ED64_PRO_FIFO_CHUNK];
    char events[262144];
    size_t event_count;
    void (*sdk_hook)(void);
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
    uint32_t voltage_bcd;
    size_t info_reply_limit;
    bool info_trailing;
    unsigned info_requests;
    uint8_t rtc_bytes[ED64_PRO_RTC_SIZE];
    size_t rtc_reply_limit;
    bool rtc_trailing;
    unsigned rtc_get_commands;
    unsigned nresp_requests;
    uint8_t nresp_detail;
    bool nresp_missing;
    bool nresp_trailing;
    uint8_t sectors[256 * 512];
    uint8_t fci[ED64_PRO_IPL_SIZE];
    unsigned fci_transfers;
    uint32_t fci_address;
    uint16_t flags;
    unsigned flag_commands;
    uint32_t sysstat_value;
    bool sysstat_busy;
    bool sysstat_unstable;
    unsigned sysstat_reads;
    bool flags_trailing;
    uint8_t dev_path_frame[ED64_PRO_FRAME_MAX];
    size_t dev_path_expected;
    size_t dev_path_received;
    char dev_paths[3][ED64_PRO_PATH_MAX + 1];
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
 * @brief Supply the host equivalent of the transport memory barrier.
 */
void mock_memory_barrier (void) {
}

/**
 * @brief Enter mock IRQ exclusion for caller-context fixtures.
 */
void disable_interrupts (void) {
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
        frame[4] == 0x21 && (frame[5] == 0 || frame[5] == 2 || frame[5] == 4)
    );
    size_t path_length = (size_t)frame[6] << 8 | frame[7];
    TEST_ASSERT(path_length <= ED64_PRO_PATH_MAX && length == path_length + 8);
    size_t slot = frame[5] / 2;
    memcpy(mock.dev_paths[slot], frame + 8, path_length);
    mock.dev_paths[slot][path_length] = '\0';
    mock.path_commands++;
    TEST_ASSERT(mock.dev_step_count < sizeof(mock.dev_steps));
    mock.dev_steps[mock.dev_step_count++] = slot == 0 ? 'G' : slot == 1 ? 'B' : 'D';
}

/**
 * @brief Execute a synchronous bus access with literal MCU protocol and RDRAM-side effects.
 *
 * Schedule status replies and post-stop cleanup without directly publishing
 * read data into CPU staging; cache invalidation remains a separate operation.
 */
static void execute_bus (void) {
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
            case 0x83:
                TEST_ASSERT(mock.ram[4] == 0x10 && bytes == 5);
                mock.rtc_get_commands++;
                for (size_t i = 0; i < mock.rtc_reply_limit; i++) {
                    rx_push(mock.rtc_bytes[i]);
                }
                if (mock.rtc_trailing) {
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
                    TEST_ASSERT(id == ED64_PRO_INFS_TS_FW || id == ED64_PRO_INFS_MAX_ROM_SIZE ||
                                id == ED64_PRO_INFD_VCC_3V3);
                    uint32_t value = id == ED64_PRO_INFD_VCC_3V3 ? mock.voltage_bcd
                                                                              : mock.info_values[id == ED64_PRO_INFS_TS_FW ? 0 : 1];
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
                        uint32_t target = mock.fci_address;
                        if (target >= ED64_PRO_FCI_IPL) target -= ED64_PRO_FCI_IPL;
                        TEST_ASSERT(target + mock.transfer_length <= sizeof(mock.fci));
                        TEST_ASSERT(mock.transfer_lba * 512 + mock.transfer_length <= sizeof(mock.sectors));
                        for (size_t i = 0; i < mock.transfer_length; i++) {
                            size_t source = mock.transfer_lba * 512 + (mock.flags & 1 ? i ^ 1 : i);
                            mock.fci[target + i] = mock.sectors[source];
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
            mock.ram[0] = mock.sysstat_value | (mock.sysstat_busy ? 1 : 0);
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
    event('F');
}

/**
 * @brief Finish a queued mailbox write before another serialized SDK access.
 */
static void flush_register_write (void) {
    if (mock.mailbox_write_pending) {
        mock.mailbox = 0;
        mock.mailbox_write_pending = false;
        event('F');
    }
}

/**
 * @brief Record one SDK access while allowing an interrupt-style hook during the call.
 */
static void record_access (pi_addr_t pi, unsigned long length, bool write) {
    TEST_ASSERT(mock.irq_depth == 0 && !mock.active);
    TEST_ASSERT((pi & 3u) == 0 && length > 0 && length <= 512 && length % 4 == 0);
    flush_register_write();
    TEST_ASSERT(mock.bus_count < sizeof(mock.bus) / sizeof(mock.bus[0]));
    bus_record_t *record = &mock.bus[mock.bus_count++];
    record->address = pi;
    record->length = length;
    record->write = write;
    memcpy(record->words, mock.ram, length);
    mock.cart_writes += write;
    mock.accesses++;
    mock.pi = pi;
    mock.length = length;
    mock.write = write;
    if (mock.sdk_hook) {
        mock.sdk_hook();
    }
}

/**
 * @brief Perform exact FIFO DMA and return only after its RDRAM-side effects complete.
 */
static void synchronous_dma (const void *ram, pi_addr_t pi, unsigned long length, bool write) {
    TEST_ASSERT(ram == pro_staging && ((uintptr_t)ram & 511u) == 0);
    TEST_ASSERT(pi == ED64_PRO_REG_FIFO);
    TEST_ASSERT(mock.cache_length == ((length + ED64_PRO_CACHE_LINE - 1) & ~(ED64_PRO_CACHE_LINE - 1ul)));
    TEST_ASSERT(mock.event_count > 0 && mock.events[mock.event_count - 1] == (write ? 'C' : 'B'));
    record_access(pi, length, write);
    mock.active = true;
    event(write ? 'W' : 'R');
    execute_bus();
}

void dma_read (void *ram, pi_addr_t pi, unsigned long length) {
    mock.dma_reads++;
    synchronous_dma(ram, pi, length, false);
}

void dma_write (const void *ram, pi_addr_t pi, unsigned long length) {
    mock.dma_writes++;
    synchronous_dma(ram, pi, length, true);
}

uint32_t io_read (pi_addr_t pi) {
    TEST_ASSERT(pi != ED64_PRO_REG_FIFO);
    mock.io_reads++;
    record_access(pi, sizeof(uint32_t), false);
    mock.active = true;
    event('r');
    execute_bus();
    return mock.ram[0];
}

void io_write (pi_addr_t pi, uint32_t value) {
    TEST_ASSERT(pi == ED64_PRO_REG_MBX && value == 0);
    mock.io_writes++;
    mock.ram[0] = value;
    record_access(pi, sizeof(uint32_t), true);
    mock.mailbox_write_pending = true;
    event('w');
}

void dma_wait (void) {
    TEST_ASSERT(mock.irq_depth == 0 && !mock.active);
    mock.waits++;
    flush_register_write();
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
    TEST_ASSERT(!mock.write && mock.pi == ED64_PRO_REG_FIFO);
    TEST_ASSERT(mock.event_count > 0 && mock.events[mock.event_count - 1] == 'F');
    check_cache(ram, length);
    memcpy((void *)ram, mock.ram, length);
    event('I');
}

/**
 * @brief Reset the SDK model and ROM-lifetime driver state with well-formed default replies.
 */
static void power_on (void) {
    ed64_pro_rtc_close();
    test_rtc_hooks = (rtc_hooks_t){0};
    test_rtc_init_calls = 0;
    memset(&mock, 0, sizeof(mock));
    memset(&pro_ll, 0, sizeof(pro_ll));
    memset(&pro_diagnostics, 0, sizeof(pro_diagnostics));
    memset(&pro_runtime, 0, sizeof(pro_runtime));
    memset(&pro_voltage, 0, sizeof(pro_voltage));
    mock.voltage_bcd = 0x0330;
    memset(pro_error_message, 0, sizeof(pro_error_message));
    pro_attempt_start = 0;
    pro_attempt_deadline = 0;
    pro_phase_start = 0;
    memset(pro_staging, 0, sizeof(pro_staging));
    mock.cp0 = C0_STATUS_IE;
    mock.tick_step = 1;
    mock.register_value = 0xED640127u;
    mock.fifo_upper = 0xF00D0000u;
    mock.info_values[0] = 0x90555D4Eu;
    mock.info_values[1] = 0x04000000u;
    mock.info_reply_limit = 8;
    mock.rtc_reply_limit = ED64_PRO_RTC_SIZE;
    const uint8_t rtc_bytes[] = {0x26, 0x11, 0x16, 0x12, 0x34, 0x56, 0x02, 0xA5};
    memcpy(mock.rtc_bytes, rtc_bytes, sizeof(rtc_bytes));
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
 * @brief Complete passive identification and reset transfer traces for an isolated transport check.
 */
static void identify (void) {
    ed64_pro_probe_t probe = ed64_pro_probe();
    TEST_ASSERT(probe.outcome == ED64_PRO_PROBE_IDENTIFIED && probe.id_valid && probe.error == ED64_PRO_LL_OK);
    mock.accesses = 0;
    mock.bus_count = 0;
    mock.event_count = 0;
    mock.events[0] = '\0';
}

/**
 * @brief Read EDID through the public register API without staging or cache work.
 */
static void test_register_read (void) {
    power_on();
    uint32_t output = 0xDEADBEEFu;
    TEST_CHECK(ed64_pro_ll_read_reg(ED64_PRO_REG_EDID, &output, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_CHECK(output == 0xED640127u);
    TEST_CHECK(mock.pi == 0x1F800014u && mock.length == 4);
    TEST_CHECK(mock.cache_length == 0 && mock.io_reads == 1 && mock.dma_reads == 0);
    TEST_CHECK(mock.accesses == 1 && strcmp(mock.events, "rF") == 0);
}

/**
 * @brief Write the mailbox NOP through io_write and finish its queued register access.
 */
static void test_register_write (void) {
    power_on();
    identify();
    memset(pro_staging, 0xFF, sizeof(pro_staging));
    TEST_CHECK(ed64_pro_ll_write_reg(ED64_PRO_REG_MBX, 0, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_CHECK(mock.pi == 0x1F800010u && mock.length == 4 && mock.ram[0] == 0);
    TEST_CHECK(mock.cache_length == 0 && mock.io_writes == 1 && mock.dma_writes == 0);
    TEST_CHECK(mock.waits == 1 && !mock.mailbox_write_pending && strcmp(mock.events, "wF") == 0);
}

/**
 * @brief Reject invalid register/length arguments without DMA or latched faults.
 */
static void test_invalid_input (void) {
    power_on();
    uint32_t output = 42;
    TEST_CHECK(ed64_pro_ll_read_reg(ED64_PRO_REG_EDID, NULL, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(
        ed64_pro_ll_read_reg(ED64_PRO_REG_FIFO, &output, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(ed64_pro_ll_write_reg(ED64_PRO_REG_EDID, 0, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_write_reg(ED64_PRO_REG_MBX, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    const size_t lengths[] = {0, 1, 2, 3, 513, SIZE_MAX};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        TEST_CHECK(
            pro_fifo_transfer(lengths[i], false, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT
        );
    }
    TEST_CHECK(mock.accesses == 0 && mock.event_count == 0 && output == 42);
    TEST_CHECK(ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
}

/**
 * @brief Treat a deadline equal to the current tick as expired before DMA submission.
 */
static void test_exact_deadline (void) {
    power_on();
    mock.tick_step = 0;
    uint32_t output = 42;
    TEST_CHECK(
        ed64_pro_ll_read_reg(ED64_PRO_REG_EDID, &output, mock.now) ==
        ED64_PRO_LL_TIMEOUT
    );
    TEST_CHECK(mock.accesses == 0 && output == 42);
    const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
    TEST_CHECK(failure->code == ED64_PRO_LL_TIMEOUT && failure->transferred == 0);
}

static uint32_t finish_deadline;

/**
 * @brief Advance the mock clock to the configured completion deadline.
 */
static void expire_on_completion (void) {
    mock.now = finish_deadline;
}

/**
 * @brief Keep completed synchronous access distinct from the next protocol deadline check.
 */
static void test_completion_at_deadline (void) {
    power_on();
    mock.tick_step = 0;
    finish_deadline = deadline_ms(2);
    mock.sdk_hook = expire_on_completion;
    uint32_t output = 42;
    TEST_CHECK(
        ed64_pro_ll_read_reg(ED64_PRO_REG_EDID, &output, finish_deadline) == ED64_PRO_LL_OK
    );
    TEST_CHECK(!mock.active && output == mock.register_value);
    const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
    TEST_CHECK(failure->code == ED64_PRO_LL_OK);
    TEST_CHECK(strcmp(mock.events, "rF") == 0);
    TEST_CHECK(ed64_pro_ll_read_reg(ED64_PRO_REG_EDID, &output, finish_deadline) == ED64_PRO_LL_TIMEOUT);
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
    mock.sdk_hook = neighboring_interrupt;
    TEST_CHECK(
        ed64_pro_ll_read_reg(ED64_PRO_REG_EDID, &shared_line.output, deadline_ms(1000)) == ED64_PRO_LL_OK
    );
    TEST_CHECK(shared_line.output == mock.register_value && shared_line.irq_updates == 1);
    TEST_CHECK(shared_line.before == 0x11223344u && shared_line.after == 0x55667788u);
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

            ed64_pro_status_t status;
            TEST_CHECK(
                ed64_pro_ll_write_reg(ED64_PRO_REG_MBX, 0, deadline_ms(1000)) ==
                ED64_PRO_LL_NOT_IDENTIFIED
            );
            TEST_CHECK(ed64_pro_ll_read_status(&status, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
            TEST_CHECK(mock.cart_writes == 0 && mock.bus_count == 1);
        }
    }
}

/**
 * @brief Keep a pre-probe protocol deadline failure indeterminate and sticky without bus traffic.
 */
static void test_probe_deadline (void) {
    power_on();
    mock.tick_step = 0;
    ed64_pro_probe_t probe = ed64_pro_ll_probe(mock.now);
    TEST_CHECK(probe.outcome == ED64_PRO_PROBE_IO_ERROR && probe.error == ED64_PRO_LL_TIMEOUT);
    TEST_CHECK(!probe.id_valid && mock.accesses == 0 && mock.cart_writes == 0);
    probe = ed64_pro_ll_probe(deadline_ms(1000));
    TEST_CHECK(probe.outcome == ED64_PRO_PROBE_IO_ERROR && probe.error == ED64_PRO_LL_TIMEOUT);
    TEST_CHECK(!probe.id_valid && mock.accesses == 0);
}

/**
 * @brief Reject state-changing and FIFO commands until Pro identification completes.
 */
static void test_unidentified_commands (void) {
    power_on();
    ed64_pro_status_t status;
    size_t drained;
    TEST_CHECK(pro_send_command(PRO_CMD_STATUS, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(pro_send_command(PRO_CMD_STOP, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(pro_send_command(PRO_CMD_SD_INIT, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(ed64_pro_ll_read_status(&status, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(ed64_pro_ll_drain(&drained, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(ed64_pro_ll_stop(deadline_ms(5000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(ed64_pro_ll_init_sd(&status, deadline_ms(5000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(mock.accesses == 0 && mock.cart_writes == 0);
    TEST_CHECK(ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
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
        TEST_CHECK(pro_send_command(commands[i], deadline_ms(1000)) == ED64_PRO_LL_OK);
        size_t length = i == 0 ? 16 : 20;
        TEST_CHECK(mock.bus_count == 1 && mock.bus[0].write);
        TEST_CHECK(mock.bus[0].address == 0x1F800000u && mock.bus[0].length == length);
        TEST_CHECK(memcmp(mock.bus[0].words, frames[i], length) == 0);
        TEST_CHECK(mock.cache_length == (i == 0 ? 16 : 32));
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

        ed64_pro_status_t status;
        TEST_CHECK(ed64_pro_ll_read_status(&status, deadline_ms(1000)) == ED64_PRO_LL_OK);
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

            TEST_CHECK(pro_fifo_read(output + offset, length, deadline_ms(1000)) == ED64_PRO_LL_OK);
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
        }
    }
}

/**
 * @brief Accept zero-length FIFO reads but reject null or oversized outputs before DMA.
 */
static void test_fifo_invalid_lengths (void) {
    power_on();
    identify();
    uint8_t output = 42;
    TEST_CHECK(pro_fifo_read(NULL, 0, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_CHECK(pro_fifo_read(NULL, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(pro_fifo_read(&output, 2049, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(pro_fifo_read(&output, SIZE_MAX / 4 + 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(pro_fifo_read(&output, SIZE_MAX, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_read_status(NULL, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_drain(NULL, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.accesses == 0 && output == 42);
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

        size_t drained = SIZE_MAX;
        TEST_CHECK(ed64_pro_ll_drain(&drained, deadline_ms(1000)) == ED64_PRO_LL_OK);
        TEST_CHECK(drained == lengths[n] && mock.rx_count == 0 && mock.rx_consumed == lengths[n]);
        TEST_CHECK(mock.cart_writes == 0);
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

        size_t drained = SIZE_MAX;
        ed64_pro_ll_error_t expected = mode == 2 ? ED64_PRO_LL_DRAIN_LIMIT : ED64_PRO_LL_FIFO_COUNT;
        TEST_CHECK(ed64_pro_ll_drain(&drained, deadline_ms(1000)) == expected);
        TEST_CHECK(mock.rx_consumed == (mode == 2 ? 2048 : 0));
        TEST_CHECK(ed64_pro_ll_get_failure()->transferred == mock.rx_consumed && drained == SIZE_MAX);
        TEST_CHECK(mock.cart_writes == 0);

        unsigned accesses = mock.accesses;
        TEST_CHECK(ed64_pro_ll_drain(&drained, deadline_ms(1000)) == expected);
        TEST_CHECK(mock.accesses == accesses);
    }
}

/**
 * @brief Recognize protocol deadline expiration.
 */
static bool is_deadline_error (ed64_pro_ll_error_t error) {
    return error == ED64_PRO_LL_TIMEOUT;
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

    uint32_t start = mock.now;
    size_t drained = SIZE_MAX;
    TEST_CHECK(is_deadline_error(ed64_pro_ll_drain(&drained, deadline_ms(1000))));
    TEST_CHECK((uint32_t)(mock.now - start) <= TICKS_FROM_MS(101));
    TEST_CHECK(mock.rx_consumed > 0 && mock.rx_consumed < 2048 && drained == SIZE_MAX);
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

        ed64_pro_status_t status;
        memset(&status, 0xCD, sizeof(status));
        uint32_t start = mock.now;
        TEST_CHECK(is_deadline_error(ed64_pro_ll_read_status(&status, deadline_ms(1000))));
        TEST_CHECK((uint32_t)(mock.now - start) <= TICKS_FROM_MS(1001));
        TEST_CHECK(status.key == 0xCD && status.mcu_status == 0xCD);
        TEST_CHECK(ed64_pro_ll_get_failure()->transferred == length && mock.rx_consumed == length);
        TEST_CHECK(mock.status_requests == 1);
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

        ed64_pro_status_t status;
        memset(&status, 0xCD, sizeof(status));
        ed64_pro_ll_error_t expected = mode == 0 ? ED64_PRO_LL_FRAME : ED64_PRO_LL_TRAILING_DATA;
        TEST_CHECK(ed64_pro_ll_read_status(&status, deadline_ms(1000)) == expected);
        TEST_CHECK(status.key == 0xCD && status.mcu_status == 0xCD);
        TEST_CHECK(mock.rx_consumed == (mode == 2 ? 0 : 4) && mock.cart_writes == (mode == 2 ? 0 : 1));
        unsigned accesses = mock.accesses;
        size_t drained = SIZE_MAX;
        TEST_CHECK(ed64_pro_ll_read_status(&status, deadline_ms(1000)) == expected);
        TEST_CHECK(ed64_pro_ll_drain(&drained, deadline_ms(1000)) == expected);
        TEST_CHECK(mock.accesses == accesses && drained == SIZE_MAX);
    }
}

/**
 * @brief Preserve caller deadlines despite slow byte arrival and counter wrap.
 */
static void test_slow_partial_status (void) {
    const uint32_t budgets[] = {1000, 600};
    for (size_t i = 0; i < 2; i++) {
        power_on();
        identify();
        mock.now = UINT32_MAX - TICKS_FROM_MS(250);
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        mock.reply_interval = TICKS_FROM_MS(400);

        ed64_pro_status_t status;
        memset(&status, 0xCD, sizeof(status));
        uint32_t start = mock.now;
        TEST_CHECK(is_deadline_error(ed64_pro_ll_read_status(&status, deadline_ms(budgets[i]))));
        TEST_CHECK((uint32_t)(mock.now - start) <= TICKS_FROM_MS(budgets[i] + 1));
        TEST_CHECK(ed64_pro_ll_get_failure()->transferred == (i == 0 ? 2 : 1));
        TEST_CHECK(status.key == 0xCD && mock.status_requests == 1);
    }
}

/**
 * @brief Exhaust the protocol budget during the first completed FIFO chunk.
 */
static void expire_after_chunk (void) {
    if (mock.pi == ED64_PRO_REG_FIFO && !mock.write && mock.length == 512) {
        mock.now += TICKS_FROM_MS(1000);
    }
}

/**
 * @brief Retain a completed chunk and leave the final FIFO byte untouched after timeout.
 */
static void test_partial_fifo_timeout (void) {
    power_on();
    identify();
    for (size_t i = 0; i < 129; i++) {
        rx_push((uint8_t)i);
    }
    mock.sdk_hook = expire_after_chunk;
    uint8_t output[129];
    memset(output, 0xCD, sizeof(output));
    TEST_CHECK(pro_fifo_read(output, sizeof(output), deadline_ms(1000)) == ED64_PRO_LL_TIMEOUT);
    TEST_CHECK(ed64_pro_ll_get_failure()->transferred == 128 && mock.rx_consumed == 128);
    for (size_t i = 0; i < 128; i++) {
        TEST_CHECK(output[i] == (uint8_t)i);
    }
    TEST_CHECK(output[128] == 0xCD && mock.rx_count == 1 && !mock.active);
    unsigned accesses = mock.accesses;

    TEST_CHECK(pro_fifo_read(output, sizeof(output), deadline_ms(1000)) == ED64_PRO_LL_TIMEOUT);
    TEST_CHECK(mock.accesses == accesses && mock.rx_consumed == 128);
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
    TEST_CHECK(ed64_pro_ll_get_metrics()->rx_peak == 4);
    for (ed64_pro_phase_t phase = ED64_PRO_PHASE_PROBE; phase < ED64_PRO_PHASE_READY; phase++) {
        TEST_CHECK(diagnostics->phase_ticks[phase] > 0);
    }
    TEST_CHECK(diagnostics->attempt_ticks < TICKS_FROM_MS(15000));
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "transport ready") != NULL);
    TEST_CHECK(mock.irq_depth == 0);

    ed64_pro_diagnostics_t before = *diagnostics;
    size_t bus_count = mock.bus_count;
    size_t events = mock.event_count;
    mock.now += TICKS_FROM_MS(20000);
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_OK);
    TEST_CHECK(mock.bus_count == bus_count && mock.event_count == events);
    TEST_CHECK(memcmp(&before, diagnostics, sizeof(before)) == 0);
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
        TEST_CHECK(diagnostics->failure.code == expected);
        TEST_CHECK(diagnostics->inherited_status_valid == (status_index != 0));
        TEST_CHECK(mock.stop_commands == (status_index == 0 ? 0 : 1));
        TEST_CHECK(mock.sd_commands == (status_index == 2 ? 1 : 0));
        if (status_index) {
            TEST_CHECK(diagnostics->status_valid && diagnostics->status.mcu_status == 0xB0);
            TEST_CHECK(strstr(ed64_pro_get_error_message(), "MCU status 0xB0") != NULL);
        }
        unsigned accesses = mock.accesses;
        mock.replies[status_index].bytes[0] = 0x5A;
        mock.replies[status_index].bytes[3] = 0;
        TEST_CHECK(ed64_pro_init_transport() == expected && mock.accesses == accesses);
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
        unsigned accesses = mock.accesses;
        TEST_CHECK(ed64_pro_init_transport() == error && mock.accesses == accesses);
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
            TEST_CHECK(mock.accesses == 1 && mock.cart_writes == 0);
        }
    }
}

/**
 * @brief Cache confirmed absence without MCU writes.
 */
static void test_init_absent (void) {
    power_on();
    mock.register_value = 0xED640025u;
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(ed64_pro_get_diagnostics()->state == ED64_PRO_ABSENT && mock.accesses == 1);
    TEST_CHECK(ed64_pro_get_diagnostics()->probe.error == ED64_PRO_LL_OK);
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "Cartridge is not an EverDrive-64 Pro") != NULL);
    TEST_CHECK(mock.accesses == 1);
    TEST_CHECK(ed64_pro_init_transport() == ED64_PRO_LL_NOT_IDENTIFIED && mock.accesses == 1);
    TEST_CHECK(mock.cart_writes == 0);
}

/**
 * @brief Avoid attributing a protocol-only fault to the last successful PI access.
 */
static void test_protocol_fault_context (void) {
    power_on();
    identify();
    mock.replies[0].bytes[0] = 0;

    ed64_pro_status_t status;
    memset(&status, 0xAA, sizeof(status));
    TEST_CHECK(ed64_pro_ll_read_status(&status, deadline_ms(1000)) == ED64_PRO_LL_FRAME);
    const ed64_pro_ll_failure_t *failure = ed64_pro_ll_get_failure();
    TEST_CHECK(failure->code == ED64_PRO_LL_FRAME && failure->transferred == 4);
    ed64_pro_ll_failure_t first = *failure;
    unsigned accesses = mock.accesses;
    TEST_CHECK(ed64_pro_ll_read_status(&status, deadline_ms(1000)) == ED64_PRO_LL_FRAME);
    TEST_CHECK(memcmp(&first, failure, sizeof(first)) == 0 && mock.accesses == accesses);
    TEST_CHECK(mock.accesses == 4 && mock.rx_consumed == 4 && status.key == 0xAA);
}

/**
 * @brief Map transport success and sticky failures through the normal flashcart adapter.
 */
static void test_adapter_initialization (void) {
    flashcart_t *adapter = ed64_pro_get_flashcart();
    for (unsigned failed = 0; failed < 2; failed++) {
        handoff_fixture();
        if (failed) {
            mock.replies[0].bytes[0] = 0x5B;
        }
        flashcart_err_t expected = failed ? FLASHCART_ERR_INT : FLASHCART_OK;
        TEST_CHECK(adapter->init() == expected);
        const ed64_pro_diagnostics_t *diagnostics = ed64_pro_get_diagnostics();
        TEST_CHECK(diagnostics->state == (failed ? ED64_PRO_FAILED : ED64_PRO_READY));
        unsigned accesses = mock.accesses;
        TEST_CHECK(
            failed ? strstr(adapter->get_error_message(expected), "startup failed") != NULL
                   : adapter->get_error_message(expected) == NULL
        );
        TEST_CHECK(adapter->get_error_message(FLASHCART_ERR_SD_CARD) != NULL);
        TEST_CHECK(adapter->get_error_message(FLASHCART_OK) == NULL);
        TEST_CHECK(adapter->init() == expected && mock.accesses == accesses);
    }
}

/**
 * @brief Keep cached queries and unavailable Pro callbacks free of bus traffic after a protocol failure.
 */
static void test_adapter_unavailable_operations (void) {
    for (unsigned failed = 0; failed < 2; failed++) {
        handoff_fixture();
        flashcart_t *adapter = ed64_pro_get_flashcart();
        if (failed) {
            mock.replies[0].bytes[0] = 0x5B;
        }
        (void)adapter->init();
        unsigned accesses = mock.accesses;
        for (flashcart_features_t feature = FLASHCART_FEATURE_64DD; feature <= FLASHCART_FEATURE_ROM_REBOOT_FAST;
             feature++) {
            bool supported = feature == FLASHCART_FEATURE_AUTO_CIC || feature == FLASHCART_FEATURE_AUTO_REGION
                || feature == FLASHCART_FEATURE_SAVE_WRITEBACK || feature == FLASHCART_FEATURE_RTC
                || feature == FLASHCART_FEATURE_64DD || feature == FLASHCART_FEATURE_DIAGNOSTIC_DATA;
            TEST_CHECK(adapter->has_feature(feature) == supported);
        }
        flashcart_firmware_version_t version = adapter->get_firmware_version();
        TEST_CHECK(version.major == (failed ? 0 : 26) && version.minor == (failed ? 0 : 1014) && version.revision == 0);
        TEST_CHECK(adapter->load_rom("sd:/test.z64", NULL) == FLASHCART_ERR_LOAD);
        TEST_CHECK(adapter->load_file("sd:/test.bin", 0, 0) == FLASHCART_ERR_LOAD);
        TEST_CHECK(adapter->load_save("sd:/test.sra") == FLASHCART_ERR_ARGS);
        TEST_CHECK(adapter->set_save_type(FLASHCART_SAVE_TYPE_SRAM_256KBIT) == FLASHCART_ERR_LOAD);
        TEST_CHECK(adapter->deinit != NULL && adapter->load_64dd_ipl != NULL && adapter->load_64dd_disk != NULL);
        TEST_CHECK(adapter->load_64dd_disks != NULL && adapter->get_button_state != NULL && adapter->get_voltage != NULL);
        TEST_CHECK(adapter->get_voltage_temperature == NULL && adapter->set_save_writeback == NULL);
        TEST_CHECK(adapter->set_next_boot_mode == NULL && mock.accesses == accesses);
    }
}

int cart_type;
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
    (void)prefix; (void)disk; (void)flags;
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
    mock.raw_frames = true;
    uint8_t payload[ED64_PRO_FRAME_MAX];
    for (size_t i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i * 19);
    }
    TEST_CHECK(
        pro_send_frame(PRO_CMD_SYS, 0x10, payload, ED64_PRO_FRAME_MAX - 4, deadline_ms(1000)) ==
        ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(pro_send_frame(PRO_CMD_SYS, 0x10, NULL, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.accesses == 0);
    TEST_CHECK(
        pro_send_frame(PRO_CMD_SYS, 0x10, payload, ED64_PRO_FRAME_MAX - 5, deadline_ms(1000)) ==
        ED64_PRO_LL_OK
    );
    TEST_CHECK(mock.tx_count == ED64_PRO_FRAME_MAX && mock.bus_count == ED64_PRO_FRAME_MAX / ED64_PRO_FIFO_CHUNK);
    const uint8_t header[] = {0x2B, 0xD4, 0x84, 0x7B, 0x10};
    TEST_CHECK(memcmp(mock.tx_bytes, header, sizeof(header)) == 0);
    TEST_CHECK(memcmp(mock.tx_bytes + sizeof(header), payload, ED64_PRO_FRAME_MAX - sizeof(header)) == 0);
    for (size_t i = 0; i < mock.bus_count; i++) {
        TEST_CHECK(mock.bus[i].length == ED64_PRO_FIFO_CHUNK * 4 && mock.bus[i].write);
    }
}

/**
 * @brief Verify byte-exact physical RTC observation framing.
 */
static void test_rtc_frames (void) {
    power_on();
    identify();
    uint8_t output[ED64_PRO_RTC_SIZE];
    const uint8_t initial[] = {0x26, 0x11, 0x16, 0x12, 0x34, 0x56, 0x02, 0xA5};
    TEST_CHECK(ed64_pro_ll_rtc_get(output, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_CHECK(memcmp(output, initial, sizeof(output)) == 0);
    const uint8_t get_frame[] = {0x2B, 0xD4, 0x83, 0x7C, 0x10};
    TEST_CHECK(mock.tx_count == sizeof(get_frame) && memcmp(mock.tx_bytes, get_frame, sizeof(get_frame)) == 0);
    TEST_CHECK(mock.bus[1].write && mock.bus[1].length == 20);
    TEST_CHECK(mock.rtc_get_commands == 1 && mock.status_requests == 0);
}

/**
 * @brief Reject invalid arguments and unidentified hardware before issuing RTC commands.
 */
static void test_rtc_rejections (void) {
    power_on();
    uint8_t bytes[ED64_PRO_RTC_SIZE] = {0};
    TEST_CHECK(ed64_pro_ll_rtc_get(bytes, deadline_ms(1000)) == ED64_PRO_LL_NOT_IDENTIFIED);
    TEST_CHECK(mock.accesses == 0);
    identify();
    TEST_CHECK(ed64_pro_ll_rtc_get(NULL, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.accesses == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
}

/**
 * @brief Preserve output and sticky failures for partial, trailing, and unexpected RTC replies.
 */
static void test_rtc_reply_faults (void) {
    for (unsigned fault = 0; fault < 4; fault++) {
        power_on();
        identify();
        uint8_t output[ED64_PRO_RTC_SIZE];
        memset(output, 0xAA, sizeof(output));
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        mock.rtc_reply_limit = fault == 0 ? 0 : fault == 1 ? 7 : ED64_PRO_RTC_SIZE;
        mock.rtc_trailing = fault == 2;
        if (fault == 3) {
            rx_push(0x00); /* Unsolicited data must not be consumed as a fresh reply. */
        }
        ed64_pro_ll_error_t error = ed64_pro_ll_rtc_get(output, deadline_ms(1000));
        TEST_CHECK(fault < 2 ? is_deadline_error(error) : error == ED64_PRO_LL_TRAILING_DATA);
        for (size_t i = 0; i < sizeof(output); i++) {
            TEST_CHECK(output[i] == 0xAA);
        }
        TEST_CHECK(mock.rtc_get_commands == (fault == 3 ? 0u : 1u));
        unsigned accesses = mock.accesses;
        TEST_CHECK(ed64_pro_ll_rtc_get(output, deadline_ms(1000)) == error);
        TEST_CHECK(mock.accesses == accesses);
    }
}

/**
 * @brief Verify byte-exact system-info requests and decoded values.
 */
static void test_sys_info (void) {
    for (size_t count = 1; count <= 2; count++) {
        power_on();
        identify();
        const uint32_t ids[] = {ED64_PRO_INFS_TS_FW, ED64_PRO_INFS_MAX_ROM_SIZE};
        uint32_t values[2] = {0, 0xDEADBEEF};
        TEST_CHECK(ed64_pro_ll_get_sys_info(ids, values, count, deadline_ms(1000)) == ED64_PRO_LL_OK);
        TEST_CHECK(values[0] == 0x90555D4E);
        TEST_CHECK(values[1] == (count == 2 ? 0x04000000 : 0xDEADBEEF));
        const uint8_t frame[] = {0x2B, 0xD4, 0x84, 0x7B, 0x10, count, 0, 0, 0, 6, 0, 0, 0, 9};
        TEST_CHECK(mock.tx_count == 6 + count * 4 && memcmp(mock.tx_bytes, frame, mock.tx_count) == 0);
        TEST_CHECK(mock.info_requests == 1 && mock.rx_count == 0 && mock.status_requests == 0);
    }
}

/** @brief Send the exact one-ID 3.3 V GET_INF and retain its fixed two-ID limit. */
static void test_voltage_get_info (void) {
    power_on();
    identify();
    uint16_t output = 0xBEEF;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_OK && output == 3300);
    const uint8_t frame[] = {0x2B, 0xD4, 0x84, 0x7B, 0x10, 1, 0, 0, 0, 139};
    TEST_CHECK(mock.tx_count == sizeof(frame) && memcmp(mock.tx_bytes, frame, sizeof(frame)) == 0);
    TEST_CHECK(mock.info_requests == 1 && mock.status_requests == 0 && mock.nresp_requests == 0);
    TEST_CHECK(mock.rx_count == 0);
    unsigned accesses = mock.accesses;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_OK && output == 3300 && mock.accesses == accesses);
    mock.now = pro_voltage.ticks + TICKS_FROM_MS(PRO_VOLTAGE_REFRESH_MS);
    mock.voltage_bcd = 0x0328;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_OK && output == 3280 && mock.info_requests == 2);
    TEST_CHECK(mock.tx_count == 2 * sizeof(frame) && memcmp(mock.tx_bytes + sizeof(frame), frame, sizeof(frame)) == 0);
}

/** @brief A truncated, missing or trailing voltage reply leaves output and terminal failure intact. */
static void test_voltage_get_info_faults (void) {
    for (unsigned after_sample = 0; after_sample < 2; after_sample++) {
        for (size_t length = 0; length <= 4; length++) {
            power_on();
            identify();
            uint16_t output = 0xBEEF;
            if (after_sample) {
                TEST_ASSERT(pro_get_voltage(&output) == FLASHCART_OK);
                mock.now = pro_voltage.ticks + TICKS_FROM_MS(PRO_VOLTAGE_REFRESH_MS);
            }
            mock.info_reply_limit = length;
            mock.info_trailing = length == 4;
            mock.tick_step = TICKS_FROM_MS(1) / 8;
            output = 0xBEEF;
            TEST_CHECK(pro_get_voltage(&output) == FLASHCART_ERR_INT && output == 0xBEEF);
            ed64_pro_ll_failure_t failure = *ed64_pro_ll_get_failure();
            TEST_CHECK(length == 4 ? failure.code == ED64_PRO_LL_TRAILING_DATA : is_deadline_error(failure.code));
            TEST_CHECK(failure.transferred == length);
            TEST_CHECK(mock.info_requests == after_sample + 1);
            unsigned accesses = mock.accesses;
            /* Even an unexpired successful cache cannot mask the terminal fault. */
            pro_voltage.result = FLASHCART_OK;
            TEST_CHECK(pro_get_voltage(&output) == FLASHCART_ERR_INT && output == 0xBEEF);
            bool pressed = true;
            TEST_CHECK(pro_get_button_state(&pressed) == FLASHCART_ERR_INT && pressed);
            TEST_CHECK(mock.accesses == accesses);
            TEST_CHECK(memcmp(&failure, ed64_pro_ll_get_failure(), sizeof(failure)) == 0);
        }
    }
}

/** @brief Read the full-width button register and preserve arguments/output on failure. */
static void test_button_register (void) {
    power_on();
    identify();
    const uint32_t statuses[] = {0, 0xFFFF, 0xFFFEFFFF, 0x00010000, 0xFFFFFFFF, 0x000100A8, 0xA8};
    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
        mock.sysstat_value = statuses[i];
        bool pressed = !(statuses[i] & ED64_PRO_SYSSTAT_BTN);
        TEST_CHECK(pro_get_button_state(&pressed) == FLASHCART_OK);
        TEST_CHECK(pressed == ((statuses[i] & ED64_PRO_SYSSTAT_BTN) != 0));
        TEST_CHECK(mock.bus[i].address == ED64_PRO_REG_SYSSTAT && mock.bus[i].length == sizeof(uint32_t));
        TEST_CHECK(!mock.bus[i].write && mock.sysstat_reads == i + 1);
        TEST_CHECK(mock.tx_count == 0 && mock.dma_reads == 0 && mock.dma_writes == 0);
    }
    unsigned accesses = mock.accesses;
    TEST_CHECK(pro_get_button_state(NULL) == FLASHCART_ERR_ARGS);
    TEST_CHECK(mock.accesses == accesses);
    pro_fail(ED64_PRO_LL_TIMEOUT);
    bool pressed = true;
    TEST_CHECK(pro_get_button_state(&pressed) == FLASHCART_ERR_INT && pressed);
    TEST_CHECK(mock.accesses == accesses);
}

/**
 * @brief Attribute preexisting FIFO data to the new request, without stale receive progress.
 */
static void test_new_request_progress (void) {
    power_on();
    identify();
    ed64_pro_status_t status;
    TEST_ASSERT(ed64_pro_ll_read_status(&status, deadline_ms(1000)) == ED64_PRO_LL_OK);
    TEST_ASSERT(pro_ll.transferred == sizeof(status));
    rx_push(0xEE);
    unsigned writes = mock.cart_writes;
    const uint32_t id = ED64_PRO_INFS_TS_FW;
    uint32_t value = 0xDEADBEEF;
    TEST_CHECK(ed64_pro_ll_get_sys_info(&id, &value, 1, deadline_ms(1000)) == ED64_PRO_LL_TRAILING_DATA);
    TEST_CHECK(ed64_pro_ll_get_failure()->transferred == 0);
    TEST_CHECK(mock.cart_writes == writes && mock.info_requests == 0 && mock.rx_count == 1);
    TEST_CHECK(value == 0xDEADBEEF);
}

/**
 * @brief Reject invalid system-info and NRESP arguments before DMA.
 */
static void test_sys_info_invalid (void) {
    power_on();
    identify();
    uint32_t ids[] = {ED64_PRO_INFS_TS_FW, 8};
    uint32_t values[2] = {0};
    TEST_CHECK(ed64_pro_ll_get_sys_info(NULL, values, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_get_sys_info(ids, NULL, 1, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_get_sys_info(ids, values, 0, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_get_sys_info(ids, values, 3, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_get_sys_info(ids, values, 2, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    uint8_t detail;
    TEST_CHECK(ed64_pro_ll_read_nresp(0xB0, &detail, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.accesses == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
}

/**
 * @brief Keep outputs unchanged and latch truncated or trailing system-info replies.
 */
static void test_sys_info_faults (void) {
    for (unsigned trailing = 0; trailing < 2; trailing++) {
        power_on();
        identify();
        const uint32_t ids[] = {ED64_PRO_INFS_TS_FW, ED64_PRO_INFS_MAX_ROM_SIZE};
        uint32_t values[] = {0xDEADBEEF, 0xDEADBEEF};
        mock.info_reply_limit = trailing ? 8 : 7;
        mock.info_trailing = trailing;
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        ed64_pro_ll_error_t error = ed64_pro_ll_get_sys_info(ids, values, 2, deadline_ms(1000));
        TEST_CHECK(trailing ? error == ED64_PRO_LL_TRAILING_DATA : is_deadline_error(error));
        TEST_CHECK(values[0] == 0xDEADBEEF && values[1] == 0xDEADBEEF);
        TEST_CHECK(mock.info_requests == 1 && mock.nresp_requests == 0);
        unsigned accesses = mock.accesses;
        TEST_CHECK(ed64_pro_ll_get_sys_info(ids, values, 2, deadline_ms(1000)) == error);
        TEST_CHECK(mock.accesses == accesses);
    }
}

/**
 * @brief Verify MCU error details and sticky malformed, missing, or trailing replies.
 */
static void test_nresp (void) {
    for (unsigned fault = 0; fault < 4; fault++) {
        power_on();
        identify();
        mock.replies[0].bytes[3] = 0xB0;
        mock.replies[0].bytes[0] = fault == 1 ? 0xEE : 0x5A;
        mock.nresp_missing = fault == 2;
        mock.nresp_trailing = fault == 3;
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        ed64_pro_status_t status;
        ed64_pro_ll_error_t error = ed64_pro_ll_check_status(&status, deadline_ms(1000));
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
        unsigned accesses = mock.accesses;
        TEST_CHECK(ed64_pro_ll_check_status(&status, deadline_ms(1000)) == error);
        TEST_CHECK(mock.accesses == accesses);
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
                write ? pro_fifo_write_ack(buffer, lengths[i], false, deadline_ms(5000))
                      : pro_fifo_read_ack(buffer, lengths[i], deadline_ms(5000));
            TEST_CHECK(error == ED64_PRO_LL_OK);
            TEST_CHECK(memcmp(buffer, mock.sectors + 3 * 512, lengths[i]) == 0);
            TEST_CHECK((write ? mock.write_acks : mock.read_acks) == (lengths[i] + 1023) / 1024);
            TEST_CHECK(!mock.transfer_active && mock.rx_count == 0 && mock.status_requests == 0);
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
            uint32_t count = counts[i];
            size_t length = count * 512;
            memset(buffer, 0xA5, sizeof(buffer));
            ed64_pro_ll_error_t error = write ? ed64_pro_ll_sd_write(buffer, 3, count, deadline_ms(5000))
                                              : ed64_pro_ll_sd_read(buffer, 3, count, deadline_ms(5000));
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
        }
    }
}

/**
 * @brief Reject invalid SD buffers, sector counts, and LBA ranges before DMA.
 */
static void test_sd_invalid (void) {
    power_on();
    identify();
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
                            cases[i].buffer, cases[i].lba, cases[i].count, deadline_ms(5000)
                        )
                      : ed64_pro_ll_sd_read(
                            cases[i].buffer, cases[i].lba, cases[i].count, deadline_ms(5000)
                        );
            TEST_CHECK(error == ED64_PRO_LL_ARGUMENT);
        }
    }
    TEST_CHECK(mock.accesses == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
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
            mock.stall_read_ack = write ? 0 : block;
            mock.stall_write_ack = write ? block : 0;
            mock.tick_step = TICKS_FROM_MS(1) / 64;
            uint32_t deadline = deadline_ms(1000);
            ed64_pro_ll_error_t error = write ? ed64_pro_ll_sd_write(buffer, 0, 8, deadline)
                                              : ed64_pro_ll_sd_read(buffer, 0, 8, deadline);
            TEST_CHECK(is_deadline_error(error));
            TEST_CHECK((int32_t)(mock.now - deadline) >= 0 && mock.now - deadline < TICKS_FROM_MS(2));
            TEST_CHECK((write ? mock.write_acks : mock.read_acks) == block);
            TEST_CHECK(mock.status_requests == 0);
            unsigned accesses = mock.accesses;
            TEST_CHECK(ed64_pro_ll_sd_read(buffer, 0, 1, deadline_ms(5000)) == error);
            TEST_CHECK(mock.accesses == accesses);
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
        mock.wre_reply = fault == 0;
        mock.data_trailing = fault == 1;
        mock.replies[0].bytes[0] = fault == 2 ? 0xEE : 0x5A;
        mock.replies[0].bytes[3] = fault == 3 ? 0xB0 : 0;
        mock.replies[0].length = fault == 4 ? 3 : 4;
        mock.tick_step = TICKS_FROM_MS(1) / 64;
        ed64_pro_ll_error_t error = ed64_pro_ll_sd_write(buffer, 0, 3, deadline_ms(1000));
        TEST_CHECK(
            fault <= 1   ? error == ED64_PRO_LL_TRAILING_DATA
            : fault == 2 ? error == ED64_PRO_LL_FRAME
            : fault == 3 ? error == ED64_PRO_LL_MCU_ERROR
                         : is_deadline_error(error)
        );
        TEST_CHECK(mock.status_requests == (fault <= 1 ? 0 : 1));
        TEST_CHECK(mock.nresp_requests == (fault == 3 ? 1 : 0));
        TEST_CHECK(mock.transfer_done == (fault == 0 ? 0 : sizeof(buffer)));
        unsigned accesses = mock.accesses;
        TEST_CHECK(ed64_pro_ll_sd_write(buffer, 0, 3, deadline_ms(5000)) == error);
        TEST_CHECK(mock.accesses == accesses);
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
            uint32_t count = counts[i];
            uint32_t fci = 0x10000;
            TEST_ASSERT(
                ed64_pro_ll_set_flags(ED64_PRO_FLAG_WRSW, swap, deadline_ms(5000)) == ED64_PRO_LL_OK
            );
            size_t frame_start = mock.tx_count;
            TEST_CHECK(ed64_pro_ll_sd_read_fci(fci, 0, count, deadline_ms(5000)) == ED64_PRO_LL_OK);
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
                ed64_pro_ll_set_flags(ED64_PRO_FLAG_WRSW, 0, deadline_ms(5000)) == ED64_PRO_LL_OK
            );
            TEST_CHECK(mock.flags == 0 && mock.flag_commands == 2 && mock.status_requests == 1);
        }
    }
}

/**
 * @brief Reject invalid FCI ranges and flag arguments before DMA.
 */
static void test_fci_invalid (void) {
    power_on();
    identify();
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
            ed64_pro_ll_sd_read_fci(cases[i].fci, cases[i].lba, cases[i].count, deadline_ms(5000)) ==
            ED64_PRO_LL_ARGUMENT
        );
    }
    TEST_CHECK(ed64_pro_ll_set_flags(0, 0, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_set_flags(2, 2, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(ed64_pro_ll_set_flags(1, 2, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.accesses == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
}

/**
 * @brief Verify flag frames, stable completion, and sticky wait or trailing-data faults.
 */
static void test_flag_frames_and_faults (void) {
    for (unsigned fault = 0; fault < 4; fault++) {
        power_on();
        identify();
        mock.sysstat_busy = fault == 1;
        mock.sysstat_unstable = fault == 2;
        mock.flags_trailing = fault == 3;
        mock.tick_step = TICKS_FROM_MS(1) / 8;
        ed64_pro_ll_error_t error = ed64_pro_ll_set_flags(1, 1, deadline_ms(1000));
        TEST_CHECK(
            fault == 0   ? error == ED64_PRO_LL_OK
            : fault == 3 ? error == ED64_PRO_LL_TRAILING_DATA
                         : (error == ED64_PRO_LL_TIMEOUT || is_deadline_error(error))
        );
        const uint8_t frame[] = {0x2B, 0xD4, 0x85, 0x7A, 0x28, 0, 1, 0, 1};
        TEST_CHECK(mock.tx_count == sizeof(frame) && memcmp(mock.tx_bytes, frame, sizeof(frame)) == 0);
        TEST_CHECK(mock.flag_commands == 1 && mock.status_requests == 0 && mock.sysstat_reads >= 2);
        if (fault) {
            unsigned accesses = mock.accesses;
            TEST_CHECK(ed64_pro_ll_set_flags(1, 0, deadline_ms(1000)) == error);
            TEST_CHECK(mock.accesses == accesses);
        }
    }
}

/**
 * @brief Latch malformed and MCU-error status replies after FCI transfers.
 */
static void test_fci_status_failure (void) {
    for (unsigned malformed = 0; malformed < 2; malformed++) {
        power_on();
        identify();
        mock.replies[0].bytes[0] = malformed ? 0xEE : 0x5A;
        mock.replies[0].bytes[3] = 0xB0;
        ed64_pro_ll_error_t error = ed64_pro_ll_sd_read_fci(0, 0, 1, deadline_ms(5000));
        TEST_CHECK(error == (malformed ? ED64_PRO_LL_FRAME : ED64_PRO_LL_MCU_ERROR));
        TEST_CHECK(mock.nresp_requests == (malformed ? 0 : 1));
        unsigned accesses = mock.accesses;
        TEST_CHECK(ed64_pro_ll_sd_read_fci(0, 0, 1, deadline_ms(5000)) == error);
        TEST_CHECK(mock.accesses == accesses && mock.fci_transfers == 1);
    }
}

/**
 * @brief Encode the RTC-only OFF/zero profile and reject storage or mapping additions.
 */
static void test_menu_config (void) {
    power_on();
    identify();
    ed64_pro_dev_cfg_t config = {.rtc_mode = ED64_PRO_RTC_STD};
    TEST_ASSERT(ed64_pro_ll_dev_set_cfg(&config, deadline_ms(5000)) == ED64_PRO_LL_OK);
    const uint8_t expected[] = {
        0x2B, 0xD4, 0x85, 0x7A, 0x20,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
    };
    TEST_CHECK(mock.tx_count == 73 && memcmp(mock.tx_bytes, expected, sizeof(expected)) == 0);
    for (size_t i = sizeof(expected); i < 69; i++) {
        TEST_CHECK(mock.tx_bytes[i] == 0);
    }
    const uint8_t status[] = {0x2B, 0xD4, 0x10, 0xEF};
    TEST_CHECK(memcmp(mock.tx_bytes + 69, status, sizeof(status)) == 0);
    TEST_CHECK(mock.config_commands == 1 && mock.start_commands == 0 && mock.status_requests == 1);

    power_on();
    identify();
    for (unsigned fault = 0; fault < 20; fault++) {
        config = (ed64_pro_dev_cfg_t){.rtc_mode = ED64_PRO_RTC_STD};
        switch (fault) {
        case 0: config.brom_type = 1; break;
        case 1: config.brom_type = ED64_PRO_ROM_GPAK; break;
        case 2: config.brom_type = 3; break;
        case 3: config.gpak_size = 1; break;
        case 4: config.gpak_size = 512; break;
        case 5: config.brm_type = ED64_PRO_BRM_EEP4K; config.brm_size = 512; break;
        case 6: config.brm_size = 512; break;
        case 7: config.rtc_mode = ED64_PRO_RTC_RDO; break;
        case 8: config.rtc_mode = 0; break;
        case 9: config.dd_en = 1; break;
        case 10: config.gpak_wren = 1; break;
        case 11: config.gpak_key = 1; break;
        default: config.reserved[fault - 12] = 1; break;
        }
        TEST_CHECK(ed64_pro_ll_dev_set_cfg(&config, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    }
    ed64_pro_dev_cfg_t unsafe = {.brom_type = ED64_PRO_ROM_GPAK, .gpak_size = 512,
                               .brm_type = ED64_PRO_BRM_SRM32K, .brm_size = 32768, .rtc_mode = ED64_PRO_RTC_STD};
    TEST_CHECK(ed64_pro_ll_dev_set_cfg(&unsafe, deadline_ms(1000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.accesses == 0 && ed64_pro_ll_get_failure()->code == ED64_PRO_LL_OK);
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
    const char *paths[] = {"/game.z64", "/saves/caf\xC3\xA9.sav", "/DD/test.ndd"};
    for (unsigned type = 0; type < 3; type++) {
        power_on();
        identify();
        ed64_pro_path_t path_type = type == 0 ? ED64_PRO_PATH_GPAK : type == 1 ? ED64_PRO_PATH_BRM : ED64_PRO_PATH_DISK;
        TEST_CHECK(ed64_pro_ll_dev_set_path(path_type, paths[type], deadline_ms(5000)) == ED64_PRO_LL_OK);
        const uint8_t header[] = {0x2B, 0xD4, 0x85, 0x7A, 0x21, type * 2, 0, strlen(paths[type])};
        TEST_CHECK(memcmp(mock.tx_bytes, header, sizeof(header)) == 0);
        TEST_CHECK(memcmp(mock.tx_bytes + sizeof(header), paths[type], strlen(paths[type])) == 0);
        const uint8_t status[] = {0x2B, 0xD4, 0x10, 0xEF};
        TEST_CHECK(memcmp(mock.tx_bytes + sizeof(header) + strlen(paths[type]), status, sizeof(status)) == 0);
        TEST_CHECK(mock.tx_count == sizeof(header) + strlen(paths[type]) + sizeof(status));
        TEST_CHECK(
            strcmp(mock.dev_paths[type], paths[type]) == 0 && mock.path_commands == 1 && mock.status_requests == 1
        );
    }
    power_on();
    identify();
    char path[ED64_PRO_PATH_MAX + 1];
    memset(path, 'a', sizeof(path));
    path[0] = '/';
    path[ED64_PRO_PATH_MAX] = '\0';
    TEST_CHECK(ed64_pro_ll_dev_set_path(ED64_PRO_PATH_GPAK, path, deadline_ms(5000)) == ED64_PRO_LL_OK);
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
}

/**
 * @brief Reject invalid device paths and launch configurations before DMA.
 */
static void test_dev_invalid_arguments (void) {
    power_on();
    identify();
    const char *paths[] = {NULL, "", "relative/file", "sd:/game.z64"};
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        TEST_CHECK(
            ed64_pro_ll_dev_set_path(ED64_PRO_PATH_GPAK, paths[i], deadline_ms(5000)) ==
            ED64_PRO_LL_ARGUMENT
        );
    }
    char path[ED64_PRO_PATH_MAX + 2];
    memset(path, 'a', sizeof(path));
    path[0] = '/';
    path[sizeof(path) - 1] = '\0';
    TEST_CHECK(
        ed64_pro_ll_dev_set_path(ED64_PRO_PATH_GPAK, path, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(
        ed64_pro_ll_dev_set_path((ed64_pro_path_t)1, "/file", deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(
        ed64_pro_ll_dev_set_path((ed64_pro_path_t)3, "/file", deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(
        ed64_pro_ll_dev_set_path((ed64_pro_path_t)5, "/file", deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT
    );
    TEST_CHECK(ed64_pro_ll_dev_set_cfg(NULL, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
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
        case 7: config.dd_en = 2; break;
        case 8: config.gpak_wren = 1; break;
        case 9: config.gpak_key = 1; break;
        case 10: config.reserved[0] = 1; break;
        case 11: config.reserved[7] = 1; break;
        }
        TEST_CHECK(ed64_pro_ll_dev_set_cfg(&config, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    }
    TEST_CHECK(mock.accesses == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
}

/**
 * @brief Verify the device configuration frame for every supported save type.
 */
static void test_dev_config_frame (void) {
    const uint32_t sizes[] = {0, 512, 2048, 32768, 98304, 131072, 131072};
    for (unsigned type = 0; type < sizeof(sizes) / sizeof(sizes[0]); type++) {
        power_on();
        identify();
        ed64_pro_dev_cfg_t config = launch_config();
        config.brm_type = type;
        config.brm_size = sizes[type];
        TEST_CHECK(ed64_pro_ll_dev_set_cfg(&config, deadline_ms(5000)) == ED64_PRO_LL_OK);
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
    }
}

/**
 * @brief Verify launch command order and stop at the first failed step.
 */
static void test_dev_order_and_faults (void) {
    for (unsigned failed_step = 0; failed_step <= 4; failed_step++) {
        power_on();
        identify();
        if (failed_step < 4) {
            mock.replies[failed_step].bytes[3] = 0xB0;
        }
        ed64_pro_dev_cfg_t config = launch_config();
        ed64_pro_ll_error_t error =
            ed64_pro_ll_dev_set_path(ED64_PRO_PATH_GPAK, "/game.z64", deadline_ms(5000));
        if (error == ED64_PRO_LL_OK) {
            error = ed64_pro_ll_dev_set_path(ED64_PRO_PATH_BRM, "/saves/game.sav", deadline_ms(5000));
        }
        if (error == ED64_PRO_LL_OK) {
            error = ed64_pro_ll_dev_set_cfg(&config, deadline_ms(5000));
        }
        if (error == ED64_PRO_LL_OK) {
            error = ed64_pro_ll_dev_start(deadline_ms(5000));
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
            unsigned accesses = mock.accesses;
            TEST_CHECK(ed64_pro_ll_dev_start(deadline_ms(5000)) == error);
            TEST_CHECK(mock.accesses == accesses);
        }
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

    for (unsigned i = 1; i < ED64_PRO_TEST_FAIL_WRITE_N; i++) {
        TEST_ASSERT(ed64_pro_ll_sd_write(buffer, 0, 3, deadline_ms(5000)) == ED64_PRO_LL_OK);
    }
    uint8_t original[512];
    memcpy(original, mock.sectors + 1024, sizeof(original));
    memset(buffer, 0xA6, sizeof(buffer));
    TEST_CHECK(ed64_pro_ll_sd_write(buffer, 0, 3, deadline_ms(5000)) == ED64_PRO_LL_TIMEOUT);
    TEST_CHECK(memcmp(mock.sectors, buffer, 1024) == 0);
    TEST_CHECK(memcmp(mock.sectors + 1024, original, sizeof(original)) == 0);
    TEST_CHECK(mock.transfer_done == 1024 && mock.status_requests == ED64_PRO_TEST_FAIL_WRITE_N - 1);
    unsigned accesses = mock.accesses;
    TEST_CHECK(ed64_pro_ll_sd_read(buffer, 0, 1, deadline_ms(5000)) == ED64_PRO_LL_TIMEOUT);
    TEST_CHECK(mock.accesses == accesses);
}
#endif

/** @brief The IPL helper uses FCI addresses while the ordinary ROM helper excludes them. */
static void test_dd_ipl_fci_frames_and_bounds (void) {
    const uint32_t destinations[] = {ED64_PRO_FCI_IPL, ED64_PRO_FCI_IPL + ED64_PRO_IPL_SIZE - 512};
    for (unsigned i = 0; i < 2; i++) {
        power_on();
        identify();
        TEST_CHECK(ed64_pro_ll_sd_read_ipl_fci(destinations[i], 1, 1, deadline_ms(5000)) == ED64_PRO_LL_OK);
        const uint8_t prefix[] = {0x2B, 0xD4, 0x81, 0x7E, 0x10, 0, 0, 0, 1};
        TEST_CHECK(memcmp(mock.tx_bytes, prefix, sizeof(prefix)) == 0);
        TEST_CHECK(mock.tx_bytes[9] == (uint8_t)(destinations[i] >> 24));
        TEST_CHECK(mock.tx_bytes[10] == (uint8_t)(destinations[i] >> 16));
        TEST_CHECK(mock.tx_bytes[11] == (uint8_t)(destinations[i] >> 8));
        TEST_CHECK(mock.tx_bytes[12] == (uint8_t)destinations[i]);
        const uint8_t tail[] = {0, 0, 2, 0, 0x19, 0x13, 0, 0, 0};
        TEST_CHECK(memcmp(mock.tx_bytes + 13, tail, sizeof(tail)) == 0 && mock.tx_count == 26);
        TEST_CHECK(mock.fci_address == destinations[i] && mock.fci_transfers == 1);
        TEST_CHECK(memcmp(mock.fci + (destinations[i] - ED64_PRO_FCI_IPL), mock.sectors + 512, 512) == 0);
    }
    power_on();
    identify();
    static const struct { uint32_t address, lba, count; } invalid[] = {
        {0, 0, 1}, {ED64_PRO_FCI_IPL - 512, 0, 1}, {ED64_PRO_FCI_IPL + 1, 0, 1},
        {ED64_PRO_FCI_IPL + ED64_PRO_IPL_SIZE, 0, 1},
        {ED64_PRO_FCI_IPL + ED64_PRO_IPL_SIZE - 512, 0, 2},
        {0x1F800000, 0, 1}, {ED64_PRO_FCI_IPL, 0, 0}, {ED64_PRO_FCI_IPL, 0, 257},
        {ED64_PRO_FCI_IPL, UINT32_MAX, 2}, {ED64_PRO_FCI_IPL, 0, UINT32_MAX}
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        TEST_CHECK(ed64_pro_ll_sd_read_ipl_fci(invalid[i].address, invalid[i].lba,
                                            invalid[i].count, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    }
    TEST_CHECK(ed64_pro_ll_sd_read_fci(ED64_PRO_FCI_IPL, 0, 1, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    TEST_CHECK(mock.accesses == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
}

/** @brief Disk-only and combo profiles encode documented firmware DD and writable RTC settings. */
static void test_dd_config_profiles (void) {
    for (unsigned combo = 0; combo < 2; combo++) {
        power_on();
        identify();
        ed64_pro_dev_cfg_t config = combo ? launch_config() : (ed64_pro_dev_cfg_t){.brom_type = ED64_PRO_ROM_IPL};
        config.dd_en = 1;
        config.rtc_mode = ED64_PRO_RTC_STD;
        TEST_CHECK(ed64_pro_ll_dev_set_cfg(&config, deadline_ms(5000)) == ED64_PRO_LL_OK);
        TEST_CHECK(mock.config_bytes[3] == (combo ? ED64_PRO_ROM_GPAK : ED64_PRO_ROM_IPL));
        TEST_CHECK(mock.config_bytes[19] == ED64_PRO_RTC_STD && mock.config_bytes[23] == 1);
        TEST_CHECK(mock.config_bytes[15] == (combo ? ED64_PRO_BRM_EEP4K : ED64_PRO_BRM_OFF));
    }
    power_on();
    identify();
    for (unsigned fault = 0; fault < 7; fault++) {
        ed64_pro_dev_cfg_t config = {.brom_type = ED64_PRO_ROM_IPL, .rtc_mode = ED64_PRO_RTC_STD, .dd_en = 1};
        switch (fault) {
        case 0: config.gpak_size = 512; break;
        case 1: config.brm_type = ED64_PRO_BRM_EEP4K; config.brm_size = 512; break;
        case 2: config.dd_en = 0; break;
        case 3: config.dd_en = 2; break;
        case 4: config.rtc_mode = ED64_PRO_RTC_RDO; break;
        case 5: config.brom_type = ED64_PRO_ROM_OFF; break;
        case 6: config.brom_type = ED64_PRO_ROM_GPAK; config.gpak_size = ED64_PRO_DD_ROM_LIMIT + 512; break;
        }
        TEST_CHECK(ed64_pro_ll_dev_set_cfg(&config, deadline_ms(5000)) == ED64_PRO_LL_ARGUMENT);
    }
    TEST_CHECK(mock.accesses == 0 && pro_ll.failure.code == ED64_PRO_LL_OK);
}

TEST_LIST = {
    {"voltage-only exact GET_INF and cached refresh", test_voltage_get_info},
    {"voltage missing partial trailing replies and retained failure", test_voltage_get_info_faults},
    {"button full-width SYSSTAT, arguments and latched failure", test_button_register},
    {"native DD IPL FCI frames and independent bounds", test_dd_ipl_fci_frames_and_bounds},
    {"native DD standalone and combo configuration profiles", test_dd_config_profiles},
    {"RTC physical observation framing", test_rtc_frames},
    {"menu RTC configuration uses OFF/zero and disables storage services", test_menu_config},
    {"RTC invalid inputs and unidentified hardware never write", test_rtc_rejections},
    {"RTC partial and unexpected replies latch", test_rtc_reply_faults},
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
    {"new request fault resets previous receive progress", test_new_request_progress},
    {"system info partial and trailing replies latch", test_sys_info_faults},
    {"NRESP only follows well-formed nonzero status", test_nresp},
    {"ACK helper read and write block boundaries", test_ack_helpers},
    {"raw sectors and ACK boundary transcripts", test_sd_read_write},
    {"raw-sector arguments never submit DMA", test_sd_invalid},
    {"each read/write ACK uses the original deadline", test_sd_ack_timeouts},
    {"ambiguous write faults never retry", test_sd_faults},
    {"public register read without cache work", test_register_read},
    {"public mailbox write waits for completion", test_register_write},
    {"invalid input never submits DMA", test_invalid_input},
    {"exact deadline is expired", test_exact_deadline},
    {"synchronous completion precedes next protocol timeout", test_completion_at_deadline},
    {"caller cache-line interrupt sharing", test_cache_sharing},
    {"Pro family and device identity fixtures", test_probe_identity},
    {"expired probe stays indeterminate without bus traffic", test_probe_deadline},
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
    {"partial FIFO timeout preserves completed chunks", test_partial_fifo_timeout},
    {"Pro adapter maps startup through shared errors", test_adapter_initialization},
    {"Pro adapter operations remain unavailable without bus traffic", test_adapter_unavailable_operations},
    {"golden one-shot MCU initialization transcript", test_init_transcript},
    {"inherited versus fresh MCU errors", test_inherited_and_fresh_errors},
    {"partial replies at every initialization status", test_init_partial_statuses},
    {"mailbox requires matching READY samples", test_mailbox_stability},
    {"mailbox stall and instability are bounded", test_mailbox_deadlines},
    {"both handoff cleanup phases are bounded", test_init_cleanup_limits},
    {"SD initialization uses its original five-second budget", test_sd_deadline},
    {"whole attempt budget spans probe and initialization", test_whole_attempt_deadline},
    {"absent cartridges never initialize", test_init_absent},
    {"protocol fault retains confirmed progress and first failure", test_protocol_fault_context},
    {NULL, NULL},
};
