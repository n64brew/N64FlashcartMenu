#include <acutest.h>
#include <string.h>

#include "../src/flashcart/ed64_pro/ed64_pro.c"

/* Compare the adapter's mapping with the table used by actual shared save allocation. */
#undef ALIGN
#undef MIN
#undef MAX
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../src/flashcart/flashcart.c"
#pragma GCC diagnostic pop

int cart_type;

/* These legacy dispatch paths must stay unused by the Pro adapter tests. */
bool sys_bbplayer (void) {
    TEST_CHECK(false);
    return false;
}
int bbfs_init (void) {
    TEST_CHECK(false);
    return -1;
}
bool daisydrive_detected (void) {
    TEST_CHECK(false);
    return false;
}
bool daisydrive_init_sdfs (const char *prefix, int max_files) {
    (void)prefix;
    (void)max_files;
    TEST_CHECK(false);
    return false;
}
bool debug_init_sdfs (const char *prefix, int npart) {
    (void)prefix;
    (void)npart;
    TEST_CHECK(false);
    return false;
}
bool debug_init_isviewer (void) {
    TEST_CHECK(false);
    return false;
}
bool debug_init_usblog (void) {
    TEST_CHECK(false);
    return false;
}
flashcart_t *d64_get_flashcart (void) {
    TEST_CHECK(false);
    return NULL;
}
flashcart_t *ed64_xseries_get_flashcart (void) {
    TEST_CHECK(false);
    return NULL;
}
flashcart_t *ed64_vseries_get_flashcart (void) {
    TEST_CHECK(false);
    return NULL;
}
flashcart_t *sc64_get_flashcart (void) {
    TEST_CHECK(false);
    return NULL;
}
flashcart_t *daisydrive_get_flashcart (void) {
    TEST_CHECK(false);
    return NULL;
}
bool file_exists (char *path) {
    (void)path;
    TEST_CHECK(false);
    return false;
}
int64_t file_get_size (char *path) {
    (void)path;
    TEST_CHECK(false);
    return -1;
}
bool file_allocate (char *path, size_t size) {
    (void)path;
    (void)size;
    TEST_CHECK(false);
    return true;
}
bool file_fill (char *path, uint8_t value) {
    (void)path;
    (void)value;
    TEST_CHECK(false);
    return true;
}

static struct {
    uint32_t now;
    FATFS fs;
    FSIZE_t file_size;
    unsigned opens, file_reads, closes, seeks;
    FRESULT open_result, read_result, close_result, seek_result;
    UINT short_read;
    uintptr_t destinations[8];
    UINT read_sizes[8];
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
    uint32_t fci[8];
    uint16_t flags;
    unsigned flag_calls;
    unsigned fail_flag_call;
    float progress[8];
    unsigned progress_calls;
    unsigned command_calls;
    unsigned fail_command;
    ed64_pro_dev_cfg_t config;
    char launch_paths[2][ED64_PRO_PATH_MAX + 1];
    uint32_t info[2];
    unsigned info_calls;
    unsigned begin_calls;
    unsigned end_calls;
    unsigned reads;
    unsigned writes;
    uint32_t lbas[8];
    uint32_t counts[8];
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
    const fat_disk_t *disk;
    char events[64];
    unsigned events_count;
} mock;

uint32_t mock_ticks_read (void) {
    return mock.now++;
}

uint32_t ed64_pro_ll_limit_deadline (uint32_t deadline, uint32_t milliseconds) {
    (void)milliseconds;
    return deadline;
}

ed64_pro_probe_t ed64_pro_ll_probe (uint32_t deadline) {
    (void)deadline;
    return (ed64_pro_probe_t){.outcome = ED64_PRO_PROBE_IDENTIFIED, .id = 0xED640027u, .id_valid = true};
}

const ed64_pro_ll_failure_t *ed64_pro_ll_get_failure (void) {
    return &mock.failure;
}

ed64_pro_ll_error_t ed64_pro_ll_begin (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    transaction->deadline = deadline;
    mock.begin_calls++;
    return mock.begin_error;
}

ed64_pro_ll_error_t ed64_pro_ll_end (ed64_pro_ll_transaction_t *transaction) {
    (void)transaction;
    mock.end_calls++;
    return ED64_PRO_LL_OK;
}

ed64_pro_ll_error_t ed64_pro_ll_drain (ed64_pro_ll_transaction_t *transaction, size_t *drained, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    *drained = 0;
    return ED64_PRO_LL_OK;
}

ed64_pro_ll_error_t
ed64_pro_ll_read_status (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    *status = (ed64_pro_status_t){.key = 0x5A};
    return ED64_PRO_LL_OK;
}

ed64_pro_ll_error_t
ed64_pro_ll_check_status (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline) {
    return ed64_pro_ll_read_status(transaction, status, deadline);
}

ed64_pro_ll_error_t ed64_pro_ll_stop (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    return ED64_PRO_LL_OK;
}

