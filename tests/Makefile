CC ?= cc
HOST_BUILD_DIR ?= build/tests
RTC_STUB_SOURCE := tests/stubs/rtc.c
FATFS_UNICODE_SOURCE := libdragon/src/fatfs/ffunicode.c
FATFS_UNICODE_DEPS := $(FATFS_UNICODE_SOURCE) libdragon/src/fatfs/ff.h libdragon/src/fatfs/ffconf.h
HOST_FLAGS := -std=gnu11 -Wall -Wextra -Werror -g -Itests/stubs -Isrc -isystem src/libs/acutest
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

include tests/ed64_pro/host.mk

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

$(ROM_INFO_TEST): $(ROM_INFO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(ROM_INFO_FLAGS) -O2 tests/rom_info_test.c $(LDFLAGS) $(ROM_INFO_LINK_FLAGS) -o $@

$(ROM_INFO_SANITIZE): $(ROM_INFO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(ROM_INFO_FLAGS) -O1 $(SANITIZE_FLAGS) tests/rom_info_test.c $(LDFLAGS) $(ROM_INFO_LINK_FLAGS) -o $@
