#ifndef MENU_EXTERNAL_THEME_H
#define MENU_EXTERNAL_THEME_H

#include "theme.h"

/** Reports a theme scan's progress: `done` of `total` packages parsed. */
typedef void (*external_theme_progress_t)(int done, int total, void *data);
/**
 * Discover bundled and storage-backed single-PNG themes. Packages are parsed
 * only when the cached index is out of date; `progress`, if given, is called
 * after each one.
 */
void external_themes_init(const char *storage_prefix, external_theme_progress_t progress, void *data);
/** Load one persisted external package without discovering the full catalog. */
bool external_theme_load_selected(const char *path, const char *name);
/** Parse a catalog placeholder before it is previewed. Entries outside the registry cannot be prepared. */
bool external_theme_prepare(const menu_theme_t *theme);
/** Release runtime theme assets and registry-owned paths. */
void external_themes_close(void);

/** One parameter-animation frame; unmasked channels hold the theme's static values. */
typedef struct {
    color_t colors[THEME_COLOR_COUNT];
    float parameters[THEME_PARAM_FLOAT_COUNT];
    uint8_t layer_alpha[2];
    uint8_t layer_frame[2];
} external_theme_parameter_frame_t;

typedef struct {
    surface_t *layer0_frames;
    surface_t *layer1_frames;
    surface_t layer0_blend;
    surface_t layer1_blend;
    surface_t border;
    surface_t static_background;
    _Alignas(8) uint16_t texture_palette[16];
    _Alignas(8) uint16_t border_palette[16];
    int texture_colors;
    int border_colors;
    int layer0_frame_count;
    int layer1_frame_count;
    bool shared_layers;
    external_theme_parameter_frame_t *parameter_frames;
    int parameter_frame_count;
    uint8_t layer0_parameter_frame;
    uint8_t layer1_parameter_frame;
    /** CI8 static images: base palette, and TLUTs the RDP may still be reading. */
    _Alignas(8) uint16_t static_palette[256];
    _Alignas(8) uint16_t static_tlut[4][256];
    int static_tlut_slot;
} external_theme_asset_t;

/** Resolve this frame's animated palette and background parameters in place. */
void external_theme_animate(menu_theme_t *theme, external_theme_asset_t *asset,
                            float *ticks, float elapsed_ticks);
/** Resolve animation into the rendering copy, never the selected catalog entry. */
void theme_render_update(external_theme_asset_t *asset, float *ticks, float elapsed_ticks);

/** Color-cycled TLUT for a CI8 static image at `ticks` nominal frames. */
const uint16_t *external_theme_static_tlut(external_theme_asset_t *asset,
                                           const menu_theme_static_palette_t *palette, float ticks);

/**
 * The package's decoded artwork (inferred RGBA16 or quantized CI4), or NULL
 * while it decodes in the background or if it is invalid. A new theme starts
 * its decode here and abandons any other; external_theme_poll() advances it.
 */
const external_theme_asset_t *external_theme_asset(const menu_theme_t *theme);
/** Decode the package now, as external_theme_asset() would over several frames. */
const external_theme_asset_t *external_theme_asset_wait(const menu_theme_t *theme);
/** Advance a background package decode for a slice of time. */
void external_theme_poll(void);
void external_theme_invalidate(void);

#endif