ed64_pro_ll_error_t
ed64_pro_ll_init_sd (ed64_pro_ll_transaction_t *transaction, ed64_pro_status_t *status, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    mock.fs_inits++;
    mock.events[mock.events_count++] = 'F';
    *status = (ed64_pro_status_t){.key = 0x5A};
    return mock.init_error;
}

ed64_pro_ll_error_t ed64_pro_ll_get_sys_info (
    ed64_pro_ll_transaction_t *transaction,
    const uint32_t *ids,
    uint32_t *values,
    size_t count,
    uint32_t deadline
) {
    (void)transaction;
    (void)deadline;
    TEST_CHECK(count == 2 && ids[0] == ED64_PRO_INFS_TS_FW && ids[1] == ED64_PRO_INFS_MAX_ROM_SIZE);
    mock.info_calls++;
    memcpy(values, mock.info, sizeof(mock.info));
    return mock.info_error;
}

static ed64_pro_ll_error_t sector_transfer (uint32_t lba, uint32_t count, bool write) {
    TEST_ASSERT(mock.operations < 8);
    mock.lbas[mock.operations] = lba;
    mock.counts[mock.operations++] = count;
    if (write) {
        mock.writes++;
    } else {
        mock.reads++;
    }
    return mock.operations == mock.fail_operation ? mock.io_error : ED64_PRO_LL_OK;
}

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

FRESULT f_open (FIL *file, const char *path, uint8_t mode) {
    TEST_CHECK(mode == FA_READ);
    mock.opens++;
    snprintf(mock.opened_path, sizeof(mock.opened_path), "%s", path);
    file->obj.fs = &mock.fs;
    file->obj.objsize = mock.file_size;
    file->fptr = 0;
    return mock.open_result;
}
FRESULT f_read (FIL *file, void *buffer, UINT bytes, UINT *read) {
    TEST_ASSERT(mock.file_reads < 8);
    mock.destinations[mock.file_reads] = (uintptr_t)buffer;
    mock.read_sizes[mock.file_reads++] = bytes;
    *read = 0;
    if (mock.read_result != FR_OK) {
        return mock.read_result;
    }
    if (pro_disk_read(buffer, 1000 + file->fptr / 512, bytes / 512) != RES_OK) {
        return FR_DISK_ERR;
    }
    *read = mock.short_read ? bytes - mock.short_read : bytes;
    file->fptr += *read;
    return FR_OK;
}
FRESULT f_lseek (FIL *file, FSIZE_t offset) {
    mock.seeks++;
    file->fptr = offset;
    return mock.seek_result;
}
FRESULT f_close (FIL *file) {
    (void)file;
    mock.closes++;
    return mock.close_result;
}
void fatfs_fix_file_size (FIL *file) {
    file->obj.objsize = (file->obj.objsize + 511) & ~UINT64_C(511);
}
char *strip_fs_prefix (char *path) {
    char *prefix = strstr(path, ":/");
    return prefix ? prefix + 1 : path;
}

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

ed64_pro_ll_error_t ed64_pro_ll_dev_set_path (
    ed64_pro_ll_transaction_t *transaction,
    ed64_pro_path_t type,
    const char *path,
    uint32_t deadline
) {
    (void)transaction;
    (void)deadline;
    unsigned index = type == ED64_PRO_PATH_GPAK ? 0 : 1;
    snprintf(mock.launch_paths[index], sizeof(mock.launch_paths[index]), "%s", path);
    return launch_command(index == 0 ? 'P' : 'B');
}
ed64_pro_ll_error_t
ed64_pro_ll_dev_set_cfg (ed64_pro_ll_transaction_t *transaction, const ed64_pro_dev_cfg_t *config, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    mock.config = *config;
    return launch_command('G');
}
ed64_pro_ll_error_t ed64_pro_ll_dev_start (ed64_pro_ll_transaction_t *transaction, uint32_t deadline) {
    (void)transaction;
    (void)deadline;
    return launch_command('S');
}

static void progress_callback (float progress) {
    TEST_ASSERT(mock.progress_calls < 8);
    mock.progress[mock.progress_calls++] = progress;
}

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

int fat_unmount (int volume_id) {
    TEST_CHECK(volume_id == mock.mount_result);
    mock.unmounts++;
    mock.events[mock.events_count++] = 'U';
    return mock.unmount_result;
}

static uint32_t fat_date (unsigned year, unsigned month, unsigned day) {
    return ((year - 1980) << 9) | (month << 5) | day;
}

static void fixture (void) {
    memset(&mock, 0, sizeof(mock));
    memset(&pro_diagnostics, 0, sizeof(pro_diagnostics));
    pro_diagnostics.state = ED64_PRO_READY;
    mock.info[0] = 0x90555D4Eu;
    mock.info[1] = 0x10000000u;
    mock.io_error = ED64_PRO_LL_TIMEOUT;
    mock.file_size = 1024;
    cart_card_byteswap = 0;
}

