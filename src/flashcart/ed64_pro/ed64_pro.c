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
#include <string.h>

#include <fatfs/ff.h>

#include <fatfs/diskio.h>
#include <libcart/cart.h>
#include <libdragon.h>

#include "../flashcart_utils.h"
#include "ed64_pro.h"
#include "utils/fs.h"

static ed64_pro_diagnostics_t pro_diagnostics;
static uint32_t pro_attempt_start;
static uint32_t pro_attempt_deadline;
static uint32_t pro_phase_start;
static char pro_error_message[256];

/**
 * @brief Record the completed non-probe phase time and start timing the next phase.
 */
static void pro_set_phase (ed64_pro_phase_t phase) {
    uint32_t now = TICKS_READ();
    if (pro_diagnostics.phase != ED64_PRO_PHASE_PROBE) {
        pro_diagnostics.phase_ticks[pro_diagnostics.phase] = now - pro_phase_start;
    }
    pro_diagnostics.phase = phase;
    pro_phase_start = now;
}

/**
 * @brief Latch the first startup failure with its phase, elapsed time, and DMA evidence.
 *
 * Later calls return the original failure without replacing its diagnostics.
 */
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

/**
 * @brief Probe once within the whole-attempt budget, distinguishing absence from I/O failure.
 *
 * Cold-start rejections retain their probe result without latching a failure;
 * busy or unsupported-context results leave the startup state unchanged.
 */
ed64_pro_probe_t ed64_pro_probe (void) {
    uint32_t start = TICKS_READ();
    uint32_t deadline = pro_diagnostics.state == ED64_PRO_COLD ? start + TICKS_FROM_MS(ED64_PRO_ATTEMPT_TIMEOUT_MS)
                                                               : pro_attempt_deadline;
    ed64_pro_probe_t result = ed64_pro_ll_probe(deadline);
    if (result.error == ED64_PRO_LL_BUSY || result.error == ED64_PRO_LL_CONTEXT) {
        if (pro_diagnostics.state == ED64_PRO_COLD) {
            pro_diagnostics.probe = result;
        }
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

/**
 * @brief Perform the bounded MCU/SD handoff, reusing cached readiness or the first failure.
 *
 * Transport failures remain latched for this launch. READY never implies a
 * mounted filesystem or a usable storage backend.
 */
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
        pro_init_fail(error);
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

/**
 * @brief Return static display text for an initialization phase, with an unknown fallback.
 */
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
    case ED64_PRO_PHASE_FIRMWARE_INFO: return "Firmware info";
    default: return "Unknown phase";
    }
}

/**
 * @brief Map a transport error to built-in display text, including unknown error values.
 */
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

/**
 * @brief Format the startup result without accessing the cartridge or filesystem.
 *
 * Failures report retained MCU status or confirmed RX progress; READY reports
 * the missing storage backend. The shared output buffer is overwritten on each call.
 */
char *ed64_pro_get_error_message (void) {
    ed64_pro_ll_error_t error = pro_diagnostics.failure.code != ED64_PRO_LL_OK ? pro_diagnostics.failure.code
                                                                           : pro_diagnostics.probe.error;
    if (pro_diagnostics.storage_detail) {
        if (pro_diagnostics.storage_operation) {
            char mcu_detail[48] = "";
            if (pro_diagnostics.storage_failure.status_valid) {
                if (pro_diagnostics.storage_failure.nresp_valid) {
                    snprintf(
                        mcu_detail,
                        sizeof(mcu_detail),
                        " MCU status 0x%02X, NRESP 0x%02X.",
                        (unsigned)pro_diagnostics.storage_failure.status,
                        (unsigned)pro_diagnostics.storage_failure.nresp
                    );
                } else {
                    snprintf(
                        mcu_detail,
                        sizeof(mcu_detail),
                        " MCU status 0x%02X.",
                        (unsigned)pro_diagnostics.storage_failure.status
                    );
                }
            }
            snprintf(
                pro_error_message,
                sizeof(pro_error_message),
                "EverDrive-64 Pro: %s.\n%s: LBA %" PRIu32 ", sectors %" PRIu32 ".\n%s.%s Power-cycle console and USB.",
                pro_diagnostics.storage_detail,
                pro_diagnostics.storage_operation,
                pro_diagnostics.storage_lba,
                pro_diagnostics.storage_count,
                pro_error_name(pro_diagnostics.storage_failure.code),
                mcu_detail
            );
        } else {
            snprintf(
                pro_error_message, sizeof(pro_error_message), "EverDrive-64 Pro: %s.", pro_diagnostics.storage_detail
            );
        }
    } else if (pro_diagnostics.info_queried && pro_diagnostics.info_result == FLASHCART_ERR_OUTDATED) {
        snprintf(pro_error_message, sizeof(pro_error_message), "EverDrive-64 Pro requires firmware 26.1014 or newer.");
    } else if (
        pro_diagnostics.info_queried && pro_diagnostics.info_result == FLASHCART_ERR_INT &&
        pro_diagnostics.failure.code == ED64_PRO_LL_OK
    ) {
        snprintf(
            pro_error_message, sizeof(pro_error_message), "EverDrive-64 Pro returned invalid firmware or ROM capacity."
        );
    } else if (pro_diagnostics.state == ED64_PRO_READY) {
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
    } else if (error != ED64_PRO_LL_OK) {
        const char *identity = pro_diagnostics.probe.outcome == ED64_PRO_PROBE_IDENTIFIED
                                   ? "EverDrive-64 Pro startup failed."
                                   : "Cartridge identity unknown.";
        if (error == ED64_PRO_LL_MCU_ERROR && pro_diagnostics.status_valid) {
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
                pro_error_name(error),
                pro_diagnostics.failure.transferred
            );
        }
    } else {
        snprintf(pro_error_message, sizeof(pro_error_message), "EverDrive-64 Pro transport is not ready.");
    }
    return pro_error_message;
}

