/**
 * @file background.c
 * @brief Implementation of the background UI component.
 * @ingroup ui_components
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "../ui_components.h"
#include "../external_theme.h"
#include "../fonts.h"
#include "constants.h"
#include "utils/fs.h"

#define CACHE_METADATA_MAGIC    (0x424B4731)

/**
 * @brief Structure representing the background component.
 */
typedef struct {
    char *cache_location;      /**< Path to the cache file location. */
    int image_hue;            /**< Hue already applied to the loaded image. */
    surface_t *image;          /**< Pointer to the loaded image surface. */
    rspq_block_t *image_display_list; /**< Display list for rendering the image. */
    sprite_t *border_atlas;    /**< Shared nine-patch style selected by the theme. */
    char border_key[32];       /**< Border source now loaded: a sprite name, or "external". */
    external_theme_asset_t *external_asset; /**< Decoded single-PNG package. */
    const menu_theme_t *theme; /**< Theme whose atlases are currently loaded. */
    float layer0_u, layer0_v;  /**< Time-integrated layer-zero UV origin. */
    float layer1_u, layer1_v;  /**< Time-integrated layer-one UV origin. */
    float layer0_animation;    /**< Layer-zero animation time in 60 Hz ticks. */
    float layer1_animation;    /**< Layer-one animation time in 60 Hz ticks. */
    float parameter_animation; /**< Parameter animation time in 60 Hz ticks. */
    float texture_angle;       /**< Shared dual-texture rotation in radians. */
    float layer0_scale, layer1_scale; /**< Previous scales for centered zoom. */
    float gradient_phase;      /**< Elapsed-time phase for vertex gradients. */
    float static_x, static_y;  /**< Static image origin; within one wrap period when panning. */
    float palette_ticks;       /**< Color-cycling clock for a CI8 static image. */
    long long last_ticks;      /**< Timestamp of the previous background draw. */
} component_background_t;

/**
 * @brief Structure for background image cache metadata.
 */
typedef struct {
    uint32_t magic;    /**< Magic number for cache validation. */
    uint32_t width;    /**< Image width in pixels. */
    uint32_t height;   /**< Image height in pixels. */
    uint32_t size;     /**< Image buffer size in bytes. */
} cache_metadata_t;

static component_background_t *background = NULL;
static bool panel_integrated;

#define THEME_PATCH_SIZE   8
#define THEME_PATCH_SHEET  (THEME_PATCH_SIZE * 3)
#define THEME_REFERENCE_FPS 60.0f
#define THEME_MOTION_SCALE  8.0f

static int relative_scale_log(float base_scale, float overlay_scale) {
    float ratio = overlay_scale / base_scale;
    int scale_log = 0;
    while (ratio >= 1.41421356f && scale_log < 10) {
        ratio *= 0.5f;
        scale_log++;
    }
    while (ratio < 0.70710678f && scale_log > -5) {
        ratio *= 2.0f;
        scale_log--;
    }
    /* Tile scale_log describes rendered texture size, while descriptor scale
     * describes texel frequency, so their signs are opposite. */
    return -scale_log;
}

static float wrap_texture_offset(float value, int period) {
    value = fmodf(value, period);
    return value < 0.0f ? value + period : value;
}

static surface_t animated_surface(surface_t *frames, surface_t blend,
                                  const menu_theme_animation_t *animation,
                                  float ticks, uint8_t forced_frame) {
    if (forced_frame != THEME_FRAME_AUTOMATIC) return frames[forced_frame];
    int current = theme_animation_frame(animation, ticks);
    if (!(animation->flags & THEME_ANIMATION_INTERPOLATE)) return frames[current];
    rspq_wait();
    int next = theme_animation_frame(animation, ticks + animation->frame_ticks);
    int weight = (int)(fmodf(ticks, animation->frame_ticks)
        * 256.0f / animation->frame_ticks);
    int inverse = 256 - weight;
    surface_t a = frames[current], b = frames[next];
    for (int y = 0; y < blend.height; y++) {
        const uint16_t *row_a = (const uint16_t *)((const uint8_t *)a.buffer + y * a.stride);
        const uint16_t *row_b = (const uint16_t *)((const uint8_t *)b.buffer + y * b.stride);
        uint16_t *row_out = (uint16_t *)((uint8_t *)blend.buffer + y * blend.stride);
        for (int x = 0; x < blend.width; x++) {
            uint16_t ca = row_a[x], cb = row_b[x];
            int r = ((((ca >> 11) & 31) * inverse + ((cb >> 11) & 31) * weight + 128) >> 8);
            int g = ((((ca >> 6) & 31) * inverse + ((cb >> 6) & 31) * weight + 128) >> 8);
            int blue = ((((ca >> 1) & 31) * inverse + ((cb >> 1) & 31) * weight + 128) >> 8);
            int alpha = ((ca & 1) * inverse + (cb & 1) * weight) >= 128;
            row_out[x] = (r << 11) | (g << 6) | (blue << 1) | alpha;
        }
    }
    return blend;
}

static void sprite_deferred_free(void *arg) {
    sprite_free((sprite_t *)arg);
}

