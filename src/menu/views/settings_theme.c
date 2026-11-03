/**
 * @file settings_theme.c
 * @brief Theme picker pane for the unified Settings tab.
 *
 * The theme catalog lives in theme.c; this module owns its settings UI state
 * and adapts the picker to settings_pane_t.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../external_theme.h"
#include "../fonts.h"
#include "../settings.h"
#include "../sound.h"
#include "../theme.h"
#include "../theme_text.h"
#include "../ui_components.h"
#include "../ui_components/constants.h"
#include "views.h"

#define THEME_DIVIDER_X (SETTINGS_PANE_X0 + 120)
#define THEME_LIST_X0   (THEME_DIVIDER_X + 10)

typedef struct {
    const char *name;
    const menu_theme_t *themes;
    int count;
} theme_family_t;

typedef enum { PICKER_FAMILIES, PICKER_THEMES, PICKER_CUSTOM } theme_picker_layer_t;
typedef enum { CUSTOM_ROW_BASE, CUSTOM_ROW_HUE, CUSTOM_ROW_BORDER, CUSTOM_ROW_COLORS,
    CUSTOM_ROW_COUNT = CUSTOM_ROW_COLORS + THEME_CUSTOM_COLOR_COUNT } custom_row_t;
typedef enum { CUSTOM_CHANNEL_NONE = -1, CUSTOM_CHANNEL_RED, CUSTOM_CHANNEL_GREEN,
    CUSTOM_CHANNEL_BLUE, CUSTOM_CHANNEL_ALPHA, CUSTOM_CHANNEL_COUNT } custom_channel_t;

static theme_picker_layer_t layer;
static int family_index;
static int theme_row;
static custom_row_t custom_row;
static custom_channel_t custom_channel;
static bool menu_hidden;
/** Theme restored when leaving a preset list without choosing one with A. */
static const menu_theme_t *chosen_theme;

/* Image-backed packages Custom can use as its base. Borrowed catalog pointers,
 * stable until this pane is left. */
static const menu_theme_t **custom_bases;
static int custom_base_count;
/* Index into custom_bases, or one of these. */
enum { CUSTOM_BASE_USER_IMAGE = -2, CUSTOM_BASE_NONE = -1 };
static int custom_base_index;

static bool category_has_themes(int category_index) {
    const menu_theme_t *catalog = theme_catalog();
    return category_index + 1 < theme_catalog_count()
        && !theme_is_category(&catalog[category_index + 1]);
}

static int family_count(void) {
    int count = 0;
    const menu_theme_t *catalog = theme_catalog();
    for (int i = 0; i < theme_catalog_count(); i++) {
        if (theme_is_category(&catalog[i]) && category_has_themes(i)) count++;
    }
    return count;
}

static theme_family_t family_get(int wanted) {
    theme_family_t family = { 0 };
    const menu_theme_t *catalog = theme_catalog();
    int category = 0;
    for (int i = 0; i < theme_catalog_count(); i++) {
        if (theme_is_category(&catalog[i])) {
            if (family.themes) break;
            if (!category_has_themes(i)) continue;
            if (category++ == wanted) {
                family.name = catalog[i].name;
                family.themes = &catalog[i + 1];
            }
        } else if (family.themes) {
            family.count++;
        }
    }
    return family;
}

static int family_for_theme(const menu_theme_t *theme) {
    for (int i = 0; i < family_count(); i++) {
        theme_family_t family = family_get(i);
        for (int row = 0; row < family.count; row++)
            if (&family.themes[row] == theme) return i;
    }
    return 0;
}

static void preview_theme(const menu_theme_t *selected) {
    if (!external_theme_prepare(selected)) return;
    theme_set(selected);
    fonts_apply_theme();
    ui_components_background_apply_theme();
}

static void choose_theme(const menu_theme_t *selected) {
    preview_theme(selected);
    chosen_theme = theme_selected();
}

static void custom_bases_build(void) {
    free(custom_bases);
    custom_bases = calloc(theme_catalog_count(), sizeof(*custom_bases));
    custom_base_count = 0;
    if (!custom_bases) return;
    const menu_theme_t *catalog = theme_catalog();
    for (int i = 0; i < theme_catalog_count(); i++)
        if (theme_is_custom_base(&catalog[i])) custom_bases[custom_base_count++] = &catalog[i];
}

static int custom_base_find(void) {
    const menu_theme_t *custom = theme_custom();
    if (theme_custom_has_background_image()) return CUSTOM_BASE_USER_IMAGE;
    for (int i = 0; custom->package && i < custom_base_count; i++)
        if (strcmp(custom_bases[i]->package->path, custom->package->path) == 0) return i;
    return CUSTOM_BASE_NONE;
}