static bool pro_decode_firmware (uint32_t timestamp, flashcart_firmware_version_t *version) {
    unsigned date = timestamp & 0xFFFFu;
    unsigned year = 1980u + (date >> 9);
    unsigned month = (date >> 5) & 15u;
    unsigned day = date & 31u;
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 2000 || month == 0 || month > 12 || day == 0) {
        return false;
    }
    unsigned max_day = days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
        max_day++;
    }
    if (day > max_day) {
        return false;
    }
    *version = (flashcart_firmware_version_t){.major = year - 2000, .minor = month * 100 + day};
    return true;
}

/** @brief Cache firmware and capacity while keeping storage activation behind its hardware gate. */
static flashcart_err_t pro_init (void) {
    if (ed64_pro_init_transport() != ED64_PRO_LL_OK) {
        return FLASHCART_ERR_INT;
    }
    if (!pro_diagnostics.info_queried) {
        const uint32_t ids[] = {ED64_PRO_INFS_TS_FW, ED64_PRO_INFS_MAX_ROM_SIZE};
        uint32_t values[2];
        uint32_t deadline = TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS);
        ed64_pro_ll_transaction_t transaction;
        pro_set_phase(ED64_PRO_PHASE_FIRMWARE_INFO);
        ed64_pro_ll_error_t error = ed64_pro_ll_begin(&transaction, deadline);
        if (error == ED64_PRO_LL_OK) {
            error = ed64_pro_ll_get_sys_info(&transaction, ids, values, 2, deadline);
            ed64_pro_ll_error_t end_error = ed64_pro_ll_end(&transaction);
            if (error == ED64_PRO_LL_OK) {
                error = end_error;
            }
        }
        pro_diagnostics.info_queried = true;
        pro_diagnostics.info_result = FLASHCART_ERR_INT;
        if (error != ED64_PRO_LL_OK) {
            pro_init_fail(error);
            return FLASHCART_ERR_INT;
        }
        pro_diagnostics.firmware_timestamp = values[0];
        pro_diagnostics.rom_capacity = values[1] < 0x0F000000u ? values[1] : 0x0F000000u;
        if (!pro_decode_firmware(values[0], &pro_diagnostics.firmware_version) || values[1] == 0) {
            return FLASHCART_ERR_INT;
        }
        flashcart_firmware_version_t version = pro_diagnostics.firmware_version;
        pro_diagnostics.info_result =
            version.major < 26 || (version.major == 26 && version.minor < 1014) ? FLASHCART_ERR_OUTDATED : FLASHCART_OK;
        if (pro_diagnostics.info_result == FLASHCART_OK) {
            pro_diagnostics.storage_state = ED64_PRO_STORAGE_MCU_OWNED;
            pro_diagnostics.volume_id = -1;
        }
        pro_set_phase(ED64_PRO_PHASE_READY);
    }
    return pro_diagnostics.info_result;
}

static const char *pro_get_name (void) {
    return "EverDrive-64 Pro";
}

/** @brief No menu features are available before the storage backend is implemented. */
static bool pro_has_feature (flashcart_features_t feature) {
    (void)feature;
    return false;
}