/* Load the border source the theme now needs, if it differs from the loaded one. */
static void refresh_border(component_background_t *c, const menu_theme_t *theme) {
    bool external = c->external_asset && c->external_asset->border.buffer;
    /* A package whose embedded border is still decoding or failed to decode uses Plain. */
    const char *key = external ? "external"
        : theme_has_package_border(theme) ? "border-plain" : theme_border_descriptor(theme)->asset_name;
    if (!strcmp(key, c->border_key) && (external || c->border_atlas)) return;
    if (c->border_atlas) {
        rdpq_call_deferred(sprite_deferred_free, c->border_atlas);
        c->border_atlas = NULL;
    }
    snprintf(c->border_key, sizeof(c->border_key), "%s", key);
    if (!external) {
        char path[96];
        snprintf(path, sizeof(path), "rom:/menu/sprites/theme-%s.sprite", key);
        c->border_atlas = sprite_load(path);
    }
}

static void load_theme_assets(component_background_t *c) {
    const menu_theme_t *theme = theme_selected();
    /* NULL while the package decodes; see external_asset_poll(). */
    external_theme_asset_t *asset = (external_theme_asset_t *)external_theme_asset(theme);
    /* Editing the current theme (Custom colors) must not restart its animations. */
    if (c->theme == theme && asset == c->external_asset && (asset || !theme->package)) {
        refresh_border(c, theme);
        return;
    }
    c->external_asset = asset;
    refresh_border(c, theme);
    c->theme = theme;
    c->layer0_u = c->layer1_u = theme->texture_origin_u;
    c->layer0_v = c->layer1_v = theme->texture_origin_v;
    c->layer0_animation = c->layer1_animation = 0.0f;
    c->parameter_animation = 0.0f;
    c->texture_angle = 0.0f;
    c->layer0_scale = theme->layer0_scale;
    c->layer1_scale = theme->layer1_scale;
    c->gradient_phase = 0.0f;
    c->palette_ticks = 0.0f;
    c->static_x = c->static_y = 0.0f;
    const menu_theme_static_layout_t *layout = theme->package ? &theme->package->static_layout : NULL;
    if (layout && layout->width) {
        /* Start centered; a panned axis keeps its origin within one period. */
        float scale = fmaxf((float)DISPLAY_WIDTH / layout->width, (float)DISPLAY_HEIGHT / layout->height);
        float width = layout->width * scale, height = layout->height * scale;
        c->static_x = (DISPLAY_WIDTH - width) * 0.5f;
        c->static_y = (DISPLAY_HEIGHT - height) * 0.5f;
        if (layout->pan_x) c->static_x = wrap_texture_offset(c->static_x, width);
        if (layout->pan_y) c->static_y = wrap_texture_offset(c->static_y, height);
    }
    c->last_ticks = timer_ticks();
}

/* Pick up a package that finished decoding in the background, or drop one that
 * was invalidated under us; the decoded asset is a single static that
 * external_theme_invalidate() clears in place. Until the package is decoded the
 * theme draws its background color and the Plain border. */
static void external_asset_poll(component_background_t *c) {
    if (!c->theme || !c->theme->package) return;
    external_theme_asset_t *asset = (external_theme_asset_t *)external_theme_asset(c->theme);
    if (asset == c->external_asset) return;
    c->external_asset = asset;
    if (!asset) {
        refresh_border(c, c->theme);
        return;
    }
    if (asset->border.buffer && c->border_atlas) {
        rdpq_call_deferred(sprite_deferred_free, c->border_atlas);
        c->border_atlas = NULL;
        snprintf(c->border_key, sizeof(c->border_key), "external");
    }
}

/**
 * @brief Load background image from cache file if available.
 *
 * @param c Pointer to the background component structure.
 */
static void load_from_cache(component_background_t *c) {
    if (!c->cache_location) {
        return;
    }

    FILE *f;

    if ((f = fopen(c->cache_location, "rb")) == NULL) {
        return;
    }

    cache_metadata_t cache_metadata;

    if (fread(&cache_metadata, sizeof(cache_metadata), 1, f) != 1) {
        fclose(f);
        return;
    }

    if (cache_metadata.magic != CACHE_METADATA_MAGIC || !cache_metadata.width || !cache_metadata.height || cache_metadata.width > DISPLAY_WIDTH || cache_metadata.height > DISPLAY_HEIGHT) {
        fclose(f);
        return;
    }

    c->image = calloc(1, sizeof(surface_t));
    if (c->image == NULL) {
        fclose(f);
        return;
    }
    *c->image = surface_alloc(FMT_RGBA16, cache_metadata.width, cache_metadata.height);
    if (c->image->buffer == NULL) {
        free(c->image);
        c->image = NULL;
        fclose(f);
        return;
    }

    if (cache_metadata.size != (c->image->height * c->image->stride)) {
        surface_free(c->image);
        free(c->image);
        c->image = NULL;
        fclose(f);
        return;
    }

    if (fread(c->image->buffer, cache_metadata.size, 1, f) != 1) {
        surface_free(c->image);
        free(c->image);
        c->image = NULL;
    }

    fclose(f);
}

/**
 * @brief Save background image to cache file.
 *
 * @param c Pointer to the background component structure.
 */
static void save_to_cache(component_background_t *c) {
    if (!c->cache_location || !c->image) {
        return;
    }

    FILE *f;

    if ((f = fopen(c->cache_location, "wb")) == NULL) {
        return;
    }

    cache_metadata_t cache_metadata = {
        .magic = CACHE_METADATA_MAGIC,
        .width = c->image->width,
        .height = c->image->height,
        .size = (c->image->height * c->image->stride),
    };

    fwrite(&cache_metadata, sizeof(cache_metadata), 1, f);
    fwrite(c->image->buffer, cache_metadata.size, 1, f);

    fclose(f);
}

