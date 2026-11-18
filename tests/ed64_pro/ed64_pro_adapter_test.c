#include <acutest.h>
#include "host_compat.h"
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>

#include "../../src/flashcart/ed64_pro/ed64_pro.c"
#include "../../src/flashcart/ed64_pro/ed64_pro_rtc.c"

/* Compare the adapter's mapping with the table used by actual shared save allocation. */
#undef ALIGN
#undef MIN
#undef MAX
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../../src/flashcart/flashcart.c"
#pragma GCC diagnostic pop

/* The integration cases use the unchanged shared loader and actual Pro adapter. */
typedef struct { uint8_t r, g, b, a; } color_t;
bool is_memory_expanded (void);
#include "../../src/menu/cart_load.c"
#include "../../src/menu/path.c"

static bool loader_active, loader_ipl_present, loader_bad_save;

static jmp_buf handoff_halt;
static bool handoff_halt_expected;
static unsigned handoff_halt_calls;
static char handoff_halt_message[256];

/** @brief Capture the production fatal handoff path in debug and NDEBUG host builds. */
void debug_assert_func_f (const char *file, int line, const char *func, const char *expression, const char *format, ...) {
    (void)file; (void)line; (void)func;
    TEST_ASSERT(handoff_halt_expected);
    TEST_CHECK(strcmp(expression, "error == FLASHCART_OK") == 0);
    va_list args;
    va_start(args, format);
    vsnprintf(handoff_halt_message, sizeof(handoff_halt_message), format, args);
    va_end(args);
    handoff_halt_calls++;
    longjmp(handoff_halt, 1);
}

int cart_type;

/* These legacy dispatch paths must stay unused by the Pro adapter tests. */
/**
 * @brief Fail if the Pro adapter attempts iQue detection.
 */
bool sys_bbplayer (void) {
    TEST_CHECK(false);
    return false;
}

/**
 * @brief Fail if the Pro adapter initializes iQue storage.
 */
int bbfs_init (void) {
    TEST_CHECK(false);
    return -1;
}

/**
 * @brief Fail if the Pro adapter probes DaisyDrive.
 */
bool daisydrive_detected (void) {
    TEST_CHECK(false);
    return false;
}

/**
 * @brief Fail if the Pro adapter mounts DaisyDrive storage.
 */
bool daisydrive_init_sdfs (const char *prefix, int max_files) {
    (void)prefix;
    (void)max_files;
    TEST_CHECK(false);
    return false;
}

/**
 * @brief Fail if the Pro adapter initializes legacy SD storage.
 */
bool debug_init_sdfs (const char *prefix, int npart) {
    (void)prefix;
    (void)npart;
    TEST_CHECK(false);
    return false;
}

/**
 * @brief Fail if the Pro adapter initializes ISViewer logging.
 */
bool debug_init_isviewer (void) {
    TEST_CHECK(false);
    return false;
}

/**
 * @brief Fail if the Pro adapter initializes legacy USB logging.
 */
bool debug_init_usblog (void) {
    TEST_CHECK(false);
    return false;
}

/**
 * @brief Fail if the Pro adapter selects the 64drive backend.
 */
flashcart_t *d64_get_flashcart (void) {
    TEST_CHECK(false);
    return NULL;
}

/**
 * @brief Fail if the Pro adapter selects an EverDrive X-series backend.
 */
flashcart_t *ed64_xseries_get_flashcart (void) {
    TEST_CHECK(false);
    return NULL;
}

/**
 * @brief Fail if the Pro adapter selects an EverDrive V-series backend.
 */
flashcart_t *ed64_vseries_get_flashcart (void) {
    TEST_CHECK(false);
    return NULL;
}

/**
 * @brief Fail if the Pro adapter selects the SummerCart64 backend.
 */
flashcart_t *sc64_get_flashcart (void) {
    TEST_CHECK(false);
    return NULL;
}

/**
 * @brief Fail if the Pro adapter selects the DaisyDrive backend.
 */
flashcart_t *daisydrive_get_flashcart (void) {
    TEST_CHECK(false);
    return NULL;
}

/**
 * @brief Fail if the Pro adapter invokes the shared file-existence helper.
 */
bool file_exists (char *path) {
    TEST_CHECK(loader_active);
    return strstr(path, ".sav") != NULL || loader_ipl_present;
}

/**
 * @brief Fail if the Pro adapter invokes the shared file-size helper.
 */
int64_t file_get_size (char *path) {
    (void)path;
    TEST_CHECK(loader_active);
    return loader_bad_save ? -1 : 32768;
}

bool directory_create (char *path) {
    (void)path;
    TEST_CHECK(false);
    return true;
}

bool is_memory_expanded (void) { return get_memory_size() > 4 * 1024 * 1024; }
rom_save_type_t rom_info_get_save_type (rom_info_t *info) { return info->save_type; }

/**
 * @brief Fail if the Pro adapter invokes shared file allocation.
 */
bool file_allocate (char *path, size_t size) {
    (void)path;
    (void)size;
    TEST_CHECK(false);
    return true;
}

/**
 * @brief Fail if the Pro adapter invokes shared file filling.
 */
bool file_fill (char *path, uint8_t value) {
    (void)path;
    (void)value;
    TEST_CHECK(false);
    return true;
}

static struct {
    uint32_t now;
    int memory_size;
    FATFS fs;
    FSIZE_t file_size;
    unsigned opens, file_reads, closes, seeks;
    FRESULT open_result, read_result, close_result, seek_result;
    UINT short_read;
    uintptr_t destinations[128];
    UINT read_sizes[128];
    bool ipl_fixture;
    bool bad_ipl_magic;
    bool bad_ipl_identity;
    bool bad_ipl_mapping;
    uint8_t open_mode;
    char opened_path[ED64_PRO_PATH_MAX + 1];
    struct {
        const char *path;
        const char *alias;
        FRESULT result;
        bool unterminated;
    } aliases[8];
    unsigned alias_count;
    unsigned stats;
    char stat_paths[8][ED64_PRO_PATH_MAX + 1];
    uint32_t fci[128];
    uint16_t flags;
    unsigned flag_calls;
    unsigned fail_flag_call;
    float progress[128];
    unsigned progress_calls;
    unsigned command_calls;
    unsigned fail_command;
    ed64_pro_dev_cfg_t config;
    char launch_paths[3][ED64_PRO_PATH_MAX + 1];
    uint32_t info[2];
    unsigned info_calls;
    uint32_t voltage_bcd, sysstat;
    unsigned voltage_calls, button_reads;
    ed64_pro_ll_error_t register_error, end_error;
    unsigned begin_calls;
    unsigned end_calls;
    unsigned reads;
    unsigned writes;
    uint32_t lbas[128];
    uint32_t counts[128];
    unsigned operations;
    unsigned fail_operation;
    ed64_pro_ll_error_t io_error;
    ed64_pro_ll_error_t begin_error;
    ed64_pro_ll_error_t info_error;
    ed64_pro_ll_error_t init_error;
    ed64_pro_ll_failure_t failure;
    int mount_result;
    int unmount_result;
    unsigned mounts;
    unsigned unmounts;
    unsigned fs_inits;
    unsigned rtc_starts, rtc_stops;
    bool rtc_pending, rtc_running;
    ed64_pro_dev_cfg_t rtc_config;
    ed64_pro_ll_error_t rtc_stop_error;
    const fat_disk_t *disk;
    char events[512];
    unsigned events_count;
} mock;

int get_memory_size (void) {
    return mock.memory_size;
}

/**
 * @brief Return and advance the simulated clock.
 */
uint32_t mock_ticks_read (void) {
    return mock.now++;
}

/**
 * @brief Return the supplied deadline without applying a mock time limit.
 */
uint32_t ed64_pro_ll_limit_deadline (uint32_t deadline, uint32_t milliseconds) {
    (void)milliseconds;
    return deadline;
}

/**
 * @brief Simulate successful Pro identification.
 */
ed64_pro_probe_t ed64_pro_ll_probe (uint32_t deadline) {
    (void)deadline;
    return (ed64_pro_probe_t){.outcome = ED64_PRO_PROBE_IDENTIFIED, .id = 0xED640027u, .id_valid = true};
}

/**
 * @brief Return the configured transport failure details.
 */
const ed64_pro_ll_failure_t *ed64_pro_ll_get_failure (void) {
    return &mock.failure;
}

/**
 * @brief Record transaction acquisition and return the configured result.
 */
ed64_pro_ll_error_t ed64_pro_ll_begin (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    transaction->deadline = deadline;
    mock.begin_calls++;
    return mock.begin_error;
}

/**
 * @brief Record transaction release and report success.
 */
ed64_pro_ll_error_t ed64_pro_ll_end (ed64_pro_ll_transaction_t *transaction) {
    (void)transaction;
    mock.end_calls++;
    return mock.end_error;
}

/**
 * @brief Simulate an empty receive FIFO.
 */
ed64_pro_ll_error_t ed64_pro_ll_drain (ed64_pro_ll_transaction_t *transaction, size_t *drained, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    *drained = 0;
    return ED64_PRO_LL_OK;
}

/**
 * @brief Return a valid mock MCU status key.
 */
ed64_pro_ll_error_t
ed64_pro_ll_read_status (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    *status = (ed64_pro_status_t){.key = 0x5A};
    return ED64_PRO_LL_OK;
}

/**
 * @brief Delegate status validation to the mock status reader.
 */
ed64_pro_ll_error_t
ed64_pro_ll_check_status (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline) {
    return ed64_pro_ll_read_status(transaction, status, deadline);
}

/**
 * @brief Simulate a successful stop command.
 */
ed64_pro_ll_error_t ed64_pro_ll_stop (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    if (mock.rtc_running) {
        mock.rtc_stops++;
        mock.events[mock.events_count++] = 'T';
        mock.rtc_running = false;
        return mock.rtc_stop_error;
    }
    return ED64_PRO_LL_OK;
}

/**
 * @brief Record MCU filesystem initialization and return its configured result.
 */
ed64_pro_ll_error_t
ed64_pro_ll_init_sd (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    mock.fs_inits++;
    mock.events[mock.events_count++] = 'F';
    *status = (ed64_pro_status_t){.key = 0x5A};
    return mock.init_error;
}

/**
 * @brief Check the requested information IDs and return mock firmware data.
 */
ed64_pro_ll_error_t ed64_pro_ll_get_sys_info (
    ed64_pro_ll_transaction_t *transaction,
    const uint32_t *ids,
    uint32_t *values,
    size_t count,
    uint32_t deadline
) {
    (void)transaction;
    (void)deadline;
    if (count == 1 && ids[0] == ED64_PRO_INFD_VCC_3V3) {
        mock.voltage_calls++;
        *values = mock.voltage_bcd;
    } else {
        TEST_CHECK(count == 2 && ids[0] == ED64_PRO_INFS_TS_FW && ids[1] == ED64_PRO_INFS_MAX_ROM_SIZE);
        mock.info_calls++;
        memcpy(values, mock.info, sizeof(mock.info));
    }
    return mock.info_error;
}

/** @brief Preserve the full-width button status supplied by the fixture. */
ed64_pro_ll_error_t ed64_pro_ll_read_reg (
    ed64_pro_ll_transaction_t *transaction, ed64_pro_reg_t reg, uint32_t *value, uint32_t deadline
) {
    TEST_CHECK(transaction->deadline == deadline && reg == ED64_PRO_REG_SYSSTAT);
    mock.button_reads++;
    *value = mock.sysstat;
    return mock.register_error;
}

/**
 * @brief Record sector ranges and inject the configured transfer failure.
 */
static ed64_pro_ll_error_t sector_transfer (uint32_t lba, uint32_t count, bool write) {
    TEST_ASSERT(mock.operations < 128);
    mock.lbas[mock.operations] = lba;
    mock.counts[mock.operations++] = count;
    if (write) {
        mock.writes++;
    } else {
        mock.reads++;
    }
    return mock.operations == mock.fail_operation ? mock.io_error : ED64_PRO_LL_OK;
}