/** @brief Return cached information without MCU traffic. */
static flashcart_firmware_version_t pro_get_firmware_version (void) {
    return pro_diagnostics.firmware_version;
}

static int pro_disk_status (void) {
    bool owned = pro_diagnostics.storage_state == ED64_PRO_STORAGE_MOUNTING ||
                 pro_diagnostics.storage_state == ED64_PRO_STORAGE_FATFS_OWNED;
    return owned && pro_diagnostics.state == ED64_PRO_READY && !pro_diagnostics.storage_failed ? 0 : STA_NOINIT;
}

static void
pro_storage_fail (const char *detail, const char *operation, uint32_t lba, uint32_t count, ed64_pro_ll_error_t error) {
    if (pro_diagnostics.storage_failed) {
        return;
    }
    pro_diagnostics.storage_failed = true;
    pro_diagnostics.storage_state = ED64_PRO_STORAGE_FAILED;
    pro_diagnostics.storage_detail = detail;
    pro_diagnostics.storage_operation = operation;
    pro_diagnostics.storage_lba = lba;
    pro_diagnostics.storage_count = count;
    pro_diagnostics.storage_failure = *ed64_pro_ll_get_failure();
    pro_diagnostics.storage_failure.code = error;
}

static bool pro_disk_arguments (const void *buffer, int64_t sector, int count, bool write) {
    if (!buffer || sector < 0 || count <= 0 || (uint64_t)sector + (unsigned)count > UINT64_C(0x100000000)) {
        return false;
    }
    uintptr_t physical = PhysicalAddr(buffer);
    uint64_t bytes = (uint64_t)(unsigned)count * 512;
    if (physical < 0x00800000u) {
        return bytes <= 0x00800000u - physical;
    }
    return !write && physical >= 0x10000000u && (physical & 511u) == 0 &&
           physical - 0x10000000u <= pro_diagnostics.rom_capacity &&
           bytes <= pro_diagnostics.rom_capacity - (physical - 0x10000000u);
}

static int pro_disk_transfer (void *buffer, int64_t sector, int count, bool write) {
    if (!pro_disk_arguments(buffer, sector, count, write)) {
        return RES_PARERR;
    }
    if (pro_disk_status() != 0) {
        return RES_NOTRDY;
    }
    uint8_t *cursor = buffer;
    bool fci = PhysicalAddr(buffer) >= 0x10000000u;
    unsigned max_chunk = fci ? 256 : 64;
    while (count > 0) {
        uint32_t chunk = (unsigned)count > max_chunk ? max_chunk : (unsigned)count;
        uint32_t deadline = TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS);
        ed64_pro_ll_transaction_t transaction;
        ed64_pro_ll_error_t error = ed64_pro_ll_begin(&transaction, deadline);
        if (error == ED64_PRO_LL_OK) {
            if (fci) {
                /* WRSW is confined to the SD-to-FCI operation; FIFO metadata
                 * transfers must always run with it cleared. */
                error = ed64_pro_ll_set_flags(
                    &transaction, ED64_PRO_FLAG_WRSW, pro_diagnostics.load_byte_swap ? ED64_PRO_FLAG_WRSW : 0, deadline
                );
                if (error == ED64_PRO_LL_OK) {
                    error = ed64_pro_ll_sd_read_fci(
                        &transaction, PhysicalAddr(cursor) - 0x10000000u, sector, chunk, deadline
                    );
                }
                if (error == ED64_PRO_LL_OK) {
                    error = ed64_pro_ll_set_flags(&transaction, ED64_PRO_FLAG_WRSW, 0, deadline);
                }
            } else {
                error = write ? ed64_pro_ll_sd_write(&transaction, cursor, sector, chunk, deadline)
                              : ed64_pro_ll_sd_read(&transaction, cursor, sector, chunk, deadline);
            }
            ed64_pro_ll_error_t end_error = ed64_pro_ll_end(&transaction);
            if (error == ED64_PRO_LL_OK) {
                error = end_error;
            }
        }
        if (error == ED64_PRO_LL_CONTEXT) {
            return RES_NOTRDY;
        }
        if (error == ED64_PRO_LL_ARGUMENT) {
            return RES_PARERR;
        }
        if (error != ED64_PRO_LL_OK) {
            /* Once WRE is sent, a failed write is ambiguous. Never retry it. */
            pro_storage_fail("SD I/O failed", write ? "Write" : "Read", sector, chunk, error);
            return RES_ERROR;
        }
        if (write) {
            pro_diagnostics.unsynced = true;
        }
        cursor += chunk * 512;
        sector += chunk;
        count -= chunk;
    }
    return RES_OK;
}