/* Cycle Custom's base through None and the image-backed packages. Leaving the
 * saved user image always returns to None. */
static void custom_select_base(int direction) {
    int positions = custom_base_count + 1;
    int position = custom_base_index == CUSTOM_BASE_USER_IMAGE ? 0
        : (custom_base_index + 1 + direction + positions) % positions;
    if (position == 0) theme_custom_clear_base();
    else if (!theme_custom_copy_from(custom_bases[position - 1])) return;
    theme_custom_set_hue_shift(0);
    custom_base_index = position - 1;
    preview_theme(theme_custom());
}

static bool is_custom_family(theme_family_t family) {
    return family.themes == theme_custom();
}

static bool prepared;

static void pane_leave(menu_t *menu) {
    /* Left before the themes were discovered: nothing was previewed or chosen. */
    if (!prepared) return;
    if (theme_selected() != chosen_theme) preview_theme(chosen_theme);
    free(custom_bases);
    custom_bases = NULL;
    custom_base_count = 0;
    free(menu->settings.theme);
    menu->settings.theme = strdup(theme_get()->name);
    free(menu->settings.theme_path);
    menu->settings.theme_path = strdup(theme_get()->package ? theme_get()->package->path : "");
    settings_save(&menu->settings);
}

static void row_draw(int x0, int x1, int y, const char *label,
                     const char *value, bool selected, bool focused) {
    if (selected) {
        ui_components_box_draw(x0, y, x1, y + SETTINGS_ROW_HEIGHT - 2,
            focused ? theme_get()->highlight : theme_get()->tab_active);
        if (focused) ui_components_focus_draw(x0, y, x1, y + SETTINGS_ROW_HEIGHT - 2);
    }
    /* Package names are plain text, not font/style markup. */
    char safe_label[128], safe_value[128];
    theme_text_escape(safe_label, sizeof(safe_label), label);
    theme_text_escape(safe_value, sizeof(safe_value), value);
    int value_width = value[0] ? (strlen(value) > 13 ? 174 : 146) : 0;
    ui_components_text_draw(x0 + 8, y + 4, x1 - x0 - 16 - value_width,
        SETTINGS_ROW_HEIGHT, selected ? STL_DEFAULT : STL_GRAY,
        ALIGN_LEFT, WRAP_ELLIPSES, safe_label);
    if (value_width) {
        ui_components_text_draw(x1 - value_width, y + 4, value_width - 8,
            SETTINGS_ROW_HEIGHT, selected ? STL_DEFAULT : STL_GRAY,
            ALIGN_RIGHT, WRAP_ELLIPSES, safe_value);
    }
}

typedef struct {
    menu_t *menu;
    ui_components_scan_progress_t progress;
} theme_scan_t;

static void theme_scan_progress(int done, int total, void *data) {
    theme_scan_t *scan = data;
    ui_components_scan_progress_draw(&scan->progress, scan->menu, SETTINGS_TAB_INDEX,
                                     "Scanning themes", done, total);
}

/* Entering only marks the pane unprepared: discovering themes can take a
 * while, so it runs after the frame, where scan progress can be drawn. */
static void pane_enter(menu_t *menu) {
    prepared = false;
    menu_hidden = false;
}

static void pane_after_frame(menu_t *menu) {
    if (prepared) return;
    prepared = true;
    /* The first visit after themes change parses every package; show progress. */
    theme_scan_t scan = { .menu = menu, .progress.start_us = get_ticks_us() };
    external_themes_init(menu->storage_prefix, theme_scan_progress, &scan);
    custom_bases_build();
    choose_theme(theme_get_by_name(menu->settings.theme));
    layer = PICKER_FAMILIES;
    family_index = family_for_theme(theme_selected());
    theme_row = 0;
    custom_row = CUSTOM_ROW_BASE;
    custom_channel = CUSTOM_CHANNEL_NONE;
    menu_hidden = false;
}

