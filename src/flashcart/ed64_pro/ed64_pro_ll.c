/**
 * @file ed64_pro_ll.c
 * @brief Foreground EverDrive-64 Pro protocol over libdragon PI access.
 *
 * Register layout and selected wire protocol: krikzz/ed64-pro-pub.
 * See README.md for the pinned revision, adaptations, and upstream MIT notice.
 */

#include <libdragon.h>
#include <string.h>

#include "ed64_pro_ll.h"

static uint32_t pro_staging[ED64_PRO_FIFO_CHUNK] __attribute__((aligned(512)));

static struct {
    ed64_pro_ll_failure_t failure;
    size_t transferred;
    uint32_t id;
    bool probed;
    bool identified;
    bool status_valid;
    uint8_t status;
    bool nresp_valid;
    uint8_t nresp;
#ifdef ED64_PRO_TEST_FAIL_WRITE_N
    unsigned raw_writes;
#endif
    ed64_pro_ll_metrics_t metrics;
} pro_ll;

_Static_assert(sizeof(pro_staging) == 512, "Pro staging must own complete cache lines");
_Static_assert(sizeof(ed64_pro_status_t) == 4, "Pro status is exactly four protocol bytes");
_Static_assert(sizeof(ed64_pro_dev_cfg_t) == 64, "Pro device configuration is sixteen 32-bit fields");

typedef enum {
    PRO_CMD_STATUS = 0x10,
    PRO_CMD_NRESP = 0x13,
    PRO_CMD_EPO = 0x81,
    PRO_CMD_RTC = 0x83,
    PRO_CMD_SYS = 0x84,
    PRO_CMD_SD_INIT = 0x80,
    PRO_CMD_STOP = 0x85,
    PRO_CMD_DEV = 0x85,
} pro_command_t;

/**
 * @brief Check whether an absolute CP0 deadline has expired.
 */
static bool pro_expired (uint32_t deadline) {
    return (int32_t)(TICKS_READ() - deadline) >= 0;
}

/**
 * @brief Cap an absolute deadline by a local timeout using wrap-safe signed comparison.
 *
 * Deadlines must remain within the signed half-range of the CP0 counter.
 */
uint32_t ed64_pro_ll_limit_deadline (uint32_t deadline, uint32_t timeout_ms) {
    uint32_t local = TICKS_READ() + TICKS_FROM_MS(timeout_ms);
    return (int32_t)(local - deadline) < 0 ? local : deadline;
}

/**
 * @brief Latch the first protocol fault with confirmed RX progress and MCU status.
 */
static ed64_pro_ll_error_t pro_fail (ed64_pro_ll_error_t error) {
    if (pro_ll.failure.code == ED64_PRO_LL_OK) {
        pro_ll.failure = (ed64_pro_ll_failure_t){
            .code = error,
            .transferred = pro_ll.transferred,
            .status_valid = pro_ll.status_valid,
            .status = pro_ll.status,
            .nresp_valid = pro_ll.nresp_valid,
            .nresp = pro_ll.nresp,
        };
    }
    return pro_ll.failure.code;
}

/**
 * @brief Return the first latched transport failure and its diagnostic context.
 */
const ed64_pro_ll_failure_t *ed64_pro_ll_get_failure (void) {
    return &pro_ll.failure;
}

/**
 * @brief Return the peak observed FIFO receive count.
 */
const ed64_pro_ll_metrics_t *ed64_pro_ll_get_metrics (void) {
    return &pro_ll.metrics;
}

/**
 * @brief Whitelist selected register reads, excluding the side-effectful FIFO data port.
 */
static bool pro_readable_reg (ed64_pro_reg_t reg) {
    switch (reg) {
    case ED64_PRO_REG_FIFOSTAT:
    case ED64_PRO_REG_SYSSTAT:
    case ED64_PRO_REG_TIMER:
    case ED64_PRO_REG_MBX:
    case ED64_PRO_REG_EDID:
    case ED64_PRO_REG_MCTR: return true;
    default: return false;
    }
}

/**
 * @brief Transfer exact FIFO bus words through cache-owned staging.
 *
 * Aligned staging and word-multiple lengths keep dma_read on its aligned path,
 * with no CPU alignment fixups or extra side-effectful FIFO reads. The SDK waits
 * for completion; protocol deadlines cannot interrupt that hardware wait.
 */