static int pro_disk_read (uint8_t *buffer, int64_t sector, int count) {
    return pro_disk_transfer(buffer, sector, count, false);
}

static int pro_disk_write (const uint8_t *buffer, int64_t sector, int count) {
    return pro_disk_transfer((void *)buffer, sector, count, true);
}

static int pro_disk_ioctl (uint8_t command, void *buffer) {
    (void)buffer;
    if (command != CTRL_SYNC) {
        return RES_PARERR;
    }
    if (pro_disk_status() != 0) {
        return RES_NOTRDY;
    }
    /* Every raw write has already completed its MCU STATUS exchange. */
    pro_diagnostics.unsynced = false;
    return RES_OK;
}

static const fat_disk_t pro_disk = {
    .disk_initialize = pro_disk_status,
    .disk_status = pro_disk_status,
    .disk_read = pro_disk_read,
    .disk_write = pro_disk_write,
    .disk_ioctl = pro_disk_ioctl,
};

bool ed64_pro_init_sdfs (const char *prefix, int max_files) {
    (void)max_files;
    if (!prefix || pro_diagnostics.storage_state != ED64_PRO_STORAGE_MCU_OWNED ||
        pro_diagnostics.state != ED64_PRO_READY || pro_diagnostics.storage_failed) {
        return false;
    }
    pro_diagnostics.storage_state = ED64_PRO_STORAGE_MOUNTING;
    int volume_id = fat_mount(prefix, &pro_disk, 0);
    if (volume_id != 0 || pro_diagnostics.storage_failed) {
        if (volume_id >= 0) {
            fat_unmount(volume_id);
        }
        pro_storage_fail(volume_id > 0 ? "SD must use FatFs volume 0" : "SD mount failed", NULL, 0, 0, ED64_PRO_LL_OK);
        return false;
    }
    pro_diagnostics.volume_id = volume_id;
    pro_diagnostics.storage_state = ED64_PRO_STORAGE_FATFS_OWNED;
    return true;
}

static flashcart_err_t pro_release_storage (void) {
    if (pro_diagnostics.storage_state != ED64_PRO_STORAGE_FATFS_OWNED || pro_diagnostics.storage_failed ||
        pro_diagnostics.unsynced) {
        pro_storage_fail("unclosed write or unavailable storage", NULL, 0, 0, ED64_PRO_LL_OK);
        return FLASHCART_ERR_SD_CARD;
    }
    if (fat_unmount(pro_diagnostics.volume_id) != 0) {
        pro_storage_fail("unmount failed", NULL, 0, 0, ED64_PRO_LL_OK);
        return FLASHCART_ERR_SD_CARD;
    }
    pro_diagnostics.volume_id = -1;
    pro_diagnostics.storage_state = ED64_PRO_STORAGE_RELEASING;
    uint32_t deadline = TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS);
    ed64_pro_ll_transaction_t transaction;
    ed64_pro_ll_error_t error = ed64_pro_ll_begin(&transaction, deadline);
    if (error == ED64_PRO_LL_OK) {
        error = ed64_pro_ll_init_sd(&transaction, &pro_diagnostics.status, deadline);
        pro_diagnostics.status_valid = error == ED64_PRO_LL_OK || error == ED64_PRO_LL_MCU_ERROR;
        ed64_pro_ll_error_t end_error = ed64_pro_ll_end(&transaction);
        if (error == ED64_PRO_LL_OK) {
            error = end_error;
        }
    }
    if (error != ED64_PRO_LL_OK) {
        pro_storage_fail("MCU filesystem reinitialization failed", "FS INIT", 0, 0, error);
        return FLASHCART_ERR_INT;
    }
    pro_diagnostics.storage_state = ED64_PRO_STORAGE_MCU_OWNED;
    return FLASHCART_OK;
}

static flashcart_err_t pro_deinit (void) {
    flashcart_err_t error = pro_release_storage();
    if (error != FLASHCART_OK) {
        return error;
    }
    pro_diagnostics.storage_detail = "Game launch not implemented; storage released. Power-cycle console and USB";
    return FLASHCART_ERR_FUNCTION_NOT_SUPPORTED;
}