/**
 * @brief Prepare the background image for display (darken and center).
 *
 * @param c Pointer to the background component structure.
 */
static void prepare_background(component_background_t *c) {
    if (!c->image || c->image->width == 0 || c->image->height == 0) {
        return;
    }

    int hue = theme_custom_hue_shift();
    if (hue && surface_get_format(c->image) == FMT_RGBA16) {
        /* RGBA5551 has only 32768 colors. Convert each once, preserving alpha
         * separately, instead of performing HSV conversion for every pixel. */
        uint16_t *palette = malloc(32768 * sizeof(*palette));
        if (!palette) hue = 0;
        else {
            for (int i = 0; i < 32768; i++)
                palette[i] = theme_hue_shift_rgba16(i << 1, hue);
            for (int y = 0; y < c->image->height; y++) {
                uint16_t *row = (uint16_t *)((uint8_t *)c->image->buffer + y * c->image->stride);
                for (int x = 0; x < c->image->width; x++)
                    row[x] = palette[row[x] >> 1] | (row[x] & 1);
            }
            free(palette);
        }
    }
    c->image_hue = hue;

    // Darken the image
    rdpq_attach(c->image, NULL);
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_set_prim_color(BACKGROUND_OVERLAY_COLOR);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_fill_rectangle(0, 0, c->image->width, c->image->height);
    rdpq_mode_pop();
    rdpq_detach();

    uint16_t image_center_x = (c->image->width / 2);
    uint16_t image_center_y = (c->image->height / 2);

    // Prepare display list
    rspq_block_begin();
    rdpq_mode_push();
        if ((c->image->width != DISPLAY_WIDTH) || (c->image->height != DISPLAY_HEIGHT)) {
            rdpq_set_mode_fill(BACKGROUND_EMPTY_COLOR);
        }
        if (c->image->width != DISPLAY_WIDTH) {
            rdpq_fill_rectangle(
                0,
                DISPLAY_CENTER_Y - image_center_y,
                DISPLAY_CENTER_X - image_center_x,
                DISPLAY_CENTER_Y + image_center_y
            );
            rdpq_fill_rectangle(
                DISPLAY_CENTER_X + image_center_x - (c->image->width % 2),
                DISPLAY_CENTER_Y - image_center_y,
                DISPLAY_WIDTH,
                DISPLAY_CENTER_Y + image_center_y
            );
        }
        if (c->image->height != DISPLAY_HEIGHT) {
            rdpq_fill_rectangle(
                0,
                0,
                DISPLAY_WIDTH,
                DISPLAY_CENTER_Y - image_center_y
            );
            rdpq_fill_rectangle(
                0,
                DISPLAY_CENTER_Y + image_center_y - (c->image->height % 2),
                DISPLAY_WIDTH,
                DISPLAY_HEIGHT
            );
        }
        rdpq_set_mode_copy(false);
        rdpq_tex_blit(c->image, DISPLAY_CENTER_X - image_center_x, DISPLAY_CENTER_Y - image_center_y, NULL);
    rdpq_mode_pop();
    c->image_display_list = rspq_block_end();
}

/**
 * @brief Free the display list for the background image.
 *
 * @param arg Pointer to the display list (rspq_block_t *).
 */
static void display_list_free(void *arg) {
    rspq_block_free((rspq_block_t *) (arg));
}

static void surface_image_free(void *arg) {
    surface_t *image = (surface_t *)(arg);
    surface_free(image);
    free(image);
}

/**
 * @brief Initialize the background component and load from cache.
 *
 * @param cache_location Path to the cache file location.
 */
void ui_components_background_init(char *cache_location) {
    if (!background) {
        background = calloc(1, sizeof(component_background_t));
        background->cache_location = strdup(cache_location);
        ui_components_background_reload();
        load_theme_assets(background);
    }
}

/**
 * @brief Free the background component and its resources.
 */
void ui_components_background_free(void) {
    if (!background) return;
    ui_components_background_image_free_only();
    if (background->border_atlas) rdpq_call_deferred(sprite_deferred_free, background->border_atlas);
    free(background->cache_location);
    free(background);
    background = NULL;
}

void ui_components_background_clear(void) {
    if (!background) return;
    if (background->cache_location) remove(background->cache_location);
    ui_components_background_image_free_only();
}

/**
 * @brief Replace the background image and update cache/display list.
 *
 * @param image Pointer to the new background image surface.
 */
void ui_components_background_replace_image(surface_t *image) {
    if (!background) {
        return;
    }

    if (background->image) {
        rdpq_call_deferred(surface_image_free, background->image);
        background->image = NULL;
    }

    if (background->image_display_list) {
        rdpq_call_deferred(display_list_free, background->image_display_list);
        background->image_display_list = NULL;
    }

    background->image = image;
    save_to_cache(background);
    prepare_background(background);
    load_theme_assets(background);
}

void ui_components_background_apply_theme(void) {
    if (!background) return;
    load_theme_assets(background);
    ui_components_background_reload();
}

