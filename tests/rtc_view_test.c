#include <acutest.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "../src/flashcart/flashcart.h"
#include "../src/menu/sound.h"

/* Exercise the production pane with drawing and menu dependencies stubbed. */
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

typedef enum { RTC_SOURCE_NONE, RTC_SOURCE_JOYBUS } rtc_source_t;

static struct {
    bool custom_writable, joybus_available;
    rtc_source_t source;
    time_t now, written, minimum, maximum;
    int set_result;
    unsigned syncs, writes;
    sound_effect_t last_sound;
    char text[32][256];
    unsigned texts;
} mock;

bool flashcart_get_rtc_write_range (time_t *minimum, time_t *maximum) {
    if (!mock.custom_writable) {
        return false;
    }
    *minimum = mock.minimum;
    *maximum = mock.maximum;
    return true;
}

rtc_source_t rtc_get_source (void) { return mock.source; }
bool rtc_is_source_available (rtc_source_t source) {
    return source == RTC_SOURCE_JOYBUS && mock.joybus_available;
}
int rtc_set_source (rtc_source_t source) {
    TEST_CHECK(source == mock.source);
    mock.syncs++;
    return 0;
}

void sound_play_effect (sound_effect_t effect) { mock.last_sound = effect; }

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
    (void)y; (void)label; (void)value; (void)selected;
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

static time_t date (int year, int month, int day) {
    struct tm value = { .tm_year = year - 1900, .tm_mon = month - 1, .tm_mday = day };
    return mktime(&value);
}

static menu_t fixture (void) {
    setenv("TZ", "UTC0", 1);
    tzset();
    memset(&mock, 0, sizeof(mock));
    mock.custom_writable = true;
    mock.minimum = date(2000, 1, 1);
    mock.maximum = date(2100, 1, 1) - 1;
    mock.now = date(2026, 11, 17);
    return (menu_t){0};
}

static bool drawn_text_contains (menu_t *menu, const char *text) {
    mock.texts = 0;
    settings_pane_time.draw(menu, true);
    for (unsigned i = 0; i < mock.texts; i++) {
        if (strstr(mock.text[i], text)) {
            return true;
        }
    }
    return false;
}

static void save (menu_t *menu) {
    memset(&menu->actions, 0, sizeof(menu->actions));
    menu->actions.enter = true;
    TEST_CHECK(settings_pane_time.process(menu));
}

static void adjust_year (menu_t *menu, bool increase) {
    memset(&menu->actions, 0, sizeof(menu->actions));
    menu->actions.go_up = increase;
    menu->actions.go_down = !increase;
    TEST_CHECK(settings_pane_time.process(menu));
}

static void test_preview_before_enter (void) {
    menu_t menu = fixture();
    menu.current_time = date(2099, 1, 15);
    settings_pane_time.draw(&menu, false);
    bool shown = false;
    for (unsigned i = 0; i < mock.texts; i++) {
        shown |= strcmp(mock.text[i], "2099") == 0;
    }
    TEST_CHECK(shown);
}

static void test_custom_clock_write (void) {
    menu_t menu = fixture();
    settings_pane_time.enter(&menu);
    TEST_CHECK(menu.current_time == mock.now && mock.syncs == 1);
    adjust_year(&menu, true);
    save(&menu);
    TEST_CHECK(mock.writes == 1 && mock.written == date(2027, 11, 17));
    TEST_CHECK(drawn_text_contains(&menu, "Time saved") && mock.last_sound == SFX_ENTER);
}

static void test_software_clock_blocked (void) {
    menu_t menu = fixture();
    mock.custom_writable = false;
    settings_pane_time.enter(&menu);
    save(&menu);
    TEST_CHECK(mock.writes == 0);
    TEST_CHECK(drawn_text_contains(&menu, "RTC is not writable") && mock.last_sound == SFX_ERROR);

    mock.now = -1;
    settings_pane_time.enter(&menu);
    save(&menu);
    TEST_CHECK(mock.writes == 0);
    TEST_CHECK(drawn_text_contains(&menu, "Unknown"));
    TEST_CHECK(strstr(settings_pane_time.hint(&menu, SETTINGS_HINT_LEFT), "Save") == NULL);
}

static void test_joybus_fallback (void) {
    menu_t menu = fixture();
    mock.custom_writable = false;
    mock.source = RTC_SOURCE_JOYBUS;
    mock.joybus_available = true;
    mock.now = date(1996, 1, 15);
    settings_pane_time.enter(&menu);
    adjust_year(&menu, false);
    save(&menu);
    TEST_CHECK(mock.writes == 1 && mock.written == date(2095, 1, 15));

    mock.joybus_available = false;
    settings_pane_time.enter(&menu);
    save(&menu);
    TEST_CHECK(mock.writes == 1);
    TEST_CHECK(drawn_text_contains(&menu, "RTC is not writable"));
}

static void test_custom_year_range (void) {
    menu_t menu = fixture();
    mock.now = date(2000, 1, 15);
    settings_pane_time.enter(&menu);
    adjust_year(&menu, false);
    TEST_CHECK(drawn_text_contains(&menu, "2099"));
    save(&menu);
    TEST_CHECK(mock.written == date(2099, 1, 15));
    adjust_year(&menu, true);
    save(&menu);
    TEST_CHECK(mock.writes == 2 && mock.written == date(2000, 1, 15));
}

static void test_invalid_clock_repair_and_write_failure (void) {
    menu_t menu = fixture();
    mock.now = -1;
    settings_pane_time.enter(&menu);
    TEST_CHECK(drawn_text_contains(&menu, "2000"));
    TEST_CHECK(strstr(settings_pane_time.hint(&menu, SETTINGS_HINT_LEFT), "Save") != NULL);
    adjust_year(&menu, true);
    save(&menu);
    TEST_CHECK(mock.writes == 1 && mock.written == date(2001, 1, 1));
    TEST_CHECK(menu.current_time == mock.now);
    TEST_CHECK(drawn_text_contains(&menu, "Time saved"));

    mock.set_result = -2;
    adjust_year(&menu, true);
    save(&menu);
    TEST_CHECK(mock.writes == 2 && menu.current_time == date(2001, 1, 1));
    TEST_CHECK(drawn_text_contains(&menu, "Failed to set RTC time") && mock.last_sound == SFX_ERROR);
}

TEST_LIST = {
    {"Time pane previews the clock before first entering the editor", test_preview_before_enter},
    {"Time pane saves a custom hardware clock through settimeofday", test_custom_clock_write},
    {"Time pane refuses writes to a software fallback clock", test_software_clock_blocked},
    {"Time pane preserves Joybus writing and year bounds", test_joybus_fallback},
    {"Time pane uses the custom 2000 through 2099 year range", test_custom_year_range},
    {"Time pane repairs an invalid writable clock and reports setter failure", test_invalid_clock_repair_and_write_failure},
    {NULL, NULL},
};
