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

#if FF_CODE_PAGE != 437 || FF_LFN_UNICODE != 2
#error "ED64 Pro path staging requires CP437 and the UTF-8 FatFs API"
#endif

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

/** @brief Return the retained startup and storage diagnostics. */
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
 * Failures report retained MCU status or confirmed RX progress.
 * The shared output buffer is overwritten on each call.
 */
char *ed64_pro_get_error_message (void) {
    ed64_pro_ll_error_t error =
        pro_diagnostics.failure.code != ED64_PRO_LL_OK ? pro_diagnostics.failure.code : pro_diagnostics.probe.error;
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
            char location[80];
            if (strcmp(pro_diagnostics.storage_operation, "Read") == 0 ||
                strcmp(pro_diagnostics.storage_operation, "Write") == 0) {
                snprintf(
                    location,
                    sizeof(location),
                    "%s: LBA %" PRIu32 ", sectors %" PRIu32,
                    pro_diagnostics.storage_operation,
                    pro_diagnostics.storage_lba,
                    pro_diagnostics.storage_count
                );
            } else {
                snprintf(location, sizeof(location), "%s", pro_diagnostics.storage_operation);
            }
            snprintf(
                pro_error_message,
                sizeof(pro_error_message),
                "EverDrive-64 Pro: %s.\n%s.\n%s.%s Power-cycle console and USB.",
                pro_diagnostics.storage_detail,
                location,
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
        snprintf(pro_error_message, sizeof(pro_error_message), "EverDrive-64 Pro transport ready.");
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

/** @brief Validate the firmware FAT date and convert it to a version. */
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

/** @brief Cache firmware and capacity before mounting storage. */
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

/** @brief Return the cartridge display name. */
static const char *pro_get_name (void) {
    return "EverDrive-64 Pro";
}

/** @brief Report the hardware-qualified Pro features. */
static bool pro_has_feature (flashcart_features_t feature) {
    return feature == FLASHCART_FEATURE_AUTO_CIC || feature == FLASHCART_FEATURE_SAVE_WRITEBACK;
}

/** @brief Return cached information without MCU traffic. */
static flashcart_firmware_version_t pro_get_firmware_version (void) {
    return pro_diagnostics.firmware_version;
}

/** @brief Report whether FatFs owns a ready, healthy SD transport. */
static int pro_disk_status (void) {
    bool owned = pro_diagnostics.storage_state == ED64_PRO_STORAGE_MOUNTING ||
                 pro_diagnostics.storage_state == ED64_PRO_STORAGE_FATFS_OWNED;
    return owned && pro_diagnostics.state == ED64_PRO_READY && !pro_diagnostics.storage_failed ? 0 : STA_NOINIT;
}

/** @brief Latch the first storage failure and its operation details. */
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

/** @brief Validate the sector range and RDRAM or cartridge transfer buffer. */
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

/** @brief Transfer SD sectors in bounded chunks and retain any storage failure. */
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

/** @brief Read SD sectors into RDRAM or cartridge ROM memory. */
static int pro_disk_read (uint8_t *buffer, int64_t sector, int count) {
    return pro_disk_transfer(buffer, sector, count, false);
}

/** @brief Write SD sectors from RDRAM. */
static int pro_disk_write (const uint8_t *buffer, int64_t sector, int count) {
    return pro_disk_transfer((void *)buffer, sector, count, true);
}

/** @brief Acknowledge CTRL_SYNC for completed writes; reject other controls. */
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

/** @brief Mount the Pro SD card as FatFs volume 0 at the requested prefix. */
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

/** @brief Unmount synchronized FatFs storage and return SD ownership to the MCU. */
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

/** @brief Release storage and configure MCU game services for the prepared launch. */
static flashcart_err_t pro_deinit (void) {
    if (pro_diagnostics.storage_failed) {
        return FLASHCART_ERR_SD_CARD;
    }
#ifndef ED64_PRO_TEST_STORAGE
    if (!pro_diagnostics.launch_ready || !pro_diagnostics.save_ready || pro_diagnostics.launch_extent == 0 ||
        pro_diagnostics.launch_extent > pro_diagnostics.rom_capacity ||
        (pro_diagnostics.brm_type != ED64_PRO_BRM_OFF && pro_diagnostics.save_path[0] == '\0')) {
        pro_diagnostics.storage_detail = "Game launch is not prepared";
        return FLASHCART_ERR_LOAD;
    }
#endif
    flashcart_err_t release_error = pro_release_storage();
    if (release_error != FLASHCART_OK) {
        return release_error;
    }
#if defined(ED64_PRO_TEST_HALT_AFTER_RELEASE) || defined(ED64_PRO_TEST_STORAGE)
    pro_diagnostics.storage_detail = "Test halt after storage release. Power-cycle console and USB";
    return FLASHCART_ERR_FUNCTION_NOT_SUPPORTED;
#endif
    ed64_pro_dev_cfg_t config = {
        .brom_type = ED64_PRO_ROM_GPAK,
        .gpak_size = (pro_diagnostics.launch_extent + 511u) & ~511u,
        .brm_size = pro_diagnostics.brm_size,
        .brm_type = pro_diagnostics.brm_type,
        .rtc_mode = ED64_PRO_RTC_RDO,
    };
    ed64_pro_ll_transaction_t transaction;
    uint32_t deadline = TICKS_READ() + 4u * TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS);
    ed64_pro_ll_error_t error = ed64_pro_ll_begin(&transaction, deadline);
    const char *step = "Game services";
    if (error == ED64_PRO_LL_OK) {
        step = "GPAK path";
        error = ed64_pro_ll_dev_set_path(
            &transaction,
            ED64_PRO_PATH_GPAK,
            pro_diagnostics.rom_path,
            TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS)
        );
        if (error == ED64_PRO_LL_OK && pro_diagnostics.brm_type != ED64_PRO_BRM_OFF) {
            step = "Save path";
            error = ed64_pro_ll_dev_set_path(
                &transaction,
                ED64_PRO_PATH_BRM,
                pro_diagnostics.save_path,
                TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS)
            );
        }
        if (error == ED64_PRO_LL_OK) {
            step = "Game configuration";
            error =
                ed64_pro_ll_dev_set_cfg(&transaction, &config, TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS));
        }
        if (error == ED64_PRO_LL_OK) {
            step = "Game start";
            error = ed64_pro_ll_dev_start(&transaction, TICKS_READ() + TICKS_FROM_MS(ED64_PRO_IO_TIMEOUT_MS));
        }
        ed64_pro_ll_error_t end_error = ed64_pro_ll_end(&transaction);
        if (error == ED64_PRO_LL_OK) {
            error = end_error;
        }
    }
    if (error != ED64_PRO_LL_OK) {
        pro_storage_fail("Game startup failed", step, 0, 0, error);
        return FLASHCART_ERR_INT;
    }
    pro_diagnostics.launch_ready = false;
    return FLASHCART_OK;
}