static float gradient_clamp(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float gradient_channel(float tl, float tr, float bl, float br, float u, float v) {
    float top = tl + (tr - tl) * u;
    float bottom = bl + (br - bl) * u;
    return top + (bottom - top) * v;
}

static void gradient_vertex(float vertex[6], const menu_theme_t *theme,
                            float x, float y, float u, float v, float phase) {
    float warp = theme->gradient_warp;
    float sample_u = gradient_clamp(u + sinf(phase + v * 4.1f) * warp);
    float sample_v = gradient_clamp(v + cosf(phase * 0.83f + u * 3.7f) * warp);

    vertex[0] = x;
    vertex[1] = y;
    vertex[2] = gradient_channel(theme->gradient_tl.r, theme->gradient_tr.r,
        theme->gradient_bl.r, theme->gradient_br.r, sample_u, sample_v) / 255.0f;
    vertex[3] = gradient_channel(theme->gradient_tl.g, theme->gradient_tr.g,
        theme->gradient_bl.g, theme->gradient_br.g, sample_u, sample_v) / 255.0f;
    vertex[4] = gradient_channel(theme->gradient_tl.b, theme->gradient_tr.b,
        theme->gradient_bl.b, theme->gradient_br.b, sample_u, sample_v) / 255.0f;
    vertex[5] = 1.0f;
}

/* Advance the gradient's phase and compute its 3x3 vertex grid. */
static void build_vertex_gradient(component_background_t *c, const menu_theme_t *theme,
                                  float elapsed_frames, float vertices[3][3][6]) {
    c->gradient_phase = fmodf(c->gradient_phase
        + theme->gradient_speed * (elapsed_frames / THEME_REFERENCE_FPS) * 6.2831853f,
        6.2831853f);

    float phase = c->gradient_phase;
    float center_x = DISPLAY_WIDTH * (0.5f
        + sinf(phase * 0.71f) * theme->gradient_warp * 0.18f);
    float center_y = DISPLAY_HEIGHT * (0.5f
        + cosf(phase * 0.89f) * theme->gradient_warp * 0.18f);
    float xs[3] = { 0.0f, center_x, DISPLAY_WIDTH };
    float ys[3] = { 0.0f, center_y, DISPLAY_HEIGHT };

    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            gradient_vertex(vertices[row][column], theme,
                xs[column], ys[row], column * 0.5f, row * 0.5f, phase);
        }
    }
}

static void draw_vertex_gradient(const menu_theme_t *theme, float vertices[3][3][6], bool darkened) {
    rdpq_mode_push();
        rdpq_set_mode_standard();
        /* On RGBA16 displays, ordered RGB dithering breaks up otherwise
         * obvious Gouraud bands. RGBA32 targets simply preserve the result. */
        rdpq_mode_dithering(DITHER_SQUARE_NONE);
        if (darkened) {
            rdpq_set_prim_color(theme->panel);
            rdpq_mode_combiner(RDPQ_COMBINER1(
                (PRIM, SHADE, PRIM_ALPHA, SHADE), (0, 0, 0, 1)));
        } else {
            rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        }
        for (int row = 0; row < 2; row++) {
            for (int column = 0; column < 2; column++) {
                float *top_left = vertices[row][column];
                float *top_right = vertices[row][column + 1];
                float *bottom_left = vertices[row + 1][column];
                float *bottom_right = vertices[row + 1][column + 1];
                rdpq_triangle(&TRIFMT_SHADE, top_left, top_right, bottom_right);
                rdpq_triangle(&TRIFMT_SHADE, top_left, bottom_right, bottom_left);
            }
        }
    rdpq_mode_pop();
}

static void draw_textured_background(const menu_theme_t *theme, float angle) {
    float scale = theme->layer0_scale;
    if (fabsf(angle) < 0.00001f) {
        rdpq_texture_rectangle_scaled(TILE0, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
            0, 0, DISPLAY_WIDTH * scale, DISPLAY_HEIGHT * scale);
        return;
    }

    float cosine = cosf(angle);
    float sine = sinf(angle);
    float center_x = DISPLAY_WIDTH * 0.5f;
    float center_y = DISPLAY_HEIGHT * 0.5f;
    float center_s = center_x * scale;
    float center_t = center_y * scale;
    float vertex[4][5];
    const float position[4][2] = {
        { 0, 0 }, { DISPLAY_WIDTH, 0 },
        { DISPLAY_WIDTH, DISPLAY_HEIGHT }, { 0, DISPLAY_HEIGHT },
    };
    for (int i = 0; i < 4; i++) {
        float x = position[i][0];
        float y = position[i][1];
        float dx = x - center_x;
        float dy = y - center_y;
        vertex[i][0] = x;
        vertex[i][1] = y;
        vertex[i][2] = center_s + (dx * cosine - dy * sine) * scale;
        vertex[i][3] = center_t + (dx * sine + dy * cosine) * scale;
        vertex[i][4] = 1.0f;
    }
    rdpq_triangle(&TRIFMT_TEX, vertex[0], vertex[1], vertex[2]);
    rdpq_triangle(&TRIFMT_TEX, vertex[0], vertex[2], vertex[3]);
}

/* Static images are filtered only when magnified; at 1:1 they are drawn on
 * whole pixels. libdragon's filtered strip loader stalls when only two rows
 * fit in TMEM, so filtered images are drawn in slices narrow enough for four. */