/**
 * @brief Record a sector read and fill successful reads with test data.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_read (
    ed64_pro_ll_transaction_t *transaction,
    void *buffer,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
) {
    (void)transaction;
    (void)deadline;
    ed64_pro_ll_error_t result = sector_transfer(lba, count, false);
    if (result == ED64_PRO_LL_OK) {
        memset(buffer, 0xA5, count * 512);
    }
    return result;
}

/**
 * @brief Record a sector write and return the configured transfer result.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_write (
    ed64_pro_ll_transaction_t *transaction,
    const void *buffer,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
) {
    (void)transaction;
    (void)buffer;
    (void)deadline;
    return sector_transfer(lba, count, true);
}

char cart_card_byteswap;

/**
 * @brief Record metadata lookups and return configured short aliases or errors.
 */
FRESULT f_stat (const char *path, FILINFO *info) {
    TEST_ASSERT(mock.stats < 8);
    snprintf(mock.stat_paths[mock.stats++], sizeof(mock.stat_paths[0]), "%s", path);
    for (unsigned i = 0; i < mock.alias_count; i++) {
        if (strcmp(path, mock.aliases[i].path) == 0) {
            /* A failed lookup may leave altname stale; callers must check status. */
            if (mock.aliases[i].unterminated) {
                memset(info->altname, 'A', sizeof(info->altname));
            } else {
                snprintf(info->altname, sizeof(info->altname), "%s", mock.aliases[i].alias);
            }
            return mock.aliases[i].result;
        }
    }
    return FR_NO_FILE;
}

/**
 * @brief Record the read-only path and initialize the mock file.
 */
FRESULT f_open (FIL *file, const char *path, uint8_t mode) {
    TEST_CHECK(mode == FA_READ || mode == (FA_READ | FA_WRITE));
    mock.open_mode = mode;
    mock.opens++;
    snprintf(mock.opened_path, sizeof(mock.opened_path), "%s", path);
    file->obj.fs = &mock.fs;
    file->obj.objsize = loader_active && strstr(path, ".ndd") ? ED64_PRO_NATIVE_DISK_SIZE : mock.file_size;
    file->fptr = 0;
    return mock.open_result;
}

/**
 * @brief Read through the adapter disk callback and simulate short reads or errors.
 */
FRESULT f_read (FIL *file, void *buffer, UINT bytes, UINT *read) {
    TEST_ASSERT(mock.file_reads < 128);
    mock.destinations[mock.file_reads] = (uintptr_t)buffer;
    mock.read_sizes[mock.file_reads++] = bytes;
    *read = 0;
    if (mock.read_result != FR_OK) {
        return mock.read_result;
    }
    if (mock.ipl_fixture && bytes < 512) {
        memset(buffer, 0x21, bytes);
        if (file->fptr == 0) {
            const uint8_t magic[] = {0x80, 0x27, 0x07, 0x40};
            memcpy(buffer, magic, 4);
            if (bytes >= 64) {
                const uint8_t identity[] = {'N', 'D', 'D', 'J', 2};
                memcpy((uint8_t *)buffer + 0x3B, identity, sizeof(identity));
                if (mock.bad_ipl_identity) ((uint8_t *)buffer)[0x3F] = 0;
            }
            if (mock.bad_ipl_magic) ((uint8_t *)buffer)[0] = 0;
        }
    } else if (pro_disk_read(buffer, 1000 + file->fptr / 512, bytes / 512) != RES_OK) {
        return FR_DISK_ERR;
    }
    *read = mock.short_read ? bytes - mock.short_read : bytes;
    file->fptr += *read;
    return FR_OK;
}

/**
 * @brief Record the file offset and return the configured seek result.
 */
FRESULT f_lseek (FIL *file, FSIZE_t offset) {
    mock.seeks++;
    file->fptr = offset;
    return mock.seek_result;
}

/**
 * @brief Record file closure and return the configured result.
 */
FRESULT f_close (FIL *file) {
    (void)file;
    mock.closes++;
    return mock.close_result;
}

/**
 * @brief Round the mock file size up to a sector boundary.
 */
void fatfs_fix_file_size (FIL *file) {
    file->obj.objsize = (file->obj.objsize + 511) & ~UINT64_C(511);
}

/**
 * @brief Skip a filesystem prefix while retaining the leading slash.
 */
char *strip_fs_prefix (char *path) {
    char *prefix = strstr(path, ":/");
    return prefix ? prefix + 1 : path;
}

/**
 * @brief Record byte-swap flag changes and inject the configured failure.
 */
ed64_pro_ll_error_t
ed64_pro_ll_set_flags (ed64_pro_ll_transaction_t *transaction, uint16_t mask, uint16_t value, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    TEST_CHECK(mask == ED64_PRO_FLAG_WRSW);
    mock.flag_calls++;
    mock.events[mock.events_count++] = value ? 'S' : 'C';
    if (mock.flag_calls == mock.fail_flag_call) {
        return ED64_PRO_LL_TIMEOUT;
    }
    mock.flags = value;
    return ED64_PRO_LL_OK;
}

/**
 * @brief Check byte-swap state and record a transfer to cartridge memory.
 */
ed64_pro_ll_error_t ed64_pro_ll_sd_read_fci (
    ed64_pro_ll_transaction_t *transaction,
    uint32_t fci,
    uint32_t lba,
    uint32_t count,
    uint32_t deadline
) {
    (void)transaction;
    (void)deadline;
    TEST_CHECK(mock.flags == (pro_diagnostics.load_byte_swap ? ED64_PRO_FLAG_WRSW : 0));
    mock.fci[mock.operations] = fci;
    mock.events[mock.events_count++] = 'R';
    return sector_transfer(lba, count, false);
}

ed64_pro_ll_error_t ed64_pro_ll_sd_read_ipl_fci (
    ed64_pro_ll_transaction_t *transaction, uint32_t fci, uint32_t lba, uint32_t count, uint32_t deadline
) {
    TEST_CHECK(fci >= ED64_PRO_FCI_IPL && fci + count * 512 <= ED64_PRO_FCI_IPL + ED64_PRO_IPL_SIZE);
    return ed64_pro_ll_sd_read_fci(transaction, fci, lba, count, deadline);
}

uint32_t io_read (pi_addr_t pi) {
    if (loader_active && pi == 0x05000540u) return UINT32_MAX;
    TEST_CHECK(pi == 0x06001010u || (pi >= 0x06000000u && pi < 0x06000010u));
    if (mock.bad_ipl_mapping) return 0;
    return pi == 0x06000000u ? 0x80270740u : 0x21212121u;
}

/**
 * @brief Record launch command order and inject the configured MCU failure.
 */
static ed64_pro_ll_error_t launch_command (char event) {
    mock.events[mock.events_count++] = event;
    mock.command_calls++;
    if (mock.command_calls == mock.fail_command) {
        mock.failure.code = ED64_PRO_LL_MCU_ERROR;
        mock.failure.status_valid = true;
        mock.failure.status = 0x31;
        mock.failure.nresp_valid = true;
        mock.failure.nresp = 0xA3;
        return ED64_PRO_LL_MCU_ERROR;
    }
    return ED64_PRO_LL_OK;
}

/**
 * @brief Capture a launch path and record its command result.
 */
ed64_pro_ll_error_t ed64_pro_ll_dev_set_path (
    ed64_pro_ll_transaction_t *transaction,
    ed64_pro_path_t type,
    const char *path,
    uint32_t deadline
) {
    (void)transaction;
    (void)deadline;
    unsigned index = type == ED64_PRO_PATH_GPAK ? 0 : type == ED64_PRO_PATH_BRM ? 1 : 2;
    snprintf(mock.launch_paths[index], sizeof(mock.launch_paths[index]), "%s", path);
    return launch_command(index == 0 ? 'P' : index == 1 ? 'B' : 'D');
}

/**
 * @brief Capture the launch configuration and record its command result.
 */
ed64_pro_ll_error_t
ed64_pro_ll_dev_set_cfg (ed64_pro_ll_transaction_t *transaction, const ed64_pro_dev_cfg_t *config, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    if (config->brom_type == ED64_PRO_ROM_OFF && config->rtc_mode == ED64_PRO_RTC_STD) {
        mock.rtc_config = *config;
        mock.rtc_pending = true;
        return ED64_PRO_LL_OK;
    }
    mock.config = *config;
    return launch_command('G');
}

/**
 * @brief Record the game-start command and return its configured result.
 */
ed64_pro_ll_error_t ed64_pro_ll_dev_start (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    if (mock.rtc_pending) {
        mock.rtc_pending = false;
        mock.rtc_running = true;
        mock.rtc_starts++;
        return ED64_PRO_LL_OK;
    }
    return launch_command('S');
}

/**
 * @brief Capture ROM-loading progress values.
 */
static void progress_callback (float progress) {
    TEST_ASSERT(mock.progress_calls < 128);
    mock.progress[mock.progress_calls++] = progress;
}

/**
 * @brief Check mount state and exercise the supplied disk callbacks.
 */
int fat_mount (const char *prefix, const fat_disk_t *disk, int flags) {
    TEST_CHECK(strcmp(prefix, "sd:/") == 0 && flags == 0);
    TEST_CHECK(pro_diagnostics.storage_state == ED64_PRO_STORAGE_MOUNTING);
    mock.mounts++;
    mock.disk = disk;
    uint8_t sector[512];
    TEST_CHECK(disk->disk_initialize() == 0);
    if (disk->disk_read(sector, 0, 1) != RES_OK) {
        return -1;
    }
    TEST_CHECK(sector[0] == 0xA5 && sector[511] == 0xA5);
    return mock.mount_result;
}

/**
 * @brief Check the volume and record the unmount attempt.
 */
int fat_unmount (int volume_id) {
    TEST_CHECK(volume_id == mock.mount_result);
    mock.unmounts++;
    mock.events[mock.events_count++] = 'U';
    return mock.unmount_result;
}

/**
 * @brief Encode a date in FAT timestamp format.
 */
static uint32_t fat_date (unsigned year, unsigned month, unsigned day) {
    return ((year - 1980) << 9) | (month << 5) | day;
}

/**
 * @brief Reset adapter state and configure successful mock defaults.
 */
static void fixture (void) {
    mock.begin_error = mock.rtc_stop_error = ED64_PRO_LL_OK;
    ed64_pro_rtc_close();
    /* Separate fixtures represent fresh console boots, including RTC latches. */
    pro_rtc_failed = pro_rtc_running = false;
    pro_rtc_error = NULL;
    test_rtc_init_calls = test_rtc_close_calls = test_rtc_set_source_calls = 0;
    test_rtc_set_source_result = RTC_ESUCCESS;
    test_rtc_hooks = (rtc_hooks_t){0};
    test_rtc_source = RTC_SOURCE_JOYBUS;
    test_rtc_available = test_rtc_init_result = true;
    test_rtc_initialized = false;
    test_rtc_init_observer = NULL;
    memset(&mock, 0, sizeof(mock));
    mock.memory_size = 8 * 1024 * 1024;
    memset(&pro_diagnostics, 0, sizeof(pro_diagnostics));
    memset(&pro_voltage, 0, sizeof(pro_voltage));
    mock.voltage_bcd = 0x0330;
    pro_diagnostics.state = ED64_PRO_READY;
    mock.info[0] = 0x90555D4Eu;
    mock.info[1] = 0x10000000u;
    mock.io_error = ED64_PRO_LL_TIMEOUT;
    mock.file_size = 1024;
    cart_card_byteswap = 0;
}

/**
 * @brief Initialize and mount the adapter, then reset transfer counters.
 */
static void observe_native_init (void) {
    TEST_CHECK(mock.mounts == 1 && mock.rtc_starts == 1 && mock.rtc_running);
    TEST_CHECK(pro_diagnostics.storage_state == ED64_PRO_STORAGE_FATFS_OWNED);
    TEST_CHECK(cart_type == CART_MAX);
}

