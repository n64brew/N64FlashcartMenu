#include <fatfs/ff.h>
#include <libcart/cart.h>
#include <libdragon.h>
#include <stdio.h>
#include <usb.h>

#include "flashcart/ed64_pro/ed64_pro.h"
#include "flashcart/flashcart.h"

static bool terminal_only;
static bool pro_active;

static void guard_failed (const char *operation) __attribute__((noreturn));
/**
 * @brief Report a forbidden startup call forever on the PI-independent console.
 */
static void guard_failed (const char *operation) {
    console_init();
    console_set_debug(false);
    console_set_render_mode(RENDER_MANUAL);
    printf(
        "ED64 Pro STARTUP GUARD FAILED\n\nForbidden call after %s:\n%s\n\n"
        "Power-cycle; do not retry.\n",
        terminal_only ? "flashcart startup error" : "successful Pro startup",
        operation
    );
    for (;;) {
        console_render();
    }
}

/**
 * @brief Reject guarded calls after a flashcart startup error.
 */
static void check_guard (const char *operation) {
    if (terminal_only) {
        guard_failed(operation);
    }
}

flashcart_err_t __real_flashcart_init (const char **prefix);
/**
 * @brief Arm terminal-call and legacy-call guards from the flashcart startup result.
 */
flashcart_err_t __wrap_flashcart_init (const char **prefix) {
    flashcart_err_t error = __real_flashcart_init(prefix);
    terminal_only = error != FLASHCART_OK;
    pro_active = error == FLASHCART_OK && ed64_pro_get_diagnostics()->probe.outcome == ED64_PRO_PROBE_IDENTIFIED;
    return error;
}

/**
 * @brief Generate a value-returning wrapper that checks the startup guard.
 */
#define WRAP_VALUE(type, name, parameters, arguments) \
    type __real_##name parameters; \
    type __wrap_##name parameters { \
        check_guard(#name); \
        return __real_##name arguments; \
    }

/**
 * @brief Generate a void wrapper that checks the startup guard.
 */
#define WRAP_VOID(name, parameters, arguments) \
    void __real_##name parameters; \
    void __wrap_##name parameters { \
        check_guard(#name); \
        __real_##name arguments; \
    }

/**
 * @brief Generate a wrapper that also rejects legacy calls after Pro startup.
 */
#define WRAP_LEGACY_VALUE(type, name, parameters, arguments) \
    type __real_##name parameters; \
    type __wrap_##name parameters { \
        if (pro_active) { \
            guard_failed(#name); \
        } \
        check_guard(#name); \
        return __real_##name arguments; \
    }

/**
 * @brief Reject legacy cartridge initialization after Pro startup or any startup failure.
 */
WRAP_LEGACY_VALUE(int, cart_init, (void), ())
/**
 * @brief Check the startup guard before forwarding an asynchronous raw DMA read.
 */
WRAP_VALUE(uint64_t, dma_read_raw_async, (void *ram, pi_addr_t pi, unsigned long len), (ram, pi, len))
/**
 * @brief Check the startup guard before forwarding an asynchronous raw DMA write.
 */
WRAP_VALUE(uint64_t, dma_write_raw_async, (const void *ram, pi_addr_t pi, unsigned long len), (ram, pi, len))
/**
 * @brief Check the startup guard before forwarding an asynchronous DMA read.
 */
WRAP_VALUE(uint64_t, dma_read_async, (void *ram, pi_addr_t pi, unsigned long len), (ram, pi, len))
/**
 * @brief Check the startup guard before forwarding a DMA read.
 */
WRAP_VOID(dma_read, (void *ram, pi_addr_t pi, unsigned long len), (ram, pi, len))
/**
 * @brief Check the startup guard before forwarding a DMA write.
 */
WRAP_VOID(dma_write, (const void *ram, pi_addr_t pi, unsigned long len), (ram, pi, len))
/**
 * @brief Check the startup guard before forwarding a PI register read.
 */
WRAP_VALUE(uint32_t, io_read, (pi_addr_t pi), (pi))
/**
 * @brief Check the startup guard before forwarding a PI register write.
 */
WRAP_VOID(io_write, (pi_addr_t pi, uint32_t value), (pi, value))
/**
 * @brief Reject legacy SD initialization after Pro startup or any startup failure.
 */
WRAP_LEGACY_VALUE(bool, debug_init_sdfs, (const char *prefix, int npart), (prefix, npart))
/**
 * @brief Reject USB logging initialization after Pro startup or any startup failure.
 */
WRAP_LEGACY_VALUE(bool, debug_init_usblog, (void), ())
/**
 * @brief Reject emulator logging initialization after Pro startup or any startup failure.
 */
WRAP_LEGACY_VALUE(bool, debug_init_emulog, (void), ())
/* Current libdragon aliases the old isviewer entry point to debug_init_emulog. */
#ifndef debug_init_isviewer
/**
 * @brief Reject ISViewer initialization after Pro startup or any startup failure.
 */
WRAP_LEGACY_VALUE(bool, debug_init_isviewer, (void), ())
#endif
/**
 * @brief Check the startup guard before initializing DragonFS.
 */
WRAP_VALUE(int, dfs_init, (pi_addr_t location), (location))
/**
 * @brief Check the startup guard before mounting a FatFs volume.
 */
WRAP_VALUE(FRESULT, f_mount, (FATFS * fs, const TCHAR *path, BYTE opt), (fs, path, opt))
/**
 * @brief Check the startup guard before opening a FatFs file.
 */
WRAP_VALUE(FRESULT, f_open, (FIL * file, const TCHAR *path, BYTE mode), (file, path, mode))
/**
 * @brief Check the startup guard before reading a FatFs file.
 */
WRAP_VALUE(FRESULT, f_read, (FIL * file, void *buffer, UINT count, UINT *read), (file, buffer, count, read))
/**
 * @brief Check the startup guard before writing a FatFs file.
 */
WRAP_VALUE(
    FRESULT,
    f_write,
    (FIL * file, const void *buffer, UINT count, UINT *written),
    (file, buffer, count, written)
)
/**
 * @brief Check the startup guard before opening a FatFs directory.
 */
WRAP_VALUE(FRESULT, f_opendir, (DIR * directory, const TCHAR *path), (directory, path))
/**
 * @brief Check the startup guard before querying FatFs metadata.
 */
WRAP_VALUE(FRESULT, f_stat, (const TCHAR *path, FILINFO *info), (path, info))
/**
 * @brief Check the startup guard before creating a FatFs directory.
 */
WRAP_VALUE(FRESULT, f_mkdir, (const TCHAR *path), (path))
/**
 * @brief Reject legacy USB initialization after Pro startup or any startup failure.
 */
WRAP_LEGACY_VALUE(char, usb_initialize, (void), ())
/**
 * @brief Check the startup guard before polling USB.
 */
WRAP_VALUE(unsigned long, usb_poll, (void), ())
/**
 * @brief Check the startup guard before writing USB data.
 */
WRAP_VALUE(char, usb_write, (int type, const void *data, int size), (type, data, size))
/**
 * @brief Check the startup guard before reading USB data.
 */
WRAP_VOID(usb_read, (void *data, int size), (data, size))
/**
 * @brief Check the startup guard before initializing controllers.
 */
WRAP_VOID(joypad_init, (void), ())