static void blit_static(const surface_t *image, float x, float y, float scale) {
    bool filtering = scale > 1.001f;
    if (!filtering) {
        x = floorf(x + 0.5f);
        y = floorf(y + 0.5f);
    }
    int slice = filtering ? 512 : image->width;
    for (int s0 = 0; s0 < image->width; s0 += slice) {
        rdpq_tex_blit(image, x + s0 * scale, y, &(rdpq_blitparms_t) {
            .s0 = s0, .width = image->width - s0 < slice ? image->width - s0 : slice,
            .scale_x = scale, .scale_y = scale, .filtering = filtering,
        });
    }
}

/* Type-11 static images cover the screen at a uniform scale, centered, with
 * panned axes wrapping; CI8 images use this frame's color-cycled TLUT. */
static void draw_static_background(component_background_t *c, const menu_theme_t *theme,
                                   float elapsed_frames) {
    external_theme_asset_t *asset = c->external_asset;
    surface_t *image = &asset->static_background;
    const menu_theme_static_layout_t *layout = &theme->package->static_layout;
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);
        if (surface_get_format(image) == FMT_CI8) {
            const menu_theme_static_palette_t *palette = &theme->package->static_palette;
            c->palette_ticks += elapsed_frames;
            rdpq_mode_tlut(TLUT_RGBA16);
            rdpq_tex_upload_tlut((uint16_t *)external_theme_static_tlut(asset, palette, c->palette_ticks),
                                 0, palette->count);
        }
        if (!layout->width) {
            rdpq_tex_blit(image, 0, 0, &(rdpq_blitparms_t) {
                .scale_x = (float)DISPLAY_WIDTH / image->width,
                .scale_y = (float)DISPLAY_HEIGHT / image->height,
                .filtering = true,
            });
        } else {
            float scale = fmaxf((float)DISPLAY_WIDTH / image->width, (float)DISPLAY_HEIGHT / image->height);
            float width = image->width * scale, height = image->height * scale;
            if (layout->pan_x) c->static_x = wrap_texture_offset(c->static_x + layout->pan_x * elapsed_frames, width);
            if (layout->pan_y) c->static_y = wrap_texture_offset(c->static_y + layout->pan_y * elapsed_frames, height);
            float x0 = layout->pan_x ? c->static_x - width : c->static_x;
            float y0 = layout->pan_y ? c->static_y - height : c->static_y;
            for (float y = y0; y < DISPLAY_HEIGHT; y += height) {
                for (float x = x0; x < DISPLAY_WIDTH; x += width) {
                    if (x + width > 0 && y + height > 0) blit_static(image, x, y, scale);
                    if (!layout->pan_x) break;
                }
                if (!layout->pan_y) break;
            }
        }
    rdpq_mode_pop();
}

static color_t blended_panel_color(color_t background_color, color_t panel) {
    int alpha = panel.a;
    int inverse = 255 - alpha;
    return RGBA32((panel.r * alpha + background_color.r * inverse + 127) / 255,
                  (panel.g * alpha + background_color.g * inverse + 127) / 255,
                  (panel.b * alpha + background_color.b * inverse + 127) / 255, 255);
}