static void mounted_fixture (void) {
    fixture();
    test_rtc_init_observer = observe_native_init;
    TEST_ASSERT(pro_init() == FLASHCART_OK);
    TEST_ASSERT(ed64_pro_init_sdfs("sd:/", 4));
    mock.operations = 0;
    mock.reads = 0;
    mock.begin_calls = 0;
    mock.events_count = mock.command_calls = 0;
    memset(mock.events, 0, sizeof(mock.events));
}

/** @brief Decode all BCD digits and retain the caller output on invalid input. */
static void test_voltage_bcd (void) {
    const struct { uint32_t bcd; uint16_t mv; bool valid; } cases[] = {
        {0x0330, 3300, true}, {0x0299, 2990, true}, {0x0300, 3000, true},
        {0x0301, 3010, true}, {0x0000, 0, true}, {0x0001, 10, true},
        {0x6553, 65530, true}, {0x6554, 0, false}, {0x9999, 0, false},
        {0x033A, 0, false}, {0x03B0, 0, false}, {0x0C30, 0, false},
        {0xD330, 0, false}, {0x10330, 0, false},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        fixture();
        mock.voltage_bcd = cases[i].bcd;
        uint16_t output = 0xBEEF;
        TEST_CHECK(pro_get_voltage(&output) == (cases[i].valid ? FLASHCART_OK : FLASHCART_ERR_INT));
        TEST_CHECK(output == (cases[i].valid ? cases[i].mv : 0xBEEF));
        TEST_CHECK(mock.voltage_calls == 1 && mock.begin_calls == 1 && mock.end_calls == 1);
    }
}

/** @brief Refresh at the fixed interval, cache errors, and never publish stale success. */
static void test_voltage_cache (void) {
    fixture();
    uint16_t output = 0xBEEF;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_OK && output == 3300);
    TEST_CHECK(mock.voltage_calls == 1);
    mock.voltage_bcd = 0x0329;
    mock.now = pro_voltage.ticks + TICKS_FROM_MS(PRO_VOLTAGE_REFRESH_MS) - 1;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_OK && output == 3300 && mock.voltage_calls == 1);
    mock.now = pro_voltage.ticks + TICKS_FROM_MS(PRO_VOLTAGE_REFRESH_MS);
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_OK && output == 3290 && mock.voltage_calls == 2);
    mock.now = pro_voltage.ticks + TICKS_FROM_MS(PRO_VOLTAGE_REFRESH_MS);
    mock.info_error = ED64_PRO_LL_TIMEOUT;
    output = 0xBEEF;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_ERR_INT && output == 0xBEEF && mock.voltage_calls == 3);
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_ERR_INT && output == 0xBEEF && mock.voltage_calls == 3);

    fixture();
    mock.now = UINT32_MAX - TICKS_FROM_MS(PRO_VOLTAGE_REFRESH_MS) / 2;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_OK);
    mock.now = pro_voltage.ticks + TICKS_FROM_MS(PRO_VOLTAGE_REFRESH_MS);
    mock.voltage_bcd = 0x0331;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_OK && output == 3310 && mock.voltage_calls == 2);
}

/** @brief Keep argument, ownership, query, and release failures from publishing output. */
static void test_voltage_failures (void) {
    fixture();
    TEST_CHECK(pro_get_voltage(NULL) == FLASHCART_ERR_ARGS && mock.begin_calls == 0);
    const ed64_pro_ll_error_t errors[] = {ED64_PRO_LL_CONTEXT, ED64_PRO_LL_BUSY, ED64_PRO_LL_TIMEOUT};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        fixture();
        uint16_t output = 0xBEEF;
        mock.begin_error = errors[i];
        TEST_CHECK(pro_get_voltage(&output) == FLASHCART_ERR_INT && output == 0xBEEF);
        TEST_CHECK(mock.voltage_calls == 0 && mock.end_calls == 0);
    }
    fixture();
    uint16_t output = 0xBEEF;
    mock.info_error = ED64_PRO_LL_TIMEOUT;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_ERR_INT && output == 0xBEEF);
    TEST_CHECK(mock.voltage_calls == 1 && mock.end_calls == 1);
    fixture();
    mock.end_error = ED64_PRO_LL_BUSY;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_ERR_INT && output == 0xBEEF);
    fixture();
    TEST_ASSERT(pro_get_voltage(&output) == FLASHCART_OK);
    output = 0xBEEF;
    mock.begin_error = ED64_PRO_LL_TIMEOUT;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_ERR_INT && output == 0xBEEF && mock.voltage_calls == 1);
}

/** @brief A release failure also keeps the caller output and cached result unavailable. */
static void test_voltage_release_failure (void) {
    fixture();
    uint16_t output = 0xBEEF;
    mock.end_error = ED64_PRO_LL_CONTEXT;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_ERR_INT && output == 0xBEEF);
    mock.end_error = ED64_PRO_LL_OK;
    TEST_CHECK(pro_get_voltage(&output) == FLASHCART_ERR_INT && output == 0xBEEF);
    TEST_CHECK(mock.voltage_calls == 1);
}

/** @brief Read bit 16 on every call despite unrelated status changes and failure. */
static void test_live_button (void) {
    fixture();
    TEST_CHECK(pro_get_button_state(NULL) == FLASHCART_ERR_ARGS && mock.begin_calls == 0);
    const uint32_t statuses[] = {0, 0xFFFF, 0xFFFEFFFF, 0x00010000, 0xFFFFFFFF, 0x000100A8, 0xA8};
    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
        mock.sysstat = statuses[i];
        bool pressed = (statuses[i] & ED64_PRO_SYSSTAT_BTN) == 0;
        TEST_CHECK(pro_get_button_state(&pressed) == FLASHCART_OK);
        TEST_CHECK(pressed == ((statuses[i] & ED64_PRO_SYSSTAT_BTN) != 0));
        TEST_CHECK(mock.button_reads == i + 1 && mock.begin_calls == i + 1 && mock.end_calls == i + 1);
    }
    mock.register_error = ED64_PRO_LL_TIMEOUT;
    bool pressed = true;
    mock.sysstat = 0;
    TEST_CHECK(pro_get_button_state(&pressed) == FLASHCART_ERR_INT && pressed);
    mock.begin_error = ED64_PRO_LL_CONTEXT;
    unsigned reads = mock.button_reads;
    TEST_CHECK(pro_get_button_state(&pressed) == FLASHCART_ERR_INT && pressed && mock.button_reads == reads);
}

/**
 * @brief Verify cached firmware version, ROM capacity, and cartridge name.
 */
static void test_firmware_info (void) {
    fixture();
    TEST_CHECK(pro_init() == FLASHCART_OK);
    TEST_CHECK(pro_get_firmware_version().major == 26);
    TEST_CHECK(pro_get_firmware_version().minor == 1014);
    TEST_CHECK(pro_get_firmware_version().revision == 0);
    TEST_CHECK(pro_diagnostics.rom_capacity == 0x0F000000u);
    TEST_CHECK(strcmp(pro_get_name(), "EverDrive-64 Pro") == 0);
    TEST_CHECK(pro_init() == FLASHCART_OK && mock.info_calls == 1);
}

/**
 * @brief Verify minimum firmware dates and reject invalid dates or capacity.
 */
static void test_firmware_boundaries (void) {
    static const struct {
        unsigned year, month, day;
        flashcart_err_t result;
    } cases[] = {
        {2026, 10, 13, FLASHCART_ERR_OUTDATED},
        {2026, 10, 14, FLASHCART_OK},
        {2027, 1, 1, FLASHCART_OK},
        {2028, 2, 29, FLASHCART_OK},
        {2026, 2, 29, FLASHCART_ERR_INT},
        {2100, 2, 29, FLASHCART_ERR_INT},
        {2026, 4, 31, FLASHCART_ERR_INT},
        {2026, 0, 1, FLASHCART_ERR_INT},
        {2026, 13, 1, FLASHCART_ERR_INT},
        {2026, 1, 0, FLASHCART_ERR_INT},
        {1999, 1, 1, FLASHCART_ERR_INT},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        fixture();
        mock.info[0] = fat_date(cases[i].year, cases[i].month, cases[i].day);
        TEST_CHECK(pro_init() == cases[i].result);
        TEST_CHECK(mock.info_calls == 1);
        TEST_CHECK((pro_get_error_message(cases[i].result) == NULL) == (cases[i].result == FLASHCART_OK));
    }
    fixture();
    mock.info[1] = 0;
    TEST_CHECK(pro_init() == FLASHCART_ERR_INT);
}

/**
 * @brief Verify firmware query failures retain diagnostics and prevent retries.
 */
static void test_firmware_query_failure (void) {
    fixture();
    mock.info_error = ED64_PRO_LL_TIMEOUT;
    TEST_CHECK(pro_init() == FLASHCART_ERR_INT);
    TEST_CHECK(pro_diagnostics.phase == ED64_PRO_PHASE_FIRMWARE_INFO);
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "Firmware info") != NULL);
    TEST_CHECK(pro_init() == FLASHCART_ERR_INT && mock.info_calls == 1);
}

/**
 * @brief Verify disk callback mounting and reject a second mount.
 */
static void test_mount (void) {
    mounted_fixture();
    TEST_CHECK(mock.mounts == 1 && mock.disk == &pro_disk);
    TEST_CHECK(pro_diagnostics.storage_state == ED64_PRO_STORAGE_FATFS_OWNED);
    TEST_CHECK(!ed64_pro_init_sdfs("sd:/", 4) && mock.mounts == 1);
    TEST_CHECK(pro_disk.disk_status() == 0);
    TEST_CHECK(mock.rtc_starts == 1 && test_rtc_init_calls == 1);
    TEST_CHECK(rtc_get_source() == RTC_SOURCE_JOYBUS);
    TEST_CHECK(mock.rtc_config.brom_type == 0); /* Vendor ROM_OFF. */
    TEST_CHECK(mock.rtc_config.gpak_size == 0 && mock.rtc_config.rtc_mode == ED64_PRO_RTC_STD);
    TEST_CHECK(mock.rtc_config.brm_type == ED64_PRO_BRM_OFF && mock.rtc_config.brm_size == 0);
    TEST_CHECK(test_rtc_hooks.gettime == NULL && test_rtc_hooks.settime == NULL);
}

static void test_mount_without_clock (void) {
    for (unsigned failure = 0; failure < 2; failure++) {
        fixture();
        TEST_ASSERT(pro_init() == FLASHCART_OK);
        test_rtc_available = false;
        test_rtc_source = RTC_SOURCE_NONE;
        if (failure) test_rtc_init_result = false;
        TEST_ASSERT(ed64_pro_init_sdfs("sd:/", 4));
        TEST_CHECK(!pro_diagnostics.storage_failed && mock.unmounts == 0);
        TEST_CHECK(pro_disk.disk_status() == 0 && test_rtc_initialized);
        TEST_CHECK(mock.rtc_starts == 1 && mock.rtc_stops == 0 && mock.rtc_running);
        TEST_CHECK(rtc_get_source() == RTC_SOURCE_NONE && test_rtc_set_source_calls == 0);
        TEST_ASSERT(pro_load_rom("sd:/game.z64", NULL) == FLASHCART_OK);
        TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_NONE) == FLASHCART_OK);
        TEST_CHECK(pro_deinit() == FLASHCART_OK);
        TEST_CHECK(mock.rtc_stops == 1 && !mock.rtc_running && !test_rtc_initialized);
        TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 1);
        TEST_CHECK(strstr(mock.events, "TUFP") != NULL);
    }
}

/**
 * @brief Verify failed or unexpected-volume mounts disable further storage use.
 */
static void test_mount_failure (void) {
    for (int volume = -1; volume <= 1; volume += 2) {
        fixture();
        TEST_ASSERT(pro_init() == FLASHCART_OK);
        mock.mount_result = volume;
        TEST_CHECK(!ed64_pro_init_sdfs("sd:/", 4));
        TEST_CHECK(mock.unmounts == (volume > 0 ? 1u : 0u));
        TEST_CHECK(pro_diagnostics.storage_failed && pro_disk.disk_status() == STA_NOINIT);
        TEST_CHECK(mock.fs_inits == 0);
        TEST_CHECK(!ed64_pro_init_sdfs("sd:/", 4) && mock.mounts == 1);
    }
}

