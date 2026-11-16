#include <acutest.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "../src/flashcart/flashcart.h"
#include "../src/menu/sound.h"

/* Exercise the production pane with only its drawing and menu dependencies stubbed. */
#define VIEWS_H__
#define COMPONENTS_CONSTANTS_H__
#define SETTINGS_PANE_X0 192
#define SETTINGS_PANE_X1 600
#define SETTINGS_PANE_Y0 64
#define FILE_LIST_HIGHLIGHT_COLOR 0
enum { STL_DEFAULT, STL_GRAY, STL_YELLOW, ALIGN_LEFT, ALIGN_CENTER, WRAP_WORD, WRAP_NONE };

typedef struct {
    time_t current_time;
    struct { bool back, go_left, go_right, go_up, go_down, enter; } actions;
} menu_t;

typedef enum { SETTINGS_HINT_LEFT, SETTINGS_HINT_CENTER, SETTINGS_HINT_RIGHT } settings_hint_t;
typedef struct {
    const char *label;
    void (*enter)(menu_t *);
    void (*leave)(menu_t *);
    bool (*process)(menu_t *);
    void (*draw)(menu_t *, bool);
    const char *(*hint)(menu_t *, settings_hint_t);
} settings_pane_t;

static struct {
    flashcart_err_t sync_result;
    rtc_source_t source;
    rtc_range_t range;
    bool writable;
    time_t now, written, confirm_advance;
    int set_result;
    bool confirm_failed;
    unsigned syncs, writes, writable_queries, sounds;
    sound_effect_t last_sound;
    char text[32][256];
    unsigned texts;
    char weekday[8];
} mock;

flashcart_err_t flashcart_resync_rtc (void) {
    mock.syncs++;
    return mock.sync_result;
}

bool flashcart_rtc_is_writable (void) {
    mock.writable_queries++;
    return mock.writable;
}

rtc_range_t flashcart_get_rtc_range (void) {
    return mock.range;
}

char *flashcart_convert_error_message (flashcart_err_t error) {
    TEST_CHECK(error == FLASHCART_ERR_INT);
    return "Hardware clock date is invalid";
}

rtc_source_t rtc_get_source (void) {
    return mock.source;
}

void sound_play_effect (sound_effect_t effect) {
    mock.sounds++;
    mock.last_sound = effect;
}

static time_t mock_time (time_t *out) {
    if (out) {
        *out = mock.now;
    }
    return mock.now;
}

static int mock_settimeofday (const struct timeval *value, const struct timezone *zone) {
    TEST_CHECK(zone == NULL);
    mock.writes++;
    mock.written = value->tv_sec;
    if (!mock.set_result) {
        mock.now = mock.confirm_failed ? -1 : value->tv_sec + mock.confirm_advance;
    }
    return mock.set_result;
}

static void ui_components_text_draw (
    int x, int y, int width, int height, int style, int align, int wrap, const char *text
) {
    (void)x; (void)y; (void)width; (void)height; (void)style; (void)align; (void)wrap;
    TEST_ASSERT(mock.texts < 32);
    snprintf(mock.text[mock.texts++], sizeof(mock.text[0]), "%s", text);
}

static void ui_components_box_draw (int x0, int y0, int x1, int y1, int color) {
    (void)x0; (void)y0; (void)x1; (void)y1; (void)color;
}

static void ui_components_focus_draw (int x0, int y0, int x1, int y1) {
    (void)x0; (void)y0; (void)x1; (void)y1;
}

static void ui_components_settings_row_draw (int y, const char *label, const char *value, bool selected) {
    (void)y; (void)selected;
    TEST_CHECK(strcmp(label, "DoW") == 0);
    snprintf(mock.weekday, sizeof(mock.weekday), "%s", value);
}

/* macOS headers already define these before the project's utility macros. */
#undef ALIGN
#undef MIN
#undef MAX
#define time mock_time
#define settimeofday mock_settimeofday
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "../src/menu/views/rtc.c"
#pragma GCC diagnostic pop
#undef time
#undef settimeofday

static time_t date (int year, int month, int day, int hour, int minute, int second) {
    struct tm value = {
        .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day,
        .tm_hour = hour, .tm_min = minute, .tm_sec = second,
    };
    return mktime(&value);
}