static const char *pro_mcu_path (char *path) {
    if (!path || strnlen(path, ED64_PRO_PATH_MAX + 4) == ED64_PRO_PATH_MAX + 4) {
        return NULL;
    }
    char *mcu_path = strip_fs_prefix(path);
    if (mcu_path[0] != '/' || strnlen(mcu_path, ED64_PRO_PATH_MAX + 1) > ED64_PRO_PATH_MAX) {
        return NULL;
    }
    return mcu_path;
}

static flashcart_err_t pro_load_rom (char *rom_path, flashcart_progress_callback_t *progress) {
    pro_diagnostics.launch_ready = false;
    pro_diagnostics.launch_extent = 0;
    pro_diagnostics.rom_path[0] = '\0';
    pro_diagnostics.save_path[0] = '\0';
    pro_diagnostics.brm_type = ED64_PRO_BRM_OFF;
    pro_diagnostics.brm_size = 0;
    pro_diagnostics.save_ready = false;
    const char *mcu_path = pro_mcu_path(rom_path);
    if (!mcu_path) {
        return FLASHCART_ERR_ARGS;
    }
    if (pro_disk_status() != 0) {
        return FLASHCART_ERR_LOAD;
    }
    FIL file;
    if (f_open(&file, mcu_path, FA_READ) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }
    flashcart_err_t result = FLASHCART_ERR_LOAD;
    if (!file.obj.fs || file.obj.fs->wflag || f_size(&file) == 0 || f_size(&file) > pro_diagnostics.rom_capacity) {
        goto done;
    }
    fatfs_fix_file_size(&file);
    if (f_size(&file) > pro_diagnostics.rom_capacity) {
        goto done;
    }
    uint32_t size = f_size(&file);
    pro_diagnostics.load_byte_swap = cart_card_byteswap != 0;
    for (uint32_t offset = 0; offset < size;) {
        UINT chunk = size - offset > 128 * 1024u ? 128 * 1024u : size - offset;
        UINT read = 0;
        if (f_read(&file, (void *)(uintptr_t)(0x10000000u + offset), chunk, &read) != FR_OK || read != chunk) {
            goto done;
        }
        offset += chunk;
        if (progress) {
            progress(f_tell(&file) / (float)size);
        }
    }
    if (f_tell(&file) != size) {
        goto done;
    }
    result = FLASHCART_OK;

done:
    pro_diagnostics.load_byte_swap = false;
    if (f_close(&file) != FR_OK) {
        result = FLASHCART_ERR_LOAD;
    }
    if (result == FLASHCART_OK) {
        memcpy(pro_diagnostics.rom_path, mcu_path, strlen(mcu_path) + 1);
        pro_diagnostics.launch_extent = f_size(&file);
        pro_diagnostics.launch_ready = true;
    }
    return result;
}

static flashcart_err_t pro_load_file (char *file_path, uint32_t rom_offset, uint32_t file_offset) {
    const char *mcu_path = pro_mcu_path(file_path);
    if (!mcu_path || (rom_offset & 511u) || (file_offset & 511u) || rom_offset > pro_diagnostics.rom_capacity) {
        pro_diagnostics.launch_ready = false;
        return FLASHCART_ERR_ARGS;
    }
    if (!pro_diagnostics.launch_ready || pro_disk_status() != 0) {
        return FLASHCART_ERR_LOAD;
    }
    pro_diagnostics.launch_ready = false;
    FIL file;
    if (f_open(&file, mcu_path, FA_READ) != FR_OK) {
        return FLASHCART_ERR_LOAD;
    }
    flashcart_err_t result = FLASHCART_ERR_LOAD;
    if (file_offset > f_size(&file) || f_size(&file) - file_offset > pro_diagnostics.rom_capacity - rom_offset) {
        result = FLASHCART_ERR_ARGS;
        goto done;
    }
    if (!file.obj.fs || file.obj.fs->wflag) {
        goto done;
    }
    fatfs_fix_file_size(&file);
    if (f_size(&file) < file_offset || f_size(&file) - file_offset > pro_diagnostics.rom_capacity - rom_offset) {
        result = FLASHCART_ERR_ARGS;
        goto done;
    }
    if (f_lseek(&file, file_offset) != FR_OK || f_tell(&file) != file_offset) {
        goto done;
    }
    uint32_t size = f_size(&file) - file_offset;
    for (uint32_t offset = 0; offset < size;) {
        UINT chunk = size - offset > 128 * 1024u ? 128 * 1024u : size - offset;
        UINT read = 0;
        if (f_read(&file, (void *)(uintptr_t)(0x10000000u + rom_offset + offset), chunk, &read) != FR_OK ||
            read != chunk) {
            goto done;
        }
        offset += chunk;
    }
    if (f_tell(&file) != f_size(&file)) {
        goto done;
    }
    result = FLASHCART_OK;

done:
    if (f_close(&file) != FR_OK) {
        result = FLASHCART_ERR_LOAD;
    }
    if (result == FLASHCART_OK) {
        uint32_t end = rom_offset + f_size(&file) - file_offset;
        if (end > pro_diagnostics.launch_extent) {
            pro_diagnostics.launch_extent = end;
        }
        pro_diagnostics.launch_ready = true;
    }
    return result;
}