/**
 * @brief Verify mount failures retain the underlying sector error.
 */
static void test_mount_io_failure (void) {
    fixture();
    TEST_ASSERT(pro_init() == FLASHCART_OK);
    mock.fail_operation = 1;
    TEST_CHECK(!ed64_pro_init_sdfs("sd:/", 4));
    TEST_CHECK(pro_diagnostics.storage_failed);
    TEST_CHECK(pro_diagnostics.storage_failure.code == ED64_PRO_LL_TIMEOUT);
    TEST_CHECK(mock.fs_inits == 0);
}

/**
 * @brief Verify sector chunking, byte contents, and write synchronization.
 */
static void test_sector_chunks (void) {
    mounted_fixture();
    uint8_t data[129 * 512];
    TEST_CHECK(pro_disk.disk_read(data, 100, 129) == RES_OK);
    TEST_CHECK(mock.operations == 3);
    TEST_CHECK(mock.lbas[0] == 100 && mock.lbas[1] == 164 && mock.lbas[2] == 228);
    TEST_CHECK(mock.counts[0] == 64 && mock.counts[1] == 64 && mock.counts[2] == 1);
    TEST_CHECK(data[0] == 0xA5 && data[sizeof(data) - 1] == 0xA5);
    TEST_CHECK(!pro_diagnostics.unsynced);
    TEST_CHECK(pro_disk.disk_write(data, 200, 65) == RES_OK);
    TEST_CHECK(mock.writes == 2 && pro_diagnostics.unsynced);
    TEST_CHECK(pro_disk.disk_ioctl(CTRL_SYNC, NULL) == RES_OK && !pro_diagnostics.unsynced);
    TEST_CHECK(pro_disk.disk_ioctl(GET_SECTOR_COUNT, NULL) == RES_PARERR);
}

/**
 * @brief Verify invalid sector requests fail before acquiring transport.
 */
static void test_sector_arguments (void) {
    mounted_fixture();
    uint8_t data[512];
    TEST_CHECK(pro_disk.disk_read(NULL, 0, 1) == RES_PARERR);
    TEST_CHECK(pro_disk.disk_read(data, -1, 1) == RES_PARERR);
    TEST_CHECK(pro_disk.disk_read(data, 0, 0) == RES_PARERR);
    TEST_CHECK(pro_disk.disk_read(data, UINT32_MAX, 2) == RES_PARERR);
    TEST_CHECK(pro_disk.disk_read((void *)0x10000001u, 0, 1) == RES_PARERR);
    TEST_CHECK(pro_disk.disk_write((void *)0x807FFF00u, 0, 1) == RES_PARERR);
    TEST_CHECK(mock.begin_calls == 0);
    TEST_CHECK(pro_disk.disk_read(data, UINT32_MAX, 1) == RES_OK);
}

/**
 * @brief Reject buffers outside installed RDRAM before any transport access.
 */
static void test_sector_rdram_bounds (void) {
    mounted_fixture();
    static const uintptr_t aliases[] = {0x80000000u, 0xA0000000u};
    for (unsigned expanded = 0; expanded < 2; expanded++) {
        mock.memory_size = (expanded ? 8 : 4) * 1024 * 1024;
        for (unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
            uintptr_t end = aliases[i] + (unsigned)mock.memory_size;
            TEST_CHECK(pro_disk_arguments((void *)(end - 512), 0, 1, false));
            TEST_CHECK(pro_disk_arguments((void *)(end - 512), 0, 1, true));
            TEST_CHECK(!pro_disk_arguments((void *)(end - 511), 0, 1, false));
            TEST_CHECK(!pro_disk_arguments((void *)(end - 511), 0, 1, true));
            TEST_CHECK(!pro_disk_arguments((void *)end, 0, 1, false));
            TEST_CHECK(!pro_disk_arguments((void *)end, 0, 1, true));
        }
        TEST_CHECK(pro_disk_arguments((void *)0x10000000u, 0, 1, false));
        TEST_CHECK(!pro_disk_arguments((void *)0x10000000u, 0, 1, true));
    }

    mock.memory_size = 4 * 1024 * 1024;
    /* Keep a defective guard from dereferencing synthetic host addresses. */
    mock.begin_error = ED64_PRO_LL_CONTEXT;
    mock.begin_calls = 0;
    static const uintptr_t invalid[] = {
        0x803FFF00u, 0x80400000u, 0x807FFE00u,
        0xA03FFF00u, 0xA0400000u, 0xA07FFE00u,
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        TEST_CHECK(pro_disk.disk_read((void *)invalid[i], 0, 1) == RES_PARERR);
        TEST_CHECK(pro_disk.disk_write((void *)invalid[i], 0, 1) == RES_PARERR);
    }
    TEST_CHECK(mock.begin_calls == 0 && mock.operations == 0);
    TEST_CHECK(!pro_diagnostics.storage_failed);
}

/**
 * @brief Verify transfer failures retain diagnostics and block further storage operations.
 */
static void test_storage_latch (void) {
    mounted_fixture();
    uint8_t data[65 * 512];
    mock.fail_operation = 2;
    mock.failure.code = ED64_PRO_LL_TIMEOUT;
    mock.failure.status_valid = true;
    mock.failure.status = 0x31;
    mock.failure.nresp_valid = true;
    mock.failure.nresp = 0xA3;
    TEST_CHECK(pro_disk.disk_write(data, 123, 65) == RES_ERROR);
    TEST_CHECK(pro_diagnostics.unsynced && pro_diagnostics.storage_failed);
    TEST_CHECK(pro_diagnostics.storage_lba == 187 && pro_diagnostics.storage_count == 1);
    TEST_CHECK(pro_disk.disk_status() == STA_NOINIT);
    TEST_CHECK(pro_disk.disk_read(data, 0, 1) == RES_NOTRDY);
    TEST_CHECK(pro_disk.disk_write(data, 0, 1) == RES_NOTRDY);
    TEST_CHECK(pro_disk.disk_ioctl(CTRL_SYNC, NULL) == RES_NOTRDY);
    TEST_CHECK(mock.operations == 2);
    unsigned calls = mock.begin_calls;
    TEST_CHECK(pro_release_storage() == FLASHCART_ERR_SD_CARD);
    TEST_CHECK(mock.begin_calls == calls && mock.unmounts == 0);
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "LBA 187") != NULL);
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "MCU status 0x31, NRESP 0xA3") != NULL);
}

/**
 * @brief Verify unsupported contexts reject I/O without disabling storage.
 */
static void test_unsupported_context (void) {
    mounted_fixture();
    uint8_t data[512];
    mock.begin_error = ED64_PRO_LL_CONTEXT;
    TEST_CHECK(pro_disk.disk_read(data, 0, 1) == RES_NOTRDY);
    TEST_CHECK(mock.operations == 0 && !pro_diagnostics.storage_failed && pro_disk.disk_status() == 0);
}

/**
 * @brief Verify storage unmounts before MCU filesystem ownership resumes.
 */
static void test_release (void) {
    mounted_fixture();
    TEST_CHECK(pro_release_storage() == FLASHCART_OK);
    TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 1);
    TEST_CHECK(mock.events[0] == 'U' && mock.events[1] == 'F');
    TEST_CHECK(pro_diagnostics.storage_state == ED64_PRO_STORAGE_MCU_OWNED);
    TEST_CHECK(pro_disk.disk_status() == STA_NOINIT);
}

/**
 * @brief Verify pending writes and release failures block storage handoff.
 */
static void test_release_refusals (void) {
    mounted_fixture();
    uint8_t data[512] = {0};
    TEST_CHECK(pro_disk.disk_write(data, 2, 1) == RES_OK);
    TEST_CHECK(pro_release_storage() == FLASHCART_ERR_SD_CARD);
    TEST_CHECK(mock.unmounts == 0 && mock.fs_inits == 0);
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "unclosed write") != NULL);

    mounted_fixture();
    mock.unmount_result = -1;
    TEST_CHECK(pro_release_storage() == FLASHCART_ERR_SD_CARD);
    TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 0);
    TEST_CHECK(pro_release_storage() == FLASHCART_ERR_SD_CARD && mock.unmounts == 1);

    mounted_fixture();
    mock.init_error = ED64_PRO_LL_MCU_ERROR;
    TEST_CHECK(pro_release_storage() == FLASHCART_ERR_INT);
    TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 1);
    TEST_CHECK(pro_release_storage() == FLASHCART_ERR_SD_CARD && mock.fs_inits == 1);
}

/**
 * @brief Verify ROM chunking, byte swapping, progress, and launch staging.
 */
static void test_rom_loading (void) {
    mounted_fixture();
    mock.file_size = 300 * 1024;
    cart_card_byteswap = 1;
    TEST_CHECK(pro_load_rom("sd:/games/test.v64", progress_callback) == FLASHCART_OK);
    TEST_CHECK(mock.opens == 1 && mock.closes == 1 && mock.file_reads == 3);
    TEST_CHECK(strcmp(mock.opened_path, "/games/test.v64") == 0);
    TEST_CHECK(mock.destinations[0] == 0x10000000u && mock.destinations[1] == 0x10020000u);
    TEST_CHECK(mock.read_sizes[0] == 128 * 1024 && mock.read_sizes[2] == 44 * 1024);
    TEST_CHECK(mock.fci[0] == 0 && mock.fci[1] == 128 * 1024 && mock.fci[2] == 256 * 1024);
    TEST_CHECK(mock.flag_calls == 6 && mock.flags == 0);
    TEST_CHECK(memcmp(mock.events, "SRCSRCSRC", 9) == 0);
    TEST_CHECK(mock.progress_calls == 3 && mock.progress[0] > 0 && mock.progress[2] == 1.0f);
    TEST_CHECK(pro_diagnostics.launch_ready && pro_diagnostics.launch_extent == 300 * 1024);
    TEST_CHECK(strcmp(pro_diagnostics.rom_path, "/games/test.v64") == 0);
    TEST_CHECK(!pro_diagnostics.load_byte_swap);
}

/**
 * @brief Verify ROM size, dirty cache, read, close, and flag failures block launch.
 */
static void test_rom_load_refusals (void) {
    mounted_fixture();
    mock.file_size = (uint64_t)pro_diagnostics.rom_capacity + 1;
    TEST_CHECK(pro_load_rom("sd:/too-big.z64", NULL) == FLASHCART_ERR_LOAD);
    TEST_CHECK(mock.file_reads == 0 && mock.closes == 1 && !pro_diagnostics.launch_ready);
    TEST_CHECK(pro_get_error_message(FLASHCART_ERR_LOAD) == NULL);
    mounted_fixture();
    mock.fs.wflag = 1;
    TEST_CHECK(pro_load_rom("sd:/dirty.z64", NULL) == FLASHCART_ERR_LOAD);
    TEST_CHECK(mock.file_reads == 0 && mock.closes == 1);
    mounted_fixture();
    mock.short_read = 512;
    TEST_CHECK(pro_load_rom("sd:/short.z64", NULL) == FLASHCART_ERR_LOAD);
    TEST_CHECK(mock.closes == 1 && !pro_diagnostics.launch_ready);
    mounted_fixture();
    mock.close_result = FR_DISK_ERR;
    TEST_CHECK(pro_load_rom("sd:/close.z64", NULL) == FLASHCART_ERR_LOAD);
    TEST_CHECK(!pro_diagnostics.launch_ready);
    mounted_fixture();
    mock.fail_flag_call = 2;
    cart_card_byteswap = 1;
    TEST_CHECK(pro_load_rom("sd:/swap.v64", NULL) == FLASHCART_ERR_LOAD);
    TEST_CHECK(pro_diagnostics.storage_failed && mock.flag_calls == 2 && mock.operations == 1);
}

/**
 * @brief Verify auxiliary-file offsets extend the staged ROM extent.
 */
