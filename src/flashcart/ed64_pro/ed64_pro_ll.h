/**
 * @file ed64_pro_ll.h
 * @brief Bounded, foreground-only EverDrive-64 Pro transport.
 */

#ifndef FLASHCART_ED64_PRO_LL_H__
#define FLASHCART_ED64_PRO_LL_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ED64_PRO_REG_FIFO = 0x1F800000u,
    ED64_PRO_REG_FIFOSTAT = 0x1F800004u,
    ED64_PRO_REG_SYSSTAT = 0x1F800008u,
    ED64_PRO_REG_TIMER = 0x1F80000Cu,
    ED64_PRO_REG_MBX = 0x1F800010u,
    ED64_PRO_REG_EDID = 0x1F800014u,
    ED64_PRO_REG_MCTR = 0x1F800018u,
} ed64_pro_reg_t;

typedef enum {
    ED64_PRO_LL_OK,
    ED64_PRO_LL_ARGUMENT,
    ED64_PRO_LL_CONTEXT,
    ED64_PRO_LL_BUSY,
    ED64_PRO_LL_TIMEOUT,
    ED64_PRO_LL_PI_ACQUIRE_TIMEOUT,
    ED64_PRO_LL_PI_TIMEOUT,
    ED64_PRO_LL_PI_ERROR,
} ed64_pro_ll_error_t;

enum {
    ED64_PRO_PI_TIMEOUT_MS = 10,
    ED64_PRO_ATTEMPT_TIMEOUT_MS = 15000,
    ED64_PRO_FIFO_CHUNK = 128,
    ED64_PRO_CACHE_LINE = 16,
};

typedef struct {
    uint32_t deadline;
} ed64_pro_ll_transaction_t;

typedef struct {
    ed64_pro_ll_error_t code;
    size_t transferred;
    uint64_t ticket;
    bool poisoned;
} ed64_pro_ll_failure_t;

/* Absolute CP0 deadlines must be within the signed half-range of the counter. */
uint32_t ed64_pro_ll_limit_deadline (uint32_t deadline, uint32_t timeout_ms);

/*
 * The transaction's address is its ownership token. Keep it alive and unchanged
 * until end; never pass it to another caller. Interrupts must be enabled and the
 * libdragon kernel must not be running. No ISR may produce competing PI work.
 */
ed64_pro_ll_error_t ed64_pro_ll_begin (ed64_pro_ll_transaction_t *transaction, uint32_t deadline);
ed64_pro_ll_error_t ed64_pro_ll_end (ed64_pro_ll_transaction_t *transaction);
const ed64_pro_ll_failure_t *ed64_pro_ll_get_failure (void);

ed64_pro_ll_error_t
ed64_pro_ll_read_reg (ed64_pro_ll_transaction_t *transaction, ed64_pro_reg_t reg, uint32_t *value, uint32_t deadline);
/* Only the selected mailbox NOP write is supported, not arbitrary register I/O. */
ed64_pro_ll_error_t
ed64_pro_ll_write_reg (ed64_pro_ll_transaction_t *transaction, ed64_pro_reg_t reg, uint32_t value, uint32_t deadline);

#endif
