CC ?= cc
HOST_BUILD_DIR := build/tests
PRO_TEST := $(HOST_BUILD_DIR)/ed64_pro_transport_test
PRO_TEST_SANITIZE := $(HOST_BUILD_DIR)/ed64_pro_transport_test_sanitize
PRO_SOURCE := tests/ed64_pro_transport_test.c
PRO_DEPS := $(PRO_SOURCE) tests/stubs/libdragon.h \
	src/flashcart/ed64_pro/ed64_pro_ll.c src/flashcart/ed64_pro/ed64_pro_ll.h \
	src/flashcart/ed64_pro/ed64_pro.c src/flashcart/ed64_pro/ed64_pro.h src/flashcart/flashcart.h \
	src/libs/acutest/acutest.h tests/Makefile
HOST_FLAGS := -std=gnu11 -Wall -Wextra -Werror -g -Itests/stubs -Isrc -isystem src/libs/acutest
SANITIZE_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: test-ed64-pro test-ed64-pro-sanitize
test-ed64-pro: $(PRO_TEST)
	$(PRO_TEST)

test-ed64-pro-sanitize: $(PRO_TEST_SANITIZE)
	$(PRO_TEST_SANITIZE)

$(PRO_TEST): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 $(PRO_SOURCE) $(LDFLAGS) -o $@

$(PRO_TEST_SANITIZE): $(PRO_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) $(PRO_SOURCE) $(LDFLAGS) -o $@