static void test_auxiliary_loading (void) {
    mounted_fixture();
    TEST_ASSERT(pro_load_rom("sd:/emulator.z64", NULL) == FLASHCART_OK);
    mock.file_size = 4608;
    mock.file_reads = 0;
    mock.operations = 0;
    TEST_CHECK(pro_load_file("sd:/rom.smc", 0x80000, 512) == FLASHCART_OK);
    TEST_CHECK(mock.seeks == 1 && mock.destinations[0] == 0x10080000u && mock.read_sizes[0] == 4096);
    TEST_CHECK(pro_diagnostics.launch_ready && pro_diagnostics.launch_extent == 0x81000);
    TEST_CHECK(strcmp(pro_diagnostics.rom_path, "/emulator.z64") == 0);
}

/**
 * @brief Verify invalid paths, offsets, and cartridge ranges reject loading safely.
 */
static void test_load_arguments (void) {
    mounted_fixture();
    TEST_CHECK(pro_load_rom(NULL, NULL) == FLASHCART_ERR_ARGS);
    TEST_CHECK(mock.opens == 0);
    char too_long[ED64_PRO_PATH_MAX + 5];
    memcpy(too_long, "sd:/", 4);
    memset(too_long + 4, 'a', sizeof(too_long) - 5);
    too_long[sizeof(too_long) - 1] = '\0';
    TEST_CHECK(pro_load_rom(too_long, NULL) == FLASHCART_ERR_ARGS);
    TEST_CHECK(mock.opens == 0);
    TEST_ASSERT(pro_load_rom("sd:/emulator.z64", NULL) == FLASHCART_OK);
    unsigned opens = mock.opens;
    TEST_CHECK(pro_load_file("sd:/rom.smc", 1, 0) == FLASHCART_ERR_ARGS && mock.opens == opens);
    TEST_ASSERT(pro_load_rom("sd:/emulator.z64", NULL) == FLASHCART_OK);
    unsigned reads = mock.file_reads;
    TEST_CHECK(pro_load_file("sd:/rom.smc", 0, 1536) == FLASHCART_ERR_ARGS && mock.file_reads == reads);
    TEST_ASSERT(pro_load_rom("sd:/emulator.z64", NULL) == FLASHCART_OK);
    TEST_CHECK(pro_load_file("sd:/rom.smc", pro_diagnostics.rom_capacity, 0) == FLASHCART_ERR_ARGS);
    TEST_CHECK(!pro_diagnostics.launch_ready);
    TEST_CHECK(pro_disk_read((void *)0x10000001u, 0, 1) == RES_PARERR);
    TEST_CHECK(pro_disk_read((void *)(uintptr_t)(0x10000000u + pro_diagnostics.rom_capacity), 0, 1) == RES_PARERR);
    TEST_CHECK(pro_disk_write((void *)0x10000000u, 0, 1) == RES_PARERR);
}

/**
 * @brief Verify save types and paths stage without MCU commands.
 */
static void test_save_mapping (void) {
    static const uint32_t types[] = {
        ED64_PRO_BRM_OFF,
        ED64_PRO_BRM_EEP4K,
        ED64_PRO_BRM_EEP16K,
        ED64_PRO_BRM_SRM32K,
        ED64_PRO_BRM_SRM96K,
        ED64_PRO_BRM_SRM128K,
        ED64_PRO_BRM_FLASH,
        ED64_PRO_BRM_FLASH
    };
    mounted_fixture();
    unsigned traffic = mock.begin_calls;
    for (unsigned type = 0; type < __FLASHCART_SAVE_TYPE_END; type++) {
        TEST_CHECK(pro_set_save_type(type) == FLASHCART_OK);
        TEST_CHECK(pro_diagnostics.brm_type == types[type] && pro_diagnostics.brm_size == SAVE_SIZE[type]);
        if (type != FLASHCART_SAVE_TYPE_NONE) {
            TEST_CHECK(!pro_diagnostics.save_ready);
            TEST_CHECK(pro_load_save("sd:/saves/test.sav") == FLASHCART_OK);
            TEST_CHECK(strcmp(pro_diagnostics.save_path, "/saves/test.sav") == 0 && pro_diagnostics.save_ready);
        } else {
            TEST_CHECK(pro_diagnostics.save_ready && pro_diagnostics.save_path[0] == '\0');
        }
    }
    TEST_CHECK(mock.begin_calls == traffic && mock.opens == 0);
    TEST_CHECK(pro_set_save_type(__FLASHCART_SAVE_TYPE_END) == FLASHCART_ERR_ARGS);
    TEST_CHECK(pro_set_save_type((flashcart_save_type_t)-1) == FLASHCART_ERR_ARGS);
    TEST_CHECK(!pro_diagnostics.save_ready);
}

/**
 * @brief Verify save and ROM changes clear stale staged save state.
 */
static void test_save_staging_reset (void) {
    mounted_fixture();
    TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_SRAM_1MBIT) == FLASHCART_OK);
    TEST_ASSERT(pro_load_save("sd:/save.sav") == FLASHCART_OK);
    TEST_CHECK(pro_load_save(NULL) == FLASHCART_ERR_ARGS && !pro_diagnostics.save_ready);
    TEST_ASSERT(pro_load_save("sd:/save.sav") == FLASHCART_OK);
    TEST_CHECK(pro_set_save_type(FLASHCART_SAVE_TYPE_NONE) == FLASHCART_OK);
    TEST_CHECK(pro_diagnostics.save_path[0] == '\0' && pro_diagnostics.brm_size == 0);
    TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_EEPROM_4KBIT) == FLASHCART_OK);
    TEST_ASSERT(pro_load_save("sd:/save.eep") == FLASHCART_OK);
    TEST_ASSERT(pro_load_rom("sd:/another.z64", NULL) == FLASHCART_OK);
    TEST_CHECK(pro_diagnostics.brm_type == ED64_PRO_BRM_OFF && !pro_diagnostics.save_ready);
    TEST_CHECK(pro_diagnostics.save_path[0] == '\0');
}

/**
 * @brief Stage a ROM and optional save, then reset launch command tracking.
 */
static void staged_game_fixture (bool save) {
    mounted_fixture();
    TEST_ASSERT(pro_load_rom("sd:/game.z64", NULL) == FLASHCART_OK);
    TEST_ASSERT(pro_set_save_type(save ? FLASHCART_SAVE_TYPE_SRAM_1MBIT : FLASHCART_SAVE_TYPE_NONE) == FLASHCART_OK);
    if (save) {
        TEST_ASSERT(pro_load_save("sd:/saves/game.sav") == FLASHCART_OK);
    }
    mock.events_count = 0;
    memset(mock.events, 0, sizeof(mock.events));
    mock.begin_calls = 0;
}

/**
 * @brief Verify launch command order and configuration with and without saves.
 */
static void test_game_handoff (void) {
    for (unsigned save = 0; save < 2; save++) {
        staged_game_fixture(save);
        TEST_CHECK(pro_deinit() == FLASHCART_OK);
        TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 1);
        TEST_CHECK(mock.command_calls == (save ? 4u : 3u));
        TEST_CHECK(strcmp(mock.events, save ? "TUFPBGS" : "TUFPGS") == 0);
        TEST_CHECK(strcmp(mock.launch_paths[0], "/game.z64") == 0);
        TEST_CHECK(strcmp(mock.launch_paths[1], save ? "/saves/game.sav" : "") == 0);
        TEST_CHECK(mock.config.brom_type == ED64_PRO_ROM_GPAK && mock.config.gpak_size == 1024);
        TEST_CHECK(mock.config.brm_type == (save ? ED64_PRO_BRM_SRM128K : ED64_PRO_BRM_OFF));
        TEST_CHECK(mock.config.brm_size == (save ? 131072u : 0u) && mock.config.rtc_mode == ED64_PRO_RTC_RDO);
        TEST_CHECK(mock.config.dd_en == 0 && mock.config.gpak_wren == 0 && mock.config.gpak_key == 0);
        for (unsigned i = 0; i < 8; i++) {
            TEST_CHECK(mock.config.reserved[i] == 0);
        }
        unsigned commands = mock.command_calls;
        TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.command_calls == commands);
    }
}

/** @brief Exercise the actual backend callback: success returns, failed handoff cannot boot. */
static void test_game_handoff_callback (void) {
    staged_game_fixture(false);
    handoff_halt_calls = 0;
    TEST_CHECK(ed64_pro_get_flashcart()->deinit() == FLASHCART_OK);
    TEST_CHECK(handoff_halt_calls == 0 && strcmp(mock.events, "TUFPGS") == 0);

    for (unsigned failure = 0; failure < 2; failure++) {
        staged_game_fixture(true);
        if (failure == 0) {
            pro_diagnostics.unsynced = true;
        } else {
            mock.fail_command = 2;
        }
        handoff_halt_expected = true;
        if (setjmp(handoff_halt) == 0) {
            ed64_pro_get_flashcart()->deinit();
            TEST_CHECK(false);
        }
        handoff_halt_expected = false;
        TEST_CHECK(handoff_halt_calls == failure + 1);
        TEST_CHECK(strstr(handoff_halt_message, failure == 0 ? "unclosed write" : "Save path") != NULL);
        TEST_CHECK(strchr(mock.events, 'S') == NULL);
    }
}

/**
 * @brief Verify each launch command failure blocks retries and retains diagnostics.
 */
static void test_game_handoff_failures (void) {
    static const char *steps[] = {"GPAK path", "Save path", "Game configuration", "Game start"};
    for (unsigned failure = 1; failure <= 4; failure++) {
        staged_game_fixture(true);
        mock.fail_command = failure;
        TEST_CHECK(pro_deinit() == FLASHCART_ERR_INT);
        TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 1 && mock.command_calls == failure);
        TEST_CHECK(pro_diagnostics.storage_failed && pro_disk_status() == STA_NOINIT);
        TEST_CHECK(strstr(ed64_pro_get_error_message(), steps[failure - 1]) != NULL);
        TEST_CHECK(strstr(ed64_pro_get_error_message(), "MCU status 0x31, NRESP 0xA3") != NULL);
        TEST_CHECK(pro_deinit() == FLASHCART_ERR_SD_CARD && mock.command_calls == failure);
    }
}

/** @brief Failed service shutdown blocks SD handoff and cannot be retried. */
static void test_rtc_shutdown_failure (void) {
    staged_game_fixture(false);
    mock.rtc_stop_error = ED64_PRO_LL_TIMEOUT;
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_INT);
    TEST_CHECK(mock.rtc_stops == 1 && mock.unmounts == 0 && mock.fs_inits == 0 && mock.command_calls == 0);
    TEST_CHECK(!test_rtc_initialized);
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_SD_CARD && mock.rtc_stops == 1);
}

/**
 * @brief Verify incomplete staging and pending writes block launch before handoff.
 */
static void test_launch_preflight (void) {
    mounted_fixture();
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD);
    TEST_CHECK(rtc_get_source() == RTC_SOURCE_JOYBUS && mock.rtc_stops == 0);
    TEST_CHECK(mock.unmounts == 0 && mock.fs_inits == 0 && mock.command_calls == 0);
    TEST_ASSERT(pro_load_rom("sd:/game.z64", NULL) == FLASHCART_OK);
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.unmounts == 0);
    TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_EEPROM_4KBIT) == FLASHCART_OK);
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.unmounts == 0);
    TEST_ASSERT(pro_load_save("sd:/game.eep") == FLASHCART_OK);
    pro_diagnostics.unsynced = true;
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_SD_CARD);
    TEST_CHECK(mock.unmounts == 0 && mock.fs_inits == 0 && mock.command_calls == 0);
}

/**
 * @brief Verify MCU paths use CP437 while FatFs paths retain UTF-8.
 */