/** @brief Decode one UTF-8 code point and advance the cursor on success. */
static bool pro_decode_utf8 (const char **cursor, const char *end, uint32_t *value) {
    const unsigned char *bytes = (const unsigned char *)*cursor;
    uint32_t codepoint = *bytes++;
    unsigned continuation = 0;
    uint32_t minimum = 0;
    if (codepoint < 0x80) {
        *cursor = (const char *)bytes;
        *value = codepoint;
        return true;
    }
    if (codepoint >= 0xC2 && codepoint <= 0xDF) {
        codepoint &= 0x1F;
        continuation = 1;
        minimum = 0x80;
    } else if (codepoint >= 0xE0 && codepoint <= 0xEF) {
        codepoint &= 0x0F;
        continuation = 2;
        minimum = 0x800;
    } else if (codepoint >= 0xF0 && codepoint <= 0xF4) {
        codepoint &= 0x07;
        continuation = 3;
        minimum = 0x10000;
    } else {
        return false;
    }
    if ((size_t)(end - (const char *)bytes) < continuation) {
        return false;
    }
    for (unsigned i = 0; i < continuation; i++) {
        if ((bytes[i] & 0xC0) != 0x80) {
            return false;
        }
        codepoint = (codepoint << 6) | (bytes[i] & 0x3F);
    }
    if (codepoint < minimum || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        return false;
    }
    *cursor = (const char *)bytes + continuation;
    *value = codepoint;
    return true;
}

