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
    ED64_PRO_LL_NOT_IDENTIFIED,
    ED64_PRO_LL_FIFO_COUNT,
    ED64_PRO_LL_DRAIN_LIMIT,
    ED64_PRO_LL_FRAME,
    ED64_PRO_LL_TRAILING_DATA,
    ED64_PRO_LL_MAILBOX_TIMEOUT,
    ED64_PRO_LL_MCU_ERROR,
} ed64_pro_ll_error_t;

enum {
    ED64_PRO_PI_TIMEOUT_MS = 10,
    ED64_PRO_DRAIN_TIMEOUT_MS = 100,
    ED64_PRO_STATUS_TIMEOUT_MS = 1000,
    ED64_PRO_STOP_TIMEOUT_MS = 5000,
    ED64_PRO_SD_TIMEOUT_MS = 5000,
    ED64_PRO_IO_TIMEOUT_MS = 5000,
    ED64_PRO_FLAG_WRSW = 0x01,
    ED64_PRO_PATH_MAX = 1023,
    ED64_PRO_FCI_ROM_LIMIT = 0x0F000000,
    ED64_PRO_FRAME_MAX = 1536,
    ED64_PRO_ACK_BLOCK = 1024,
    ED64_PRO_INFS_TS_FW = 6,
    ED64_PRO_INFS_MAX_ROM_SIZE = 9,
    ED64_PRO_ATTEMPT_TIMEOUT_MS = 15000,
    ED64_PRO_FIFO_CHUNK = 128,
    ED64_PRO_FIFO_LIMIT = 2048,
    ED64_PRO_CACHE_LINE = 16,
};

typedef enum {
    ED64_PRO_PROBE_ABSENT,
    ED64_PRO_PROBE_IDENTIFIED,
    ED64_PRO_PROBE_IO_ERROR,
} ed64_pro_probe_outcome_t;

typedef struct {
    ed64_pro_probe_outcome_t outcome;
    ed64_pro_ll_error_t error;
    uint32_t id;
    bool id_valid;
} ed64_pro_probe_t;

typedef struct {
    uint8_t key;
    uint8_t opaque[2];
    uint8_t mcu_status;
} ed64_pro_status_t;

typedef enum {
    ED64_PRO_PATH_GPAK = 0,
    ED64_PRO_PATH_BRM = 2,
} ed64_pro_path_t;

typedef enum {
    ED64_PRO_BRM_OFF = 0,
    ED64_PRO_BRM_EEP4K,
    ED64_PRO_BRM_EEP16K,
    ED64_PRO_BRM_SRM32K,
    ED64_PRO_BRM_SRM96K,
    ED64_PRO_BRM_FLASH,
    ED64_PRO_BRM_SRM128K,
} ed64_pro_brm_t;

enum {
    ED64_PRO_ROM_GPAK = 2,
    ED64_PRO_RTC_RDO = 2,
};

typedef struct {
    uint32_t brom_type;
    uint32_t gpak_size;
    uint32_t brm_size;
    uint32_t brm_type;
    uint32_t rtc_mode;
    uint32_t dd_en;
    uint32_t gpak_wren;
    uint32_t gpak_key;
    uint32_t reserved[8];
} ed64_pro_dev_cfg_t;

typedef struct {
    uint32_t deadline;
} ed64_pro_ll_transaction_t;

typedef struct {
    ed64_pro_ll_error_t code;
    size_t transferred;
    uint64_t ticket;
    bool poisoned;
    bool status_valid;
    uint8_t status;
    bool nresp_valid;
    uint8_t nresp;
    /* Valid only for an attempted PI operation active when this fault latched.
     * Protocol-only faults do not identify the last completed PI access as failed.
     * Length is bus bytes (FIFO payload bytes use one 32-bit word each).
     */
    bool pi_context_valid;
    uint32_t pi_address;
    size_t pi_length;
    bool pi_write;
    bool pi_submitted;
    /* Effective budget and existing PI timer measurement, in CP0 ticks.
     * phase_limited means the phase/transaction deadline shortened the PI cap.
     */
    uint32_t pi_budget_ticks;
    uint32_t pi_elapsed_ticks;
    bool pi_phase_limited;
} ed64_pro_ll_failure_t;

typedef struct {
    uint32_t max_pi_ticks;
    uint32_t rx_peak;
    uint32_t submissions;
} ed64_pro_ll_metrics_t;

/**
 * @brief Cap an absolute deadline by a local timeout.
 *
 * Absolute CP0 deadlines must be within the signed half-range of the counter.
 */
uint32_t ed64_pro_ll_limit_deadline (uint32_t deadline, uint32_t timeout_ms);

/**
 * @brief Acquire exclusive foreground ownership of the transport.
 *
 * The transaction's address is its ownership token. Keep it alive and unchanged
 * until end; never pass it to another caller. Interrupts must be enabled and the
 * libdragon kernel must not be running. No ISR may produce competing PI work.
 */