static void test_cp437_paths (void) {
    mounted_fixture();
    char rom[] = "sd:/Caf\xC3\xA9/\xC3\xA9.z64";
    TEST_CHECK(pro_load_rom(rom, NULL) == FLASHCART_OK);
    TEST_CHECK(strcmp(mock.opened_path, "/Caf\xC3\xA9/\xC3\xA9.z64") == 0);
    TEST_CHECK(strcmp(pro_diagnostics.rom_path, "/Caf\x82/\x82.z64") == 0);
    TEST_CHECK(strcmp(rom, "sd:/Caf\xC3\xA9/\xC3\xA9.z64") == 0);
    TEST_CHECK(mock.stats == 0);
    TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_EEPROM_4KBIT) == FLASHCART_OK);
    TEST_CHECK(pro_load_save("sd:/Caf\xC3\xA9/saves/\xC3\xA9.sav") == FLASHCART_OK);
    TEST_CHECK(strcmp(pro_diagnostics.save_path, "/Caf\x82/saves/\x82.sav") == 0);
    TEST_CHECK(mock.stats == 0);
    TEST_CHECK(pro_deinit() == FLASHCART_OK);
    TEST_CHECK(strcmp(mock.launch_paths[0], "/Caf\x82/\x82.z64") == 0);
    TEST_CHECK(strcmp(mock.launch_paths[1], "/Caf\x82/saves/\x82.sav") == 0);

    mounted_fixture();
    TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_EEPROM_4KBIT) == FLASHCART_OK);
    TEST_CHECK(pro_load_save("/µùÑµ£¼Φ¬₧-├⌐.sav") == FLASHCART_OK);
    TEST_CHECK(strcmp(pro_diagnostics.save_path, "/\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E-\xC3\xA9.sav") == 0);
    TEST_CHECK(mock.stats == 0);
}

/**
 * @brief Verify unmappable path components use validated short aliases.
 */
static void test_unicode_short_aliases (void) {
    mounted_fixture();
    mock.aliases[0].path = "/日本語";
    mock.aliases[0].alias = "NIPPON~1";
    mock.aliases[1].path = "/日本語/Café/雪-😀.z64";
    mock.aliases[1].alias = "CAFÉ~2.Z64";
    mock.aliases[2].path = "/日本語/Café/雪-😀.sav";
    mock.aliases[2].alias = "ÉTÉ~1.SAV";
    mock.alias_count = 3;
    TEST_CHECK(pro_load_rom("sd:/日本語/Café/雪-😀.z64", NULL) == FLASHCART_OK);
    TEST_CHECK(strcmp(mock.opened_path, "/日本語/Café/雪-😀.z64") == 0);
    TEST_CHECK(strcmp(pro_diagnostics.rom_path, "/NIPPON~1/Caf\x82/CAF\x90~2.Z64") == 0);
    TEST_CHECK(mock.stats == 2);
    TEST_CHECK(strcmp(mock.stat_paths[0], "/日本語") == 0);
    TEST_CHECK(strcmp(mock.stat_paths[1], "/日本語/Café/雪-😀.z64") == 0);
    TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_EEPROM_4KBIT) == FLASHCART_OK);
    TEST_CHECK(mock.stats == 2);
    TEST_CHECK(pro_load_save("sd:/日本語/Café/雪-😀.sav") == FLASHCART_OK);
    TEST_CHECK(strcmp(pro_diagnostics.save_path, "/NIPPON~1/Caf\x82/\x90T\x90~1.SAV") == 0);
    TEST_CHECK(mock.stats == 4 && strcmp(mock.stat_paths[3], "/日本語/Café/雪-😀.sav") == 0);
    TEST_CHECK(pro_deinit() == FLASHCART_OK);
    TEST_CHECK(mock.stats == 4);
    TEST_CHECK(strcmp(mock.launch_paths[0], "/NIPPON~1/Caf\x82/CAF\x90~2.Z64") == 0);
    TEST_CHECK(strcmp(mock.launch_paths[1], "/NIPPON~1/Caf\x82/\x90T\x90~1.SAV") == 0);
}

/**
 * @brief Verify malformed ROM paths clear staging before metadata or data I/O.
 */
static void test_invalid_utf8_paths (void) {
    static const char *paths[] = {
        "/\x80.z64",
        "/\xC0\xAF.z64",
        "/\xE0\x80\xAF.z64",
        "/\xED\xA0\x80.z64",
        "/\xF4\x90\x80\x80.z64",
        "/\xF5\x80\x80\x80.z64",
        "/\xE2\x28\xA1.z64",
        "/\xC2",
        "/\xE2\x82",
        "/\xF0\x9F\x92",
        "/日本語/\x80.z64"
    };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        mounted_fixture();
        TEST_ASSERT(pro_load_rom("sd:/old.z64", NULL) == FLASHCART_OK);
        unsigned opens = mock.opens;
        unsigned reads = mock.file_reads;
        unsigned io = mock.begin_calls;
        TEST_CHECK(pro_load_rom((char *)paths[i], NULL) == FLASHCART_ERR_ARGS);
        TEST_CHECK(!pro_diagnostics.launch_ready && !pro_diagnostics.save_ready && pro_diagnostics.rom_path[0] == '\0');
        TEST_CHECK(mock.opens == opens && mock.file_reads == reads && mock.stats == 0 && mock.begin_calls == io);
        TEST_CHECK(strstr(pro_get_error_message(FLASHCART_ERR_ARGS), "invalid UTF-8") != NULL);
        TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.unmounts == 0 && mock.command_calls == 0);
    }
}

/**
 * @brief Verify malformed save paths clear staging without I/O.
 */
static void test_invalid_utf8_save_path (void) {
    mounted_fixture();
    TEST_ASSERT(pro_load_rom("/game.z64", NULL) == FLASHCART_OK);
    TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_EEPROM_4KBIT) == FLASHCART_OK);
    TEST_ASSERT(pro_load_save("/old.sav") == FLASHCART_OK);
    unsigned io = mock.begin_calls;
    TEST_CHECK(pro_load_save("/日本語/\xED\xA0\x80.sav") == FLASHCART_ERR_ARGS);
    TEST_CHECK(!pro_diagnostics.save_ready && pro_diagnostics.save_path[0] == '\0');
    TEST_CHECK(mock.stats == 0 && mock.begin_calls == io);
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.unmounts == 0 && mock.command_calls == 0);
}

/**
 * @brief Verify missing or invalid short aliases block launch staging.
 */
static void test_missing_or_invalid_alias (void) {
    static const struct {
        const char *alias;
        FRESULT result;
        bool unterminated;
    } cases[] = {
        {"", FR_OK, false},
        {"日本語", FR_OK, false},
        {"BAD/NAME", FR_OK, false},
        {"BAD?NAME", FR_OK, false},
        {"\xC3", FR_OK, false},
        {"IGNORED", FR_OK, true},
        {"STALE~1.SAV", FR_NO_FILE, false},
        {"STALE~1.SAV", FR_DISK_ERR, false}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        mounted_fixture();
        TEST_ASSERT(pro_load_rom("/game.z64", NULL) == FLASHCART_OK);
        TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_EEPROM_4KBIT) == FLASHCART_OK);
        TEST_ASSERT(pro_load_save("/old.sav") == FLASHCART_OK);
        mock.aliases[0].path = "/日本語.sav";
        mock.aliases[0].alias = cases[i].alias;
        mock.aliases[0].result = cases[i].result;
        mock.aliases[0].unterminated = cases[i].unterminated;
        mock.alias_count = 1;
        flashcart_err_t expected = cases[i].result == FR_OK ? FLASHCART_ERR_ARGS : FLASHCART_ERR_LOAD;
        unsigned io = mock.begin_calls;
        TEST_CHECK(pro_load_save("/日本語.sav") == expected);
        TEST_CHECK(pro_diagnostics.save_path[0] == '\0' && !pro_diagnostics.save_ready);
        TEST_CHECK(mock.stats == 1 && mock.begin_calls == io);
        TEST_CHECK(pro_get_error_message(expected) != NULL);
        TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.unmounts == 0 && mock.command_calls == 0);
        TEST_CHECK(pro_load_save("/new.sav") == FLASHCART_OK && pro_diagnostics.path_error == NULL);
    }
    mounted_fixture();
    TEST_CHECK(pro_load_rom("/日本語.z64", NULL) == FLASHCART_ERR_LOAD);
    TEST_CHECK(mock.opens == 0 && mock.file_reads == 0 && !pro_diagnostics.launch_ready);
    TEST_CHECK(pro_diagnostics.rom_path[0] == '\0');
}

/**
 * @brief Verify encoded path limits include short-alias expansion.
 */
static void test_encoded_path_bounds (void) {
    mounted_fixture();
    char path[ED64_PRO_PATH_MAX + 2];
    memset(path, 'a', ED64_PRO_PATH_MAX + 1);
    path[0] = '/';
    for (unsigned i = 64; i < ED64_PRO_PATH_MAX; i += 64) {
        path[i] = '/';
    }
    path[ED64_PRO_PATH_MAX] = '\0';
    TEST_CHECK(pro_load_rom(path, NULL) == FLASHCART_OK);
    TEST_CHECK(strlen(pro_diagnostics.rom_path) == ED64_PRO_PATH_MAX && mock.stats == 0);
    unsigned opens = mock.opens;
    path[ED64_PRO_PATH_MAX] = 'a';
    path[ED64_PRO_PATH_MAX + 1] = '\0';
    TEST_CHECK(pro_load_rom(path, NULL) == FLASHCART_ERR_ARGS && mock.opens == opens && mock.stats == 0);

    mounted_fixture();
    path[ED64_PRO_PATH_MAX - 4] = '/';
    memcpy(path + ED64_PRO_PATH_MAX - 3, "水", 3);
    path[ED64_PRO_PATH_MAX] = '\0';
    mock.aliases[0].path = path;
    mock.aliases[0].alias = "WATER~1";
    mock.alias_count = 1;
    TEST_CHECK(pro_load_rom(path, NULL) == FLASHCART_ERR_ARGS);
    TEST_CHECK(mock.stats == 1 && mock.opens == 0 && pro_diagnostics.rom_path[0] == '\0');
    mock.aliases[0].alias = "W";
    TEST_CHECK(pro_load_rom(path, NULL) == FLASHCART_OK);
    TEST_CHECK(strlen(pro_diagnostics.rom_path) == ED64_PRO_PATH_MAX - 2);
}

/**
 * @brief Verify path lookup and loading require FatFs storage ownership.
 */
static void test_path_metadata_requires_ownership (void) {
    mounted_fixture();
    TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_EEPROM_4KBIT) == FLASHCART_OK);
    pro_diagnostics.storage_state = ED64_PRO_STORAGE_MOUNTING;
    TEST_CHECK(pro_load_save("/日本語.sav") == FLASHCART_ERR_LOAD);
    TEST_CHECK(mock.stats == 0 && mock.opens == 0 && !pro_diagnostics.save_ready);
    TEST_CHECK(pro_load_rom("/日本語.z64", NULL) == FLASHCART_ERR_LOAD);
    TEST_CHECK(mock.stats == 0 && mock.opens == 0 && !pro_diagnostics.launch_ready);
    pro_diagnostics.launch_ready = true;
    TEST_CHECK(pro_load_file("/日本語.gb", 0x80000, 0) == FLASHCART_ERR_LOAD);
    TEST_CHECK(mock.stats == 0 && mock.opens == 0 && mock.begin_calls == 0 && !pro_diagnostics.launch_ready);
}

/**
 * @brief Verify Unicode auxiliary reads use FatFs without changing the staged MCU path.
 */
static void test_unicode_auxiliary_uses_fatfs_only (void) {
    mounted_fixture();
    TEST_ASSERT(pro_load_rom("/emulator.z64", NULL) == FLASHCART_OK);
    mock.operations = 0;
    mock.file_reads = 0;
    TEST_CHECK(pro_load_file("sd:/日本語/雪.gb", 0x80000, 0) == FLASHCART_OK);
    TEST_CHECK(strcmp(mock.opened_path, "/日本語/雪.gb") == 0 && mock.stats == 0);
    TEST_CHECK(strcmp(pro_diagnostics.rom_path, "/emulator.z64") == 0);
}

