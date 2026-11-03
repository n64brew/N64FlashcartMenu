#ifndef MENU_THEME_H__
#define MENU_THEME_H__

#include <libdragon.h>

typedef enum {
    THEME_BACKGROUND_CATEGORY,
    THEME_BACKGROUND_DUAL_UV,
    THEME_BACKGROUND_SINGLE_UV,
    THEME_BACKGROUND_VERTEX_GRADIENT,
    THEME_BACKGROUND_SOLID,
    THEME_BACKGROUND_STATIC_IMAGE,
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

/** Parameter-animation channels in PNG wire order; colors are channels 0-15. */
typedef enum {
    THEME_PARAM_GRADIENT_SPEED = THEME_COLOR_COUNT,
    THEME_PARAM_GRADIENT_WARP,
    THEME_PARAM_LAYER0_SCALE,
    THEME_PARAM_LAYER1_SCALE,
    THEME_PARAM_LAYER0_DX,
    THEME_PARAM_LAYER0_DY,
    THEME_PARAM_LAYER1_DX,
    THEME_PARAM_LAYER1_DY,
    THEME_PARAM_ROTATION_SPEED,
    THEME_PARAM_LAYER0_ALPHA,
    THEME_PARAM_LAYER1_ALPHA,
    THEME_PARAM_LAYER0_FRAME,
    THEME_PARAM_LAYER1_FRAME,
    THEME_PARAM_COUNT,
} menu_theme_parameter_t;
#define THEME_PARAM_FLOAT_COUNT (THEME_PARAM_LAYER0_ALPHA - THEME_PARAM_GRADIENT_SPEED)
#define THEME_PARAM_BIT(channel) (UINT32_C(1) << (channel))
#define THEME_PARAM_TEXTURE_FRAMES (THEME_PARAM_BIT(THEME_PARAM_LAYER0_FRAME) | THEME_PARAM_BIT(THEME_PARAM_LAYER1_FRAME))

typedef enum {
    THEME_BORDER_PLAIN,
    THEME_BORDER_DOUBLE,
    THEME_BORDER_FACETS,
    THEME_BORDER_BEADS,
    THEME_BORDER_BRACKETS,
    THEME_BORDER_STITCHES,
    THEME_BORDER_NODES,
    THEME_BORDER_SCALLOPS,
    THEME_BORDER_SOFT,
    THEME_BORDER_COUNT,
} menu_theme_border_style_t;

typedef struct {
    const char *asset_name;
    uint8_t tab_overlap;
    uint8_t tab_join_depth;
    uint8_t selected_expansion;
} menu_theme_border_descriptor_t;

typedef struct {
    uint8_t frame_count;
    uint8_t columns;
    uint16_t frame_ticks;
    uint8_t flags;
} menu_theme_animation_t;

enum {
    THEME_ANIMATION_LOOP = 0,
    THEME_ANIMATION_PING_PONG = 1,
    THEME_ANIMATION_ONCE = 2,
    THEME_ANIMATION_MODE_MASK = 3,
    THEME_ANIMATION_REVERSED = 4,
    THEME_ANIMATION_INTERPOLATE = 8,
};

enum { THEME_FRAME_AUTOMATIC = 255, THEME_BORDER_SHEET_SIZE = 24 };

/** Byte-sized PNG coordinates. These layouts also match the v1 wire format. */
typedef struct { uint8_t x, y, width, height; } menu_theme_rect_t;
typedef struct {
    menu_theme_rect_t layer0, layer1, border;
} menu_theme_regions_t;
_Static_assert(sizeof(menu_theme_rect_t) == 4, "theme rectangle wire layout");
_Static_assert(sizeof(menu_theme_regions_t) == 12, "theme regions wire layout");

/** Type 11: a static image's PNG rectangle and pan rates (pixels per nominal frame). */
typedef struct {
    uint16_t x, y, width, height;
    float pan_x, pan_y;
} menu_theme_static_layout_t;

enum {
    THEME_PALETTE_RANGE_REVERSE = 1,
    THEME_PALETTE_RANGE_BLEND = 2,
    THEME_PALETTE_MAX_RANGES = 16,
};

typedef struct {
    uint8_t start, length;
    uint16_t step_ticks;
    uint8_t flags;
} menu_theme_palette_range_t;

/** Type 12: the CI8 palette strip of a static image and its color-cycling ranges. */
typedef struct {
    uint16_t x, y, count;
    uint8_t range_count;
    menu_theme_palette_range_t ranges[THEME_PALETTE_MAX_RANGES];
} menu_theme_static_palette_t;

typedef struct {
    char *path;
    char *family;
    bool indexed_only;
    menu_theme_regions_t regions;
    uint8_t metadata_rows;
    menu_theme_animation_t animations[2];
    menu_theme_rect_t parameter_region;
    uint32_t parameter_mask;
    menu_theme_animation_t parameter_animation;
    menu_theme_border_descriptor_t border;
    /** Static mode: layer 0 drawn over the image (18-byte background record). */
    bool static_overlay;
    /** Zero width when the static image uses the original full-artwork layout. */
    menu_theme_static_layout_t static_layout;
    /** Zero count when the static image is RGBA5551 rather than CI8. */
    menu_theme_static_palette_t static_palette;
} menu_theme_package_t;

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
    menu_theme_border_style_t border_style;
    /* Animatable floats, in parameter channel order. */
    union {
        float parameters[THEME_PARAM_FLOAT_COUNT];
        struct {
            float gradient_speed;
            float gradient_warp;
            float layer0_scale;
            float layer1_scale;
            float layer0_dx;
            float layer0_dy;
            float layer1_dx;
            float layer1_dy;
            float texture_rotation_speed;
        };
    };
    float texture_origin_u;
    float texture_origin_v;
    union {
        uint8_t layer_alpha[2];
        struct { uint8_t layer0_alpha, layer1_alpha; };
    };
    /** External single-PNG data, or NULL for a built-in theme. */
    menu_theme_package_t *package;
} menu_theme_t;
_Static_assert(sizeof(((menu_theme_t *)0)->colors) == 64,
               "theme palette must match sixteen wire RGBA8888 colors");

