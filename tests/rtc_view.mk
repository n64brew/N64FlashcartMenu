RTC_VIEW_TEST := $(HOST_BUILD_DIR)/rtc_view_test
RTC_VIEW_SANITIZE := $(HOST_BUILD_DIR)/rtc_view_test_sanitize
RTC_VIEW_DEPS := tests/rtc_view_test.c src/menu/views/rtc.c src/menu/sound.h \
	src/menu/views/views.h src/menu/ui_components/constants.h src/utils/utils.h \
	src/flashcart/flashcart.h tests/stubs/libdragon.h src/libs/acutest/acutest.h \
	tests/Makefile tests/rtc_view.mk

.PHONY: test-rtc-view test-rtc-view-sanitize
test-rtc-view: $(RTC_VIEW_TEST)
	$(RTC_VIEW_TEST)

test-rtc-view-sanitize: $(RTC_VIEW_SANITIZE)
	$(RTC_VIEW_SANITIZE)

$(RTC_VIEW_TEST): $(RTC_VIEW_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O2 tests/rtc_view_test.c $(LDFLAGS) -o $@

$(RTC_VIEW_SANITIZE): $(RTC_VIEW_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HOST_FLAGS) -O1 $(SANITIZE_FLAGS) tests/rtc_view_test.c $(LDFLAGS) -o $@

# Include the shared Time pane regression in the existing CI host suite.
test-ed64-pro: test-rtc-view
test-ed64-pro-sanitize: test-rtc-view-sanitize