static menu_t fixture (void) {
    setenv("TZ", "UTC0", 1);
    tzset();
    memset(&mock, 0, sizeof(mock));
    mock.range = (rtc_range_t){.min = INT64_C(946684800), .max = INT64_C(4102444799)};
    mock.writable = true;
    mock.now = date(2026, 11, 16, 12, 34, 56);
    menu_t menu = {0};
    settings_pane_time.enter(&menu);
    return menu;
}

static void draw (menu_t *menu, bool focused) {
    mock.texts = 0;
    settings_pane_time.draw(menu, focused);
}

static void test_supported_years (void) {
    menu_t menu = fixture();
    for (int year = 2096; year <= 2099; year++) {
        mock.now = date(year, 12, 31, 23, 59, 59);
        settings_pane_time.enter(&menu);
        char expected[8];
        snprintf(expected, sizeof(expected), "%d", year);
        draw(&menu, true);
        TEST_CHECK(strcmp(mock.text[1], expected) == 0);
        draw(&menu, false);
        TEST_CHECK(strcmp(mock.text[1], expected) == 0);
    }
    TEST_CHECK(year_min == 2000 && year_max == 2099);
    editing_field_type = RTC_EDIT_YEAR;
    adjust_rtc_time(&rtc_tm, 1);
    TEST_CHECK(rtc_tm.tm_year == 100);
    adjust_rtc_time(&rtc_tm, -1);
    TEST_CHECK(rtc_tm.tm_year == 199);
}

static void test_calendar_edits (void) {
    menu_t menu = fixture();
    mock.now = date(2024, 2, 29, 12, 34, 56);
    settings_pane_time.enter(&menu);
    editing_field_type = RTC_EDIT_YEAR;
    adjust_rtc_time(&rtc_tm, 1);
    TEST_CHECK(rtc_tm.tm_year == 125 && rtc_tm.tm_mon == 1 && rtc_tm.tm_mday == 28);
    mock.now = date(2024, 1, 31, 12, 34, 56);
    settings_pane_time.enter(&menu);
    editing_field_type = RTC_EDIT_MONTH;
    adjust_rtc_time(&rtc_tm, 1);
    TEST_CHECK(rtc_tm.tm_mon == 1 && rtc_tm.tm_mday == 29);
    editing_field_type = RTC_EDIT_DAY;
    adjust_rtc_time(&rtc_tm, 1);
    TEST_CHECK(rtc_tm.tm_mon == 1 && rtc_tm.tm_mday == 1);
    adjust_rtc_time(&rtc_tm, -1);
    TEST_CHECK(rtc_tm.tm_mon == 1 && rtc_tm.tm_mday == 29);
    mock.now = date(2025, 3, 31, 0, 0, 0);
    settings_pane_time.enter(&menu);
    editing_field_type = RTC_EDIT_MONTH;
    adjust_rtc_time(&rtc_tm, -1);
    TEST_CHECK(rtc_tm.tm_mon == 1 && rtc_tm.tm_mday == 28 && rtc_tm.tm_wday == 5);
}

static void test_invalid_clock_repair (void) {
    menu_t menu = fixture();
    mock.now = -1;
    mock.sync_result = FLASHCART_ERR_INT;
    settings_pane_time.enter(&menu);
    TEST_CHECK(menu.current_time == -1 && clock_writable);
    TEST_CHECK(strstr(status_message, "invalid") != NULL);
    TEST_CHECK(rtc_tm.tm_year == 100 && rtc_tm.tm_mon == 0 && rtc_tm.tm_mday == 1);
    draw(&menu, true);
    TEST_CHECK(strcmp(mock.text[1], "2000") == 0);
    TEST_CHECK(strstr(settings_pane_time.hint(&menu, SETTINGS_HINT_LEFT), "A: Save") != NULL);
    menu.actions.go_up = true;
    TEST_CHECK(settings_pane_time.process(&menu));
    TEST_CHECK(rtc_tm.tm_year == 101);
    menu.actions.go_up = false;
    menu.actions.enter = true;
    TEST_CHECK(settings_pane_time.process(&menu));
    TEST_CHECK(mock.writes == 1 && menu.current_time == date(2001, 1, 1, 0, 0, 0));
    TEST_CHECK(strcmp(status_message, "Time saved") == 0 && mock.last_sound == SFX_ENTER);
}

