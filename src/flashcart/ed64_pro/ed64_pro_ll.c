/**
 * @file ed64_pro_ll.c
 * @brief Cache-owned, bounded PI access for the EverDrive-64 Pro.
 *
 * Register layout and selected wire protocol: krikzz/ed64-pro-pub.
 * See README.md for the pinned revision, adaptations, and upstream MIT notice.
 */

#include <libdragon.h>
#include <string.h>

#include "ed64_pro_ll.h"

static uint32_t pro_staging[ED64_PRO_FIFO_CHUNK] __attribute__((aligned(512)));

static struct {
    ed64_pro_ll_transaction_t *owner;
    ed64_pro_ll_failure_t failure;
    uint64_t ticket;
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
    uint32_t pi_started;
    bool pi_active;
    bool pi_context_valid;
    uint32_t pi_address;
    size_t pi_length;
    bool pi_write;
    bool pi_submitted;
    uint32_t pi_budget_ticks;
    uint32_t pi_elapsed_ticks;
    bool pi_phase_limited;
} pro_ll;

_Static_assert(sizeof(pro_staging) == 512, "Pro staging must own complete cache lines");
_Static_assert(sizeof(ed64_pro_status_t) == 4, "Pro status is exactly four protocol bytes");
_Static_assert(sizeof(ed64_pro_dev_cfg_t) == 64, "Pro device configuration is sixteen 32-bit fields");

typedef enum {
    PRO_CMD_STATUS = 0x10,
    PRO_CMD_NRESP = 0x13,
    PRO_CMD_EPO = 0x81,
    PRO_CMD_SYS = 0x84,
    PRO_CMD_SD_INIT = 0x80,
    PRO_CMD_STOP = 0x85,
    PRO_CMD_DEV = 0x85,
} pro_command_t;

/**
 * @brief Allow transport only in an interrupt-enabled, non-kernel foreground context.
 */
static bool pro_context_valid (void) {
    uint32_t sr = C0_STATUS();
    return get_interrupts_state() == INTERRUPTS_ENABLED && (sr & C0_STATUS_IE) &&
           !(sr & (C0_STATUS_EXL | C0_STATUS_ERL)) && kthread_current() == NULL;
}

/**
 * @brief Check whether an absolute CP0 deadline has expired.
 */
static bool pro_expired (uint32_t deadline) {
    return (int32_t)(TICKS_READ() - deadline) >= 0;
}

/* The SDK's suggested replacement, dma_wait, cannot honor a deadline. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
/**
 * @brief Check whether the PI has an active DMA transfer.
 */
static bool pro_pi_busy (void) {
    return dma_busy() != 0;
}
#pragma GCC diagnostic pop

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
 * @brief Clamp a phase deadline to the owning transaction's whole-attempt deadline.
 */
static uint32_t pro_deadline (const ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    return (int32_t)(transaction->deadline - deadline) < 0 ? transaction->deadline : deadline;
}

/**
 * @brief Finish timing an active PI operation and retain the largest observed duration.
 */
static void pro_measure_pi (void) {
    if (pro_ll.pi_active) {
        uint32_t elapsed = TICKS_READ() - pro_ll.pi_started;
        pro_ll.pi_elapsed_ticks = elapsed;
        if (elapsed > pro_ll.metrics.max_pi_ticks) {
            pro_ll.metrics.max_pi_ticks = elapsed;
        }
        pro_ll.pi_active = false;
    }
}

/**
 * @brief Latch the first transport fault with confirmed RX progress and DMA ownership.
 *
 * A retained ticket poisons the transport: its staging buffer must remain
 * quarantined even if the outstanding DMA completes later.
 */