/** @brief Load a fresh, structurally valid native IPL using the production callback. */
static void stage_ipl (void) {
    mock.ipl_fixture = true;
    mock.file_size = ED64_PRO_IPL_SIZE;
    TEST_ASSERT(ed64_pro_get_flashcart()->load_64dd_ipl("sd:/menu/64ddipl/NDDJ2.n64", progress_callback) == FLASHCART_OK);
    mock.ipl_fixture = false;
}

static void stage_disk (void) {
    flashcart_disk_parameters_t parameters = {.disk_type = 0};
    mock.file_size = ED64_PRO_NATIVE_DISK_SIZE;
    TEST_ASSERT(ed64_pro_get_flashcart()->load_64dd_disk("sd:/DD/Caf\xC3\xA9.ndd", &parameters) == FLASHCART_OK);
}

/** @brief Standalone IPL loads use FCI addresses, then the native firmware owns disk I/O. */
static void test_dd_standalone (void) {
    mounted_fixture();
    stage_ipl();
    TEST_CHECK(mock.operations == 32 && mock.progress_calls == 32 && mock.progress[31] == 1.0f);
    TEST_CHECK(mock.file_reads == 34 && !pro_diagnostics.loading_ipl);
    for (unsigned i = 0; i < 32; i++) {
        TEST_CHECK(mock.fci[i] == ED64_PRO_FCI_IPL + i * 128 * 1024u);
        TEST_CHECK(mock.destinations[i + 2] == mock.fci[i] && mock.fci[i] != 0x1F800000u);
    }
    TEST_CHECK(pro_diagnostics.ipl_ready && !pro_diagnostics.disk_ready && !pro_diagnostics.save_ready);
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.unmounts == 0 && mock.rtc_running);
    stage_disk();
    TEST_CHECK(mock.open_mode == (FA_READ | FA_WRITE) && mock.writes == 0);
    TEST_CHECK(strcmp(pro_diagnostics.disk_path, "/DD/Caf\x82.ndd") == 0);
    mock.events_count = mock.command_calls = 0;
    memset(mock.events, 0, sizeof(mock.events));
    TEST_CHECK(pro_deinit() == FLASHCART_OK);
    TEST_CHECK(strcmp(mock.events, "TUFDGS") == 0 && mock.unmounts == 1 && mock.fs_inits == 1);
    TEST_CHECK(strcmp(mock.launch_paths[2], "/DD/Caf\x82.ndd") == 0 && !mock.launch_paths[0][0]);
    TEST_CHECK(mock.config.brom_type == ED64_PRO_ROM_IPL && mock.config.gpak_size == 0);
    TEST_CHECK(mock.config.brm_type == ED64_PRO_BRM_OFF && mock.config.brm_size == 0);
    TEST_CHECK(mock.config.dd_en == 1 && mock.config.rtc_mode == ED64_PRO_RTC_STD);
}

/** @brief Attaching DD preserves the cartridge extent and its existing save type/path. */
static void test_dd_combo_and_reset (void) {
    staged_game_fixture(true);
    stage_ipl();
    stage_disk();
    TEST_CHECK(pro_diagnostics.launch_extent == 1024 && pro_diagnostics.save_ready);
    mock.events_count = mock.command_calls = 0;
    memset(mock.events, 0, sizeof(mock.events));
    TEST_CHECK(pro_deinit() == FLASHCART_OK);
    TEST_CHECK(strcmp(mock.events, "TUFPDBGS") == 0);
    TEST_CHECK(mock.config.brom_type == ED64_PRO_ROM_GPAK && mock.config.gpak_size == 1024);
    TEST_CHECK(mock.config.brm_type == ED64_PRO_BRM_SRM128K && mock.config.brm_size == 131072);
    TEST_CHECK(strcmp(mock.launch_paths[1], "/saves/game.sav") == 0 && mock.config.dd_en == 1);

    mounted_fixture();
    stage_ipl();
    stage_disk();
    mock.file_reads = mock.operations = mock.progress_calls = 0;
    mock.file_size = 1024;
    TEST_CHECK(pro_load_rom("sd:/ordinary.z64", NULL) == FLASHCART_OK);
    TEST_CHECK(!pro_diagnostics.dd_requested && !pro_diagnostics.ipl_ready && !pro_diagnostics.disk_ready);
    TEST_CHECK(pro_diagnostics.disk_path[0] == '\0');
}

/** @brief DD failures preserve complete cartridges; incomplete preparations are discarded on retry. */
static void test_dd_failed_combo_retry (void) {
    for (unsigned fault = 0; fault < 5; fault++) {
        staged_game_fixture(true);
        if (fault == 0) {
            mock.file_size = 0;
            TEST_CHECK(pro_load_64dd_ipl("sd:/bad-ipl.n64", NULL) == FLASHCART_ERR_LOAD);
        } else if (fault == 1) {
            stage_ipl();
            mock.file_size = 0;
            flashcart_disk_parameters_t parameters = {0};
            TEST_CHECK(pro_load_64dd_disk("sd:/bad.ndd", &parameters) == FLASHCART_ERR_LOAD);
        } else if (fault == 2) {
            TEST_CHECK(pro_load_save(NULL) == FLASHCART_ERR_ARGS);
        } else if (fault == 3) {
            mock.file_size = 0;
            TEST_CHECK(pro_load_rom("sd:/failed.z64", NULL) == FLASHCART_ERR_LOAD);
        } else {
            TEST_CHECK(pro_load_file("sd:/failed.gb", 1, 0) == FLASHCART_ERR_ARGS);
        }
        TEST_CHECK(pro_cartridge_prepared() == (fault < 2));
        mock.file_reads = mock.operations = mock.progress_calls = 0;
        stage_ipl();
        stage_disk();
        TEST_CHECK(pro_cartridge_prepared() == (fault < 2));
        if (fault < 2) {
            TEST_CHECK(strcmp(pro_diagnostics.rom_path, "/game.z64") == 0);
            TEST_CHECK(strcmp(pro_diagnostics.save_path, "/saves/game.sav") == 0);
            TEST_CHECK(pro_diagnostics.launch_extent == 1024 && pro_diagnostics.brm_type == ED64_PRO_BRM_SRM128K);
        } else {
            TEST_CHECK(!pro_diagnostics.rom_path[0] && !pro_diagnostics.save_path[0]);
            TEST_CHECK(pro_diagnostics.launch_extent == 0 && pro_diagnostics.brm_type == ED64_PRO_BRM_OFF);
        }
        TEST_CHECK(pro_deinit() == FLASHCART_OK);
        TEST_CHECK(mock.config.brom_type == (fault < 2 ? ED64_PRO_ROM_GPAK : ED64_PRO_ROM_IPL));
        TEST_CHECK(mock.config.gpak_size == (fault < 2 ? 1024u : 0u));
        TEST_CHECK(mock.config.brm_size == (fault < 2 ? 131072u : 0u));
    }
}

/** @brief Overlap is rejected before IPL I/O and clears only the rejected cartridge for a clean retry. */
static void test_dd_overlap_preflight (void) {
    staged_game_fixture(true);
    pro_diagnostics.launch_extent = ED64_PRO_DD_ROM_LIMIT + 512;
    unsigned opens = mock.opens;
    TEST_CHECK(pro_load_64dd_ipl("sd:/ipl.n64", NULL) == FLASHCART_ERR_LOAD);
    TEST_CHECK(mock.opens == opens && mock.file_reads == 1 && mock.operations == 1);
    TEST_CHECK(!pro_diagnostics.ipl_ready && !pro_diagnostics.disk_ready && !pro_cartridge_prepared());
    TEST_CHECK(!pro_diagnostics.rom_path[0] && !pro_diagnostics.save_path[0]);
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.unmounts == 0);
    mock.file_reads = mock.operations = mock.progress_calls = 0;
    stage_ipl();
    stage_disk();
    TEST_CHECK(pro_deinit() == FLASHCART_OK && mock.config.brom_type == ED64_PRO_ROM_IPL);
    TEST_CHECK(mock.config.brm_size == 0 && mock.config.gpak_size == 0);

    staged_game_fixture(true);
    pro_diagnostics.launch_extent = ED64_PRO_DD_ROM_LIMIT;
    stage_ipl();
    stage_disk();
    TEST_CHECK(pro_deinit() == FLASHCART_OK && mock.config.gpak_size == ED64_PRO_DD_ROM_LIMIT);
}

/** @brief Ordinary payload loading can still use the full firmware-reported ROM capacity. */
static void test_ordinary_capacity_after_dd (void) {
    mounted_fixture();
    stage_ipl();
    stage_disk();
    mock.file_size = 512;
    mock.file_reads = mock.operations = mock.progress_calls = 0;
    TEST_ASSERT(pro_load_rom("sd:/emulator.z64", NULL) == FLASHCART_OK);
    TEST_ASSERT(pro_load_file("sd:/rom.bin", pro_diagnostics.rom_capacity - 512, 0) == FLASHCART_OK);
    TEST_ASSERT(pro_set_save_type(FLASHCART_SAVE_TYPE_NONE) == FLASHCART_OK);
    TEST_CHECK(pro_diagnostics.launch_extent == pro_diagnostics.rom_capacity);
    TEST_CHECK(pro_diagnostics.launch_extent > ED64_PRO_DD_ROM_LIMIT);
    TEST_CHECK(pro_deinit() == FLASHCART_OK && mock.config.gpak_size == pro_diagnostics.rom_capacity);
    TEST_CHECK(mock.config.dd_en == 0);
}

/** @brief Unsupported, read-only, missing and incomplete images cannot reach handoff. */
static void test_dd_setup_refusals (void) {
    const FSIZE_t sizes[] = {0, ED64_PRO_IPL_SIZE - 1, ED64_PRO_IPL_SIZE + 512};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        mounted_fixture();
        mock.file_size = sizes[i];
        TEST_CHECK(pro_load_64dd_ipl("sd:/bad.n64", NULL) == FLASHCART_ERR_LOAD);
        TEST_CHECK(mock.file_reads == 0 && mock.closes == 1 && !pro_diagnostics.ipl_ready);
        TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.unmounts == 0);
    }
    mounted_fixture();
    mock.file_size = ED64_PRO_IPL_SIZE;
    mock.ipl_fixture = mock.bad_ipl_magic = true;
    TEST_CHECK(pro_load_64dd_ipl("sd:/bad.n64", NULL) == FLASHCART_ERR_LOAD);
    TEST_CHECK(!pro_diagnostics.loading_ipl && !pro_diagnostics.launch_ready && mock.operations == 0);

    for (unsigned identity_fault = 0; identity_fault < 2; identity_fault++) {
        mounted_fixture();
        mock.file_size = ED64_PRO_IPL_SIZE;
        mock.ipl_fixture = true;
        mock.bad_ipl_identity = identity_fault == 0;
        TEST_CHECK(pro_load_64dd_ipl(identity_fault ? "sd:/NDDE0.n64" : "sd:/NDDJ2.n64", NULL) == FLASHCART_ERR_LOAD);
        TEST_CHECK(!pro_diagnostics.ipl_ready && mock.operations == 0 && mock.closes == 1);
    }

    flashcart_disk_parameters_t parameters = {0};
    mounted_fixture();
    TEST_CHECK(pro_load_64dd_disk("sd:/disk.ndd", &parameters) == FLASHCART_ERR_LOAD && mock.opens == 0);
    for (unsigned fault = 0; fault < 4; fault++) {
        mounted_fixture();
        stage_ipl();
        mock.file_size = fault == 0 ? ED64_PRO_NATIVE_DISK_SIZE - 512 : ED64_PRO_NATIVE_DISK_SIZE;
        if (fault == 1) mock.open_result = FR_NO_FILE;
        if (fault == 2) mock.open_result = FR_INVALID_NAME; /* Failed writable open, including read-only files. */
        if (fault == 3) mock.close_result = FR_DISK_ERR;
        TEST_CHECK(pro_load_64dd_disk("sd:/bad.ndd", &parameters) == FLASHCART_ERR_LOAD);
        TEST_CHECK(!pro_diagnostics.disk_ready && !pro_diagnostics.launch_ready && !pro_diagnostics.disk_path[0]);
        TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.unmounts == 0 && mock.command_calls == 0);
    }
}