ed64_pro_ll_error_t ed64_pro_ll_begin (ed64_pro_ll_transaction_t *transaction, uint32_t deadline);
/**
 * @brief Release transport ownership without clearing faults or cancelling DMA.
 */
ed64_pro_ll_error_t ed64_pro_ll_end (ed64_pro_ll_transaction_t *transaction);
/**
 * @brief Return the first latched transport failure and its diagnostic context.
 */
const ed64_pro_ll_failure_t *ed64_pro_ll_get_failure (void);
/**
 * @brief Return cumulative PI timing, submission, and FIFO usage metrics.
 */
const ed64_pro_ll_metrics_t *ed64_pro_ll_get_metrics (void);

/**
 * @brief Probe and cache the Pro identity with one passive EDID read per launch.
 *
 * A failed read is never absence.
 */
ed64_pro_probe_t ed64_pro_ll_probe (uint32_t deadline);

/**
 * @brief Read an allowed register and publish its value after successful DMA.
 */
ed64_pro_ll_error_t
ed64_pro_ll_read_reg (ed64_pro_ll_transaction_t *transaction, ed64_pro_reg_t reg, uint32_t *value, uint32_t deadline);
/**
 * @brief Write the mailbox NOP value; arbitrary register writes are unsupported.
 */
ed64_pro_ll_error_t
ed64_pro_ll_write_reg (ed64_pro_ll_transaction_t *transaction, ed64_pro_reg_t reg, uint32_t value, uint32_t deadline);

/**
 * @brief Discard pending FIFO data within the cleanup time and byte limits.
 *
 * Only initial and post-stop handoff cleanup may discard data.
 */
ed64_pro_ll_error_t ed64_pro_ll_drain (ed64_pro_ll_transaction_t *transaction, size_t *drained, uint32_t deadline);
/**
 * @brief Request and validate one status frame without rejecting nonzero MCU status.
 *
 * Caller supplies the original phase deadline. Output is valid only on LL_OK.
 */
ed64_pro_ll_error_t
ed64_pro_ll_read_status (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline);

/**
 * @brief Send STOP and wait for two matching mailbox READY samples.
 */
ed64_pro_ll_error_t ed64_pro_ll_stop (ed64_pro_ll_transaction_t *transaction, uint32_t deadline);
/**
 * @brief Check fresh MCU status and retain the complete frame on MCU_ERROR.
 */
ed64_pro_ll_error_t
ed64_pro_ll_check_status (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline);
/**
 * @brief Initialize the SD card and publish fresh status, including MCU_ERROR replies.
 */
ed64_pro_ll_error_t
ed64_pro_ll_init_sd (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline);

/**
 * @brief Read the detail byte for the current nonzero MCU status.
 */
ed64_pro_ll_error_t
ed64_pro_ll_read_nresp (ed64_pro_ll_transaction_t *transaction, uint8_t category, uint8_t *detail, uint32_t deadline);
/**
 * @brief Read the selected firmware timestamp or maximum ROM size values.
 */
ed64_pro_ll_error_t ed64_pro_ll_get_sys_info (
    ed64_pro_ll_transaction_t *transaction,
    const uint32_t *ids,
    uint32_t *values,
    size_t count,
    uint32_t deadline
);
/**
 * @brief Read SD sectors into the caller buffer through the FIFO.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_read (
    ed64_pro_ll_transaction_t *transaction,
    void *output,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
);
/**
 * @brief Write SD sectors from the caller buffer through the FIFO.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_write (
    ed64_pro_ll_transaction_t *transaction,
    const void *input,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
);

/**
 * @brief Copy SD sectors directly into cartridge ROM memory.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_read_fci (
    ed64_pro_ll_transaction_t *transaction,
    uint32_t fci,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
);
/**
 * @brief Set the ROM byte-swap flag and wait for stable, idle device status.
 */
ed64_pro_ll_error_t
ed64_pro_ll_set_flags (ed64_pro_ll_transaction_t *transaction, uint16_t mask, uint16_t value, uint32_t deadline);

/**
 * @brief Set the absolute Game Pak or backup-memory path on the device.
 */
ed64_pro_ll_error_t ed64_pro_ll_dev_set_path (
    ed64_pro_ll_transaction_t *transaction,
    ed64_pro_path_t type,
    const char *path,
    uint32_t deadline
);
/**
 * @brief Validate and send the Game Pak and backup-memory configuration.
 */
ed64_pro_ll_error_t
ed64_pro_ll_dev_set_cfg (ed64_pro_ll_transaction_t *transaction, const ed64_pro_dev_cfg_t *config, uint32_t deadline);
/**
 * @brief Start the configured device and validate its MCU status.
 */
ed64_pro_ll_error_t ed64_pro_ll_dev_start (ed64_pro_ll_transaction_t *transaction, uint32_t deadline);

#endif