static ed64_pro_ll_error_t pro_fail (ed64_pro_ll_error_t error) {
    pro_measure_pi();
    if (pro_ll.failure.code == ED64_PRO_LL_OK) {
        pro_ll.failure = (ed64_pro_ll_failure_t){
            .code = error,
            .transferred = pro_ll.transferred,
            .ticket = pro_ll.ticket,
            .poisoned = pro_ll.ticket != 0,
            .status_valid = pro_ll.status_valid,
            .status = pro_ll.status,
            .nresp_valid = pro_ll.nresp_valid,
            .nresp = pro_ll.nresp,
            .pi_context_valid = pro_ll.pi_context_valid,
            .pi_address = pro_ll.pi_context_valid ? pro_ll.pi_address : 0,
            .pi_length = pro_ll.pi_context_valid ? pro_ll.pi_length : 0,
            .pi_write = pro_ll.pi_context_valid && pro_ll.pi_write,
            .pi_submitted = pro_ll.pi_context_valid && pro_ll.pi_submitted,
            .pi_budget_ticks = pro_ll.pi_context_valid ? pro_ll.pi_budget_ticks : 0,
            .pi_elapsed_ticks = pro_ll.pi_context_valid ? pro_ll.pi_elapsed_ticks : 0,
            .pi_phase_limited = pro_ll.pi_context_valid && pro_ll.pi_phase_limited,
        };
    }
    return pro_ll.failure.code;
}

/**
 * @brief Reject invalid context or ownership before returning any latched transport fault.
 */
static ed64_pro_ll_error_t pro_owned (ed64_pro_ll_transaction_t *transaction) {
    if (!transaction) {
        return ED64_PRO_LL_ARGUMENT;
    }
    if (!pro_context_valid()) {
        return ED64_PRO_LL_CONTEXT;
    }
    if (pro_ll.owner != transaction) {
        return ED64_PRO_LL_BUSY;
    }
    return pro_ll.failure.code;
}

/**
 * @brief Acquire exclusive foreground ownership without retrying a latched transport fault.
 *
 * The transaction's address is its token and must stay valid through end.
 * Interrupts must already be enabled; ISR and kernel callers are unsupported.
 */
ed64_pro_ll_error_t ed64_pro_ll_begin (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    if (!transaction) {
        return ED64_PRO_LL_ARGUMENT;
    }
    if (!pro_context_valid()) {
        return ED64_PRO_LL_CONTEXT;
    }

    disable_interrupts();
    ed64_pro_ll_error_t error = pro_ll.failure.code;
    if (pro_ll.owner) {
        error = ED64_PRO_LL_BUSY;
    } else if (error == ED64_PRO_LL_OK) {
        transaction->deadline = deadline;
        pro_ll.owner = transaction;
        pro_ll.transferred = 0;
    }
    enable_interrupts();
    return error;
}

/**
 * @brief Release the owner token without clearing faults or cancelling an outstanding DMA.
 */
ed64_pro_ll_error_t ed64_pro_ll_end (ed64_pro_ll_transaction_t *transaction) {
    if (!transaction) {
        return ED64_PRO_LL_ARGUMENT;
    }
    if (!pro_context_valid()) {
        return ED64_PRO_LL_CONTEXT;
    }
    if (pro_ll.owner != transaction) {
        return ED64_PRO_LL_BUSY;
    }
    pro_ll.owner = NULL;
    return ED64_PRO_LL_OK;
}

/**
 * @brief Return the first latched transport failure and its diagnostic context.
 */
const ed64_pro_ll_failure_t *ed64_pro_ll_get_failure (void) {
    return &pro_ll.failure;
}

/**
 * @brief Return cumulative PI timing, submission, and FIFO usage metrics.
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
 * @brief Transfer an exact bus length through cache-owned staging within bounded deadlines.
 *
 * PI acquisition is rechecked under IRQ exclusion. Reads become visible only
 * after confirmed completion; an issued DMA fault retains its ticket and buffer
 * rather than resetting PI or pretending the transfer was cancelled.
 */