static void draw_theme_background(component_background_t *c, bool sliced,
                                  int panel_x0, int panel_y0,
                                  int panel_x1, int panel_y1) {
    const menu_theme_t *theme = theme_get();
    if (c->theme != theme_selected()) {
        load_theme_assets(c);
    }
    external_asset_poll(c);

    long long now = timer_ticks();
    float elapsed_frames = 0.0f;
    if (c->last_ticks > 0 && now >= c->last_ticks) {
        elapsed_frames = (float)((double)(now - c->last_ticks)
            * THEME_REFERENCE_FPS / (double)TICKS_PER_SECOND);
    }
    c->last_ticks = now;

    if (c->external_asset && c->external_asset->parameter_frames) {
        theme_render_update(c->external_asset, &c->parameter_animation, elapsed_frames);
        /* Only color channels affect font styles. */
        if (theme->package && (theme->package->parameter_mask & ((1u << THEME_COLOR_COUNT) - 1)))
            fonts_apply_theme();
    }

    if (theme->background_mode==THEME_BACKGROUND_CUSTOM_IMAGE) {
        if (c->image_display_list) rspq_block_run(c->image_display_list);
        else rdpq_clear(theme->background);
        return;
    }

    /* A static overlay continues to the texture path, drawn over the image. */
    bool overlay = false;
    if (theme->background_mode == THEME_BACKGROUND_STATIC_IMAGE
            && c->external_asset && c->external_asset->static_background.buffer) {
        draw_static_background(c, theme, elapsed_frames);
        if (!theme->package->static_overlay) return;
        overlay = true;
        sliced = false;
    }

    const int scissors[5][4] = {
        { 0, 0, DISPLAY_WIDTH, panel_y0 },
        { 0, panel_y0, panel_x0, panel_y1 },
        { panel_x1, panel_y0, DISPLAY_WIDTH, panel_y1 },
        { 0, panel_y1, DISPLAY_WIDTH, DISPLAY_HEIGHT },
        { panel_x0, panel_y0, panel_x1, panel_y1 },
    };
    bool solid = theme->background_mode == THEME_BACKGROUND_SOLID;
    if (solid || theme->background_mode == THEME_BACKGROUND_VERTEX_GRADIENT) {
        if (!sliced) {
            if (solid) rdpq_clear(theme->background);
            else {
                float vertices[3][3][6];
                build_vertex_gradient(c, theme, elapsed_frames, vertices);
                draw_vertex_gradient(theme, vertices, false);
            }
            return;
        }
        float vertices[3][3][6];
        if (!solid) build_vertex_gradient(c, theme, elapsed_frames, vertices);
        rdpq_mode_push();
        for (int i = 0; i < 5; i++) {
            rdpq_set_scissor(scissors[i][0], scissors[i][1], scissors[i][2], scissors[i][3]);
            if (solid) {
                rdpq_set_mode_fill(i == 4
                    ? blended_panel_color(theme->background, theme->panel) : theme->background);
                rdpq_fill_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            } else draw_vertex_gradient(theme, vertices, i == 4);
        }
        rdpq_mode_pop();
        rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        panel_integrated = true;
        return;
    }
    if ((theme->background_mode != THEME_BACKGROUND_SINGLE_UV
            && theme->background_mode != THEME_BACKGROUND_DUAL_UV && !overlay) || !c->external_asset) {
        rdpq_clear(theme->background);
        return;
    }
    bool dual = theme->background_mode == THEME_BACKGROUND_DUAL_UV;

    const menu_theme_animation_t *animations = theme->package->animations;
    c->layer0_animation = theme_animation_advance(c->layer0_animation, elapsed_frames,
                                             &animations[0]);
    c->layer1_animation = theme_animation_advance(c->layer1_animation, elapsed_frames,
                                             &animations[1]);
    surface_t atlas = animated_surface(c->external_asset->layer0_frames,
        c->external_asset->layer0_blend, &animations[0], c->layer0_animation,
        c->external_asset->layer0_parameter_frame);
    bool rgba16 = surface_get_format(&atlas) == FMT_RGBA16;
    uint16_t *palette = (uint16_t *)c->external_asset->texture_palette;
    /* Descriptors use texels per nominal 60 Hz frame. Integrating real time
     * keeps motion stable when PNG decoding or I/O causes dropped frames. */
    surface_t layer1 = !dual || c->external_asset->shared_layers
        ? atlas : animated_surface(c->external_asset->layer1_frames,
            c->external_asset->layer1_blend, &animations[1], c->layer1_animation,
            c->external_asset->layer1_parameter_frame);
    if (c->external_asset->parameter_frames) {
        c->layer0_u += DISPLAY_WIDTH * 0.5f * (c->layer0_scale - theme->layer0_scale);
        c->layer0_v += DISPLAY_HEIGHT * 0.5f * (c->layer0_scale - theme->layer0_scale);
        c->layer1_u += DISPLAY_WIDTH * 0.5f * (c->layer1_scale - theme->layer1_scale);
        c->layer1_v += DISPLAY_HEIGHT * 0.5f * (c->layer1_scale - theme->layer1_scale);
        c->layer0_scale = theme->layer0_scale;
        c->layer1_scale = theme->layer1_scale;
    }
    c->layer0_u = wrap_texture_offset(c->layer0_u
        + theme->layer0_dx * THEME_MOTION_SCALE * elapsed_frames, atlas.width);
    c->layer0_v = wrap_texture_offset(c->layer0_v
        + theme->layer0_dy * THEME_MOTION_SCALE * elapsed_frames, atlas.height);
    c->layer1_u = wrap_texture_offset(c->layer1_u
        + theme->layer1_dx * THEME_MOTION_SCALE * elapsed_frames, layer1.width);
    c->layer1_v = wrap_texture_offset(c->layer1_v
        + theme->layer1_dy * THEME_MOTION_SCALE * elapsed_frames, layer1.height);
    c->texture_angle = fmodf(c->texture_angle
        + theme->texture_rotation_speed * elapsed_frames, 6.2831853f);

    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_dithering(DITHER_SQUARE_NONE);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_tlut(rgba16 ? TLUT_NONE : TLUT_RGBA16);
        if (!rgba16)
            rdpq_tex_upload_tlut(palette, 0, c->external_asset->texture_colors);

        rdpq_texparms_t p0 = {
            .s.translate = c->layer0_u,
            .s.repeats = REPEAT_INFINITE,
            .t.translate = c->layer0_v,
            .t.repeats = REPEAT_INFINITE,
        };
        int overlay_scale_log = relative_scale_log(theme->layer0_scale,
                                                    theme->layer1_scale);
        rdpq_texparms_t p1 = {
            .s.translate = c->layer1_u,
            .s.scale_log = overlay_scale_log,
            .s.repeats = REPEAT_INFINITE,
            .t.translate = c->layer1_v,
            .t.scale_log = overlay_scale_log,
            .t.repeats = REPEAT_INFINITE,
        };
        if (!dual) {
            rdpq_tex_upload_sub(TILE0, &atlas, &p0,
                0, 0, atlas.width, atlas.height);
        } else if (c->external_asset->shared_layers) {
            rdpq_tex_multi_begin();
                rdpq_tex_upload_sub(TILE0, &atlas, &p0, 0, 0, atlas.width, atlas.height);
                rdpq_tex_reuse_sub(TILE1, &p1, 0, 0, atlas.width, atlas.height);
            rdpq_tex_multi_end();
        } else {
            rdpq_tex_multi_begin();
                rdpq_tex_upload_sub(TILE0, &atlas, &p0, 0, 0, atlas.width, atlas.height);
                rdpq_tex_upload_sub(TILE1, &layer1, &p1, 0, 0, layer1.width, layer1.height);
            rdpq_tex_multi_end();
        }

        rdpq_set_prim_color(RGBA32(255, 255, 255, theme->layer0_alpha));
        if (dual) {
            rdpq_set_env_color(RGBA32(theme->background.r, theme->background.g,
                                      theme->background.b, theme->layer1_alpha));
            /* Cycle zero composites the base texture over the theme color and
             * computes overlay alpha. Cycle one mixes TEX1 over that result.
             * The framebuffer is written exactly once. */
            rdpq_mode_combiner(RDPQ_COMBINER2(
                (TEX0, ENV, PRIM_ALPHA, ENV), (TEX1, 0, ENV, 0),
                (TEX1, COMBINED, COMBINED_ALPHA, COMBINED), (0, 0, 0, 1)));
        } else {
            rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
            /* An overlay blends by texel alpha × layer alpha over the image. */
            if (overlay) rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        }
        if (sliced) {
            for (int i = 0; i < 5; i++) {
                rdpq_set_scissor(scissors[i][0], scissors[i][1],
                                 scissors[i][2], scissors[i][3]);
                if (i == 4) {
                    /* Fold panel darkening into the same primitive without a
                     * framebuffer read: panel*a + texture*(1-a). */
                    rdpq_set_fog_color(theme->panel);
                    rdpq_mode_blender(RDPQ_BLENDER(
                        (FOG_RGB, FOG_ALPHA, IN_RGB, INV_MUX_ALPHA)));
                }
                draw_textured_background(theme, c->texture_angle);
            }
            rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        } else {
            draw_textured_background(theme, c->texture_angle);
        }
    rdpq_mode_pop();
    panel_integrated = sliced;
}

