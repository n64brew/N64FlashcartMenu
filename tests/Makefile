CC ?= cc
HOST_BUILD_DIR ?= build/tests
PRO_TEST := $(HOST_BUILD_DIR)/ed64_pro_transport_test
PRO_TEST_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_transport_test_sanitize
PRO_WRITE_FAILURE_TEST := $(HOST_BUILD_DIR)/ed64_pro_write_failure_test
PRO_WRITE_FAILURE_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_write_failure_test_sanitize
PRO_WRITE_FAILURE_CASE := injected nth write fails after first ACK block
PRO_SOURCE := tests/ed64_pro_transport_test.c
PRO_RTC_SOURCE := src/flashcart/ed64_pro/ed64_pro_rtc.c
RTC_STUB_SOURCE := tests/stubs/rtc.c
FATFS_UNICODE_SOURCE := libdragon/src/fatfs/ffunicode.c
FATFS_UNICODE_DEPS := $(FATFS_UNICODE_SOURCE) libdragon/src/fatfs/ff.h libdragon/src/fatfs/ffconf.h
PRO_DEPS := $(PRO_SOURCE) $(PRO_RTC_SOURCE) src/flashcart/ed64_pro/ed64_pro_rtc.h $(RTC_STUB_SOURCE) tests/stubs/rtc.h tests/stubs/system.h $(FATFS_UNICODE_DEPS) tests/stubs/libdragon.h tests/stubs/fatfs/ff.h tests/stubs/fatfs/diskio.h \
	tests/stubs/libcart/cart.h src/utils/fs.h src/flashcart/flashcart_utils.h \
	src/flashcart/ed64_pro/ed64_pro_ll.c src/flashcart/ed64_pro/ed64_pro_ll.h \
	src/flashcart/ed64_pro/ed64_pro.c src/flashcart/ed64_pro/ed64_pro.h src/flashcart/flashcart.h \
	src/libs/acutest/acutest.h tests/Makefile
LIFECYCLE_TEST := $(HOST_BUILD_DIR)/ed64_pro_lifecycle_test
LIFECYCLE_RELEASE := $(HOST_BUILD_DIR)/ed64_pro_lifecycle_test_release
LIFECYCLE_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_lifecycle_test_sanitize
LIFECYCLE_RELEASE_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_lifecycle_test_release_sanitize
LIFECYCLE_SOURCE := tests/ed64_pro_lifecycle_test.c
LIFECYCLE_DEPS := $(LIFECYCLE_SOURCE) $(RTC_STUB_SOURCE) tests/stubs/rtc.h tests/stubs/system.h tests/stubs/libdragon.h tests/stubs/libcart/cart.h \
	tests/stubs/usb.h tests/stubs/fatfs/ff.h src/flashcart/flashcart.c src/flashcart/flashcart.h \
	src/flashcart/flashcart_utils.h src/flashcart/ed64_pro/ed64_pro.h src/flashcart/ed64_pro/ed64_pro_ll.h \
	src/flashcart/64drive/64drive.h src/flashcart/ed64/ed64_vseries.h src/flashcart/ed64/ed64_xseries.h \
	src/flashcart/sc64/sc64.h src/flashcart/daisydrive/daisydrive.h src/utils/fs.h src/utils/utils.h \
	src/libs/acutest/acutest.h tests/Makefile
ADAPTER_TEST := $(HOST_BUILD_DIR)/ed64_pro_adapter_test
ADAPTER_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_adapter_test_sanitize
ADAPTER_DEPS := tests/ed64_pro_adapter_test.c $(PRO_DEPS) $(LIFECYCLE_DEPS)
HOST_FLAGS := -std=gnu11 -Wall -Wextra -Werror -g -Itests/stubs -Isrc -isystem src/libs/acutest
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test-ed64-pro test-ed64-pro-sanitize
test-ed64-pro: $(PRO_TEST) $(LIFECYCLE_TEST) $(LIFECYCLE_RELEASE) $(ADAPTER_TEST) $(PRO_WRITE_FAILURE_TEST)
	$(PRO_TEST)
	$(LIFECYCLE_TEST)
	$(LIFECYCLE_RELEASE)
	$(ADAPTER_TEST)
	$(PRO_WRITE_FAILURE_TEST) "$(PRO_WRITE_FAILURE_CASE)"

test-ed64-pro-sanitize: $(PRO_TEST_SANITIZE) $(LIFECYCLE_SANITIZE) $(LIFECYCLE_RELEASE_SANITIZE) $(ADAPTER_SANITIZE) $(PRO_WRITE_FAILURE_SANITIZE)
	$(PRO_TEST_SANITIZE)
	$(LIFECYCLE_SANITIZE)
	$(LIFECYCLE_RELEASE_SANITIZE)
	$(ADAPTER_SANITIZE)
	$(PRO_WRITE_FAILURE_SANITIZE) "$(PRO_WRITE_FAILURE_CASE)"

$(PRO_TEST): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 $(PRO_SOURCE) $(PRO_RTC_SOURCE) $(RTC_STUB_SOURCE) $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

$(PRO_TEST_SANITIZE): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) $(PRO_SOURCE) $(PRO_RTC_SOURCE) $(RTC_STUB_SOURCE) $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

$(PRO_WRITE_FAILURE_TEST): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -DED64_PRO_TEST_FAIL_WRITE_N=2 -O2 $(PRO_SOURCE) $(PRO_RTC_SOURCE) $(RTC_STUB_SOURCE) $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

$(PRO_WRITE_FAILURE_SANITIZE): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -DED64_PRO_TEST_FAIL_WRITE_N=2 -O1 $(SANITIZE_FLAGS) $(PRO_SOURCE) $(PRO_RTC_SOURCE) $(RTC_STUB_SOURCE) $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