static ed64_pro_ll_error_t pro_transfer (
    ed64_pro_ll_transaction_t *transaction,
    ed64_pro_reg_t reg,
    size_t length,
    bool write,
    uint32_t deadline
) {
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (length == 0 || length > sizeof(pro_staging) || length % sizeof(uint32_t) != 0 ||
        (reg != ED64_PRO_REG_FIFO && (!pro_readable_reg(reg) || length != sizeof(uint32_t))) ||
        (write && reg != ED64_PRO_REG_FIFO && reg != ED64_PRO_REG_MBX)) {
        return ED64_PRO_LL_ARGUMENT;
    }

    if (!pro_ll.identified && (pro_ll.probed || write || reg != ED64_PRO_REG_EDID)) {
        return ED64_PRO_LL_NOT_IDENTIFIED;
    }
    deadline = pro_deadline(transaction, deadline);
    /* Record the attempt before acquisition; this performs no additional PI I/O. */
    pro_ll.pi_context_valid = true;
    pro_ll.pi_address = (uint32_t)reg;
    pro_ll.pi_length = length;
    pro_ll.pi_write = write;
    pro_ll.pi_submitted = false;
    pro_ll.pi_started = TICKS_READ();
    pro_ll.pi_active = true;
    uint32_t pi_deadline = pro_ll.pi_started + TICKS_FROM_MS(ED64_PRO_PI_TIMEOUT_MS);
    pro_ll.pi_phase_limited = (int32_t)(deadline - pi_deadline) < 0;
    if ((int32_t)(pi_deadline - deadline) < 0) {
        deadline = pi_deadline;
    }
    pro_ll.pi_budget_ticks = (int32_t)(deadline - pro_ll.pi_started) > 0 ? deadline - pro_ll.pi_started : 0;
    pro_ll.pi_elapsed_ticks = 0;
    if (pro_expired(deadline)) {
        return pro_fail(ED64_PRO_LL_PI_ACQUIRE_TIMEOUT);
    }

    size_t cache_length = (length + ED64_PRO_CACHE_LINE - 1) & ~(size_t)(ED64_PRO_CACHE_LINE - 1);
    if (write) {
        data_cache_hit_writeback(pro_staging, cache_length);
    } else {
        data_cache_hit_writeback_invalidate(pro_staging, cache_length);
    }
    MEMORY_BARRIER();

    for (;;) {
        if (pro_expired(deadline)) {
            return pro_fail(ED64_PRO_LL_PI_ACQUIRE_TIMEOUT);
        }
        if (*PI_STATUS & PI_STATUS_ERROR) {
            return pro_fail(ED64_PRO_LL_PI_ERROR);
        }
        if (pro_pi_busy()) {
            continue;
        }

        /* An idle recheck under IRQ exclusion makes pi_enqueue's full-queue wait unreachable. */
        disable_interrupts();
        bool busy = pro_pi_busy();
        bool expired = pro_expired(deadline);
        bool pi_error = (*PI_STATUS & PI_STATUS_ERROR) != 0;
        if (!busy && !expired && !pi_error) {
            pro_ll.ticket =
                write ? dma_write_raw_async(pro_staging, reg, length) : dma_read_raw_async(pro_staging, reg, length);
            pro_ll.pi_submitted = true;
            pro_ll.metrics.submissions++;
        }
        enable_interrupts();
        if (pi_error) {
            return pro_fail(ED64_PRO_LL_PI_ERROR);
        }
        if (expired) {
            return pro_fail(ED64_PRO_LL_PI_ACQUIRE_TIMEOUT);
        }
        if (!busy) {
            break;
        }
    }
    MEMORY_BARRIER();

    for (;;) {
        if (pro_expired(deadline)) {
            return pro_fail(ED64_PRO_LL_PI_TIMEOUT);
        }
        if (*PI_STATUS & PI_STATUS_ERROR) {
            return pro_fail(ED64_PRO_LL_PI_ERROR);
        }
        bool finished = dma_finished(pro_ll.ticket);
#ifdef ED64_PRO_TEST_FORCE_TIMEOUT
        /* Software-only fault injection; never reset PI or pretend DMA was cancelled. */
        finished = false;
#endif
        if (*PI_STATUS & PI_STATUS_ERROR) {
            return pro_fail(ED64_PRO_LL_PI_ERROR);
        }
        if (pro_expired(deadline)) {
            return pro_fail(ED64_PRO_LL_PI_TIMEOUT);
        }
        if (finished) {
            break;
        }
    }

    pro_ll.ticket = 0;
    if (!write) {
        data_cache_hit_invalidate(pro_staging, cache_length);
    }
    MEMORY_BARRIER();
    pro_measure_pi();
    pro_ll.pi_context_valid = false;
    return ED64_PRO_LL_OK;
}