static void draw_patch(int x0, int y0, int x1, int y1, int column, int row) {
    int s0 = column * THEME_PATCH_SIZE;
    int t0 = row * THEME_PATCH_SIZE;
    rdpq_texture_rectangle_scaled(TILE0, x0, y0, x1, y1,
        s0, t0, s0 + THEME_PATCH_SIZE, t0 + THEME_PATCH_SIZE);
}

static void draw_patch_horizontal(int x0, int x1, int y0, int y1, int column, int row) {
    int source_s = column * THEME_PATCH_SIZE;
    int source_t = row * THEME_PATCH_SIZE;
    for (int x = x0; x < x1; x += THEME_PATCH_SIZE) {
        int width = x1 - x;
        if (width > THEME_PATCH_SIZE) width = THEME_PATCH_SIZE;
        rdpq_texture_rectangle_scaled(TILE0, x, y0, x + width, y1,
            source_s, source_t, source_s + width, source_t + THEME_PATCH_SIZE);
    }
}

static void draw_patch_vertical(int y0, int y1, int x0, int x1, int column, int row) {
    int source_s = column * THEME_PATCH_SIZE;
    int source_t = row * THEME_PATCH_SIZE;
    for (int y = y0; y < y1; y += THEME_PATCH_SIZE) {
        int height = y1 - y;
        if (height > THEME_PATCH_SIZE) height = THEME_PATCH_SIZE;
        rdpq_texture_rectangle_scaled(TILE0, x0, y, x1, y + height,
            source_s, source_t, source_s + THEME_PATCH_SIZE, source_t + height);
    }
}

static bool theme_atlas_loaded(void) {
    return background && (background->border_atlas || background->external_asset);
}

/* Fill the inside of Soft's rounded corners without painting the outer corner
 * pixels that the patch alpha exposes. */
static void fill_soft_corners(int x0, int y0, int x1, int y1, bool bottom) {
    const int p = THEME_PATCH_SIZE;
    const int curve = 3;
    rdpq_fill_rectangle(x0 + curve, y0 + 1, x0 + p, y0 + p);
    rdpq_fill_rectangle(x0 + 1, y0 + curve, x0 + p, y0 + p);
    rdpq_fill_rectangle(x1 - p, y0 + 1, x1 - curve, y0 + p);
    rdpq_fill_rectangle(x1 - p, y0 + curve, x1 - 1, y0 + p);
    if (bottom) {
        rdpq_fill_rectangle(x0 + curve, y1 - p, x0 + p, y1 - 1);
        rdpq_fill_rectangle(x0 + 1, y1 - p, x0 + p, y1 - curve);
        rdpq_fill_rectangle(x1 - p, y1 - p, x1 - curve, y1 - 1);
        rdpq_fill_rectangle(x1 - p, y1 - p, x1 - 1, y1 - curve);
    }
}

static void theme_patch_begin(color_t tint) {
    bool external_border = background->external_asset
        && background->external_asset->border.buffer;
    surface_t atlas = external_border ? background->external_asset->border
                                      : sprite_get_pixels(background->border_atlas);
    uint16_t *palette = external_border ? (uint16_t *)background->external_asset->border_palette
                                        : sprite_get_palette(background->border_atlas);
    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_tlut(TLUT_RGBA16);
    rdpq_tex_upload_tlut(palette, 0, external_border ? background->external_asset->border_colors
                                                    : sprite_get_palette_used_colors(background->border_atlas));
    rdpq_tex_upload_sub(TILE0, &atlas, NULL,
        0, 0, THEME_PATCH_SHEET, THEME_PATCH_SHEET);
    rdpq_set_prim_color(tint);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
}

