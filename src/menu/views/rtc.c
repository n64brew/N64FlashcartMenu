#include <stdbool.h>
#include <stdio.h>
#include <libdragon.h>
#include <sys/time.h>
#include "../sound.h"
#include "../ui_components/constants.h"
#include "../../utils/utils.h"
#include "views.h"

#define CLAMP(x, min, max) (MIN(MAX((x), (min)), (max)))
#define WRAP(x, min, max)  ({ \
    typeof(x) _x = x; typeof(min) _min = min; typeof(max) _max = max; \
    _x < _min ? _max : _x > _max ? _min : _x; \
})

static int year_min = 1996;
static int year_max = 2095;
static bool clock_writable;

typedef enum {
    RTC_EDIT_YEAR,
    RTC_EDIT_MONTH,
    RTC_EDIT_DAY,
    RTC_EDIT_HOUR,
    RTC_EDIT_MIN,
    RTC_EDIT_SEC,
    RTC_EDIT_FIELDS,
} rtc_field_t;

static struct tm rtc_tm = {0};
static rtc_field_t editing_field_type;
static char status_message[256];


static void adjust_rtc_time (struct tm *t, int incr) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int year = t->tm_year + 1900;
    int last_day = days[t->tm_mon] + (t->tm_mon == 1 &&
        (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)));
    switch (editing_field_type)
    {
        case RTC_EDIT_YEAR:
            t->tm_year = WRAP( t->tm_year + incr, year_min - 1900, year_max - 1900 );
            break;
        case RTC_EDIT_MONTH:
            t->tm_mon = WRAP( t->tm_mon + incr, 0, 11 );
            break;
        case RTC_EDIT_DAY:
            t->tm_mday = WRAP( t->tm_mday + incr, 1, last_day );
            break;
        case RTC_EDIT_HOUR:
            t->tm_hour = WRAP( t->tm_hour + incr, 0, 23 );
            break;
        case RTC_EDIT_MIN:
            t->tm_min = WRAP( t->tm_min + incr, 0, 59 );
            break;
        case RTC_EDIT_SEC:
            t->tm_sec = WRAP( t->tm_sec + incr, 0, 59 );
            break;
        default:
            break;
    }
    /* Keep month/year edits on a valid day, including February in leap years. */
    year = t->tm_year + 1900;
    last_day = days[t->tm_mon] + (t->tm_mon == 1 &&
        (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)));
    t->tm_mday = MIN(t->tm_mday, last_day);
    // Recalculate day-of-week and day-of-year
    time_t timestamp = mktime( t );
    *t = *gmtime( &timestamp );
}

/**
 * @brief Write the RTC time back to the hardware clock.
 */
static void save_rtc_time (menu_t *menu) {
    if (!clock_writable) {
        snprintf(status_message, sizeof(status_message), "RTC is not writable");
        sound_play_effect(SFX_ERROR);
        return;
    }

    struct timeval new_time = { .tv_sec = mktime(&rtc_tm) };
    if (settimeofday(&new_time, NULL) != 0) {
        snprintf(status_message, sizeof(status_message), "Failed to set RTC time");
        sound_play_effect(SFX_ERROR);
        return;
    }

    menu->current_time = time(NULL);
    if (menu->current_time < 0) {
        snprintf(status_message, sizeof(status_message), "Failed to confirm RTC time");
        sound_play_effect(SFX_ERROR);
        return;
    }
    rtc_tm = *gmtime(&menu->current_time);
    snprintf(status_message, sizeof(status_message), "Time saved");
    sound_play_effect(SFX_ENTER);
}

static void pane_enter (menu_t *menu) {
    flashcart_err_t result = flashcart_resync_rtc();
    menu->current_time = time(NULL);
    clock_writable = flashcart_rtc_is_writable();
    rtc_range_t range = flashcart_get_rtc_range();
    year_min = gmtime(&range.min)->tm_year + 1900;
    year_max = gmtime(&range.max)->tm_year + 1900;
    editing_field_type = RTC_EDIT_YEAR;
    status_message[0] = '\0';

    if (menu->current_time >= 0 && result == FLASHCART_OK) {
        rtc_tm = *gmtime(&menu->current_time);
        if (!clock_writable && rtc_get_source() == RTC_SOURCE_NONE) {
            snprintf(status_message, sizeof(status_message), "Software clock; no hardware RTC is available");
        }
    } else {
        rtc_tm = (struct tm) { .tm_year = MAX(year_min, 2000) - 1900, .tm_mday = 1 };
        time_t initial = mktime(&rtc_tm);
        rtc_tm = *gmtime(&initial);
        snprintf(status_message, sizeof(status_message), "%s",
            result != FLASHCART_OK ? flashcart_convert_error_message(result) : "No hardware clock is available");
    }
}

static void pane_leave (menu_t *menu) {
    (void) menu;
    status_message[0] = '\0';
}