static bool pane_process(menu_t *menu) {
    if (!prepared) return true;
    if (menu_hidden) {
        if (menu->actions.context || menu->actions.back) menu_hidden = false;
        return true;
    }
    if (menu->actions.context) {
        menu_hidden = true;
        return true;
    }
    if (menu->actions.back || (layer == PICKER_THEMES && menu->actions.go_left)) {
        if (custom_channel >= 0) custom_channel = CUSTOM_CHANNEL_NONE;
        else if (layer != PICKER_FAMILIES) {
            preview_theme(chosen_theme);
            layer = PICKER_FAMILIES;
        }
        else return false;
        sound_play_effect(SFX_EXIT);
        return true;
    }

    if (layer == PICKER_FAMILIES) {
        if (menu->actions.go_up && family_index > 0) family_index--;
        else if (menu->actions.go_down && family_index < family_count() - 1) family_index++;
        else if (menu->actions.enter || menu->actions.go_right) {
            theme_family_t family = family_get(family_index);
            if (is_custom_family(family)) {
                choose_theme(theme_custom());
                custom_base_index = custom_base_find();
                layer = PICKER_CUSTOM;
            } else {
                theme_row = 0;
                for (int i = 0; i < family.count; i++) {
                    if (theme_selected() == &family.themes[i]) theme_row = i;
                }
                preview_theme(&family.themes[theme_row]);
                layer = PICKER_THEMES;
            }
            sound_play_effect(SFX_ENTER);
        }
        return true;
    }

    if (layer == PICKER_THEMES) {
        theme_family_t family = family_get(family_index);
        int next = theme_row;
        if (menu->actions.settings) {
            if (!theme_custom_copy_from(&family.themes[theme_row])) return true;
            choose_theme(theme_custom());
            custom_base_index = custom_base_find();
            family_index = family_for_theme(theme_custom());
            layer = PICKER_CUSTOM;
            custom_row = CUSTOM_ROW_BASE;
            custom_channel = CUSTOM_CHANNEL_NONE;
            sound_play_effect(SFX_ENTER);
            return true;
        }
        if (menu->actions.go_up) next = (theme_row + family.count - 1) % family.count;
        else if (menu->actions.go_down) next = (theme_row + 1) % family.count;
        else if (menu->actions.enter) {
            chosen_theme = theme_selected();
            return false;
        }
        if (next != theme_row) {
            theme_row = next;
            preview_theme(&family.themes[theme_row]);
            sound_play_effect(SFX_CURSOR);
        }
        return true;
    }

    bool panel = custom_row == CUSTOM_ROW_COLORS + THEME_CUSTOM_PANEL;
    int channel_count = panel ? CUSTOM_CHANNEL_COUNT : CUSTOM_CHANNEL_ALPHA;
    if (custom_channel < 0) {
        int step = menu->actions.go_left ? -1 : menu->actions.go_right || menu->actions.enter ? 1 : 0;
        if (menu->actions.go_up) custom_row = (custom_row + CUSTOM_ROW_COUNT - 1) % CUSTOM_ROW_COUNT;
        else if (menu->actions.go_down) custom_row = (custom_row + 1) % CUSTOM_ROW_COUNT;
        else if (step && custom_row == CUSTOM_ROW_BASE) custom_select_base(step);
        else if (step && custom_row == CUSTOM_ROW_HUE) {
            theme_custom_set_hue_shift(theme_custom_hue_shift() + step * 5);
            preview_theme(theme_selected());
        } else if (step && custom_row == CUSTOM_ROW_BORDER) {
            /* The package's own border, when it has one, follows the built-in styles. */
            int positions = THEME_BORDER_COUNT + theme_custom_has_package_border();
            int position = theme_has_package_border(theme_custom()) ? THEME_BORDER_COUNT : theme_custom_border_style();
            position = (position + step + positions) % positions;
            if (position == THEME_BORDER_COUNT) theme_custom_use_package_border();
            else theme_custom_set_border_style(position);
            preview_theme(theme_selected());
        } else if (menu->actions.enter || menu->actions.go_right) {
            custom_channel = CUSTOM_CHANNEL_RED;
        }
    } else if (menu->actions.go_left) custom_channel = (custom_channel + channel_count - 1) % channel_count;
    else if (menu->actions.go_right) custom_channel = (custom_channel + 1) % channel_count;
    else if (menu->actions.enter) custom_channel = CUSTOM_CHANNEL_NONE;
    else if (menu->actions.go_up || menu->actions.go_down) {
        color_t color = theme_custom_color(custom_row - CUSTOM_ROW_COLORS);
        uint8_t *channel = custom_channel == CUSTOM_CHANNEL_RED ? &color.r : custom_channel == CUSTOM_CHANNEL_GREEN ? &color.g
            : custom_channel == CUSTOM_CHANNEL_BLUE ? &color.b : &color.a;
        int value = *channel + (menu->actions.go_up ? 8 : -8);
        *channel = value < 0 ? 0 : value > 255 ? 255 : value;
        theme_custom_set_color(custom_row - CUSTOM_ROW_COLORS, color);
        preview_theme(theme_selected());
    }
    return true;
}

