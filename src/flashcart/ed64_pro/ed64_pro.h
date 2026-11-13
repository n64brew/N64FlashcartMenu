/**
 * @file ed64_pro.h
 * @brief ed64_pro flashcart support
 * @ingroup flashcart
 */

#ifndef FLASHCART_ED64_PRO_H__
#define FLASHCART_ED64_PRO_H__

#include "../flashcart.h"
#include "ed64_pro_ll.h"

typedef enum {
    ED64_PRO_COLD,
    ED64_PRO_ABSENT,
    ED64_PRO_IDENTIFIED,
    ED64_PRO_INITIALIZING,
    ED64_PRO_READY,
    ED64_PRO_FAILED,
    ED64_PRO_POISONED,
} ed64_pro_state_t;

typedef enum {
    ED64_PRO_PHASE_PROBE,
    ED64_PRO_PHASE_INITIAL_DRAIN,
    ED64_PRO_PHASE_INHERITED_STATUS,
    ED64_PRO_PHASE_STOP,
    ED64_PRO_PHASE_POST_STOP_DRAIN,
    ED64_PRO_PHASE_STOP_STATUS,
    ED64_PRO_PHASE_SD_INIT,
    ED64_PRO_PHASE_READY,
    ED64_PRO_PHASE_FIRMWARE_INFO,
    ED64_PRO_PHASE_COUNT,
} ed64_pro_phase_t;

typedef enum {
    ED64_PRO_STORAGE_COLD,
    ED64_PRO_STORAGE_MCU_OWNED,
    ED64_PRO_STORAGE_MOUNTING,
    ED64_PRO_STORAGE_FATFS_OWNED,
    ED64_PRO_STORAGE_RELEASING,
    ED64_PRO_STORAGE_FAILED,
} ed64_pro_storage_state_t;

typedef struct {
    ed64_pro_state_t state;
    ed64_pro_phase_t phase;
    ed64_pro_probe_t probe;
    ed64_pro_ll_failure_t failure;
    ed64_pro_status_t inherited_status;
    bool inherited_status_valid;
    ed64_pro_status_t status;
    bool status_valid;
    size_t initial_drained;
    size_t post_stop_drained;
    uint32_t phase_ticks[ED64_PRO_PHASE_COUNT];
    uint32_t attempt_ticks;
    bool info_queried;
    flashcart_err_t info_result;
    flashcart_firmware_version_t firmware_version;
    uint32_t firmware_timestamp;
    uint32_t rom_capacity;
    ed64_pro_storage_state_t storage_state;
    bool storage_failed;
    bool unsynced;
    int volume_id;
    const char *storage_detail;
    const char *storage_operation;
    uint32_t storage_lba;
    uint32_t storage_count;
    ed64_pro_ll_failure_t storage_failure;
    const char *path_error;
    bool launch_ready;
    bool load_byte_swap;
    uint32_t launch_extent;
    char rom_path[ED64_PRO_PATH_MAX + 1];
    uint32_t brm_type;
    uint32_t brm_size;
    bool save_ready;
    char save_path[ED64_PRO_PATH_MAX + 1];
} ed64_pro_diagnostics_t;

/**
 * @addtogroup ed64_pro
 * @{
 */

/** @brief Probe cartridge identity and retain the startup result. */
ed64_pro_probe_t ed64_pro_probe (void);
/** @brief Initialize MCU/SD transport readiness without mounting storage. */
ed64_pro_ll_error_t ed64_pro_init_transport (void);
/** @brief Return the retained startup and storage diagnostics. */
const ed64_pro_diagnostics_t *ed64_pro_get_diagnostics (void);
/** @brief Return display text for an initialization phase. */
const char *ed64_pro_phase_name (ed64_pro_phase_t phase);
/** @brief Format retained startup or storage diagnostics without cartridge I/O. */
char *ed64_pro_get_error_message (void);
flashcart_t *ed64_pro_get_flashcart (void);
bool ed64_pro_init_sdfs (const char *prefix, int max_files);

/** @} */ /* ed64_pro */

#endif
