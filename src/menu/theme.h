#ifndef MENU_THEME_H__
#define MENU_THEME_H__

#include <libdragon.h>

typedef enum {
    THEME_BACKGROUND_CATEGORY,
    THEME_BACKGROUND_VERTEX_GRADIENT,
    THEME_BACKGROUND_SOLID,
    THEME_BACKGROUND_CUSTOM_IMAGE,
} menu_theme_background_mode_t;

typedef enum {
    THEME_COLOR_BACKGROUND,
    THEME_COLOR_PANEL,
    THEME_COLOR_BORDER,
    THEME_COLOR_TEXT,
    THEME_COLOR_MUTED_TEXT,
    THEME_COLOR_ACCENT,
    THEME_COLOR_HIGHLIGHT,
    THEME_COLOR_TAB_INACTIVE,
    THEME_COLOR_TAB_ACTIVE,
    THEME_COLOR_SHADOW,
    THEME_COLOR_SUCCESS,
    THEME_COLOR_WARNING,
    THEME_COLOR_GRADIENT_TL,
    THEME_COLOR_GRADIENT_TR,
    THEME_COLOR_GRADIENT_BL,
    THEME_COLOR_GRADIENT_BR,
    THEME_COLOR_COUNT,
} menu_theme_color_t;

typedef struct {
    const char *name;
    union {
        color_t colors[THEME_COLOR_COUNT];
        struct {
            color_t background;
            color_t panel;
            color_t border;
            color_t text;
            color_t muted_text;
            color_t accent;
            color_t highlight;
            color_t tab_inactive;
            color_t tab_active;
            color_t shadow;
            color_t success;
            color_t warning;
            color_t gradient_tl;
            color_t gradient_tr;
            color_t gradient_bl;
            color_t gradient_br;
        };
    };
    menu_theme_background_mode_t background_mode;
} menu_theme_t;

void theme_set(const menu_theme_t *theme);
void theme_set_by_name(const char *name);
const menu_theme_t *theme_get(void);
/** Returns NULL for unknown names and categories. */
const menu_theme_t *theme_get_by_name(const char *name);
const menu_theme_t *theme_catalog(void);
int theme_catalog_count(void);
bool theme_is_category(const menu_theme_t *theme);
bool theme_is_custom(const menu_theme_t *theme);

typedef enum {
    THEME_CUSTOM_BORDER,
    THEME_CUSTOM_PANEL,
    THEME_CUSTOM_TEXT,
    THEME_CUSTOM_SELECTION,
    THEME_CUSTOM_GRADIENT_TL,
    THEME_CUSTOM_GRADIENT_TR,
    THEME_CUSTOM_GRADIENT_BL,
    THEME_CUSTOM_GRADIENT_BR,
    THEME_CUSTOM_COLOR_COUNT,
} menu_theme_custom_color_t;

void theme_custom_reset(void);
/** The editable Custom theme entry in the catalog. */
const menu_theme_t *theme_custom(void);
color_t theme_custom_color(menu_theme_custom_color_t slot);
void theme_custom_set_color(menu_theme_custom_color_t slot, color_t color);
/** Base Custom on a preset; returns false for categories and NULL. */
bool theme_custom_copy_from(const menu_theme_t *theme);
/** Name of the preset Custom was copied from, or "Custom". */
const char *theme_custom_base_name(void);
int theme_custom_hue_shift(void);
void theme_custom_set_hue_shift(int degrees);
bool theme_custom_has_background_image(void);
void theme_custom_use_background_image(void);
void theme_custom_clear_background_image(void);
/** Copy the active theme into Custom, give it the saved image, and select it. */
void theme_select_custom_image(void);
/** Exact unshifted palette, including uneditable colors and alpha. */
void theme_custom_get_palette(color_t colors[THEME_COLOR_COUNT]);
void theme_custom_set_palette(const color_t colors[THEME_COLOR_COUNT]);
color_t theme_hue_shift_color(color_t color, int degrees);
/** RGBA5551 hue rotation; degrees must be normalized to 0..359. */
uint16_t theme_hue_shift_rgba16(uint16_t value, int degrees);

#endif