static flashcart_err_t pro_load_save (char *save_path) {
    const char *mcu_path = pro_mcu_path(save_path);
    pro_diagnostics.save_ready = false;
    pro_diagnostics.save_path[0] = '\0';
    if (!mcu_path || pro_diagnostics.brm_type == ED64_PRO_BRM_OFF) {
        return FLASHCART_ERR_ARGS;
    }
    if (pro_disk_status() != 0) {
        return FLASHCART_ERR_LOAD;
    }
    memcpy(pro_diagnostics.save_path, mcu_path, strlen(mcu_path) + 1);
    pro_diagnostics.save_ready = true;
    return FLASHCART_OK;
}

static flashcart_err_t pro_set_save_type (flashcart_save_type_t save_type) {
    static const struct {
        uint32_t type, size;
    } saves[] = {
        [FLASHCART_SAVE_TYPE_NONE] = {ED64_PRO_BRM_OFF, 0},
        [FLASHCART_SAVE_TYPE_EEPROM_4KBIT] = {ED64_PRO_BRM_EEP4K, 512},
        [FLASHCART_SAVE_TYPE_EEPROM_16KBIT] = {ED64_PRO_BRM_EEP16K, 2048},
        [FLASHCART_SAVE_TYPE_SRAM_256KBIT] = {ED64_PRO_BRM_SRM32K, 32768},
        [FLASHCART_SAVE_TYPE_SRAM_BANKED] = {ED64_PRO_BRM_SRM96K, 98304},
        [FLASHCART_SAVE_TYPE_SRAM_1MBIT] = {ED64_PRO_BRM_SRM128K, 131072},
        [FLASHCART_SAVE_TYPE_FLASHRAM_1MBIT] = {ED64_PRO_BRM_FLASH, 131072},
        [FLASHCART_SAVE_TYPE_FLASHRAM_PKST2] = {ED64_PRO_BRM_FLASH, 131072},
    };
    pro_diagnostics.save_ready = false;
    pro_diagnostics.save_path[0] = '\0';
    if ((unsigned)save_type >= __FLASHCART_SAVE_TYPE_END) {
        return FLASHCART_ERR_ARGS;
    }
    if (pro_disk_status() != 0) {
        return FLASHCART_ERR_LOAD;
    }
    pro_diagnostics.brm_type = saves[save_type].type;
    pro_diagnostics.brm_size = saves[save_type].size;
    pro_diagnostics.save_ready = save_type == FLASHCART_SAVE_TYPE_NONE;
    return FLASHCART_OK;
}

/** @brief Supply retained startup details for the errors this adapter returns. */
static char *pro_get_error_message (flashcart_err_t error) {
    if (error != FLASHCART_ERR_INT && error != FLASHCART_ERR_FUNCTION_NOT_SUPPORTED &&
        error != FLASHCART_ERR_OUTDATED && error != FLASHCART_ERR_SD_CARD) {
        return NULL;
    }
    return ed64_pro_get_error_message();
}

/** @brief Pro callbacks retain the shared flashcart interface while storage is unavailable. */
static flashcart_t flashcart_ed64_pro = {
    .init = pro_init,
    .deinit = pro_deinit,
    .has_feature = pro_has_feature,
    .get_firmware_version = pro_get_firmware_version,
    .get_name = pro_get_name,
    .load_rom = pro_load_rom,
    .load_file = pro_load_file,
    .load_save = pro_load_save,
    .load_64dd_ipl = NULL,
    .load_64dd_disk = NULL,
    .load_64dd_disks = NULL,
    .get_button_state = NULL,
    .get_voltage_temperature = NULL,
    .set_save_type = pro_set_save_type,
    .set_save_writeback = NULL,
    .set_next_boot_mode = NULL,
    .get_error_message = pro_get_error_message,
};

/** @brief Return the Pro adapter without accessing the cartridge. */
flashcart_t *ed64_pro_get_flashcart (void) {
    return &flashcart_ed64_pro;
}