static void pane_draw(menu_t *menu, bool focused) {
    int y = SETTINGS_PANE_Y0 + 4;
    int visible = (SETTINGS_PANE_Y1 - SETTINGS_PANE_Y0 - 4) / 31;
    int first_family = family_index >= visible ? family_index - visible + 1 : 0;
    for (int i = first_family; i < family_count() && i < first_family + visible; i++) {
        row_draw(SETTINGS_PANE_X0, THEME_DIVIDER_X - 5, y,
            family_get(i).name, "", i == family_index, focused && layer == PICKER_FAMILIES);
        y += 31;
    }
    ui_components_box_draw(THEME_DIVIDER_X, SETTINGS_PANE_Y0,
        THEME_DIVIDER_X + 3, SETTINGS_PANE_Y1, theme_get()->border);

    theme_family_t family = family_get(family_index);
    bool custom = layer == PICKER_CUSTOM || is_custom_family(family);
    if (!custom) {
        int first = theme_row - visible / 2;
        if (first < 0) first = 0;
        if (first > family.count - visible) first = family.count - visible;
        if (first < 0) first = 0;
        y = SETTINGS_PANE_Y0 + 4;
        for (int i = first; i < family.count && i < first + visible; i++, y += 31) {
            row_draw(THEME_LIST_X0, SETTINGS_PANE_X1 - 10, y,
                family.themes[i].name, "", i == theme_row, focused && layer == PICKER_THEMES);
        }
        return;
    }

    static const char *const labels[] = { "Base Theme", "Hue Shift", "Border Style", "Border Color", "Panel", "Text",
        "Selection", "Gradient TL", "Gradient TR", "Gradient BL", "Gradient BR" };
    static const char *const borders[THEME_BORDER_COUNT] = { "Plain", "Double", "Facets", "Beads",
        "Brackets", "Stitches", "Nodes", "Scallops", "Soft" };
    y = SETTINGS_PANE_Y0 + 4;
    for (int i = 0; i < CUSTOM_ROW_COUNT; i++, y += SETTINGS_ROW_HEIGHT + 1) {
        char value[64];
        if (i == CUSTOM_ROW_BASE) {
            /* Also drawn after leaving the pane, when custom_bases is released. */
            snprintf(value, sizeof(value), "%s", theme_custom_has_background_image() ? "User Image"
                : theme_custom()->package ? theme_custom_base_name() : "None");
        } else if (i == CUSTOM_ROW_HUE) snprintf(value, sizeof(value), "%d deg", theme_custom_hue_shift());
        else if (i == CUSTOM_ROW_BORDER) snprintf(value, sizeof(value), "%s",
            theme_has_package_border(theme_custom()) ? "Package" : borders[theme_custom_border_style()]);
        else {
            color_t color = theme_custom_color(i - CUSTOM_ROW_COLORS);
            int used = 0;
            if (layer == PICKER_CUSTOM && i == custom_row && custom_channel >= 0) {
                static const char channels[] = { 'R', 'G', 'B', 'A' };
                used = snprintf(value, sizeof(value), "%c:%u ", channels[custom_channel],
                    custom_channel == CUSTOM_CHANNEL_RED ? color.r : custom_channel == CUSTOM_CHANNEL_GREEN ? color.g
                        : custom_channel == CUSTOM_CHANNEL_BLUE ? color.b : color.a);
            }
            used += snprintf(value + used, sizeof(value) - used, "#%02X%02X%02X", color.r, color.g, color.b);
            if (i == CUSTOM_ROW_COLORS + THEME_CUSTOM_PANEL)
                snprintf(value + used, sizeof(value) - used, "%02X", color.a);
        }
        row_draw(THEME_LIST_X0, SETTINGS_PANE_X1 - 10, y, labels[i], value,
            i == custom_row, focused && layer == PICKER_CUSTOM);
    }
}

static void pane_overlay(menu_t *menu) {
    if (menu_hidden) ui_components_background_draw();
}

static const char *pane_hint(menu_t *menu, settings_hint_t slot) {
    if (menu_hidden) return slot == SETTINGS_HINT_LEFT ? "B / Z: Show menu\n" : NULL;
    if (slot == SETTINGS_HINT_RIGHT) return layer == PICKER_THEMES ? "Z: Preview\nStart: Customize" : "Z: Preview\n";
    if (slot == SETTINGS_HINT_CENTER) return custom_channel >= 0
        ? (custom_row == CUSTOM_ROW_COLORS + THEME_CUSTOM_PANEL
            ? "Left / Right: RGBA\nUp / Down: Value" : "Left / Right: RGB\nUp / Down: Value") : "D-Pad: Choose\nL / R: Tabs";
    if (custom_channel >= 0) return "A / B: Done\n";
    return layer == PICKER_FAMILIES ? "A: Open\nB: Categories" : "A: Select\nB: Theme families";
}

const settings_pane_t settings_pane_theme = {
    .label = "Theme",
    .enter = pane_enter,
    .leave = pane_leave,
    .after_frame = pane_after_frame,
    .process = pane_process,
    .draw = pane_draw,
    .overlay = pane_overlay,
    .hint = pane_hint,
};