$(LIFECYCLE_RELEASE) $(LIFECYCLE_RELEASE_SANITIZE): HOST_MODE_FLAGS=-DNDEBUG

$(LIFECYCLE_TEST) $(LIFECYCLE_RELEASE): $(LIFECYCLE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(HOST_MODE_FLAGS) -O2 $(LIFECYCLE_SOURCE) $(RTC_STUB_SOURCE) $(LDFLAGS) -o $@

$(LIFECYCLE_SANITIZE) $(LIFECYCLE_RELEASE_SANITIZE): $(LIFECYCLE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(HOST_MODE_FLAGS) -O1 $(SANITIZE_FLAGS) $(LIFECYCLE_SOURCE) $(RTC_STUB_SOURCE) $(LDFLAGS) -o $@

$(ADAPTER_TEST): $(ADAPTER_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 tests/ed64_pro_adapter_test.c $(PRO_RTC_SOURCE) $(RTC_STUB_SOURCE) $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

$(ADAPTER_SANITIZE): $(ADAPTER_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) tests/ed64_pro_adapter_test.c $(PRO_RTC_SOURCE) $(RTC_STUB_SOURCE) $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

ROM_INFO_TEST := $(HOST_BUILD_DIR)/rom_info_test
ROM_INFO_SANITIZE := $(HOST_BUILD_DIR)/rom_info_test_sanitize
ROM_INFO_DEPS := tests/rom_info_test.c src/menu/rom_info.c src/menu/rom_info.h \
	src/menu/ini_parser.c src/menu/ini_parser.h src/menu/path.c src/menu/path.h \
	src/boot/cic.c src/boot/cic.h tests/stubs/libdragon.h src/libs/acutest/acutest.h \
	$(wildcard src/libs/miniz/*.h) src/libs/miniz_export.h tests/Makefile
ROM_INFO_FLAGS := -ffunction-sections -fdata-sections -Isrc/libs -Isrc/libs/miniz
ifeq ($(shell uname -s),Darwin)
ROM_INFO_LINK_FLAGS := -Wl,-dead_strip
else
ROM_INFO_LINK_FLAGS := -Wl,--gc-sections
endif

.PHONY: test-rom-info test-rom-info-sanitize
test-rom-info: $(ROM_INFO_TEST)
	$(ROM_INFO_TEST)

test-rom-info-sanitize: $(ROM_INFO_SANITIZE)
	$(ROM_INFO_SANITIZE)

test-ed64-pro: test-rom-info
test-ed64-pro-sanitize: test-rom-info-sanitize

$(ROM_INFO_TEST): $(ROM_INFO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(ROM_INFO_FLAGS) -O2 tests/rom_info_test.c $(LDFLAGS) $(ROM_INFO_LINK_FLAGS) -o $@

$(ROM_INFO_SANITIZE): $(ROM_INFO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(ROM_INFO_FLAGS) -O1 $(SANITIZE_FLAGS) tests/rom_info_test.c $(LDFLAGS) $(ROM_INFO_LINK_FLAGS) -o $@

RTC_TEST := $(HOST_BUILD_DIR)/rtc_test
RTC_SANITIZE := $(HOST_BUILD_DIR)/rtc_test_sanitize
RTC_DEPS := tests/rtc_test.c src/flashcart/ed64_pro/ed64_pro_rtc.c \
	src/flashcart/ed64_pro/ed64_pro_rtc.h src/flashcart/ed64_pro/ed64_pro.h \
	src/flashcart/ed64_pro/ed64_pro_ll.h src/flashcart/flashcart.h \
	tests/stubs/libdragon.h tests/stubs/system.h src/libs/acutest/acutest.h tests/Makefile

.PHONY: test-rtc test-rtc-sanitize
test-rtc: $(RTC_TEST)
	$(RTC_TEST)

test-rtc-sanitize: $(RTC_SANITIZE)
	$(RTC_SANITIZE)

test-ed64-pro: test-rtc
test-ed64-pro-sanitize: test-rtc-sanitize

$(RTC_TEST): $(RTC_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 tests/rtc_test.c $(LDFLAGS) -o $@

$(RTC_SANITIZE): $(RTC_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) tests/rtc_test.c $(LDFLAGS) -o $@

RTC_VIEW_TEST := $(HOST_BUILD_DIR)/rtc_view_test
RTC_VIEW_SANITIZE := $(HOST_BUILD_DIR)/rtc_view_test_sanitize
RTC_VIEW_DEPS := tests/rtc_view_test.c src/menu/views/rtc.c src/menu/views/views.h \
	src/menu/sound.h src/menu/ui_components/constants.h src/utils/utils.h \
	src/flashcart/flashcart.h tests/stubs/libdragon.h tests/stubs/rtc.h \
	src/libs/acutest/acutest.h tests/Makefile

.PHONY: test-rtc-view test-rtc-view-sanitize
test-rtc-view: $(RTC_VIEW_TEST)
	$(RTC_VIEW_TEST)

test-rtc-view-sanitize: $(RTC_VIEW_SANITIZE)
	$(RTC_VIEW_SANITIZE)

test-ed64-pro: test-rtc-view
test-ed64-pro-sanitize: test-rtc-view-sanitize

$(RTC_VIEW_TEST): $(RTC_VIEW_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 tests/rtc_view_test.c $(LDFLAGS) -o $@

$(RTC_VIEW_SANITIZE): $(RTC_VIEW_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) tests/rtc_view_test.c $(LDFLAGS) -o $@
