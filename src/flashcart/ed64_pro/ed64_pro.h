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
    ED64_PRO_PHASE_COUNT,
} ed64_pro_phase_t;

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
} ed64_pro_diagnostics_t;

/**
 * @addtogroup ed64_pro
 * @{
 */

bool ed64_pro_init_sdfs (const char *storage_prefix, int max_files);
bool ed64_pro_detected ();
ed64_pro_probe_t ed64_pro_probe (void);
/* READY is transport/MCU readiness only, never a mounted or usable backend. */
ed64_pro_ll_error_t ed64_pro_init_transport (void);
const ed64_pro_diagnostics_t *ed64_pro_get_diagnostics (void);
const char *ed64_pro_phase_name (ed64_pro_phase_t phase);
char *ed64_pro_get_error_message (void);
flashcart_t *ed64_pro_get_flashcart (void);

/** @} */ /* ed64_pro */

#endif