/** @brief Strip the filesystem prefix and validate an absolute UTF-8 path. */
static const char *pro_fatfs_path (char *path) {
    if (!path || strnlen(path, ED64_PRO_PATH_MAX + 4) == ED64_PRO_PATH_MAX + 4) {
        pro_diagnostics.path_error = "Invalid or overlong file path";
        return NULL;
    }
    const char *utf8 = strip_fs_prefix(path);
    size_t length = strnlen(utf8, ED64_PRO_PATH_MAX + 1);
    if (utf8[0] != '/' || length > ED64_PRO_PATH_MAX) {
        pro_diagnostics.path_error = "Invalid or overlong file path";
        return NULL;
    }
    const char *cursor = utf8;
    while (cursor != utf8 + length) {
        uint32_t codepoint;
        if (!pro_decode_utf8(&cursor, utf8 + length, &codepoint)) {
            pro_diagnostics.path_error = "File path contains invalid UTF-8";
            return NULL;
        }
    }
    return utf8;
}

/** @brief Encode UTF-8 as CP437, reporting the length and any unmappable characters. */
static bool
pro_encode_oem (const char *utf8, size_t length, char *output, size_t capacity, size_t *written, bool *representable) {
    const char *cursor = utf8;
    *written = 0;
    *representable = true;
    while (cursor != utf8 + length) {
        uint32_t codepoint;
        if (!pro_decode_utf8(&cursor, utf8 + length, &codepoint)) {
            return false;
        }
        WCHAR oem = ff_uni2oem(codepoint, 437);
        if (oem == 0 || oem > 0xFF) {
            *representable = false;
        } else if (*written < capacity) {
            output[*written] = (char)oem;
        }
        (*written)++;
    }
    return true;
}

/** @brief Build a CP437 MCU path, using FatFs short aliases for unmappable components. */
static flashcart_err_t pro_stage_mcu_path (const char *utf8, char *output) {
    size_t length = strlen(utf8);
    size_t written;
    bool representable;
    flashcart_err_t result = FLASHCART_ERR_ARGS;
    if (!pro_encode_oem(utf8, length, output, ED64_PRO_PATH_MAX, &written, &representable)) {
        goto invalid;
    }
    if (representable && written <= ED64_PRO_PATH_MAX) {
        output[written] = '\0';
        return FLASHCART_OK;
    }

    /* FatFs uses UTF-8 while the MCU consumes OEM437 paths. Resolve only
     * unmappable components through their actual FatFs short-name aliases. */
    char lookup[ED64_PRO_PATH_MAX + 1];
    memcpy(lookup, utf8, length + 1);
    size_t position = 0;
    const char *component = utf8;
    while (*component) {
        if (*component == '/' || *component == '\\') {
            if (position == ED64_PRO_PATH_MAX) {
                goto invalid;
            }
            output[position++] = *component++;
            continue;
        }
        size_t component_length = strcspn(component, "/\\");
        if (!pro_encode_oem(
                component, component_length, output + position, ED64_PRO_PATH_MAX - position, &written, &representable
            )) {
            goto invalid;
        }
        if (!representable) {
            size_t prefix_length = component + component_length - utf8;
            char separator = lookup[prefix_length];
            lookup[prefix_length] = '\0';
            FILINFO info = {0};
            FRESULT status = f_stat(lookup, &info);
            lookup[prefix_length] = separator;
            if (status != FR_OK) {
                pro_diagnostics.path_error = "Could not resolve a file path alias";
                result = FLASHCART_ERR_LOAD;
                goto failed;
            }
            size_t alias_length = strnlen(info.altname, sizeof(info.altname));
            if (!alias_length || alias_length == sizeof(info.altname) ||
                strcspn(info.altname, "/\\:*?\"<>|") != alias_length ||
                !pro_encode_oem(
                    info.altname,
                    alias_length,
                    output + position,
                    ED64_PRO_PATH_MAX - position,
                    &written,
                    &representable
                ) ||
                !representable) {
                goto invalid;
            }
        }
        if (written > ED64_PRO_PATH_MAX - position) {
            goto invalid;
        }
        position += written;
        component += component_length;
    }
    output[position] = '\0';
    return FLASHCART_OK;

invalid:
    pro_diagnostics.path_error = "File path cannot be represented by the cartridge filesystem";
failed:
    output[0] = '\0';
    return result;
}

