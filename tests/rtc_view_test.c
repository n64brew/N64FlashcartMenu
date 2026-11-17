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
    bool writable;
    time_t now, written;
    int set_result;
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
        mock.now = value->tv_sec;
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
    mock.writable = true;
    mock.now = date(2026, 11, 16, 12, 34, 56);
    menu_t menu = {0};
    settings_pane_time.enter(&menu);
    return menu;
}

static void test_pro_clock_dispatch (void) {
    menu_t menu = fixture();
    TEST_CHECK(mock.syncs == 1 && menu.current_time == mock.now);
    TEST_CHECK(rtc_tm.tm_year == 126 && rtc_tm.tm_mon == 10 && rtc_tm.tm_mday == 16);
    menu.actions.enter = true;
    TEST_CHECK(settings_pane_time.process(&menu));
    TEST_CHECK(mock.writes == 1 && mock.writable_queries == 1);
    TEST_CHECK(mock.written == mock.now);
    TEST_CHECK(strcmp(status_message, "Time saved") == 0 && mock.last_sound == SFX_ENTER);
    mock.now += 20;
    settings_pane_time.enter(&menu);
    TEST_CHECK(mock.syncs == 2 && menu.current_time == mock.now && rtc_tm.tm_sec == 16);
}

static void test_write_dispatch_failures (void) {
    menu_t menu = fixture();
    mock.writable = false;
    menu.actions.enter = true;
    TEST_CHECK(settings_pane_time.process(&menu));
    TEST_CHECK(mock.writes == 0 && mock.writable_queries == 1);
    TEST_CHECK(strcmp(status_message, "RTC is not writable") == 0 && mock.last_sound == SFX_ERROR);
    mock.writable = true;
    mock.set_result = -2;
    TEST_CHECK(settings_pane_time.process(&menu));
    TEST_CHECK(mock.writes == 1 && mock.writable_queries == 2);
    TEST_CHECK(strcmp(status_message, "Failed to set RTC time") == 0 && mock.last_sound == SFX_ERROR);
}

TEST_LIST = {
    {"Time pane resyncs and writes through the active flashcart clock", test_pro_clock_dispatch},
    {"Time pane checks backend writability and reports clock write failures", test_write_dispatch_failures},
    {NULL, NULL},
};