static void test_failed_saves (void) {
    menu_t menu = fixture();
    mock.set_result = -2;
    menu.actions.enter = true;
    TEST_CHECK(settings_pane_time.process(&menu));
    TEST_CHECK(mock.writes == 1 && strstr(status_message, "Failed to set") != NULL);
    TEST_CHECK(strstr(status_message, "Time saved") == NULL && mock.last_sound == SFX_ERROR);
    mock.set_result = 0;
    mock.confirm_failed = true;
    TEST_CHECK(settings_pane_time.process(&menu));
    TEST_CHECK(mock.writes == 2 && strstr(status_message, "Failed to confirm") != NULL);
    TEST_CHECK(menu.current_time == -1 && mock.last_sound == SFX_ERROR);
}

static void test_save_readback_and_reentry (void) {
    menu_t menu = fixture();
    mock.now = date(2024, 2, 29, 23, 59, 59);
    settings_pane_time.enter(&menu);
    mock.confirm_advance = 1;
    menu.actions.enter = true;
    TEST_CHECK(settings_pane_time.process(&menu));
    TEST_CHECK(mock.written == date(2024, 2, 29, 23, 59, 59));
    TEST_CHECK(menu.current_time == date(2024, 3, 1, 0, 0, 0));
    TEST_CHECK(rtc_tm.tm_mon == 2 && rtc_tm.tm_mday == 1 && rtc_tm.tm_sec == 0);
    settings_pane_time.leave(&menu);
    TEST_CHECK(status_message[0] == '\0');
    mock.now += 20;
    settings_pane_time.enter(&menu);
    TEST_CHECK(mock.syncs == 3 && menu.current_time == mock.now && rtc_tm.tm_sec == 20);
}

static void test_fallback_and_writability (void) {
    menu_t menu = fixture();
    mock.writable = false;
    mock.source = RTC_SOURCE_NONE;
    mock.range = (rtc_range_t){.min = INT64_C(820454400), .max = INT64_C(3976214399)};
    settings_pane_time.enter(&menu);
    TEST_CHECK(strstr(status_message, "Software clock") != NULL && year_min == 1996 && year_max == 2095);
    menu.actions.enter = true;
    TEST_CHECK(settings_pane_time.process(&menu));
    TEST_CHECK(mock.writes == 0 && strcmp(status_message, "RTC is not writable") == 0);
    mock.source = RTC_SOURCE_JOYBUS;
    mock.writable = true;
    settings_pane_time.enter(&menu);
    TEST_CHECK(status_message[0] == '\0');
    TEST_CHECK(settings_pane_time.process(&menu));
    TEST_CHECK(mock.writes == 1 && strcmp(status_message, "Time saved") == 0);
    mock.writable = false;
    mock.now = -1;
    settings_pane_time.enter(&menu);
    TEST_CHECK(settings_pane_time.process(&menu) && mock.writes == 1);
    TEST_CHECK(strstr(settings_pane_time.hint(&menu, SETTINGS_HINT_LEFT), "A: Save") == NULL);
    draw(&menu, false);
    TEST_CHECK(mock.texts == 1 && strstr(mock.text[0], "Unknown") != NULL);
    menu.actions.back = true;
    TEST_CHECK(!settings_pane_time.process(&menu));
    TEST_CHECK(mock.writable_queries == 4);
}

TEST_LIST = {
    {"Time pane displays all Pro years and wraps supported bounds", test_supported_years},
    {"Time pane month and leap-year edits stay on valid days", test_calendar_edits},
    {"Time pane repairs an invalid hardware date", test_invalid_clock_repair},
    {"Time pane never reports saved after write or confirmation failure", test_failed_saves},
    {"Time pane refreshes editor from confirmed time and resyncs on reentry", test_save_readback_and_reentry},
    {"Time pane identifies software fallback and delegates legacy writability", test_fallback_and_writability},
    {NULL, NULL},
};
