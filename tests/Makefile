CC ?= cc
HOST_BUILD_DIR ?= build/tests
PRO_TEST := $(HOST_BUILD_DIR)/ed64_pro_transport_test
PRO_TEST_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_transport_test_sanitize
PRO_WRITE_FAILURE_TEST := $(HOST_BUILD_DIR)/ed64_pro_write_failure_test
PRO_WRITE_FAILURE_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_write_failure_test_sanitize
PRO_WRITE_FAILURE_CASE := injected nth write fails after first ACK block
PRO_SOURCE := tests/ed64_pro_transport_test.c
FATFS_UNICODE_SOURCE := libdragon/src/fatfs/ffunicode.c
FATFS_UNICODE_DEPS := $(FATFS_UNICODE_SOURCE) libdragon/src/fatfs/ff.h libdragon/src/fatfs/ffconf.h
PRO_DEPS := $(PRO_SOURCE) $(FATFS_UNICODE_DEPS) tests/stubs/libdragon.h tests/stubs/fatfs/ff.h tests/stubs/fatfs/diskio.h \
	tests/stubs/libcart/cart.h src/utils/fs.h src/flashcart/flashcart_utils.h \
	src/flashcart/ed64_pro/ed64_pro_ll.c src/flashcart/ed64_pro/ed64_pro_ll.h \
	src/flashcart/ed64_pro/ed64_pro.c src/flashcart/ed64_pro/ed64_pro.h src/flashcart/flashcart.h \
	src/libs/acutest/acutest.h tests/Makefile
LIFECYCLE_TEST := $(HOST_BUILD_DIR)/ed64_pro_lifecycle_test
LIFECYCLE_RELEASE := $(HOST_BUILD_DIR)/ed64_pro_lifecycle_test_release
LIFECYCLE_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_lifecycle_test_sanitize
LIFECYCLE_RELEASE_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_lifecycle_test_release_sanitize
LIFECYCLE_SOURCE := tests/ed64_pro_lifecycle_test.c
LIFECYCLE_DEPS := $(LIFECYCLE_SOURCE) tests/stubs/libdragon.h tests/stubs/libcart/cart.h \
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
MENU_PATH_TEST := $(HOST_BUILD_DIR)/menu_path_test
MENU_PATH_SANITIZE := $(HOST_BUILD_DIR)/menu_path_test_sanitize
MENU_PATH_DEPS := tests/menu_path_test.c src/menu/path.c src/menu/path.h src/libs/acutest/acutest.h tests/Makefile

.PHONY: test-ed64-pro test-ed64-pro-sanitize
test-ed64-pro: test-cic $(PRO_TEST) $(LIFECYCLE_TEST) $(LIFECYCLE_RELEASE) $(ADAPTER_TEST) $(PRO_WRITE_FAILURE_TEST)
	$(PRO_TEST)
	$(LIFECYCLE_TEST)
	$(LIFECYCLE_RELEASE)
	$(ADAPTER_TEST)
	$(PRO_WRITE_FAILURE_TEST) "$(PRO_WRITE_FAILURE_CASE)"

test-ed64-pro-sanitize: test-cic-sanitize $(PRO_TEST_SANITIZE) $(LIFECYCLE_SANITIZE) $(LIFECYCLE_RELEASE_SANITIZE) $(ADAPTER_SANITIZE) $(PRO_WRITE_FAILURE_SANITIZE)
	$(PRO_TEST_SANITIZE)
	$(LIFECYCLE_SANITIZE)
	$(LIFECYCLE_RELEASE_SANITIZE)
	$(ADAPTER_SANITIZE)
	$(PRO_WRITE_FAILURE_SANITIZE) "$(PRO_WRITE_FAILURE_CASE)"

$(PRO_TEST): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 $(PRO_SOURCE) $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

$(PRO_TEST_SANITIZE): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) $(PRO_SOURCE) $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

$(PRO_WRITE_FAILURE_TEST): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -DED64_PRO_TEST_FAIL_WRITE_N=2 -O2 $(PRO_SOURCE) $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

$(PRO_WRITE_FAILURE_SANITIZE): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -DED64_PRO_TEST_FAIL_WRITE_N=2 -O1 $(SANITIZE_FLAGS) $(PRO_SOURCE) $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

$(LIFECYCLE_RELEASE) $(LIFECYCLE_RELEASE_SANITIZE): HOST_MODE_FLAGS=-DNDEBUG

$(LIFECYCLE_TEST) $(LIFECYCLE_RELEASE): $(LIFECYCLE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(HOST_MODE_FLAGS) -O2 $(LIFECYCLE_SOURCE) $(LDFLAGS) -o $@

$(LIFECYCLE_SANITIZE) $(LIFECYCLE_RELEASE_SANITIZE): $(LIFECYCLE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(HOST_MODE_FLAGS) -O1 $(SANITIZE_FLAGS) $(LIFECYCLE_SOURCE) $(LDFLAGS) -o $@

$(ADAPTER_TEST): $(ADAPTER_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 tests/ed64_pro_adapter_test.c $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

$(ADAPTER_SANITIZE): $(ADAPTER_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) tests/ed64_pro_adapter_test.c $(FATFS_UNICODE_SOURCE) $(LDFLAGS) -o $@

.PHONY: test-menu-path test-menu-path-sanitize
test-menu-path: $(MENU_PATH_TEST)
	$(MENU_PATH_TEST)

test-menu-path-sanitize: $(MENU_PATH_SANITIZE)
	$(MENU_PATH_SANITIZE)

$(MENU_PATH_TEST): $(MENU_PATH_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 tests/menu_path_test.c $(LDFLAGS) -o $@

$(MENU_PATH_SANITIZE): $(MENU_PATH_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) tests/menu_path_test.c $(LDFLAGS) -o $@

CIC_TEST := $(HOST_BUILD_DIR)/cic_test
CIC_SANITIZE := $(HOST_BUILD_DIR)/cic_test_sanitize
CIC_DEPS := tests/cic_test.c src/boot/cic.c src/boot/cic.h libdragon/tools/ipl3.h \
	src/libs/acutest/acutest.h tests/Makefile

.PHONY: test-cic test-cic-sanitize
test-cic: $(CIC_TEST)
	$(CIC_TEST)

test-cic-sanitize: $(CIC_SANITIZE)
	$(CIC_SANITIZE)

$(CIC_TEST): $(CIC_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 tests/cic_test.c src/boot/cic.c $(LDFLAGS) -o $@

$(CIC_SANITIZE): $(CIC_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) tests/cic_test.c src/boot/cic.c $(LDFLAGS) -o $@