/** @brief Load a ROM into cartridge memory and stage its MCU launch path. */
static flashcart_err_t pro_load_rom (char *rom_path, flashcart_progress_callback_t *progress) {
    pro_diagnostics.path_error = NULL;
    pro_diagnostics.launch_ready = false;
    pro_diagnostics.launch_extent = 0;
    pro_diagnostics.rom_path[0] = '\0';
    pro_diagnostics.save_path[0] = '\0';
    pro_diagnostics.brm_type = ED64_PRO_BRM_OFF;
    pro_diagnostics.brm_size = 0;
    pro_diagnostics.save_ready = false;
    const char *utf8_path = pro_fatfs_path(rom_path);
    if (!utf8_path) {
        return FLASHCART_ERR_ARGS;
    }
    if (pro_diagnostics.storage_state != ED64_PRO_STORAGE_FATFS_OWNED || pro_disk_status() != 0) {
        return FLASHCART_ERR_LOAD;
    }
    flashcart_err_t result = pro_stage_mcu_path(utf8_path, pro_diagnostics.rom_path);
    if (result != FLASHCART_OK) {
        return result;
    }
    FIL file;
    if (f_open(&file, utf8_path, FA_READ) != FR_OK) {
        pro_diagnostics.rom_path[0] = '\0';
        return FLASHCART_ERR_LOAD;
    }
    result = FLASHCART_ERR_LOAD;
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
        pro_diagnostics.launch_extent = f_size(&file);
        pro_diagnostics.launch_ready = true;
    } else {
        pro_diagnostics.rom_path[0] = '\0';
    }
    return result;
}

/** @brief Load a file from the given offset into cartridge ROM memory. */
static flashcart_err_t pro_load_file (char *file_path, uint32_t rom_offset, uint32_t file_offset) {
    bool launch_ready = pro_diagnostics.launch_ready;
    pro_diagnostics.launch_ready = false;
    pro_diagnostics.path_error = NULL;
    const char *utf8_path = pro_fatfs_path(file_path);
    if (!utf8_path || (rom_offset & 511u) || (file_offset & 511u) || rom_offset > pro_diagnostics.rom_capacity) {
        return FLASHCART_ERR_ARGS;
    }
    if (!launch_ready || pro_diagnostics.storage_state != ED64_PRO_STORAGE_FATFS_OWNED || pro_disk_status() != 0) {
        return FLASHCART_ERR_LOAD;
    }
    FIL file;
    if (f_open(&file, utf8_path, FA_READ) != FR_OK) {
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

/** @brief Stage the save path for MCU loading when game services start. */
static flashcart_err_t pro_load_save (char *save_path) {
    pro_diagnostics.path_error = NULL;
    const char *utf8_path = pro_fatfs_path(save_path);
    pro_diagnostics.save_ready = false;
    pro_diagnostics.save_path[0] = '\0';
    if (!utf8_path || pro_diagnostics.brm_type == ED64_PRO_BRM_OFF) {
        return FLASHCART_ERR_ARGS;
    }
    if (pro_diagnostics.storage_state != ED64_PRO_STORAGE_FATFS_OWNED || pro_disk_status() != 0) {
        return FLASHCART_ERR_LOAD;
    }
    flashcart_err_t result = pro_stage_mcu_path(utf8_path, pro_diagnostics.save_path);
    if (result != FLASHCART_OK) {
        return result;
    }
    pro_diagnostics.save_ready = true;
    return FLASHCART_OK;
}

/** @brief Stage the save memory type and size, clearing the previous save path. */
static flashcart_err_t pro_set_save_type (flashcart_save_type_t save_type) {
    pro_diagnostics.path_error = NULL;
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

/** @brief Supply retained diagnostics for the errors this adapter returns. */
static char *pro_get_error_message (flashcart_err_t error) {
    if (!pro_diagnostics.storage_failed && pro_diagnostics.path_error &&
        (error == FLASHCART_ERR_ARGS || error == FLASHCART_ERR_LOAD)) {
        snprintf(pro_error_message, sizeof(pro_error_message), "EverDrive-64 Pro: %s.", pro_diagnostics.path_error);
        return pro_error_message;
    }
    if (error == FLASHCART_ERR_LOAD && !pro_diagnostics.storage_detail) {
        return NULL;
    }
    if (error != FLASHCART_ERR_INT && error != FLASHCART_ERR_FUNCTION_NOT_SUPPORTED &&
        error != FLASHCART_ERR_OUTDATED && error != FLASHCART_ERR_SD_CARD && error != FLASHCART_ERR_LOAD) {
        return NULL;
    }
    return ed64_pro_get_error_message();
}

/** @brief Supply the Pro backend through the shared flashcart interface. */
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