/**
 * @brief Read one whitelisted register and publish its value only after successful DMA.
 *
 * The first completed EDID read caches the Pro family/device identification.
 */
ed64_pro_ll_error_t
ed64_pro_ll_read_reg (ed64_pro_ll_transaction_t *transaction, ed64_pro_reg_t reg, uint32_t *value, uint32_t deadline) {
    if (!value || !pro_readable_reg(reg)) {
        return ED64_PRO_LL_ARGUMENT;
    }
    ed64_pro_ll_error_t error = pro_transfer(transaction, reg, sizeof(uint32_t), false, deadline);
    if (error == ED64_PRO_LL_OK) {
        if (reg == ED64_PRO_REG_EDID && !pro_ll.probed) {
            pro_ll.id = pro_staging[0];
            pro_ll.probed = true;
            pro_ll.identified = (pro_ll.id & 0xFFFF00FFu) == 0xED640027u;
        }
        memcpy(value, pro_staging, sizeof(*value));
    }
    return error;
}

/**
 * @brief Permit only the mailbox zero/NOP write after ownership and Pro identification checks.
 */
ed64_pro_ll_error_t
ed64_pro_ll_write_reg (ed64_pro_ll_transaction_t *transaction, ed64_pro_reg_t reg, uint32_t value, uint32_t deadline) {
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (reg != ED64_PRO_REG_MBX || value != 0) {
        return ED64_PRO_LL_ARGUMENT;
    }
    pro_staging[0] = value;
    return pro_transfer(transaction, reg, sizeof(uint32_t), true, deadline);
}

/**
 * @brief Cache one passive EDID read without ever classifying an I/O failure as absence.
 */
