CC ?= cc
HOST_BUILD_DIR := build/tests
PRO_TEST := $(HOST_BUILD_DIR)/ed64_pro_transport_test
PRO_TEST_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_transport_test_sanitize
PRO_SOURCE := tests/ed64_pro_transport_test.c
PRO_DEPS := $(PRO_SOURCE) tests/stubs/libdragon.h \
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
HOST_FLAGS := -std=gnu11 -Wall -Wextra -Werror -g -Itests/stubs -Isrc -isystem src/libs/acutest
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test-ed64-pro test-ed64-pro-sanitize
test-ed64-pro: $(PRO_TEST) $(LIFECYCLE_TEST) $(LIFECYCLE_RELEASE)
	$(PRO_TEST)
	$(LIFECYCLE_TEST)
	$(LIFECYCLE_RELEASE)

test-ed64-pro-sanitize: $(PRO_TEST_SANITIZE) $(LIFECYCLE_SANITIZE) $(LIFECYCLE_RELEASE_SANITIZE)
	$(PRO_TEST_SANITIZE)
	$(LIFECYCLE_SANITIZE)
	$(LIFECYCLE_RELEASE_SANITIZE)

$(PRO_TEST): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 $(PRO_SOURCE) $(LDFLAGS) -o $@

$(PRO_TEST_SANITIZE): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) $(PRO_SOURCE) $(LDFLAGS) -o $@

$(LIFECYCLE_RELEASE) $(LIFECYCLE_RELEASE_SANITIZE): HOST_MODE_FLAGS=-DNDEBUG

$(LIFECYCLE_TEST) $(LIFECYCLE_RELEASE): $(LIFECYCLE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(HOST_MODE_FLAGS) -O2 $(LIFECYCLE_SOURCE) $(LDFLAGS) -o $@

$(LIFECYCLE_SANITIZE) $(LIFECYCLE_RELEASE_SANITIZE): $(LIFECYCLE_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) $(HOST_MODE_FLAGS) -O1 $(SANITIZE_FLAGS) $(LIFECYCLE_SOURCE) $(LDFLAGS) -o $@
