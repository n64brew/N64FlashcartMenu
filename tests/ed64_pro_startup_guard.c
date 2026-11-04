#include <fatfs/ff.h>
#include <libdragon.h>
#include <stdio.h>
#include <usb.h>

#include "flashcart/flashcart.h"

static bool terminal_only;

static void guard_failed (const char *operation) __attribute__((noreturn));
static void guard_failed (const char *operation) {
    console_init();
    console_set_debug(false);
    console_set_render_mode(RENDER_MANUAL);
    printf(
        "ED64 Pro STARTUP GUARD FAILED\n\nForbidden call after Pro startup result:\n%s\n\n"
        "Power-cycle; do not retry.\n",
        operation
    );
    for (;;) {
        console_render();
    }
}

static void check_guard (const char *operation) {
    if (terminal_only) {
        guard_failed(operation);
    }
}

flashcart_err_t __real_flashcart_init (const char **prefix);
flashcart_err_t __wrap_flashcart_init (const char **prefix) {
    flashcart_err_t error = __real_flashcart_init(prefix);
    terminal_only = error == FLASHCART_ERR_ED64_PRO;
    return error;
}

#define WRAP_VALUE(type, name, parameters, arguments) \
    type __real_##name parameters; \
    type __wrap_##name parameters { \
        check_guard(#name); \
        return __real_##name arguments; \
    }

#define WRAP_VOID(name, parameters, arguments) \
    void __real_##name parameters; \
    void __wrap_##name parameters { \
        check_guard(#name); \
        __real_##name arguments; \
    }

WRAP_VALUE(uint64_t, dma_read_raw_async, (void *ram, pi_addr_t pi, unsigned long len), (ram, pi, len))
WRAP_VALUE(uint64_t, dma_write_raw_async, (const void *ram, pi_addr_t pi, unsigned long len), (ram, pi, len))
WRAP_VALUE(uint64_t, dma_read_async, (void *ram, pi_addr_t pi, unsigned long len), (ram, pi, len))
WRAP_VOID(dma_read, (void *ram, pi_addr_t pi, unsigned long len), (ram, pi, len))
WRAP_VOID(dma_write, (const void *ram, pi_addr_t pi, unsigned long len), (ram, pi, len))
WRAP_VALUE(uint32_t, io_read, (pi_addr_t pi), (pi))
WRAP_VOID(io_write, (pi_addr_t pi, uint32_t value), (pi, value))
WRAP_VALUE(bool, debug_init_sdfs, (const char *prefix, int npart), (prefix, npart))
WRAP_VALUE(bool, debug_init_usblog, (void), ())
WRAP_VALUE(bool, debug_init_emulog, (void), ())
WRAP_VALUE(int, dfs_init, (pi_addr_t location), (location))
WRAP_VALUE(FRESULT, f_mount, (FATFS * fs, const TCHAR *path, BYTE opt), (fs, path, opt))
WRAP_VALUE(FRESULT, f_open, (FIL * file, const TCHAR *path, BYTE mode), (file, path, mode))
WRAP_VALUE(FRESULT, f_read, (FIL * file, void *buffer, UINT count, UINT *read), (file, buffer, count, read))
WRAP_VALUE(
    FRESULT,
    f_write,
    (FIL * file, const void *buffer, UINT count, UINT *written),
    (file, buffer, count, written)
)
WRAP_VALUE(FRESULT, f_opendir, (DIR * directory, const TCHAR *path), (directory, path))
WRAP_VALUE(FRESULT, f_stat, (const TCHAR *path, FILINFO *info), (path, info))
WRAP_VALUE(FRESULT, f_mkdir, (const TCHAR *path), (path))
WRAP_VALUE(char, usb_initialize, (void), ())
WRAP_VALUE(unsigned long, usb_poll, (void), ())
WRAP_VALUE(char, usb_write, (int type, const void *data, int size), (type, data, size))
WRAP_VOID(usb_read, (void *data, int size), (data, size))
WRAP_VOID(joypad_init, (void), ())