static bool pane_process (menu_t *menu) {
    if (menu->actions.back) {
        return false;
    } else if (menu->current_time < 0 && !clock_writable) {
        return true;
    } else if (menu->actions.go_left) {
        editing_field_type = WRAP((int) editing_field_type - 1, 0, RTC_EDIT_FIELDS - 1);
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_right) {
        editing_field_type = WRAP((int) editing_field_type + 1, 0, RTC_EDIT_FIELDS - 1);
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_up) {
        adjust_rtc_time(&rtc_tm, +1);
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_down) {
        adjust_rtc_time(&rtc_tm, -1);
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.enter) {
        save_rtc_time(menu);
    }

    return true;
}

static void pane_draw (menu_t *menu, bool focused) {
    static const char *const days_of_week[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const field_names[RTC_EDIT_FIELDS] = { "YYYY", "MM", "DD", "hh", "mm", "ss" };
    char field_values[RTC_EDIT_FIELDS][8];
    struct tm shown;
    int field_width = (SETTINGS_PANE_X1 - SETTINGS_PANE_X0 - 32) / RTC_EDIT_FIELDS;
    int x = SETTINGS_PANE_X0 + 16;
    int y = SETTINGS_PANE_Y0 + 8;

    if (menu->current_time < 0 && !clock_writable) {
        ui_components_text_draw(
            x, y, SETTINGS_PANE_X1 - x - 16, 120,
            STL_DEFAULT, ALIGN_LEFT, WRAP_WORD,
            "This cart does not support a real time clock.\n\nCurrent date & time: Unknown"
        );
        return;
    }

    if (focused || menu->current_time < 0) {
        shown = rtc_tm;
    } else {
        shown = *gmtime(&menu->current_time);
    }

    snprintf(field_values[RTC_EDIT_YEAR], sizeof(field_values[0]), "%04d",
             CLAMP(shown.tm_year + 1900, year_min, year_max));
    snprintf(field_values[RTC_EDIT_MONTH], sizeof(field_values[0]), "%02d", CLAMP(shown.tm_mon + 1, 1, 12));
    snprintf(field_values[RTC_EDIT_DAY], sizeof(field_values[0]), "%02d", CLAMP(shown.tm_mday, 1, 31));
    snprintf(field_values[RTC_EDIT_HOUR], sizeof(field_values[0]), "%02d", CLAMP(shown.tm_hour, 0, 23));
    snprintf(field_values[RTC_EDIT_MIN], sizeof(field_values[0]), "%02d", CLAMP(shown.tm_min, 0, 59));
    snprintf(field_values[RTC_EDIT_SEC], sizeof(field_values[0]), "%02d", CLAMP(shown.tm_sec, 0, 59));

    for (int i = 0; i < RTC_EDIT_FIELDS; i++) {
        if (focused && (i == editing_field_type)) {
            ui_components_box_draw(x, y, x + field_width - 3, y + 58, FILE_LIST_HIGHLIGHT_COLOR);
            ui_components_focus_draw(x, y, x + field_width - 3, y + 58);
        }
        ui_components_text_draw(x, y + 6, field_width - 3, 22, STL_GRAY,
                                ALIGN_CENTER, WRAP_NONE, field_names[i]);
        ui_components_text_draw(x, y + 31, field_width - 3, 22, STL_DEFAULT,
                                ALIGN_CENTER, WRAP_NONE, field_values[i]);
        x += field_width;
    }

    ui_components_settings_row_draw(SETTINGS_PANE_Y0 + 72, "DoW",
        days_of_week[CLAMP(shown.tm_wday, 0, 6)], false);

    ui_components_text_draw(
        SETTINGS_PANE_X0 + 24, SETTINGS_PANE_Y0 + 112,
        SETTINGS_PANE_X1 - SETTINGS_PANE_X0 - 48, 180,
        status_message[0] ? STL_YELLOW : STL_GRAY, ALIGN_CENTER, WRAP_WORD,
        status_message[0]
            ? status_message
            : "Left / Right selects a field. Up / Down adjusts it. Press A to save.\n\n"
              "You can also use the PC terminal application via USB,\n"
              "or even an N64 game with RTC support."
    );
}

static const char *pane_hint (menu_t *menu, settings_hint_t slot) {
    if (menu->current_time < 0 && !clock_writable) {
        switch (slot) {
            case SETTINGS_HINT_LEFT: return "B: Categories\n";
            case SETTINGS_HINT_CENTER: return "◀L Tab R▶\n";
            default: return NULL;
        }
    }
    switch (slot) {
        case SETTINGS_HINT_LEFT: return "A: Save\nB: Categories";
        case SETTINGS_HINT_CENTER: return "D-Pad: Adjust\n◀L Tab R▶";
        default: return NULL;
    }
}

const settings_pane_t settings_pane_time = {
    .label = "Time",
    .enter = pane_enter,
    .leave = pane_leave,
    .process = pane_process,
    .draw = pane_draw,
    .hint = pane_hint,
};