static void mounted_fixture (void) {
    fixture();
    TEST_ASSERT(pro_init() == FLASHCART_OK);
    TEST_ASSERT(ed64_pro_init_sdfs("sd:/", 4));
    mock.operations = 0;
    mock.reads = 0;
    mock.begin_calls = 0;
}

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

static void test_firmware_query_failure (void) {
    fixture();
    mock.info_error = ED64_PRO_LL_TIMEOUT;
    TEST_CHECK(pro_init() == FLASHCART_ERR_INT);
    TEST_CHECK(pro_diagnostics.phase == ED64_PRO_PHASE_FIRMWARE_INFO);
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "Firmware info") != NULL);
    TEST_CHECK(pro_init() == FLASHCART_ERR_INT && mock.info_calls == 1);
}

static void test_mount (void) {
    mounted_fixture();
    TEST_CHECK(mock.mounts == 1 && mock.disk == &pro_disk);
    TEST_CHECK(pro_diagnostics.storage_state == ED64_PRO_STORAGE_FATFS_OWNED);
    TEST_CHECK(!ed64_pro_init_sdfs("sd:/", 4) && mock.mounts == 1);
    TEST_CHECK(pro_disk.disk_status() == 0);
}

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

static void test_mount_io_failure (void) {
    fixture();
    TEST_ASSERT(pro_init() == FLASHCART_OK);
    mock.fail_operation = 1;
    TEST_CHECK(!ed64_pro_init_sdfs("sd:/", 4));
    TEST_CHECK(pro_diagnostics.storage_failed);
    TEST_CHECK(pro_diagnostics.storage_failure.code == ED64_PRO_LL_TIMEOUT);
    TEST_CHECK(mock.fs_inits == 0);
}

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

static void test_unsupported_context (void) {
    mounted_fixture();
    uint8_t data[512];
    mock.begin_error = ED64_PRO_LL_CONTEXT;
    TEST_CHECK(pro_disk.disk_read(data, 0, 1) == RES_NOTRDY);
    TEST_CHECK(mock.operations == 0 && !pro_diagnostics.storage_failed && pro_disk.disk_status() == 0);
}

static void test_release (void) {
    mounted_fixture();
    TEST_CHECK(pro_release_storage() == FLASHCART_OK);
    TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 1);
    TEST_CHECK(mock.events[0] == 'U' && mock.events[1] == 'F');
    TEST_CHECK(pro_diagnostics.storage_state == ED64_PRO_STORAGE_MCU_OWNED);
    TEST_CHECK(pro_disk.disk_status() == STA_NOINIT);
}

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

static void test_game_handoff (void) {
    for (unsigned save = 0; save < 2; save++) {
        staged_game_fixture(save);
        TEST_CHECK(pro_deinit() == FLASHCART_OK);
        TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 1);
        TEST_CHECK(mock.command_calls == (save ? 4u : 3u));
        TEST_CHECK(strcmp(mock.events, save ? "UFPBGS" : "UFPGS") == 0);
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

static void test_launch_preflight (void) {
    mounted_fixture();
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_LOAD);
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

static void test_unicode_auxiliary_uses_fatfs_only (void) {
    mounted_fixture();
    TEST_ASSERT(pro_load_rom("/emulator.z64", NULL) == FLASHCART_OK);
    mock.operations = 0;
    mock.file_reads = 0;
    TEST_CHECK(pro_load_file("sd:/日本語/雪.gb", 0x80000, 0) == FLASHCART_OK);
    TEST_CHECK(strcmp(mock.opened_path, "/日本語/雪.gb") == 0 && mock.stats == 0);
    TEST_CHECK(strcmp(pro_diagnostics.rom_path, "/emulator.z64") == 0);
}

TEST_LIST = {
    {"staged paths use exact CP437 while FatFs keeps UTF-8", test_cp437_paths},
    {"unmappable components use verified UTF-8 short aliases", test_unicode_short_aliases},
    {"malformed UTF-8 rejects before metadata or data I/O", test_invalid_utf8_paths},
    {"malformed save paths clear staged writeback", test_invalid_utf8_save_path},
    {"missing exFAT or invalid short aliases block handoff", test_missing_or_invalid_alias},
    {"encoded path bounds include alias expansion", test_encoded_path_bounds},
    {"path metadata requires mounted FatFs ownership", test_path_metadata_requires_ownership},
    {"Unicode auxiliary reads stay entirely in FatFs", test_unicode_auxiliary_uses_fatfs_only},
    {"game handoff command order and exact configuration", test_game_handoff},
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
    {"immediate mount invokes disk callbacks", test_mount},
    {"mount and nonzero volume failures are terminal", test_mount_failure},
    {"mount retains sector I/O failure", test_mount_io_failure},
    {"sector chunking and sync", test_sector_chunks},
    {"sector bounds reject without I/O", test_sector_arguments},
    {"storage failure invalidates subsequent I/O", test_storage_latch},
    {"unsupported context is nonfatal and performs no I/O", test_unsupported_context},
    {"release unmounts before MCU FS INIT", test_release},
    {"release failures refuse handoff", test_release_refusals},
    {NULL, NULL},
};
