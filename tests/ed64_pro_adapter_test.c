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

static void test_firmware_info (void) {
    fixture();
    TEST_CHECK(pro_init() == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED);
    TEST_CHECK(pro_get_firmware_version().major == 26);
    TEST_CHECK(pro_get_firmware_version().minor == 1014);
    TEST_CHECK(pro_get_firmware_version().revision == 0);
    TEST_CHECK(pro_diagnostics.rom_capacity == 0x0F000000u);
    TEST_CHECK(strcmp(pro_get_name(), "EverDrive-64 Pro") == 0);
    TEST_CHECK(pro_init() == FLASHCART_ERR_FUNCTION_NOT_SUPPORTED && mock.info_calls == 1);
}

static void test_firmware_boundaries (void) {
    static const struct {
        unsigned year, month, day;
        flashcart_err_t result;
    } cases[] = {
        {2026, 10, 13, FLASHCART_ERR_OUTDATED},
        {2026, 10, 14, FLASHCART_ERR_FUNCTION_NOT_SUPPORTED},
        {2027, 1, 1, FLASHCART_ERR_FUNCTION_NOT_SUPPORTED},
        {2028, 2, 29, FLASHCART_ERR_FUNCTION_NOT_SUPPORTED},
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
        TEST_CHECK(pro_get_error_message(cases[i].result) != NULL);
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

TEST_LIST = {
    {"cached firmware identity and capacity", test_firmware_info},
    {"firmware date and minimum boundaries", test_firmware_boundaries},
    {"firmware query failure is latched", test_firmware_query_failure},
    {NULL, NULL},
};