/** @brief IPL metadata and close failures always release the file and block launch. */
static void test_dd_ipl_cleanup_failures (void) {
    for (unsigned fault = 0; fault < 3; fault++) {
        mounted_fixture();
        mock.ipl_fixture = true;
        mock.file_size = ED64_PRO_IPL_SIZE;
        if (fault == 0) mock.read_result = FR_DISK_ERR;
        if (fault == 1) mock.seek_result = FR_DISK_ERR;
        if (fault == 2) mock.close_result = FR_DISK_ERR;
        TEST_CHECK(pro_load_64dd_ipl("sd:/NDDJ2.n64", NULL) == FLASHCART_ERR_LOAD);
        TEST_CHECK(mock.opens == 1 && mock.closes == 1);
        TEST_CHECK(!pro_diagnostics.loading_ipl && !pro_diagnostics.ipl_ready && !pro_diagnostics.launch_ready);
        TEST_CHECK(mock.operations == (fault < 2 ? 0u : 32u));
        TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD && mock.unmounts == 0 && mock.command_calls == 0);
    }
}

/** @brief IPL transfer faults and unmapped launch memory stop the actual backend boot callback. */
static void test_dd_transfer_and_mapping_faults (void) {
    mounted_fixture();
    mock.ipl_fixture = true;
    mock.file_size = ED64_PRO_IPL_SIZE;
    mock.fail_operation = 2;
    TEST_CHECK(pro_load_64dd_ipl("sd:/ipl.n64", NULL) == FLASHCART_ERR_LOAD);
    TEST_CHECK(!pro_diagnostics.loading_ipl && !pro_diagnostics.ipl_ready && !pro_diagnostics.launch_ready);
    TEST_CHECK(pro_diagnostics.storage_failed && mock.operations == 2 && mock.closes == 1);

    mounted_fixture();
    stage_ipl();
    stage_disk();
    mock.bad_ipl_mapping = true;
    handoff_halt_expected = true;
    if (setjmp(handoff_halt) == 0) {
        ed64_pro_get_flashcart()->deinit();
        TEST_CHECK(false);
    }
    handoff_halt_expected = false;
    TEST_CHECK(pro_diagnostics.storage_failed && strstr(handoff_halt_message, "IPL mapping"));
    TEST_CHECK(mock.command_calls == 3 && mock.unmounts == 1);
}

/** @brief Every native disk handoff failure is terminal and no cartridge slot limit is imposed. */
static void test_dd_handoff_failures_and_native_swap (void) {
    const char *steps[] = {"Disk path", "Game configuration", "Game start"};
    for (unsigned fault = 1; fault <= 3; fault++) {
        mounted_fixture();
        stage_ipl();
        stage_disk();
        mock.fail_command = fault;
        TEST_CHECK(pro_deinit() == FLASHCART_ERR_INT);
        TEST_CHECK(mock.command_calls == fault && strstr(ed64_pro_get_error_message(), steps[fault - 1]));
        TEST_CHECK(pro_deinit() == FLASHCART_ERR_SD_CARD && mock.command_calls == fault);
    }
    mounted_fixture();
    stage_ipl();
    mock.file_size = ED64_PRO_NATIVE_DISK_SIZE;
    flashcart_disk_parameters_t parameters = {0};
    char *unused_menu_list[] = {"sd:/DD/sibling.ndd"};
    TEST_CHECK(pro_load_64dd_disks("sd:/DD/primary.ndd", &parameters, unused_menu_list, 25) == FLASHCART_OK);
    TEST_CHECK(pro_diagnostics.disk_ready && pro_has_feature(FLASHCART_FEATURE_64DD));
    TEST_CHECK(pro_load_64dd_disks("sd:/DD/primary.ndd", &parameters, NULL, -1) == FLASHCART_OK);
    TEST_CHECK(pro_diagnostics.disk_ready);
    TEST_CHECK(pro_load_64dd_disks(NULL, &parameters, unused_menu_list, 1) == FLASHCART_ERR_ARGS);
    TEST_CHECK(!pro_diagnostics.disk_ready);
}

/** @brief Missing IPL before the backend callback preserves a fully prepared cartridge/save. */
static void test_shared_loader_missing_ipl_then_standalone (void) {
    mounted_fixture();
    loader_active = true;
    loader_ipl_present = loader_bad_save = false;
    flashcart = ed64_pro_get_flashcart();
    menu_t menu = {0};
    menu.storage_prefix = "sd:";
    menu.load.combined_disk_rom = true;
    menu.load.rom_path = path_create("sd:/game.z64");
    menu.load.rom_info.save_type = SAVE_TYPE_SRAM_256KBIT;
    menu.load.disk_slots.primary.disk_path = path_create("sd:/disk.ndd");
    menu.load.disk_slots.primary.disk_info.region = DISK_REGION_JAPANESE;
    TEST_ASSERT(cart_load_n64_rom_and_save(&menu, NULL, NULL) == CART_LOAD_OK);
    TEST_CHECK(pro_cartridge_prepared());
    unsigned opens = mock.opens;
    TEST_CHECK(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_ERR_64DD_IPL_NOT_FOUND);
    TEST_CHECK(mock.opens == opens && pro_cartridge_prepared() && !pro_diagnostics.dd_requested);
    menu.load.combined_disk_rom = false;
    loader_ipl_present = true;
    mock.file_size = ED64_PRO_IPL_SIZE;
    mock.ipl_fixture = true;
    TEST_ASSERT(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_OK);
    TEST_CHECK(pro_cartridge_prepared() && pro_diagnostics.ipl_ready && pro_diagnostics.disk_ready);
    TEST_ASSERT(pro_deinit() == FLASHCART_OK);
    TEST_CHECK(mock.config.brom_type == ED64_PRO_ROM_GPAK && mock.config.gpak_size == 1024);
    TEST_CHECK(mock.config.dd_en == 1 && mock.config.brm_size == 32768);
    TEST_CHECK(strcmp(mock.launch_paths[1], "/game.sav") == 0);
    path_free(menu.load.rom_path);
    path_free(menu.load.disk_slots.primary.disk_path);
    loader_active = false;
}

/** @brief ROM/save failures in the production shared loader cannot leave a complete cartridge. */
static void test_shared_loader_failure_then_standalone (void) {
    for (unsigned fault = 0; fault < 2; fault++) {
        mounted_fixture();
        loader_active = loader_ipl_present = true;
        loader_bad_save = fault == 1;
        flashcart = ed64_pro_get_flashcart();
        menu_t menu = {0};
        menu.storage_prefix = "sd:";
        menu.load.rom_path = path_create("sd:/game.z64");
        menu.load.rom_info.save_type = SAVE_TYPE_SRAM_256KBIT;
        menu.load.disk_slots.primary.disk_path = path_create("sd:/disk.ndd");
        menu.load.disk_slots.primary.disk_info.region = DISK_REGION_JAPANESE;
        if (fault == 0) mock.file_size = 0;
        TEST_CHECK(cart_load_n64_rom_and_save(&menu, NULL, NULL) ==
            (fault == 0 ? CART_LOAD_ERR_ROM_LOAD_FAIL : CART_LOAD_ERR_SAVE_LOAD_FAIL));
        TEST_CHECK(!pro_cartridge_prepared() && !pro_diagnostics.dd_requested);
        mock.file_size = ED64_PRO_IPL_SIZE;
        mock.ipl_fixture = true;
        TEST_ASSERT(cart_load_64dd_ipl_and_disks(&menu, NULL) == CART_LOAD_OK);
        TEST_CHECK(!pro_diagnostics.rom_path[0] && !pro_diagnostics.save_path[0]);
        TEST_ASSERT(pro_deinit() == FLASHCART_OK);
        TEST_CHECK(mock.config.brom_type == ED64_PRO_ROM_IPL && mock.config.brm_size == 0);
        path_free(menu.load.rom_path);
        path_free(menu.load.disk_slots.primary.disk_path);
        loader_active = loader_bad_save = false;
    }
}

TEST_LIST = {
    {"3.3V packed BCD and invalid digits", test_voltage_bcd},
    {"first cached refreshed and failed voltage samples", test_voltage_cache},
    {"voltage argument and transport failures", test_voltage_failures},
    {"voltage release failure never publishes sample", test_voltage_release_failure},
    {"live full-width SYSSTAT button reads", test_live_button},
    {"shared loader missing IPL then standalone retains complete GPAK and save", test_shared_loader_missing_ipl_then_standalone},
    {"shared loader ROM/save failure then standalone disables cartridge services", test_shared_loader_failure_then_standalone},
    {"native DD standalone IPL FCI loading and safe handoff", test_dd_standalone},
    {"native DD combo preserves cartridge saves and ROM resets DD state", test_dd_combo_and_reset},
    {"native DD retries retain complete cartridges and discard failed preparation", test_dd_failed_combo_retry},
    {"native DD mirror overlap rejects before writing and allows standalone retry", test_dd_overlap_preflight},
    {"ordinary ROM loading retains full capacity after DD staging", test_ordinary_capacity_after_dd},
    {"native DD unsupported missing read-only and incomplete setup refuses boot", test_dd_setup_refusals},
    {"native DD IPL metadata and close failures release the file", test_dd_ipl_cleanup_failures},
    {"native DD transfer and mapping faults halt boot", test_dd_transfer_and_mapping_faults},
    {"native DD handoff failures latch and native swap has no slot cap", test_dd_handoff_failures_and_native_swap},
    {"staged paths use exact CP437 while FatFs keeps UTF-8", test_cp437_paths},
    {"unmappable components use verified UTF-8 short aliases", test_unicode_short_aliases},
    {"malformed UTF-8 rejects before metadata or data I/O", test_invalid_utf8_paths},
    {"malformed save paths clear staged writeback", test_invalid_utf8_save_path},
    {"missing exFAT or invalid short aliases block handoff", test_missing_or_invalid_alias},
    {"encoded path bounds include alias expansion", test_encoded_path_bounds},
    {"path metadata requires mounted FatFs ownership", test_path_metadata_requires_ownership},
    {"Unicode auxiliary reads stay entirely in FatFs", test_unicode_auxiliary_uses_fatfs_only},
    {"game handoff command order and exact configuration", test_game_handoff},
    {"game handoff callback halts failures and returns only on success", test_game_handoff_callback},
    {"each game handoff fault is terminal", test_game_handoff_failures},
    {"incomplete launch and unsynced writes block handoff", test_launch_preflight},
    {"save mappings stage without MCU traffic", test_save_mapping},
    {"save paths and ROM changes clear staged state", test_save_staging_reset},
    {"ROM chunking byte-swap bracketing and staging", test_rom_loading},
    {"ROM load failure and dirty-window guards", test_rom_load_refusals},
    {"emulator offsets extend staged ROM extent", test_auxiliary_loading},
    {"ROM paths offsets and capacity reject safely", test_load_arguments},
    {"cached firmware identity and capacity", test_firmware_info},
    {"firmware date and minimum boundaries", test_firmware_boundaries},
    {"firmware query failure is latched", test_firmware_query_failure},
    {"mount starts compatible RTC services after FatFs", test_mount},
    {"unavailable native RTC permits storage loading and safe handoff", test_mount_without_clock},
    {"failed RTC shutdown blocks SD handoff without retry", test_rtc_shutdown_failure},
    {"mount and nonzero volume failures are terminal", test_mount_failure},
    {"mount retains sector I/O failure", test_mount_io_failure},
    {"sector chunking and sync", test_sector_chunks},
    {"sector bounds reject without I/O", test_sector_arguments},
    {"sector buffers respect installed RDRAM", test_sector_rdram_bounds},
    {"storage failure invalidates subsequent I/O", test_storage_latch},
    {"unsupported context is nonfatal and performs no I/O", test_unsupported_context},
    {"release unmounts before MCU FS INIT", test_release},
    {"release failures refuse handoff", test_release_refusals},
    {NULL, NULL},
};
