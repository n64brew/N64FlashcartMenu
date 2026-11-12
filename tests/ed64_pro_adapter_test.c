#include <acutest.h>
#include <string.h>

#include "../src/flashcart/ed64_pro/ed64_pro.c"

static struct {
    uint32_t now;
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
    char events[32];
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
    TEST_CHECK(pro_disk.disk_read((void *)0x10000000u, 0, 1) == RES_PARERR);
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
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_SD_CARD);
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
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED);
    TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 1);
    TEST_CHECK(mock.events[0] == 'U' && mock.events[1] == 'F');
    TEST_CHECK(pro_diagnostics.storage_state == ED64_PRO_STORAGE_MCU_OWNED);
    TEST_CHECK(pro_disk.disk_status() == STA_NOINIT);
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "storage released") != NULL);
}

static void test_release_refusals (void) {
    mounted_fixture();
    uint8_t data[512] = {0};
    TEST_CHECK(pro_disk.disk_write(data, 2, 1) == RES_OK);
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_SD_CARD);
    TEST_CHECK(mock.unmounts == 0 && mock.fs_inits == 0);
    TEST_CHECK(strstr(ed64_pro_get_error_message(), "unclosed write") != NULL);

    mounted_fixture();
    mock.unmount_result = -1;
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_SD_CARD);
    TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 0);
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_SD_CARD && mock.unmounts == 1);

    mounted_fixture();
    mock.init_error = ED64_PRO_LL_MCU_ERROR;
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_INT);
    TEST_CHECK(mock.unmounts == 1 && mock.fs_inits == 1);
    TEST_CHECK(pro_deinit() == FLASHCART_ERR_SD_CARD && mock.fs_inits == 1);
}

TEST_LIST = {
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
