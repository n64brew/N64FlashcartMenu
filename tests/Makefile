.DEFAULT_GOAL := test

CC ?= cc
HOST_BUILD_DIR ?= build/tests
RTC_STUB_SOURCE := tests/stubs/rtc.c
FATFS_UNICODE_SOURCE := libdragon/src/fatfs/ffunicode.c
FATFS_UNICODE_DEPS := $(FATFS_UNICODE_SOURCE) libdragon/src/fatfs/ff.h libdragon/src/fatfs/ffconf.h
HOST_FLAGS := -std=gnu11 -Wall -Wextra -Werror -g -Itests/stubs -Isrc -isystem src/libs/acutest
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test test-sanitize
test: test-ed64-pro
test-sanitize: test-ed64-pro-sanitize

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

DIAGNOSTICS_TEST := $(HOST_BUILD_DIR)/flashcart_diagnostics_test
DIAGNOSTICS_SANITIZE := $(HOST_BUILD_DIR)/flashcart_diagnostics_test_sanitize
DIAGNOSTICS_SOURCE := tests/flashcart_diagnostics_test.c
DIAGNOSTICS_DEPS := $(DIAGNOSTICS_SOURCE) src/flashcart/flashcart.c src/flashcart/flashcart.h \
	src/menu/views/flashcart_info.c src/menu/views/startup.c src/menu/views/views.h \
	src/menu/menu_state.h src/menu/ui_components.h src/menu/ui_components/constants.h \
	src/menu/path.c src/menu/path.h src/menu/settings.h tests/stubs/libdragon.h tests/Makefile

.PHONY: test-flashcart-diagnostics test-flashcart-diagnostics-sanitize
test-flashcart-diagnostics: $(DIAGNOSTICS_TEST)
	$(DIAGNOSTICS_TEST)

test-flashcart-diagnostics-sanitize: $(DIAGNOSTICS_SANITIZE)
	$(DIAGNOSTICS_SANITIZE)

$(DIAGNOSTICS_TEST): $(DIAGNOSTICS_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(ROM_INFO_FLAGS) -O2 $(DIAGNOSTICS_SOURCE) $(LDFLAGS) $(ROM_INFO_LINK_FLAGS) -o $@

$(DIAGNOSTICS_SANITIZE): $(DIAGNOSTICS_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(ROM_INFO_FLAGS) -O1 $(SANITIZE_FLAGS) $(DIAGNOSTICS_SOURCE) $(LDFLAGS) $(ROM_INFO_LINK_FLAGS) -o $@

test-ed64-pro: test-flashcart-diagnostics
test-ed64-pro-sanitize: test-flashcart-diagnostics-sanitize