static bool draw_theme_patch(int x0, int y0, int x1, int y1, bool draw_center) {
    if (!theme_atlas_loaded()) return false;

    int p = THEME_PATCH_SIZE;

    rdpq_mode_push();
        if (draw_center) {
            rdpq_set_mode_fill(theme_get()->panel);
            /* Fill the center and edge spans, but leave the four patch-sized
             * corner blocks untouched so rounded/ornamental corner alpha can
             * reveal the background beneath the dialog. */
            rdpq_fill_rectangle(x0 + p, y0, x1 - p, y1);
            rdpq_fill_rectangle(x0, y0 + p, x1, y1 - p);
            if (theme_get()->border_style == THEME_BORDER_SOFT) fill_soft_corners(x0, y0, x1, y1, true);
        }
        theme_patch_begin(theme_get()->border);
        draw_patch_horizontal(x0 + p, x1 - p, y0, y0 + p, 1, 0);
        draw_patch_horizontal(x0 + p, x1 - p, y1 - p, y1, 1, 2);
        draw_patch_vertical(y0 + p, y1 - p, x0, x0 + p, 0, 1);
        draw_patch_vertical(y0 + p, y1 - p, x1 - p, x1, 2, 1);
        draw_patch(x0, y0, x0 + p, y0 + p, 0, 0);
        draw_patch(x1 - p, y0, x1, y0 + p, 2, 0);
        draw_patch(x0, y1 - p, x0 + p, y1, 0, 2);
        draw_patch(x1 - p, y1 - p, x1, y1, 2, 2);
    rdpq_mode_pop();
    return true;
}

bool ui_components_theme_dialog_draw(int x0, int y0, int x1, int y1) {
    return draw_theme_patch(x0, y0, x1, y1, true);
}

bool ui_components_theme_frame_draw(int x0, int y0, int x1, int y1) {
    return draw_theme_patch(x0, y0, x1, y1, false);
}

bool ui_components_theme_tab_draw(int x0, int y0, int x1, int y1,
                                  int extend_left, int extend_right,
                                  color_t fill, color_t tint) {
    if (!theme_atlas_loaded()) return false;

    int p = THEME_PATCH_SIZE;
    int patch_extra = extend_left > extend_right ? extend_left : extend_right;
    int fill_x0 = x0 - extend_left;
    int fill_x1 = x1 + extend_right;
    int left_patch_x0 = fill_x0;
    int left_patch_x1 = x0 + p + (extend_left == 0 ? patch_extra : 0);
    int right_patch_x0 = x1 - p - (extend_right == 0 ? patch_extra : 0);
    int right_patch_x1 = fill_x1;
    rdpq_mode_push();
        rdpq_set_mode_fill(fill);
        if (theme_get()->border_style == THEME_BORDER_SOFT) {
            rdpq_fill_rectangle(fill_x0 + p, y0, fill_x1 - p, y1);
            rdpq_fill_rectangle(fill_x0, y0 + p, fill_x1, y1);
            fill_soft_corners(fill_x0, y0, fill_x1, y1, false);
        } else {
            rdpq_fill_rectangle(fill_x0, y0, fill_x1, y1);
        }
        theme_patch_begin(tint);
        draw_patch_horizontal(left_patch_x1, right_patch_x0, y0, y0 + p, 1, 0);
        draw_patch_vertical(y0 + p, y1, left_patch_x0, left_patch_x1, 0, 1);
        draw_patch_vertical(y0 + p, y1, right_patch_x0, right_patch_x1, 2, 1);
        draw_patch(left_patch_x0, y0, left_patch_x1, y0 + p, 0, 0);
        draw_patch(right_patch_x0, y0, right_patch_x1, y0 + p, 2, 0);
    rdpq_mode_pop();
    return true;
}

void ui_components_background_draw(void) {
    panel_integrated = false;
    if (background) draw_theme_background(background, false, 0, 0, 0, 0);
    else rdpq_clear(BACKGROUND_EMPTY_COLOR);
}

bool ui_components_background_panel_integrated(void) {
    return panel_integrated;
}

void ui_components_background_draw_layout(bool tabbed) {
    panel_integrated = false;
    if (!background) {
        ui_components_background_draw();
        return;
    }
    draw_theme_background(background, true, VISIBLE_AREA_X0,
        VISIBLE_AREA_Y0 + (tabbed ? TAB_HEIGHT : 0), VISIBLE_AREA_X1, VISIBLE_AREA_Y1);
}

surface_t *ui_components_background_get_image(void) {
    return background ? background->image : NULL;
}

void ui_components_background_reload(void) {
    if (theme_get()->background_mode != THEME_BACKGROUND_CUSTOM_IMAGE) {
        ui_components_background_image_free_only();
        return;
    }
    if (!background || !background->cache_location) return;
    if (background->image && background->image_hue == theme_custom_hue_shift()) return;
    ui_components_background_image_free_only();
    load_from_cache(background);
    prepare_background(background);
}

void ui_components_background_image_free_only(void) {
    if (!background || (!background->image && !background->image_display_list)) {
        return;
    }
    // Free image and display list, keep struct+cache_location so it can be reloaded
    rspq_wait();
    if (background->image) {
        surface_free(background->image);
        free(background->image);
        background->image = NULL;
    }
    if (background->image_display_list) {
        rspq_block_free(background->image_display_list);
        background->image_display_list = NULL;
    }
}