/** Shared playback for validated texture and parameter sequences; ticks use 60 Hz units. */
int theme_animation_frame(const menu_theme_animation_t *animation, float ticks);
float theme_animation_advance(float ticks, float elapsed, const menu_theme_animation_t *animation);

void theme_set(const menu_theme_t *theme);
void theme_set_by_name(const char *name);
/** Colors and parameters to draw this frame, including parameter animation. */
const menu_theme_t *theme_get(void);
/** Selected catalog entry, unaffected by parameter animation. */
const menu_theme_t *theme_selected(void);
/** Returns NULL for unknown names and categories. */
const menu_theme_t *theme_get_by_name(const char *name);
/** Catalog queries never allocate or perform I/O; they are empty before theme_registry_init(). */
const menu_theme_t *theme_catalog(void);
int theme_catalog_count(void);
bool theme_is_category(const menu_theme_t *theme);
bool theme_is_custom(const menu_theme_t *theme);
/** True for image-backed packages that Custom can use as its base. */
bool theme_is_custom_base(const menu_theme_t *theme);
const char *theme_category(const menu_theme_t *theme);
/** True when a package supplies its own border sheet. */
bool theme_has_package_border(const menu_theme_t *theme);
const menu_theme_border_descriptor_t *theme_border_descriptor(const menu_theme_t *theme);
bool theme_registry_init(void);
/** The mutable registry entry for a pointer into the catalog, or NULL for any other pointer. */
menu_theme_t *theme_registry_entry(const menu_theme_t *theme);
/** Add external themes to the initialized registry; takes ownership on success. */
bool theme_registry_extend(const menu_theme_t *external, int external_count);
void theme_registry_close(void);

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
/** Base Custom on a theme and reset its hue; returns false for categories and NULL. */
bool theme_custom_copy_from(const menu_theme_t *theme);
/** Name of the theme Custom was copied from, or "Custom". */
const char *theme_custom_base_name(void);
menu_theme_border_style_t theme_custom_border_style(void);
/** Replace Custom's border with a built-in style, hiding any package border. */
void theme_custom_set_border_style(menu_theme_border_style_t style);
/** Whether Custom's package authored a border, shown or hidden by a built-in style. */
bool theme_custom_has_package_border(void);
/** Show the package border again after a built-in style replaced it. */
void theme_custom_use_package_border(void);
int theme_custom_hue_shift(void);
void theme_custom_set_hue_shift(int degrees);
bool theme_custom_has_background_image(void);
void theme_custom_use_background_image(void);
void theme_custom_clear_background_image(void);
/** Drop Custom's base package or image and return to its animated gradient. */
void theme_custom_clear_base(void);
/** Copy the active theme into Custom, give it the saved image, and select it. */
void theme_select_custom_image(void);
/** Exact unshifted palette, including uneditable colors and alpha. */
void theme_custom_get_palette(color_t colors[THEME_COLOR_COUNT]);
void theme_custom_set_palette(const color_t colors[THEME_COLOR_COUNT]);
color_t theme_hue_shift_color(color_t color, int degrees);
/** RGBA5551 hue rotation; degrees must be normalized to 0..359. */
uint16_t theme_hue_shift_rgba16(uint16_t value, int degrees);

#endif