static ed64_pro_ll_error_t pro_fifo_transfer (
    size_t length,
    bool write,
    uint32_t deadline
) {
    if (pro_ll.failure.code != ED64_PRO_LL_OK) {
        return pro_ll.failure.code;
    }
    if (length == 0 || length > sizeof(pro_staging) || length % sizeof(uint32_t) != 0) {
        return ED64_PRO_LL_ARGUMENT;
    }
    if (!pro_ll.identified) {
        return ED64_PRO_LL_NOT_IDENTIFIED;
    }
    if (pro_expired(deadline)) {
        return pro_fail(ED64_PRO_LL_TIMEOUT);
    }

    size_t cache_length = (length + ED64_PRO_CACHE_LINE - 1) & ~(size_t)(ED64_PRO_CACHE_LINE - 1);
    if (write) {
        data_cache_hit_writeback(pro_staging, cache_length);
        MEMORY_BARRIER();
        dma_write(pro_staging, ED64_PRO_REG_FIFO, length);
    } else {
        data_cache_hit_writeback_invalidate(pro_staging, cache_length);
        MEMORY_BARRIER();
        dma_read(pro_staging, ED64_PRO_REG_FIFO, length);
        data_cache_hit_invalidate(pro_staging, cache_length);
    }
    MEMORY_BARRIER();
    return ED64_PRO_LL_OK;
}

/**
 * @brief Read one whitelisted register through libdragon's serialized PI access.
 *
 * The first completed EDID read caches the Pro family/device identification.
 */
ed64_pro_ll_error_t
ed64_pro_ll_read_reg (ed64_pro_reg_t reg, uint32_t *value, uint32_t deadline) {
    if (!value || !pro_readable_reg(reg)) {
        return ED64_PRO_LL_ARGUMENT;
    }
    if (pro_ll.failure.code != ED64_PRO_LL_OK) {
        return pro_ll.failure.code;
    }
    if (!pro_ll.identified && (pro_ll.probed || reg != ED64_PRO_REG_EDID)) {
        return ED64_PRO_LL_NOT_IDENTIFIED;
    }
    if (pro_expired(deadline)) {
        return pro_fail(ED64_PRO_LL_TIMEOUT);
    }
    uint32_t result = io_read(reg);
    if (reg == ED64_PRO_REG_EDID && !pro_ll.probed) {
        pro_ll.id = result;
        pro_ll.probed = true;
        pro_ll.identified = (pro_ll.id & 0xFFFF00FFu) == 0xED640027u;
    }
    *value = result;
    return ED64_PRO_LL_OK;
}

/**
 * @brief Permit only the mailbox zero/NOP write after Pro identification.
 */
ed64_pro_ll_error_t
ed64_pro_ll_write_reg (ed64_pro_reg_t reg, uint32_t value, uint32_t deadline) {
    if (pro_ll.failure.code != ED64_PRO_LL_OK) {
        return pro_ll.failure.code;
    }
    if (reg != ED64_PRO_REG_MBX || value != 0) {
        return ED64_PRO_LL_ARGUMENT;
    }
    if (!pro_ll.identified) {
        return ED64_PRO_LL_NOT_IDENTIFIED;
    }
    if (pro_expired(deadline)) {
        return pro_fail(ED64_PRO_LL_TIMEOUT);
    }
    io_write(reg, value);
    /* io_write serializes access but returns before the bus write completes. */
    dma_wait();
    return ED64_PRO_LL_OK;
}

/**
 * @brief Cache one passive EDID read without ever classifying an I/O failure as absence.
 */
ed64_pro_probe_t ed64_pro_ll_probe (uint32_t deadline) {
    ed64_pro_ll_error_t error = pro_ll.failure.code;
    if (error == ED64_PRO_LL_OK && !pro_ll.probed) {
        uint32_t id;
        error = ed64_pro_ll_read_reg(ED64_PRO_REG_EDID, &id, deadline);
    }
    return (ed64_pro_probe_t){
        .outcome = error != ED64_PRO_LL_OK ? ED64_PRO_PROBE_IO_ERROR
                   : pro_ll.identified     ? ED64_PRO_PROBE_IDENTIFIED
                                           : ED64_PRO_PROBE_ABSENT,
        .error = error,
        .id = pro_ll.id,
        .id_valid = pro_ll.probed,
    };
}

/**
 * @brief Read the low-16-bit RX count, track its peak, and reject implausible FIFO capacity.
 */
