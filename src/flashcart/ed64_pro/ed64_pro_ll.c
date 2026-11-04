/**
 * @file ed64_pro_ll.c
 * @brief Cache-owned, bounded PI access for the EverDrive-64 Pro.
 *
 * Register layout and selected wire protocol: krikzz/ed64-pro-pub.
 * See LICENSE.krikzz for revision, adaptations, and the upstream MIT notice.
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
} pro_ll;

_Static_assert(sizeof(pro_staging) == 512, "Pro staging must own complete cache lines");

static bool pro_context_valid (void) {
    uint32_t sr = C0_STATUS();
    return get_interrupts_state() == INTERRUPTS_ENABLED && (sr & C0_STATUS_IE) &&
           !(sr & (C0_STATUS_EXL | C0_STATUS_ERL)) && kthread_current() == NULL;
}

static bool pro_expired (uint32_t deadline) {
    return (int32_t)(TICKS_READ() - deadline) >= 0;
}

/* The SDK's suggested replacement, dma_wait, cannot honor a deadline. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static bool pro_pi_busy (void) {
    return dma_busy() != 0;
}
#pragma GCC diagnostic pop

uint32_t ed64_pro_ll_limit_deadline (uint32_t deadline, uint32_t timeout_ms) {
    uint32_t local = TICKS_READ() + TICKS_FROM_MS(timeout_ms);
    return (int32_t)(local - deadline) < 0 ? local : deadline;
}

static ed64_pro_ll_error_t pro_fail (ed64_pro_ll_error_t error) {
    if (pro_ll.failure.code == ED64_PRO_LL_OK) {
        pro_ll.failure = (ed64_pro_ll_failure_t){
            .code = error,
            .transferred = pro_ll.transferred,
            .ticket = pro_ll.ticket,
            .poisoned = pro_ll.ticket != 0,
        };
    }
    return pro_ll.failure.code;
}

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

const ed64_pro_ll_failure_t *ed64_pro_ll_get_failure (void) {
    return &pro_ll.failure;
}

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

    if ((int32_t)(transaction->deadline - deadline) < 0) {
        deadline = transaction->deadline;
    }
    deadline = ed64_pro_ll_limit_deadline(deadline, ED64_PRO_PI_TIMEOUT_MS);
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
    return ED64_PRO_LL_OK;
}

ed64_pro_ll_error_t
ed64_pro_ll_read_reg (ed64_pro_ll_transaction_t *transaction, ed64_pro_reg_t reg, uint32_t *value, uint32_t deadline) {
    if (!value || !pro_readable_reg(reg)) {
        return ED64_PRO_LL_ARGUMENT;
    }
    ed64_pro_ll_error_t error = pro_transfer(transaction, reg, sizeof(uint32_t), false, deadline);
    if (error == ED64_PRO_LL_OK) {
        memcpy(value, pro_staging, sizeof(*value));
    }
    return error;
}

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
