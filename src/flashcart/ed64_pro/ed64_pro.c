/**
 * @file ed64_pro.c
 * @brief ed64_pro functions implementation
 * @ingroup flashcart
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <libdragon.h>

#include "ed64_pro.h"

static ed64_pro_diagnostics_t pro_diagnostics;
static uint32_t pro_attempt_start;
static uint32_t pro_attempt_deadline;
static uint32_t pro_phase_start;
static char pro_error_message[256];

static void pro_set_phase (ed64_pro_phase_t phase) {
    uint32_t now = TICKS_READ();
    if (pro_diagnostics.phase != ED64_PRO_PHASE_PROBE) {
        pro_diagnostics.phase_ticks[pro_diagnostics.phase] = now - pro_phase_start;
    }
    pro_diagnostics.phase = phase;
    pro_phase_start = now;
}

static ed64_pro_ll_error_t pro_init_fail (ed64_pro_ll_error_t error) {
    if (pro_diagnostics.failure.code == ED64_PRO_LL_OK) {
        uint32_t now = TICKS_READ();
        if (pro_diagnostics.state == ED64_PRO_INITIALIZING) {
            pro_diagnostics.phase_ticks[pro_diagnostics.phase] = now - pro_phase_start;
        }
        pro_diagnostics.failure = *ed64_pro_ll_get_failure();
        pro_diagnostics.failure.code = error;
        pro_diagnostics.state = pro_diagnostics.failure.poisoned ? ED64_PRO_POISONED : ED64_PRO_FAILED;
        pro_diagnostics.attempt_ticks = now - pro_attempt_start;
    }
    return pro_diagnostics.failure.code;
}

ed64_pro_probe_t ed64_pro_probe (void) {
    uint32_t start = TICKS_READ();
    uint32_t deadline = pro_diagnostics.state == ED64_PRO_COLD ? start + TICKS_FROM_MS(ED64_PRO_ATTEMPT_TIMEOUT_MS)
                                                               : pro_attempt_deadline;
    ed64_pro_probe_t result = ed64_pro_ll_probe(deadline);
    if (result.error == ED64_PRO_LL_BUSY || result.error == ED64_PRO_LL_CONTEXT) {
        return result;
    }
    if (pro_diagnostics.state == ED64_PRO_COLD) {
        pro_attempt_start = start;
        pro_attempt_deadline = deadline;
        pro_diagnostics.probe = result;
        pro_diagnostics.phase_ticks[ED64_PRO_PHASE_PROBE] = TICKS_READ() - start;
        pro_diagnostics.attempt_ticks = pro_diagnostics.phase_ticks[ED64_PRO_PHASE_PROBE];
        if (result.error == ED64_PRO_LL_OK) {
            pro_diagnostics.state = result.outcome == ED64_PRO_PROBE_IDENTIFIED ? ED64_PRO_IDENTIFIED : ED64_PRO_ABSENT;
        }
    }
    if (result.error != ED64_PRO_LL_OK) {
        pro_init_fail(result.error);
    }
    return result;
}

ed64_pro_ll_error_t ed64_pro_init_transport (void) {
    ed64_pro_probe_t probe = ed64_pro_probe();
    if (probe.error != ED64_PRO_LL_OK) {
        return probe.error;
    }
    if (probe.outcome == ED64_PRO_PROBE_ABSENT) {
        return ED64_PRO_LL_NOT_IDENTIFIED;
    }
    if (pro_diagnostics.state == ED64_PRO_READY) {
        return ED64_PRO_LL_OK;
    }
    if (pro_diagnostics.failure.code != ED64_PRO_LL_OK) {
        return pro_diagnostics.failure.code;
    }

    ed64_pro_ll_transaction_t transaction;
    ed64_pro_ll_error_t error = ed64_pro_ll_begin(&transaction, pro_attempt_deadline);
    if (error != ED64_PRO_LL_OK) {
        return error;
    }
    pro_diagnostics.state = ED64_PRO_INITIALIZING;
    pro_set_phase(ED64_PRO_PHASE_INITIAL_DRAIN);
    error = ed64_pro_ll_drain(&transaction, &pro_diagnostics.initial_drained, pro_attempt_deadline);
    if (error != ED64_PRO_LL_OK) {
        goto done;
    }

    pro_set_phase(ED64_PRO_PHASE_INHERITED_STATUS);
    error = ed64_pro_ll_read_status(
        &transaction,
        &pro_diagnostics.inherited_status,
        ed64_pro_ll_limit_deadline(pro_attempt_deadline, ED64_PRO_STATUS_TIMEOUT_MS)
    );
    if (error != ED64_PRO_LL_OK) {
        goto done;
    }
    pro_diagnostics.inherited_status_valid = true;

    pro_set_phase(ED64_PRO_PHASE_STOP);
    error = ed64_pro_ll_stop(&transaction, pro_attempt_deadline);
    if (error != ED64_PRO_LL_OK) {
        goto done;
    }

    pro_set_phase(ED64_PRO_PHASE_POST_STOP_DRAIN);
    error = ed64_pro_ll_drain(&transaction, &pro_diagnostics.post_stop_drained, pro_attempt_deadline);
    if (error != ED64_PRO_LL_OK) {
        goto done;
    }
    pro_set_phase(ED64_PRO_PHASE_STOP_STATUS);
    error = ed64_pro_ll_check_status(
        &transaction,
        &pro_diagnostics.status,
        ed64_pro_ll_limit_deadline(pro_attempt_deadline, ED64_PRO_STATUS_TIMEOUT_MS)
    );
    pro_diagnostics.status_valid = error == ED64_PRO_LL_OK || error == ED64_PRO_LL_MCU_ERROR;
    if (error != ED64_PRO_LL_OK) {
        goto done;
    }

    pro_set_phase(ED64_PRO_PHASE_SD_INIT);
    pro_diagnostics.status_valid = false;
    error = ed64_pro_ll_init_sd(&transaction, &pro_diagnostics.status, pro_attempt_deadline);
    pro_diagnostics.status_valid = error == ED64_PRO_LL_OK || error == ED64_PRO_LL_MCU_ERROR;

done:;
    ed64_pro_ll_error_t release_error = ed64_pro_ll_end(&transaction);
    if (error != ED64_PRO_LL_OK) {
        return pro_init_fail(error);
    }
    if (release_error != ED64_PRO_LL_OK) {
        return pro_init_fail(release_error);
    }
    pro_set_phase(ED64_PRO_PHASE_READY);
    pro_diagnostics.state = ED64_PRO_READY;
    pro_diagnostics.attempt_ticks = TICKS_READ() - pro_attempt_start;
    return ED64_PRO_LL_OK;
}

const ed64_pro_diagnostics_t *ed64_pro_get_diagnostics (void) {
    return &pro_diagnostics;
}

const char *ed64_pro_phase_name (ed64_pro_phase_t phase) {
    switch (phase) {
    case ED64_PRO_PHASE_PROBE: return "Pro probe";
    case ED64_PRO_PHASE_INITIAL_DRAIN: return "Initial FIFO cleanup";
    case ED64_PRO_PHASE_INHERITED_STATUS: return "Inherited status";
    case ED64_PRO_PHASE_STOP: return "Device stop / mailbox";
    case ED64_PRO_PHASE_POST_STOP_DRAIN: return "Post-stop FIFO cleanup";
    case ED64_PRO_PHASE_STOP_STATUS: return "Fresh stop status";
    case ED64_PRO_PHASE_SD_INIT: return "SD readiness";
    case ED64_PRO_PHASE_READY: return "Transport ready";
    default: return "Unknown phase";
    }
}

static const char *pro_error_name (ed64_pro_ll_error_t error) {
    switch (error) {
    case ED64_PRO_LL_OK: return "No transport error";
    case ED64_PRO_LL_ARGUMENT: return "Invalid transport argument";
    case ED64_PRO_LL_CONTEXT: return "Unsupported interrupt/thread context";
    case ED64_PRO_LL_BUSY: return "Transport already in use";
    case ED64_PRO_LL_TIMEOUT: return "Protocol deadline expired";
    case ED64_PRO_LL_PI_ACQUIRE_TIMEOUT: return "PI queue acquisition timed out";
    case ED64_PRO_LL_PI_TIMEOUT: return "PI DMA completion timed out";
    case ED64_PRO_LL_PI_ERROR: return "PI controller error";
    case ED64_PRO_LL_NOT_IDENTIFIED: return "Pro not identified";
    case ED64_PRO_LL_FIFO_COUNT: return "Implausible RX FIFO count";
    case ED64_PRO_LL_DRAIN_LIMIT: return "RX FIFO cleanup limit exceeded";
    case ED64_PRO_LL_FRAME: return "Malformed MCU status frame";
    case ED64_PRO_LL_TRAILING_DATA: return "Unexpected RX FIFO data";
    case ED64_PRO_LL_MAILBOX_TIMEOUT: return "Mailbox READY deadline expired";
    case ED64_PRO_LL_MCU_ERROR: return "MCU command failed";
    default: return "Unknown transport error";
    }
}

char *ed64_pro_get_error_message (void) {
    if (pro_diagnostics.state == ED64_PRO_READY) {
        snprintf(
            pro_error_message,
            sizeof(pro_error_message),
            "EverDrive-64 Pro storage backend is not implemented.\nTransport and SD readiness only."
        );
    } else if (pro_diagnostics.state == ED64_PRO_ABSENT) {
        snprintf(
            pro_error_message,
            sizeof(pro_error_message),
            "Cartridge is not an EverDrive-64 Pro.\nEDID: %08" PRIX32,
            pro_diagnostics.probe.id
        );
    } else if (pro_diagnostics.failure.code != ED64_PRO_LL_OK) {
        const char *identity = pro_diagnostics.probe.outcome == ED64_PRO_PROBE_IDENTIFIED
                                   ? "EverDrive-64 Pro startup failed."
                                   : "Cartridge identity unknown.";
        if (pro_diagnostics.failure.code == ED64_PRO_LL_MCU_ERROR && pro_diagnostics.status_valid) {
            snprintf(
                pro_error_message,
                sizeof(pro_error_message),
                "%s\n%s: MCU status 0x%02X.",
                identity,
                ed64_pro_phase_name(pro_diagnostics.phase),
                (unsigned)pro_diagnostics.status.mcu_status
            );
        } else {
            snprintf(
                pro_error_message,
                sizeof(pro_error_message),
                "%s\n%s: %s.\nConfirmed RX bytes: %zu.",
                identity,
                ed64_pro_phase_name(pro_diagnostics.phase),
                pro_error_name(pro_diagnostics.failure.code),
                pro_diagnostics.failure.transferred
            );
        }
    } else {
        snprintf(pro_error_message, sizeof(pro_error_message), "EverDrive-64 Pro transport is not ready.");
    }
    return pro_error_message;
}

bool ed64_pro_detected () {
    /* Production selection stays inactive until the hardware qualification gate. */
    return false;
}

bool ed64_pro_init_sdfs (const char *storage_prefix, int max_files) {
    (void)storage_prefix;
    (void)max_files;
    return false;
}

/** @brief Flashcart structure for ed64_pro. */
static flashcart_t flashcart_ed64_pro = {
    .init = NULL,
    .deinit = NULL,
    .has_feature = NULL,
    .get_firmware_version = NULL,
    .load_rom = NULL,
    .load_file = NULL,
    .load_save = NULL,
    .load_64dd_ipl = NULL,
    .load_64dd_disk = NULL,
    .load_64dd_disks = NULL,
    .get_button_state = NULL,
    .get_voltage_temperature = NULL,
    .set_save_type = NULL,
    .set_save_writeback = NULL,
    .set_next_boot_mode = NULL,
};

/**
 * @brief Get the flashcart structure for ed64_pro.
 *
 * @return flashcart_t* Pointer to the flashcart structure.
 */
flashcart_t *ed64_pro_get_flashcart (void) {
    return &flashcart_ed64_pro;
}