static ed64_pro_ll_error_t
pro_fifo_available (uint32_t *available, uint32_t deadline) {
    uint32_t value;
    ed64_pro_ll_error_t error = ed64_pro_ll_read_reg(ED64_PRO_REG_FIFOSTAT, &value, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    value &= 0xFFFFu;
    if (value > pro_ll.metrics.rx_peak) {
        pro_ll.metrics.rx_peak = value;
    }
    if (value > ED64_PRO_FIFO_LIMIT) {
        return pro_fail(ED64_PRO_LL_FIFO_COUNT);
    }
    *available = value;
    return ED64_PRO_LL_OK;
}

/**
 * @brief Read FIFO bytes within the deadline; partial output is invalid on failure.
 *
 * Zero length is a no-op.
 */
static ed64_pro_ll_error_t
pro_fifo_read (uint8_t *output, size_t length, uint32_t deadline) {
    if (length > ED64_PRO_FIFO_LIMIT || (length && !output)) {
        return ED64_PRO_LL_ARGUMENT;
    }
    pro_ll.transferred = 0;
    while (pro_ll.transferred < length) {
        if (pro_expired(deadline)) {
            return pro_fail(ED64_PRO_LL_TIMEOUT);
        }
        uint32_t available;
        ed64_pro_ll_error_t error = pro_fifo_available(&available, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        if (!available) {
            continue;
        }
        size_t chunk = length - pro_ll.transferred;
        if (chunk > available) {
            chunk = available;
        }
        if (chunk > ED64_PRO_FIFO_CHUNK) {
            chunk = ED64_PRO_FIFO_CHUNK;
        }
        error = pro_fifo_transfer(chunk * sizeof(uint32_t), false, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        for (size_t i = 0; i < chunk; i++) {
            output[pro_ll.transferred + i] = (uint8_t)pro_staging[i];
        }
        pro_ll.transferred += chunk;
    }
    return ED64_PRO_LL_OK;
}

/**
 * @brief Discard handoff RX data within fixed cleanup time and byte limits.
 *
 * Intended only for initial and post-stop cleanup. Continuous refill cannot
 * restart the deadline, and the output count is published only on success.
 */
ed64_pro_ll_error_t ed64_pro_ll_drain (size_t *drained, uint32_t deadline) {
    if (!drained) {
        return ED64_PRO_LL_ARGUMENT;
    }
    deadline = ed64_pro_ll_limit_deadline(deadline, ED64_PRO_DRAIN_TIMEOUT_MS);
    pro_ll.transferred = 0;
    for (;;) {
        if (pro_expired(deadline)) {
            return pro_fail(ED64_PRO_LL_TIMEOUT);
        }
        uint32_t available;
        ed64_pro_ll_error_t error = pro_fifo_available(&available, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        if (!available) {
            *drained = pro_ll.transferred;
            return ED64_PRO_LL_OK;
        }
        if (available > ED64_PRO_FIFO_LIMIT - pro_ll.transferred) {
            return pro_fail(ED64_PRO_LL_DRAIN_LIMIT);
        }
        size_t chunk = available < ED64_PRO_FIFO_CHUNK ? available : ED64_PRO_FIFO_CHUNK;
        error = pro_fifo_transfer(chunk * sizeof(uint32_t), false, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        pro_ll.transferred += chunk;
    }
}

/**
 * @brief Send a typed command frame through the FIFO.
 *
 * Only typed builders may select protocol operations.
 */
static ed64_pro_ll_error_t pro_send_frame (
    pro_command_t command,
    int subcommand,
    const void *payload,
    size_t payload_length,
    uint32_t deadline
) {
    size_t header_length = subcommand < 0 ? 4 : 5;
    if (payload_length > ED64_PRO_FRAME_MAX - header_length || (payload_length && !payload)) {
        return ED64_PRO_LL_ARGUMENT;
    }
    if (!pro_ll.identified) {
        return ED64_PRO_LL_NOT_IDENTIFIED;
    }
    pro_ll.transferred = 0;
    const uint8_t header[] = {0x2B, 0xD4, command, command ^ 0xFF, (uint8_t)subcommand};
    const uint8_t *bytes = payload;
    size_t length = header_length + payload_length;
    for (size_t offset = 0; offset < length;) {
        size_t chunk = length - offset;
        if (chunk > ED64_PRO_FIFO_CHUNK) {
            chunk = ED64_PRO_FIFO_CHUNK;
        }
        for (size_t i = 0; i < chunk; i++) {
            size_t index = offset + i;
            pro_staging[i] = index < header_length ? header[index] : bytes[index - header_length];
        }
        ed64_pro_ll_error_t error = pro_fifo_transfer(chunk * sizeof(uint32_t), true, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        offset += chunk;
    }
    return ED64_PRO_LL_OK;
}

/**
 * @brief Send a status, stop, or SD initialization command without a payload.
 */
static ed64_pro_ll_error_t
pro_send_command (pro_command_t command, uint32_t deadline) {
    return pro_send_frame(command, command == PRO_CMD_STATUS ? -1 : 0x10, NULL, 0, deadline);
}

/**
 * @brief Write bytes to the FIFO in staging-sized chunks within the deadline.
 */
static ed64_pro_ll_error_t
pro_fifo_write (const uint8_t *input, size_t length, uint32_t deadline) {
    if (length && !input) {
        return ED64_PRO_LL_ARGUMENT;
    }
    for (size_t offset = 0; offset < length;) {
        size_t chunk = length - offset;
        if (chunk > ED64_PRO_FIFO_CHUNK) {
            chunk = ED64_PRO_FIFO_CHUNK;
        }
        for (size_t i = 0; i < chunk; i++) {
            pro_staging[i] = input[offset + i];
        }
        ed64_pro_ll_error_t error = pro_fifo_transfer(chunk * sizeof(uint32_t), true, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        offset += chunk;
    }
    return ED64_PRO_LL_OK;
}

/**
 * @brief Reject unexpected data remaining in the receive FIFO.
 */
static ed64_pro_ll_error_t pro_assert_idle (uint32_t deadline) {
    uint32_t available = 0;
    ed64_pro_ll_error_t error = pro_fifo_available(&available, deadline);
    if (error == ED64_PRO_LL_OK && available) {
        return pro_fail(ED64_PRO_LL_TRAILING_DATA);
    }
    return error;
}

/**
 * @brief Request one exact status frame without treating inherited MCU status as failure.
 *
 * Reject preexisting/trailing FIFO data and malformed keys. Publish output only
 * after a complete four-byte reply within the original deadline.
 */
ed64_pro_ll_error_t
ed64_pro_ll_read_status (ed64_pro_status_t *status, uint32_t deadline) {
    if (!status) {
        return ED64_PRO_LL_ARGUMENT;
    }
    pro_ll.transferred = 0;
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    uint32_t available;
    ed64_pro_ll_error_t error = pro_fifo_available(&available, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (available) {
        return pro_fail(ED64_PRO_LL_TRAILING_DATA);
    }
    error = pro_send_command(PRO_CMD_STATUS, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t reply[4];
    error = pro_fifo_read(reply, sizeof(reply), deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (reply[0] != 0x5A) {
        return pro_fail(ED64_PRO_LL_FRAME);
    }
    error = pro_fifo_available(&available, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (available) {
        return pro_fail(ED64_PRO_LL_TRAILING_DATA);
    }
    pro_ll.status_valid = true;
    pro_ll.status = reply[3];
    pro_ll.nresp_valid = false;
    *status = (ed64_pro_status_t){
        .key = reply[0],
        .opaque = {reply[1], reply[2]},
        .mcu_status = reply[3],
    };
    return ED64_PRO_LL_OK;
}

/**
 * @brief Clear the mailbox, send STOP, and require two consecutive matching READY samples.
 *
 * All polls share the original stop deadline.
 */
ed64_pro_ll_error_t ed64_pro_ll_stop (uint32_t deadline) {
    deadline = ed64_pro_ll_limit_deadline(deadline, ED64_PRO_STOP_TIMEOUT_MS);
    pro_ll.transferred = 0;
    ed64_pro_ll_error_t error = ed64_pro_ll_write_reg(ED64_PRO_REG_MBX, 0, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_send_command(PRO_CMD_STOP, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    for (;;) {
        if (pro_expired(deadline)) {
            return pro_fail(ED64_PRO_LL_MAILBOX_TIMEOUT);
        }
        uint32_t first, second;
        error = ed64_pro_ll_read_reg(ED64_PRO_REG_MBX, &first, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        error = ed64_pro_ll_read_reg(ED64_PRO_REG_MBX, &second, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        if (first == second && second == 0x52445921u) {
            return ED64_PRO_LL_OK;
        }
    }
}

/**
 * @brief Require zero fresh MCU status, retaining the completed frame on MCU_ERROR.
 */
ed64_pro_ll_error_t
ed64_pro_ll_check_status (ed64_pro_status_t *status, uint32_t deadline) {
    ed64_pro_ll_error_t error = ed64_pro_ll_read_status(status, deadline);
    if (error == ED64_PRO_LL_OK && status->mcu_status != 0) {
        uint8_t detail;
        error = ed64_pro_ll_read_nresp(status->mcu_status, &detail, deadline);
        if (error == ED64_PRO_LL_OK) {
            error = pro_fail(ED64_PRO_LL_MCU_ERROR);
        }
    }
    return error;
}

/**
 * @brief Send SD_INIT and validate fresh status within one capped five-second budget.
 *
 * The complete status frame is also published when the MCU reports an error.
 */
ed64_pro_ll_error_t
ed64_pro_ll_init_sd (ed64_pro_status_t *status, uint32_t deadline) {
    if (!status) {
        return ED64_PRO_LL_ARGUMENT;
    }
    deadline = ed64_pro_ll_limit_deadline(deadline, ED64_PRO_SD_TIMEOUT_MS);
    ed64_pro_ll_error_t error = pro_send_command(PRO_CMD_SD_INIT, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    return ed64_pro_ll_check_status(status, deadline);
}

/**
 * @brief Read and retain the detail byte for the current nonzero MCU status.
 */
ed64_pro_ll_error_t
ed64_pro_ll_read_nresp (uint8_t category, uint8_t *detail, uint32_t deadline) {
    if (!detail || !category || !pro_ll.status_valid || pro_ll.status != category || pro_ll.nresp_valid) {
        return ED64_PRO_LL_ARGUMENT;
    }
    ed64_pro_ll_error_t error = pro_assert_idle(deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_send_frame(PRO_CMD_NRESP, -1, &category, 1, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t reply;
    error = pro_fifo_read(&reply, 1, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_assert_idle(deadline);
    if (error == ED64_PRO_LL_OK) {
        pro_ll.nresp = reply;
        pro_ll.nresp_valid = true;
        *detail = reply;
    }
    return error;
}

/**
 * @brief Observe physical MCU time for qualification without publishing a partial reply.
 *
 * Production clock reads and writes use native libdragon RTC integration.
 */
ed64_pro_ll_error_t ed64_pro_ll_rtc_get (
    uint8_t output[ED64_PRO_RTC_SIZE],
    uint32_t deadline
) {
    if (!output) {
        return ED64_PRO_LL_ARGUMENT;
    }
    deadline = ed64_pro_ll_limit_deadline(deadline, ED64_PRO_STATUS_TIMEOUT_MS);
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    pro_ll.transferred = 0;
    ed64_pro_ll_error_t error = pro_assert_idle(deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_send_frame(PRO_CMD_RTC, 0x10, NULL, 0, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t reply[ED64_PRO_RTC_SIZE];
    error = pro_fifo_read(reply, sizeof(reply), deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_assert_idle(deadline);
    if (error == ED64_PRO_LL_OK) {
        memcpy(output, reply, sizeof(reply));
    }
    return error;
}

/**
 * @brief Encode a 32-bit value in big-endian byte order.
 */
static void pro_put_u32 (uint8_t *output, uint32_t value) {
    output[0] = value >> 24;
    output[1] = value >> 16;
    output[2] = value >> 8;
    output[3] = value;
}

/**
 * @brief Read selected firmware, maximum ROM size, or 3.3 V supply values.
 */
ed64_pro_ll_error_t ed64_pro_ll_get_sys_info (
    const uint32_t *ids,
    uint32_t *values,
    size_t count,
    uint32_t deadline
) {
    if (!ids || !values || !count || count > 2) {
        return ED64_PRO_LL_ARGUMENT;
    }
    uint8_t payload[9];
    payload[0] = count;
    for (size_t i = 0; i < count; i++) {
        if (ids[i] != ED64_PRO_INFS_TS_FW && ids[i] != ED64_PRO_INFS_MAX_ROM_SIZE &&
            ids[i] != ED64_PRO_INFD_VCC_3V3) {
            return ED64_PRO_LL_ARGUMENT;
        }
        pro_put_u32(&payload[1 + i * 4], ids[i]);
    }
    pro_ll.transferred = 0;
    ed64_pro_ll_error_t error = pro_assert_idle(deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_send_frame(PRO_CMD_SYS, 0x10, payload, 1 + count * 4, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t reply[8];
    error = pro_fifo_read(reply, count * 4, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_assert_idle(deadline);
    if (error == ED64_PRO_LL_OK) {
        for (size_t i = 0; i < count; i++) {
            const uint8_t *value = &reply[i * 4];
            values[i] = (uint32_t)value[0] << 24 | (uint32_t)value[1] << 16 | (uint32_t)value[2] << 8 | value[3];
        }
    }
    return error;
}

/**
 * @brief Request and read FIFO data in acknowledged blocks.
 */
static inline ed64_pro_ll_error_t
pro_fifo_read_ack (uint8_t *output, size_t length, uint32_t deadline) {
    if (length && !output) {
        return ED64_PRO_LL_ARGUMENT;
    }
    for (size_t offset = 0; offset < length;) {
        size_t block = length - offset;
        if (block > ED64_PRO_ACK_BLOCK) {
            block = ED64_PRO_ACK_BLOCK;
        }
        const uint8_t ack = 0;
        ed64_pro_ll_error_t error = pro_fifo_write(&ack, 1, deadline);
        if (error == ED64_PRO_LL_OK) {
            error = pro_fifo_read(output + offset, block, deadline);
        }
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        offset += block;
    }
    return ED64_PRO_LL_OK;
}

/**
 * @brief Wait for each block acknowledgment before writing FIFO data.
 */
static inline ed64_pro_ll_error_t pro_fifo_write_ack (
    const uint8_t *input,
    size_t length,
    bool fail_after_block,
    uint32_t deadline
) {
    if (length && !input) {
        return ED64_PRO_LL_ARGUMENT;
    }
    for (size_t offset = 0; offset < length;) {
        size_t block = length - offset;
        if (block > ED64_PRO_ACK_BLOCK) {
            block = ED64_PRO_ACK_BLOCK;
        }
        uint8_t ack;
        ed64_pro_ll_error_t error = pro_fifo_read(&ack, 1, deadline);
        if (error == ED64_PRO_LL_OK) {
            error = pro_fifo_write(input + offset, block, deadline);
        }
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        if (fail_after_block) {
            return pro_fail(ED64_PRO_LL_TIMEOUT);
        }
        offset += block;
    }
    return ED64_PRO_LL_OK;
}

/**
 * @brief Send a raw SD transfer command for the requested sectors and direction.
 */
static ed64_pro_ll_error_t
pro_sd_xfer (uint32_t lba, uint32_t count, bool write, uint32_t deadline) {
    uint8_t payload[17] = {0};
    pro_put_u32(&payload[write ? 4 : 0], lba);
    pro_put_u32(&payload[8], count * 512);
    payload[12] = write ? 0x11 : 0x19;
    payload[13] = write ? 0x19 : 0x11;
    return pro_send_frame(PRO_CMD_EPO, 0x10, payload, sizeof(payload), deadline);
}

/**
 * @brief Transfer SD sectors through the FIFO and validate the final MCU status.
 */
static ed64_pro_ll_error_t pro_sd_transfer (
    void *buffer,
    uint32_t lba,
    uint32_t count,
    bool write,
    uint32_t deadline
) {
    if (!buffer || !count || count > 64 || count - 1 > UINT32_MAX - lba) {
        return ED64_PRO_LL_ARGUMENT;
    }
    deadline = ed64_pro_ll_limit_deadline(deadline, ED64_PRO_IO_TIMEOUT_MS);
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    pro_ll.transferred = 0;
    ed64_pro_ll_error_t error = pro_assert_idle(deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (write) {
        const uint8_t key[] = {0xDE, 0xAD, 0xBE, 0xEF};
        error = pro_send_frame(PRO_CMD_EPO, 0x11, key, sizeof(key), deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        error = pro_assert_idle(deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
    }
    error = pro_sd_xfer(lba, count, write, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
#ifdef ED64_PRO_TEST_FAIL_WRITE_N
    bool fail_write = write && ++pro_ll.raw_writes == ED64_PRO_TEST_FAIL_WRITE_N;
#endif
    bool fail_after_block = false;
#ifdef ED64_PRO_TEST_FAIL_WRITE_N
    fail_after_block = fail_write;
#endif
    error = write ? pro_fifo_write_ack(buffer, count * 512, fail_after_block, deadline)
                  : pro_fifo_read_ack(buffer, count * 512, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    ed64_pro_status_t status;
    return ed64_pro_ll_check_status(&status, deadline);
}

/**
 * @brief Read SD sectors into the caller buffer through the FIFO.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_read (
    void *output,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
) {
    return pro_sd_transfer(output, lba, count, false, deadline);
}

/**
 * @brief Write SD sectors from the caller buffer through the FIFO.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_write (
    const void *input,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
) {
    /* Once WRE has been sent, any failure leaves the write ambiguous. Never retry. */
    return pro_sd_transfer((void *)input, lba, count, true, deadline);
}

/**
 * @brief Copy SD sectors directly into cartridge ROM memory and check MCU status.
 */
static ed64_pro_ll_error_t pro_sd_read_fci (
    uint32_t fci,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
) {
    if (!count || count > 256 || count - 1 > UINT32_MAX - lba || (fci & 511)) {
        return ED64_PRO_LL_ARGUMENT;
    }
    deadline = ed64_pro_ll_limit_deadline(deadline, ED64_PRO_IO_TIMEOUT_MS);
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    pro_ll.transferred = 0;
    ed64_pro_ll_error_t error = pro_assert_idle(deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t payload[17] = {0};
    pro_put_u32(&payload[0], lba);
    pro_put_u32(&payload[4], fci);
    pro_put_u32(&payload[8], count * 512);
    payload[12] = 0x19;
    payload[13] = 0x13;
    error = pro_send_frame(PRO_CMD_EPO, 0x10, payload, sizeof(payload), deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    ed64_pro_status_t status;
    return ed64_pro_ll_check_status(&status, deadline);
}

/** @brief Copy SD sectors only to the ordinary ROM window. */
ed64_pro_ll_error_t ed64_pro_ll_sd_read_fci (uint32_t fci, uint32_t lba, uint32_t count, uint32_t deadline
) {
    if (!count || count > 256 || fci >= ED64_PRO_FCI_ROM_LIMIT || count * 512 > ED64_PRO_FCI_ROM_LIMIT - fci) {
        return ED64_PRO_LL_ARGUMENT;
    }
    return pro_sd_read_fci(fci, lba, count, deadline);
}

/** @brief Copy SD sectors only to the dedicated FCI IPL4 window. */
ed64_pro_ll_error_t ed64_pro_ll_sd_read_ipl_fci (uint32_t fci, uint32_t lba, uint32_t count, uint32_t deadline
) {
    if (!count || count > 256 || fci < ED64_PRO_FCI_IPL || fci - ED64_PRO_FCI_IPL >= ED64_PRO_IPL_SIZE ||
        count * 512 > ED64_PRO_IPL_SIZE - (fci - ED64_PRO_FCI_IPL)) {
        return ED64_PRO_LL_ARGUMENT;
    }
    return pro_sd_read_fci(fci, lba, count, deadline);
}

/**
 * @brief Set the ROM byte-swap flag and wait for stable, idle device status.
 */
ed64_pro_ll_error_t
ed64_pro_ll_set_flags (uint16_t mask, uint16_t value, uint32_t deadline) {
    if (mask != ED64_PRO_FLAG_WRSW || (value & ~mask)) {
        return ED64_PRO_LL_ARGUMENT;
    }
    deadline = ed64_pro_ll_limit_deadline(deadline, ED64_PRO_IO_TIMEOUT_MS);
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    pro_ll.transferred = 0;
    ed64_pro_ll_error_t error = pro_assert_idle(deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t payload[4];
    pro_put_u32(payload, (uint32_t)mask << 16 | value);
    error = pro_send_frame(PRO_CMD_DEV, 0x28, payload, sizeof(payload), deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    for (;;) {
        if (pro_expired(deadline)) {
            return pro_fail(ED64_PRO_LL_TIMEOUT);
        }
        uint32_t first = 0, second = 0;
        error = ed64_pro_ll_read_reg(ED64_PRO_REG_SYSSTAT, &first, deadline);
        if (error == ED64_PRO_LL_OK) {
            error = ed64_pro_ll_read_reg(ED64_PRO_REG_SYSSTAT, &second, deadline);
        }
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        if ((first & ~8u) == (second & ~8u) && !(second & 1u)) {
            return pro_assert_idle(deadline);
        }
    }
}

/**
 * @brief Send a device command and validate its fresh MCU status.
 */
static ed64_pro_ll_error_t pro_dev_command (
    uint8_t subcommand,
    const uint8_t *payload,
    size_t length,
    uint32_t deadline
) {
    deadline = ed64_pro_ll_limit_deadline(deadline, ED64_PRO_IO_TIMEOUT_MS);
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    pro_ll.transferred = 0;
    ed64_pro_ll_error_t error = pro_assert_idle(deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_send_frame(PRO_CMD_DEV, subcommand, payload, length, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    ed64_pro_status_t status;
    return ed64_pro_ll_check_status(&status, deadline);
}

/**
 * @brief Set the absolute Game Pak, backup-memory, or disk path on the device.
 */
ed64_pro_ll_error_t ed64_pro_ll_dev_set_path (
    ed64_pro_path_t type,
    const char *path,
    uint32_t deadline
) {
    if ((type != ED64_PRO_PATH_GPAK && type != ED64_PRO_PATH_BRM && type != ED64_PRO_PATH_DISK) || !path || path[0] != '/') {
        return ED64_PRO_LL_ARGUMENT;
    }
    size_t length = strnlen(path, ED64_PRO_PATH_MAX + 1);
    if (!length || length > ED64_PRO_PATH_MAX) {
        return ED64_PRO_LL_ARGUMENT;
    }
    uint8_t payload[ED64_PRO_PATH_MAX + 3];
    payload[0] = type;
    payload[1] = length >> 8;
    payload[2] = length;
    memcpy(payload + 3, path, length);
    return pro_dev_command(0x21, payload, length + 3, deadline);
}

/**
 * @brief Validate and send the menu, cartridge, or native disk configuration.
 */
ed64_pro_ll_error_t
ed64_pro_ll_dev_set_cfg (const ed64_pro_dev_cfg_t *config, uint32_t deadline) {
    static const uint32_t brm_sizes[] = {0, 512, 2048, 32768, 98304, 131072, 131072};
    if (!config || config->dd_en > 1 || config->gpak_wren || config->gpak_key) {
        return ED64_PRO_LL_ARGUMENT;
    }
    /* Game-loading size policies do not apply to the RTC-only OFF profile. */
    bool game = config->brom_type == ED64_PRO_ROM_GPAK && config->gpak_size &&
        !(config->gpak_size & 511) && config->gpak_size <= ED64_PRO_FCI_ROM_LIMIT &&
        (!config->dd_en || config->gpak_size <= ED64_PRO_DD_ROM_LIMIT) &&
        config->brm_type <= ED64_PRO_BRM_SRM128K && config->brm_size == brm_sizes[config->brm_type] &&
        config->rtc_mode == (config->dd_en ? ED64_PRO_RTC_STD : ED64_PRO_RTC_RDO);
    bool disk = config->brom_type == ED64_PRO_ROM_IPL && !config->gpak_size && config->dd_en == 1 &&
        config->brm_type == ED64_PRO_BRM_OFF && !config->brm_size && config->rtc_mode == ED64_PRO_RTC_STD;
    bool menu = config->brom_type == ED64_PRO_ROM_OFF && !config->gpak_size && !config->dd_en &&
        config->brm_type == ED64_PRO_BRM_OFF && !config->brm_size && config->rtc_mode == ED64_PRO_RTC_STD;
    if (!game && !disk && !menu) {
        return ED64_PRO_LL_ARGUMENT;
    }
    for (size_t i = 0; i < sizeof(config->reserved) / sizeof(config->reserved[0]); i++) {
        if (config->reserved[i]) {
            return ED64_PRO_LL_ARGUMENT;
        }
    }
    uint32_t values[16] = {
        config->brom_type,
        config->gpak_size,
        config->brm_size,
        config->brm_type,
        config->rtc_mode,
        config->dd_en,
        config->gpak_wren,
        config->gpak_key,
    };
    uint8_t payload[64];
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        pro_put_u32(payload + i * 4, values[i]);
    }
    return pro_dev_command(0x20, payload, sizeof(payload), deadline);
}

/**
 * @brief Start the configured device and validate its MCU status.
 */
ed64_pro_ll_error_t ed64_pro_ll_dev_start (uint32_t deadline) {
    return pro_dev_command(0x11, NULL, 0, deadline);
}