ed64_pro_probe_t ed64_pro_ll_probe (uint32_t deadline) {
    ed64_pro_ll_transaction_t transaction;
    ed64_pro_ll_error_t error = ed64_pro_ll_begin(&transaction, deadline);
    if (error == ED64_PRO_LL_OK) {
        if (!pro_ll.probed) {
            uint32_t id;
            error = ed64_pro_ll_read_reg(&transaction, ED64_PRO_REG_EDID, &id, deadline);
        }
        ed64_pro_ll_error_t release_error = ed64_pro_ll_end(&transaction);
        if (error == ED64_PRO_LL_OK) {
            error = release_error;
        }
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
pro_fifo_available (ed64_pro_ll_transaction_t *transaction, uint32_t *available, uint32_t deadline) {
    uint32_t value;
    ed64_pro_ll_error_t error = ed64_pro_ll_read_reg(transaction, ED64_PRO_REG_FIFOSTAT, &value, deadline);
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
pro_fifo_read (ed64_pro_ll_transaction_t *transaction, uint8_t *output, size_t length, uint32_t deadline) {
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (length > ED64_PRO_FIFO_LIMIT || (length && !output)) {
        return ED64_PRO_LL_ARGUMENT;
    }
    deadline = pro_deadline(transaction, deadline);
    pro_ll.transferred = 0;
    while (pro_ll.transferred < length) {
        if (pro_expired(deadline)) {
            return pro_fail(ED64_PRO_LL_TIMEOUT);
        }
        uint32_t available;
        error = pro_fifo_available(transaction, &available, deadline);
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
        error = pro_transfer(transaction, ED64_PRO_REG_FIFO, chunk * sizeof(uint32_t), false, deadline);
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
ed64_pro_ll_error_t ed64_pro_ll_drain (ed64_pro_ll_transaction_t *transaction, size_t *drained, uint32_t deadline) {
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (!drained) {
        return ED64_PRO_LL_ARGUMENT;
    }
    deadline = ed64_pro_ll_limit_deadline(pro_deadline(transaction, deadline), ED64_PRO_DRAIN_TIMEOUT_MS);
    pro_ll.transferred = 0;
    for (;;) {
        if (pro_expired(deadline)) {
            return pro_fail(ED64_PRO_LL_TIMEOUT);
        }
        uint32_t available;
        error = pro_fifo_available(transaction, &available, deadline);
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
        error = pro_transfer(transaction, ED64_PRO_REG_FIFO, chunk * sizeof(uint32_t), false, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        pro_ll.transferred += chunk;
    }
}

/**
 * @brief Send a frame for an allowed command and subcommand through the FIFO.
 *
 * Only typed builders may select protocol operations.
 */
static ed64_pro_ll_error_t pro_send_frame (
    ed64_pro_ll_transaction_t *transaction,
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
    bool selected =
        (command == PRO_CMD_STATUS && subcommand == -1) || (command == PRO_CMD_NRESP && subcommand == -1) ||
        ((command == PRO_CMD_STOP || command == PRO_CMD_SD_INIT || command == PRO_CMD_SYS) && subcommand == 0x10) ||
        (command == PRO_CMD_EPO && (subcommand == 0x10 || subcommand == 0x11)) ||
        (command == PRO_CMD_DEV &&
         (subcommand == 0x11 || subcommand == 0x20 || subcommand == 0x21 || subcommand == 0x28));
    if (!selected) {
        return ED64_PRO_LL_ARGUMENT;
    }
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
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
        error = pro_transfer(transaction, ED64_PRO_REG_FIFO, chunk * sizeof(uint32_t), true, deadline);
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
pro_send_command (ed64_pro_ll_transaction_t *transaction, pro_command_t command, uint32_t deadline) {
    if (command != PRO_CMD_STATUS && command != PRO_CMD_STOP && command != PRO_CMD_SD_INIT) {
        return ED64_PRO_LL_ARGUMENT;
    }
    return pro_send_frame(transaction, command, command == PRO_CMD_STATUS ? -1 : 0x10, NULL, 0, deadline);
}

/**
 * @brief Write bytes to the FIFO in staging-sized chunks within the deadline.
 */
static ed64_pro_ll_error_t
pro_fifo_write (ed64_pro_ll_transaction_t *transaction, const uint8_t *input, size_t length, uint32_t deadline) {
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
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
        error = pro_transfer(transaction, ED64_PRO_REG_FIFO, chunk * sizeof(uint32_t), true, deadline);
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
static ed64_pro_ll_error_t pro_assert_idle (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    uint32_t available = 0;
    ed64_pro_ll_error_t error = pro_fifo_available(transaction, &available, deadline);
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
ed64_pro_ll_read_status (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline) {
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (!status) {
        return ED64_PRO_LL_ARGUMENT;
    }
    deadline = pro_deadline(transaction, deadline);
    pro_ll.transferred = 0;
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    uint32_t available;
    error = pro_fifo_available(transaction, &available, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (available) {
        return pro_fail(ED64_PRO_LL_TRAILING_DATA);
    }
    error = pro_send_command(transaction, PRO_CMD_STATUS, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t reply[4];
    error = pro_fifo_read(transaction, reply, sizeof(reply), deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (reply[0] != 0x5A) {
        return pro_fail(ED64_PRO_LL_FRAME);
    }
    error = pro_fifo_available(transaction, &available, deadline);
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
 * All polls share the original capped stop/transaction deadline.
 */
ed64_pro_ll_error_t ed64_pro_ll_stop (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    deadline = ed64_pro_ll_limit_deadline(pro_deadline(transaction, deadline), ED64_PRO_STOP_TIMEOUT_MS);
    pro_ll.transferred = 0;
    error = ed64_pro_ll_write_reg(transaction, ED64_PRO_REG_MBX, 0, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_send_command(transaction, PRO_CMD_STOP, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    for (;;) {
        if (pro_expired(deadline)) {
            return pro_fail(ED64_PRO_LL_MAILBOX_TIMEOUT);
        }
        uint32_t first, second;
        error = ed64_pro_ll_read_reg(transaction, ED64_PRO_REG_MBX, &first, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        error = ed64_pro_ll_read_reg(transaction, ED64_PRO_REG_MBX, &second, deadline);
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
ed64_pro_ll_check_status (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline) {
    ed64_pro_ll_error_t error = ed64_pro_ll_read_status(transaction, status, deadline);
    if (error == ED64_PRO_LL_OK && status->mcu_status != 0) {
        uint8_t detail;
        error = ed64_pro_ll_read_nresp(transaction, status->mcu_status, &detail, deadline);
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
ed64_pro_ll_init_sd (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline) {
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (!status) {
        return ED64_PRO_LL_ARGUMENT;
    }
    deadline = ed64_pro_ll_limit_deadline(pro_deadline(transaction, deadline), ED64_PRO_SD_TIMEOUT_MS);
    error = pro_send_command(transaction, PRO_CMD_SD_INIT, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    return ed64_pro_ll_check_status(transaction, status, deadline);
}

/**
 * @brief Read and retain the detail byte for the current nonzero MCU status.
 */
ed64_pro_ll_error_t
ed64_pro_ll_read_nresp (ed64_pro_ll_transaction_t *transaction, uint8_t category, uint8_t *detail, uint32_t deadline) {
    if (!detail || !category || !pro_ll.status_valid || pro_ll.status != category || pro_ll.nresp_valid) {
        return ED64_PRO_LL_ARGUMENT;
    }
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_assert_idle(transaction, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_send_frame(transaction, PRO_CMD_NRESP, -1, &category, 1, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t reply;
    error = pro_fifo_read(transaction, &reply, 1, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_assert_idle(transaction, deadline);
    if (error == ED64_PRO_LL_OK) {
        pro_ll.nresp = reply;
        pro_ll.nresp_valid = true;
        *detail = reply;
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
 * @brief Read the selected firmware timestamp or maximum ROM size values.
 */
ed64_pro_ll_error_t ed64_pro_ll_get_sys_info (
    ed64_pro_ll_transaction_t *transaction,
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
        if (ids[i] != ED64_PRO_INFS_TS_FW && ids[i] != ED64_PRO_INFS_MAX_ROM_SIZE) {
            return ED64_PRO_LL_ARGUMENT;
        }
        pro_put_u32(&payload[1 + i * 4], ids[i]);
    }
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_assert_idle(transaction, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_send_frame(transaction, PRO_CMD_SYS, 0x10, payload, 1 + count * 4, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t reply[8];
    error = pro_fifo_read(transaction, reply, count * 4, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_assert_idle(transaction, deadline);
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
pro_fifo_read_ack (ed64_pro_ll_transaction_t *transaction, uint8_t *output, size_t length, uint32_t deadline) {
    if (length && !output) {
        return ED64_PRO_LL_ARGUMENT;
    }
    for (size_t offset = 0; offset < length;) {
        size_t block = length - offset;
        if (block > ED64_PRO_ACK_BLOCK) {
            block = ED64_PRO_ACK_BLOCK;
        }
        const uint8_t ack = 0;
        ed64_pro_ll_error_t error = pro_fifo_write(transaction, &ack, 1, deadline);
        if (error == ED64_PRO_LL_OK) {
            error = pro_fifo_read(transaction, output + offset, block, deadline);
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
    ed64_pro_ll_transaction_t *transaction,
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
        ed64_pro_ll_error_t error = pro_fifo_read(transaction, &ack, 1, deadline);
        if (error == ED64_PRO_LL_OK) {
            error = pro_fifo_write(transaction, input + offset, block, deadline);
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
pro_sd_xfer (ed64_pro_ll_transaction_t *transaction, uint32_t lba, uint32_t count, bool write, uint32_t deadline) {
    uint8_t payload[17] = {0};
    pro_put_u32(&payload[write ? 4 : 0], lba);
    pro_put_u32(&payload[8], count * 512);
    payload[12] = write ? 0x11 : 0x19;
    payload[13] = write ? 0x19 : 0x11;
    return pro_send_frame(transaction, PRO_CMD_EPO, 0x10, payload, sizeof(payload), deadline);
}

/**
 * @brief Transfer SD sectors through the FIFO and validate the final MCU status.
 */
static ed64_pro_ll_error_t pro_sd_transfer (
    ed64_pro_ll_transaction_t *transaction,
    void *buffer,
    uint32_t lba,
    uint32_t count,
    bool write,
    uint32_t deadline
) {
    if (!buffer || !count || count > 64 || count - 1 > UINT32_MAX - lba) {
        return ED64_PRO_LL_ARGUMENT;
    }
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    deadline = ed64_pro_ll_limit_deadline(pro_deadline(transaction, deadline), ED64_PRO_IO_TIMEOUT_MS);
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    error = pro_assert_idle(transaction, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    if (write) {
        const uint8_t key[] = {0xDE, 0xAD, 0xBE, 0xEF};
        error = pro_send_frame(transaction, PRO_CMD_EPO, 0x11, key, sizeof(key), deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        error = pro_assert_idle(transaction, deadline);
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
    }
    error = pro_sd_xfer(transaction, lba, count, write, deadline);
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
    error = write ? pro_fifo_write_ack(transaction, buffer, count * 512, fail_after_block, deadline)
                  : pro_fifo_read_ack(transaction, buffer, count * 512, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    ed64_pro_status_t status;
    return ed64_pro_ll_check_status(transaction, &status, deadline);
}

/**
 * @brief Read SD sectors into the caller buffer through the FIFO.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_read (
    ed64_pro_ll_transaction_t *transaction,
    void *output,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
) {
    return pro_sd_transfer(transaction, output, lba, count, false, deadline);
}

/**
 * @brief Write SD sectors from the caller buffer through the FIFO.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_write (
    ed64_pro_ll_transaction_t *transaction,
    const void *input,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
) {
    /* Once WRE has been sent, any failure leaves the write ambiguous. Never retry. */
    return pro_sd_transfer(transaction, (void *)input, lba, count, true, deadline);
}

/**
 * @brief Copy SD sectors directly into cartridge ROM memory and check MCU status.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_read_fci (
    ed64_pro_ll_transaction_t *transaction,
    uint32_t fci,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
) {
    if (!count || count > 256 || count - 1 > UINT32_MAX - lba || (fci & 511) || fci >= ED64_PRO_FCI_ROM_LIMIT ||
        count * 512 > ED64_PRO_FCI_ROM_LIMIT - fci) {
        return ED64_PRO_LL_ARGUMENT;
    }
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    deadline = ed64_pro_ll_limit_deadline(pro_deadline(transaction, deadline), ED64_PRO_IO_TIMEOUT_MS);
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    error = pro_assert_idle(transaction, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t payload[17] = {0};
    pro_put_u32(&payload[0], lba);
    pro_put_u32(&payload[4], fci);
    pro_put_u32(&payload[8], count * 512);
    payload[12] = 0x19;
    payload[13] = 0x13;
    error = pro_send_frame(transaction, PRO_CMD_EPO, 0x10, payload, sizeof(payload), deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    ed64_pro_status_t status;
    return ed64_pro_ll_check_status(transaction, &status, deadline);
}

/**
 * @brief Set the ROM byte-swap flag and wait for stable, idle device status.
 */
ed64_pro_ll_error_t
ed64_pro_ll_set_flags (ed64_pro_ll_transaction_t *transaction, uint16_t mask, uint16_t value, uint32_t deadline) {
    if (mask != ED64_PRO_FLAG_WRSW || (value & ~mask)) {
        return ED64_PRO_LL_ARGUMENT;
    }
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    deadline = ed64_pro_ll_limit_deadline(pro_deadline(transaction, deadline), ED64_PRO_IO_TIMEOUT_MS);
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    error = pro_assert_idle(transaction, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    uint8_t payload[4];
    pro_put_u32(payload, (uint32_t)mask << 16 | value);
    error = pro_send_frame(transaction, PRO_CMD_DEV, 0x28, payload, sizeof(payload), deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    for (;;) {
        if (pro_expired(pro_deadline(transaction, deadline))) {
            return pro_fail(ED64_PRO_LL_TIMEOUT);
        }
        uint32_t first = 0, second = 0;
        error = ed64_pro_ll_read_reg(transaction, ED64_PRO_REG_SYSSTAT, &first, deadline);
        if (error == ED64_PRO_LL_OK) {
            error = ed64_pro_ll_read_reg(transaction, ED64_PRO_REG_SYSSTAT, &second, deadline);
        }
        if (error != ED64_PRO_LL_OK) {
            return error;
        }
        if ((first & ~8u) == (second & ~8u) && !(second & 1u)) {
            return pro_assert_idle(transaction, deadline);
        }
    }
}

/**
 * @brief Send a device command and validate its fresh MCU status.
 */
static ed64_pro_ll_error_t pro_dev_command (
    ed64_pro_ll_transaction_t *transaction,
    uint8_t subcommand,
    const uint8_t *payload,
    size_t length,
    uint32_t deadline
) {
    ed64_pro_ll_error_t error = pro_owned(transaction);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    deadline = ed64_pro_ll_limit_deadline(pro_deadline(transaction, deadline), ED64_PRO_IO_TIMEOUT_MS);
    pro_ll.status_valid = false;
    pro_ll.nresp_valid = false;
    error = pro_assert_idle(transaction, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    error = pro_send_frame(transaction, PRO_CMD_DEV, subcommand, payload, length, deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    ed64_pro_status_t status;
    return ed64_pro_ll_check_status(transaction, &status, deadline);
}

/**
 * @brief Set the absolute Game Pak or backup-memory path on the device.
 */
ed64_pro_ll_error_t ed64_pro_ll_dev_set_path (
    ed64_pro_ll_transaction_t *transaction,
    ed64_pro_path_t type,
    const char *path,
    uint32_t deadline
) {
    if ((type != ED64_PRO_PATH_GPAK && type != ED64_PRO_PATH_BRM) || !path || path[0] != '/') {
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
    return pro_dev_command(transaction, 0x21, payload, length + 3, deadline);
}

/**
 * @brief Validate and send the Game Pak and backup-memory configuration.
 */
ed64_pro_ll_error_t
ed64_pro_ll_dev_set_cfg (ed64_pro_ll_transaction_t *transaction, const ed64_pro_dev_cfg_t *config, uint32_t deadline) {
    static const uint32_t brm_sizes[] = {0, 512, 2048, 32768, 98304, 131072, 131072};
    if (!config || config->brom_type != ED64_PRO_ROM_GPAK || !config->gpak_size || (config->gpak_size & 511) ||
        config->gpak_size > ED64_PRO_FCI_ROM_LIMIT || config->brm_type > ED64_PRO_BRM_SRM128K ||
        config->brm_size != brm_sizes[config->brm_type] || config->rtc_mode != ED64_PRO_RTC_RDO || config->dd_en ||
        config->gpak_wren || config->gpak_key) {
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
    return pro_dev_command(transaction, 0x20, payload, sizeof(payload), deadline);
}

/**
 * @brief Start the configured device and validate its MCU status.
 */
ed64_pro_ll_error_t ed64_pro_ll_dev_start (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    return pro_dev_command(transaction, 0x11, NULL, 0, deadline);
}
