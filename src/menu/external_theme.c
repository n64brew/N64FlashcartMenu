#include "external_theme.h"

#include <dir.h>
#include <math.h>
#include <miniz.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <libspng/spng/spng.h>

#define THEME_HEADER_WIDTH 64
#define THEME_MAX_WIDTH 640
#define THEME_MAX_HEIGHT 484
/* Packages with a type-11 static layout may be larger; see static_valid(). */
#define THEME_LAYOUT_MAX_WIDTH 2048
#define THEME_LAYOUT_MAX_HEIGHT 1024
#define THEME_HEADER_PREFIX 16
#define THEME_VERSION 1
#define THEME_INDEX_MAGIC 0x54484D45U
#define THEME_INDEX_VERSION 4

enum { TLV_END = 0, TLV_NAME = 1, TLV_FAMILY = 2, TLV_COLORS = 3,
       TLV_BACKGROUND = 4, TLV_REGIONS = 5, TLV_TAB = 6,
       TLV_INTERNAL_BORDER = 7, TLV_ANIMATION = 8,
       TLV_PARAMETER_ANIMATION = 9, TLV_TEXTURE_ORIGIN = 10,
       TLV_STATIC_LAYOUT = 11, TLV_STATIC_PALETTE = 12 };

#define PARAMETER_CHANNEL_MASK (THEME_PARAM_BIT(THEME_PARAM_COUNT) - 1)

typedef external_theme_parameter_frame_t parameter_frame_t;

/* A row-streamed PNG. Rows [0, rows) are kept as RGBA8, cropped to `stride`
 * columns, in room for `capacity` rows; a static image is decoded afterwards
 * straight from `row`, so large images never need a full-size RGBA8 copy. */
typedef struct {
    uint8_t *rgba;
    int width, height;      /* Full PNG size. */
    int stride, rows;       /* Kept pixels. */
    int capacity;           /* Rows reserved for png_keep_rows(). */
    spng_ctx *ctx;
    FILE *file;
    uint8_t *row;           /* One full decoded row. */
} decoded_png_t;

static external_theme_asset_t asset;
static const menu_theme_t *asset_theme;
static bool themes_initialized;
static int decode_hue_shift;

static uint16_t read_u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t read_u32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float read_float(const uint8_t *p) {
    uint32_t bits = read_u32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool float_in_range(float value, float minimum, float maximum) {
    return isfinite(value) && value >= minimum && value <= maximum;
}

static int animation_frame_count(const menu_theme_animation_t *animation) {
    return animation->frame_count > 1 ? animation->frame_count : 1;
}

static bool text_valid(const uint8_t *value, int length) {
    if (length <= 0 || length > 63) return false;
    /* Names are also persisted as INI values. Reject line/control injection. */
    for (int i = 0; i < length; i++)
        if (value[i] < 0x20 || value[i] == 0x7f) return false;
    return true;
}

/* Valid range of each animatable float, in parameter channel order. */
static const float parameter_limits[THEME_PARAM_FLOAT_COUNT][2] = {
    {0, 2}, {0, 1}, {0.015625f, 16}, {0.015625f, 16},
    {-4, 4}, {-4, 4}, {-4, 4}, {-4, 4}, {-0.25f, 0.25f},
};

static bool parameter_valid(int index, float value) {
    return float_in_range(value, parameter_limits[index][0], parameter_limits[index][1]);
}

/* Static float parameters that each background mode uses. */
static uint32_t mode_parameters(menu_theme_background_mode_t mode) {
    uint32_t single = THEME_PARAM_BIT(THEME_PARAM_LAYER0_SCALE) | THEME_PARAM_BIT(THEME_PARAM_LAYER0_DX)
        | THEME_PARAM_BIT(THEME_PARAM_LAYER0_DY) | THEME_PARAM_BIT(THEME_PARAM_ROTATION_SPEED);
    switch (mode) {
    case THEME_BACKGROUND_VERTEX_GRADIENT:
        return THEME_PARAM_BIT(THEME_PARAM_GRADIENT_SPEED) | THEME_PARAM_BIT(THEME_PARAM_GRADIENT_WARP);
    case THEME_BACKGROUND_SINGLE_UV: return single;
    case THEME_BACKGROUND_DUAL_UV:
        return single | THEME_PARAM_BIT(THEME_PARAM_LAYER1_SCALE)
            | THEME_PARAM_BIT(THEME_PARAM_LAYER1_DX) | THEME_PARAM_BIT(THEME_PARAM_LAYER1_DY);
    default: return 0;
    }
}

static bool parameters_valid(const menu_theme_t *theme, bool static_overlay) {
    if ((theme->background_mode == THEME_BACKGROUND_SINGLE_UV || theme->background_mode == THEME_BACKGROUND_DUAL_UV
            || static_overlay)
            && (!float_in_range(theme->texture_origin_u, -64.0f, 64.0f)
                || !float_in_range(theme->texture_origin_v, -64.0f, 64.0f))) return false;
    /* A static overlay uses the single-texture fields. */
    uint32_t used = mode_parameters(static_overlay ? THEME_BACKGROUND_SINGLE_UV : theme->background_mode);
    for (int i = 0; i < THEME_PARAM_FLOAT_COUNT; i++)
        if ((used & THEME_PARAM_BIT(THEME_PARAM_GRADIENT_SPEED + i))
                && !parameter_valid(i, theme->parameters[i])) return false;
    return true;
}

static bool parameter_frame_valid(const parameter_frame_t *frame,
                                  const menu_theme_animation_t animations[2]) {
    for (int i = 0; i < THEME_PARAM_FLOAT_COUNT; i++)
        if (!parameter_valid(i, frame->parameters[i])) return false;
    for (int layer = 0; layer < 2; layer++) {
        uint8_t selector = frame->layer_frame[layer];
        if (selector != THEME_FRAME_AUTOMATIC && selector >= animation_frame_count(&animations[layer]))
            return false;
    }
    return true;
}

static int parameter_channel_bytes(int channel) {
    return channel < THEME_PARAM_LAYER0_ALPHA ? 4 : 1;
}

static int masked_parameter_bytes(uint32_t mask) {
    int bytes = 0;
    for (int channel = 0; channel < THEME_PARAM_COUNT; channel++)
        if (mask & THEME_PARAM_BIT(channel)) bytes += parameter_channel_bytes(channel);
    return bytes;
}

/* Parameter frames pack bytes into the RGB of consecutive pixels. */
static uint8_t packed_byte(const uint8_t *row, int offset) {
    return row[(offset / 3) * 4 + offset % 3];
}

/* Masked channels come from the frame; the rest keep the theme's static values. */
static bool parameter_frame_read(const decoded_png_t *png, const menu_theme_rect_t *region,
                                 uint32_t mask, const menu_theme_t *theme, parameter_frame_t *frame) {
    memcpy(frame->colors, theme->colors, sizeof(frame->colors));
    memcpy(frame->parameters, theme->parameters, sizeof(frame->parameters));
    for (int layer = 0; layer < 2; layer++) {
        float *scale = &frame->parameters[THEME_PARAM_LAYER0_SCALE - THEME_PARAM_GRADIENT_SPEED + layer];
        if (*scale <= 0.0f) *scale = 1.0f;
        frame->layer_alpha[layer] = theme->layer_alpha[layer];
        frame->layer_frame[layer] = THEME_FRAME_AUTOMATIC;
    }
    const uint8_t *row = &png->rgba[(region->y * png->stride + region->x) * 4];
    int source = 0;
    for (int channel = 0; channel < THEME_PARAM_COUNT; channel++) {
        if (!(mask & THEME_PARAM_BIT(channel))) continue;
        uint8_t value[4];
        for (int i = 0; i < parameter_channel_bytes(channel); i++) value[i] = packed_byte(row, source++);
        if (channel < THEME_COLOR_COUNT)
            memcpy(&frame->colors[channel], value, sizeof(color_t));
        else if (channel < THEME_PARAM_LAYER0_ALPHA)
            frame->parameters[channel - THEME_PARAM_GRADIENT_SPEED] = read_float(value);
        else if (channel < THEME_PARAM_LAYER0_FRAME)
            frame->layer_alpha[channel - THEME_PARAM_LAYER0_ALPHA] = value[0];
        else
            frame->layer_frame[channel - THEME_PARAM_LAYER0_FRAME] = value[0];
    }
    for (; source < region->width * 3; source++)
        if (packed_byte(row, source) != 0) return false;
    for (int x = 0; x < region->width; x++)
        if (row[x * 4 + 3] != 255) return false;
    return true;
}

static void png_close(decoded_png_t *png) {
    if (png->ctx) spng_ctx_free(png->ctx);
    if (png->file) fclose(png->file);
    free(png->row);
    png->ctx = NULL;
    png->file = NULL;
    png->row = NULL;
}

static void png_free(decoded_png_t *png) {
    png_close(png);
    free(png->rgba);
    png->rgba = NULL;
}

static bool png_open(const char *path, decoded_png_t *png) {
    *png = (decoded_png_t) { 0 };
    if (!(png->file = fopen(path, "rb"))) return false;
    png->ctx = spng_ctx_new(SPNG_CTX_IGNORE_ADLER32);
    if (!png->ctx) { png_close(png); return false; }
    spng_set_crc_action(png->ctx, SPNG_CRC_USE, SPNG_CRC_USE);
    spng_set_image_limits(png->ctx, THEME_LAYOUT_MAX_WIDTH, THEME_LAYOUT_MAX_HEIGHT);
    /* Ancillary text/profile chunks are not part of the theme format. Bound
     * their cached and decompressed payloads independently of image dimensions. */
    spng_set_chunk_limits(png->ctx, 64 * 1024, 64 * 1024);
    struct spng_ihdr ihdr = { 0 };
    size_t size = 0;
    bool ok = spng_set_png_file(png->ctx, png->file) == SPNG_OK
        && spng_get_ihdr(png->ctx, &ihdr) == SPNG_OK
        && ihdr.interlace_method == 0
        && ihdr.width >= THEME_HEADER_WIDTH && ihdr.width <= THEME_LAYOUT_MAX_WIDTH
        && ihdr.height <= THEME_LAYOUT_MAX_HEIGHT
        && spng_decoded_image_size(png->ctx, SPNG_FMT_RGBA8, &size) == SPNG_OK
        && spng_decode_image(png->ctx, NULL, 0, SPNG_FMT_RGBA8,
                             SPNG_DECODE_PROGRESSIVE | SPNG_DECODE_TRNS) == SPNG_OK
        && (png->row = malloc(ihdr.width * 4));
    if (!ok) { png_close(png); return false; }
    png->width = ihdr.width;
    png->height = ihdr.height;
    return true;
}

/* Decode the next PNG row into png->row. */
static bool png_next_row(decoded_png_t *png) {
    int result = spng_decode_row(png->ctx, png->row, png->width * 4);
    return result == SPNG_OK || result == SPNG_EOI;
}

/* Make room to keep `rows` rows of `stride` columns; png_keep_rows() fills it. */
static bool png_reserve(decoded_png_t *png, int rows, int stride) {
    if (rows > png->height || stride > png->width || rows < png->rows || stride < png->stride) return false;
    if (stride != png->stride || rows != png->capacity) {
        uint8_t *rgba = malloc((size_t)rows * stride * 4);
        if (!rgba) return false;
        for (int y = 0; y < png->rows; y++)
            memcpy(rgba + y * stride * 4, png->rgba + y * png->stride * 4, png->stride * 4);
        free(png->rgba);
        png->rgba = rgba;
    }
    png->stride = stride;
    png->capacity = rows;
    return true;
}

/* Decode up to `count` more reserved rows. */
static bool png_keep_rows(decoded_png_t *png, int count) {
    for (; count > 0 && png->rows < png->capacity; count--, png->rows++) {
        if (!png_next_row(png)) return false;
        memcpy(png->rgba + png->rows * png->stride * 4, png->row, png->stride * 4);
    }
    return true;
}

/* Extend the kept region to `rows` rows of `stride` columns. */
static bool png_keep(decoded_png_t *png, int rows, int stride) {
    return png_reserve(png, rows, stride) && png_keep_rows(png, rows);
}

static bool rectangle_valid(const decoded_png_t *png, const menu_theme_rect_t *r, int max_w, int max_h) {
    return r->width && r->height && r->width <= max_w && r->height <= max_h
        && r->x + r->width <= png->width && r->y + r->height <= png->height;
}

static bool region_is_rgba16(const menu_theme_rect_t *r) {
    return r->width <= 32 && r->height <= 32;
}

static bool region_can_repeat(const menu_theme_rect_t *r) {
    return !(r->width & (r->width - 1)) && !(r->height & (r->height - 1));
}

static int region_tmem_bytes(const menu_theme_rect_t *r, bool rgba16) {
    int row_bytes = rgba16 ? r->width * 2 : (r->width + 1) / 2;
    return ((row_bytes + 7) & ~7) * r->height;
}

static bool animation_is_zero(const menu_theme_animation_t *animation) {
    static const menu_theme_animation_t zero;
    return memcmp(animation, &zero, sizeof(zero)) == 0;
}

static void animation_read(menu_theme_animation_t *animation, const uint8_t *value) {
    animation->frame_count = value[0];
    animation->columns = value[1];
    animation->frame_ticks = read_u16(value + 2);
    animation->flags = value[4];
}

static bool animation_region(const menu_theme_rect_t *base, const menu_theme_animation_t *animation,
                             int frame, menu_theme_rect_t *region) {
    int columns = animation->frame_count > 1 ? animation->columns : 1;
    int x = base->x + (frame % columns) * base->width;
    int y = base->y + (frame / columns) * base->height;
    if (x > 255 || y > 255) return false;
    region->x = x;
    region->y = y;
    region->width = base->width;
    region->height = base->height;
    return true;
}

static bool animation_valid(const decoded_png_t *png, const menu_theme_rect_t *base,
                            const menu_theme_animation_t *animation, int metadata_rows,
                            bool texture) {
    if (!rectangle_valid(png, base, 64, 64) || base->y < metadata_rows) return false;
    if (animation->frame_count <= 1)
        return animation->columns == 0 && animation->frame_ticks == 0 && animation->flags == 0;
    if (!animation->columns || !animation->frame_ticks || (animation->flags & 0xF0)
            || (animation->flags & THEME_ANIMATION_MODE_MASK) == 3
            || ((animation->flags & THEME_ANIMATION_INTERPOLATE)
                && texture && !region_is_rgba16(base))) return false;
    for (int frame = 0; frame < animation->frame_count; frame++) {
        menu_theme_rect_t region;
        if (!animation_region(base, animation, frame, &region)
                || !rectangle_valid(png, &region, 64, 64)
                || region.y < metadata_rows) return false;
    }
    return true;
}

static bool texture_layout_valid(menu_theme_background_mode_t mode, const menu_theme_regions_t *regions,
                                 const menu_theme_animation_t animations[2], uint32_t parameter_mask) {
    if (mode != THEME_BACKGROUND_SINGLE_UV
            && mode != THEME_BACKGROUND_DUAL_UV) return true;
    const menu_theme_rect_t *r0 = &regions->layer0;
    const menu_theme_rect_t *r1 = &regions->layer1;
    if (!region_can_repeat(r0)) return false;
    bool rgba0 = region_is_rgba16(r0);
    if (mode == THEME_BACKGROUND_SINGLE_UV)
        return region_tmem_bytes(r0, rgba0) <= (rgba0 ? 4096 : 2048);
    bool rgba1 = region_is_rgba16(r1);
    if (!region_can_repeat(r1)) return false;
    if (rgba0 != rgba1) return false;
    bool shared = memcmp(r0, r1, sizeof(*r0)) == 0
        && memcmp(&animations[0], &animations[1], sizeof(animations[0])) == 0
        && !(parameter_mask & THEME_PARAM_TEXTURE_FRAMES);
    int bytes = region_tmem_bytes(r0, rgba0)
        + (shared ? 0 : region_tmem_bytes(r1, rgba1));
    return bytes <= (rgba0 ? 4096 : 2048);
}

/* Fixed-layout records decoded from the metadata, validated together. */
typedef struct {
    menu_theme_regions_t regions;
    menu_theme_animation_t animations[2];
    menu_theme_rect_t parameter_region;
    uint32_t parameter_mask;
    menu_theme_animation_t parameter_animation;
    menu_theme_border_descriptor_t border;
    menu_theme_static_layout_t static_layout;
    menu_theme_static_palette_t static_palette;
    bool static_overlay;
    uint32_t seen;
} parsed_records_t;

static bool background_valid(const decoded_png_t *png, int rows, menu_theme_background_mode_t mode,
                             const parsed_records_t *rec) {
    const menu_theme_regions_t *regions = &rec->regions;
    const menu_theme_animation_t *animations = rec->animations;
    uint32_t seen = rec->seen;
    if (mode == THEME_BACKGROUND_STATIC_IMAGE)
        return png->height > rows && png->height - rows <= 480
            && !(seen & ((1u << TLV_REGIONS) | (1u << TLV_ANIMATION) | (1u << TLV_PARAMETER_ANIMATION)));
    if (mode != THEME_BACKGROUND_SINGLE_UV && mode != THEME_BACKGROUND_DUAL_UV)
        return !(seen & (1u << TLV_ANIMATION));
    bool dual = mode == THEME_BACKGROUND_DUAL_UV;
    return (seen & (1u << TLV_REGIONS))
        && texture_layout_valid(mode, regions, animations, rec->parameter_mask)
        && animation_valid(png, &regions->layer0, &animations[0], rows, true)
        && (dual ? animation_valid(png, &regions->layer1, &animations[1], rows, true)
                 : animation_is_zero(&animations[1]));
}

/* Exactly one border source: an internal style, or an embedded sheet with its tab geometry. */
static bool border_valid(const decoded_png_t *png, int rows, const parsed_records_t *rec) {
    const menu_theme_rect_t *border = &rec->regions.border;
    uint32_t seen = rec->seen;
    bool internal = seen & (1u << TLV_INTERNAL_BORDER);
    bool geometry = seen & (1u << TLV_TAB);
    if (!border->width && !border->height) return internal && !geometry;
    return !internal && geometry
        && border->width == THEME_BORDER_SHEET_SIZE && border->height == THEME_BORDER_SHEET_SIZE
        && rectangle_valid(png, border, THEME_BORDER_SHEET_SIZE, THEME_BORDER_SHEET_SIZE)
        && border->y >= rows;
}

/* Largest bottom (or right) edge of a region over every frame of its animation. */
static int rect_extent(const menu_theme_rect_t *base, const menu_theme_animation_t *animation, bool vertical) {
    static const menu_theme_animation_t still = { 0 };
    if (!base->width) return 0;
    if (!animation) animation = &still;
    int extent = 0;
    for (int frame = 0; frame < animation_frame_count(animation); frame++) {
        menu_theme_rect_t r;
        if (animation_region(base, animation, frame, &r)) {
            int edge = vertical ? r.y + r.height : r.x + r.width;
            if (edge > extent) extent = edge;
        }
    }
    return extent;
}

/* Columns of RGBA8 that asset decoding reads above a type-11 static image. */
static int artwork_right(const parsed_records_t *rec) {
    const menu_theme_static_palette_t *palette = &rec->static_palette;
    int right = THEME_HEADER_WIDTH;
    int candidates[] = {
        rect_extent(&rec->regions.layer0, &rec->animations[0], false),
        rect_extent(&rec->regions.layer1, &rec->animations[1], false),
        rect_extent(&rec->regions.border, NULL, false),
        rect_extent(&rec->parameter_region, &rec->parameter_animation, false),
        palette->count ? palette->x + palette->count : 0,
    };
    for (unsigned i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++)
        if (candidates[i] > right) right = candidates[i];
    return right;
}

/* Static mode. Without a type-11 layout, the original rules apply: the image is
 * everything below the metadata and nothing else is allowed. With one, the
 * image rectangle must come after all other artwork so it can be streamed. */
static bool static_valid(const decoded_png_t *png, int rows, const parsed_records_t *rec) {
    const menu_theme_regions_t *regions = &rec->regions;
    const menu_theme_animation_t *animations = rec->animations;
    uint32_t seen = rec->seen;
    bool overlay = rec->static_overlay;
    const menu_theme_static_layout_t *layout = (seen & (1u << TLV_STATIC_LAYOUT)) ? &rec->static_layout : NULL;
    const menu_theme_static_palette_t *palette = (seen & (1u << TLV_STATIC_PALETTE)) ? &rec->static_palette : NULL;
    if (!layout)
        return !overlay && !palette && png->height > rows && png->height - rows <= 480
            && !(seen & ((1u << TLV_REGIONS) | (1u << TLV_ANIMATION) | (1u << TLV_PARAMETER_ANIMATION)));
    int area = layout->width * layout->height;
    if (!layout->width || layout->width > THEME_LAYOUT_MAX_WIDTH
            || !layout->height || layout->height > THEME_LAYOUT_MAX_HEIGHT
            || layout->x + layout->width > png->width || layout->y + layout->height > png->height
            || layout->y < rows || area > (palette ? 1280 * 480 : 640 * 480)
            || !float_in_range(layout->pan_x, -8.0f, 8.0f) || !float_in_range(layout->pan_y, -8.0f, 8.0f))
        return false;
    if (overlay) {
        /* Layer 0 follows the single-texture rules; layer 1 is unused. */
        if (!(seen & (1u << TLV_REGIONS)) || regions->layer1.width || regions->layer1.height
                || !texture_layout_valid(THEME_BACKGROUND_SINGLE_UV, regions, animations, rec->parameter_mask)
                || !animation_valid(png, &regions->layer0, &animations[0], rows, true)
                || !animation_is_zero(&animations[1])) return false;
    } else if (regions->layer0.width || regions->layer0.height || regions->layer1.width
               || regions->layer1.height || (seen & (1u << TLV_ANIMATION))) {
        return false;
    }
    int above = rows;
    int bottoms[] = {
        rect_extent(&regions->layer0, &animations[0], true), rect_extent(&regions->border, NULL, true),
        (seen & (1u << TLV_PARAMETER_ANIMATION))
            ? rect_extent(&rec->parameter_region, &rec->parameter_animation, true) : 0,
        palette ? palette->y + 1 : 0,
    };
    for (unsigned i = 0; i < sizeof(bottoms) / sizeof(bottoms[0]); i++)
        if (bottoms[i] > above) above = bottoms[i];
    if (layout->y < above) return false;
    if (!palette) return true;
    if (!palette->count || palette->count > 256 || palette->y < rows
            || palette->x + palette->count > png->width || palette->y >= png->height) return false;
    uint8_t used[256] = { 0 };
    for (int i = 0; i < palette->range_count; i++) {
        const menu_theme_palette_range_t *range = &palette->ranges[i];
        if (range->length < 2 || range->start + range->length > palette->count
                || !range->step_ticks || (range->flags & ~3)) return false;
        for (int j = range->start; j < range->start + range->length; j++) {
            if (used[j]) return false;
            used[j] = 1;
        }
    }
    return true;
}

/* With `decoded`, the stream is left open for the caller with room reserved
 * for the PNG's artwork as RGBA8 (png_keep_rows() decodes it); a type-11
 * static image follows those rows in the stream. */
static bool parse_package(const char *path, menu_theme_t *theme, decoded_png_t *decoded) {
    decoded_png_t png;
    if (!png_open(path, &png)) return false;
    uint8_t header[THEME_HEADER_WIDTH * 3 * 4];
    if (!png_keep(&png, 1, THEME_HEADER_WIDTH)) { png_free(&png); return false; }
    int declared_rows = png.rgba[(5 / 3) * 4 + 5 % 3];
    if (declared_rows < 1 || declared_rows > 4 || !png_keep(&png, declared_rows, THEME_HEADER_WIDTH)) {
        png_free(&png); return false;
    }
    for (int i = 0; i < declared_rows * THEME_HEADER_WIDTH; i++) {
        if (png.rgba[i * 4 + 3] != 255) { png_free(&png); return false; }
        memcpy(&header[i * 3], &png.rgba[i * 4], 3);
    }
    if (memcmp(header, "N64T", 4) || header[4] != THEME_VERSION || header[5] != declared_rows) {
        png_free(&png); return false;
    }
    int capacity = header[5] * THEME_HEADER_WIDTH * 3;
    int header_size = read_u16(header + 6);
    if (header_size < THEME_HEADER_PREFIX || header_size > capacity
            || read_u32(header + 8) != 0
            || (uint32_t)mz_crc32(MZ_CRC32_INIT, header + 16, header_size - 16) != read_u32(header + 12)) {
        png_free(&png); return false;
    }
    menu_theme_t parsed = {
        .background_mode = THEME_BACKGROUND_SOLID,
        .layer0_alpha = 255,
        .layer1_alpha = 255,
    };
    char *name = NULL;
    char *family = NULL;
    parsed_records_t rec = { 0 };
    /* Fixed lengths and duplicate checks apply once, before record decoding.
     * Zero lengths here mark variable-size records, not empty payloads. */
    static const uint8_t record_lengths[TLV_STATIC_PALETTE + 1] = {
        [TLV_COLORS] = 64, [TLV_REGIONS] = 12, [TLV_TAB] = 3, [TLV_INTERNAL_BORDER] = 1,
        [TLV_ANIMATION] = 10, [TLV_PARAMETER_ANIMATION] = 13, [TLV_TEXTURE_ORIGIN] = 8,
        [TLV_STATIC_LAYOUT] = 16,
    };
    bool terminated = false;
    int offset = THEME_HEADER_PREFIX;
    while (offset + 3 <= header_size) {
        int type = header[offset];
        int length = read_u16(header + offset + 1);
        offset += 3;
        if (type == TLV_END) {
            terminated = length == 0 && offset == header_size;
            break;
        }
        if (length > header_size - offset) goto invalid;
        const uint8_t *value = header + offset;
        offset += length;
        if (type > TLV_STATIC_PALETTE) continue; /* Unknown records are extensible. */
        if ((rec.seen & (1u << type)) || (record_lengths[type] && length != record_lengths[type]))
            goto invalid;
        rec.seen |= 1u << type;
        switch (type) {
        case TLV_NAME:
        case TLV_FAMILY: {
            if (!text_valid(value, length)) goto invalid;
            char **text = type == TLV_NAME ? &name : &family;
            *text = calloc(length + 1, 1);
            if (!*text) goto invalid;
            memcpy(*text, value, length);
            break;
        }
        case TLV_COLORS:
            memcpy(parsed.colors, value, sizeof(parsed.colors));
            break;
        case TLV_BACKGROUND:
            if (length == 9 && value[0] == THEME_BACKGROUND_VERTEX_GRADIENT) {
                parsed.gradient_speed = read_float(value + 1);
                parsed.gradient_warp = read_float(value + 5);
            } else if (length == 18 && (value[0] == THEME_BACKGROUND_SINGLE_UV
                                        || value[0] == THEME_BACKGROUND_STATIC_IMAGE)) {
                /* A static image with this payload draws layer 0 over it. */
                rec.static_overlay = value[0] == THEME_BACKGROUND_STATIC_IMAGE;
                parsed.layer0_scale = read_float(value + 1);
                parsed.layer0_dx = read_float(value + 5);
                parsed.layer0_dy = read_float(value + 9);
                parsed.texture_rotation_speed = read_float(value + 13);
                parsed.layer0_alpha = value[17];
            } else if (length == 31 && value[0] == THEME_BACKGROUND_DUAL_UV) {
                float *fields[] = { &parsed.layer0_scale, &parsed.layer1_scale, &parsed.layer0_dx,
                    &parsed.layer0_dy, &parsed.layer1_dx, &parsed.layer1_dy, &parsed.texture_rotation_speed };
                for (int i = 0; i < 7; i++) *fields[i] = read_float(value + 1 + i * 4);
                parsed.layer0_alpha = value[29];
                parsed.layer1_alpha = value[30];
            } else if (length != 1 || value[0] != THEME_BACKGROUND_STATIC_IMAGE) goto invalid;
            parsed.background_mode = value[0];
            break;
        case TLV_REGIONS:
            memcpy(&rec.regions, value, sizeof(rec.regions));
            break;
        case TLV_TAB:
            rec.border.tab_overlap = value[0];
            rec.border.tab_join_depth = value[1];
            rec.border.selected_expansion = value[2];
            break;
        case TLV_INTERNAL_BORDER:
            if (value[0] >= THEME_BORDER_COUNT) goto invalid;
            parsed.border_style = value[0];
            break;
        case TLV_ANIMATION:
            animation_read(&rec.animations[0], value);
            animation_read(&rec.animations[1], value + 5);
            break;
        case TLV_PARAMETER_ANIMATION:
            rec.parameter_mask = read_u32(value);
            memcpy(&rec.parameter_region, value + 4, sizeof(rec.parameter_region));
            animation_read(&rec.parameter_animation, value + 8);
            if (!rec.parameter_mask || (rec.parameter_mask & ~PARAMETER_CHANNEL_MASK)
                    || rec.parameter_region.width != (masked_parameter_bytes(rec.parameter_mask) + 2) / 3)
                goto invalid;
            break;
        case TLV_TEXTURE_ORIGIN:
            parsed.texture_origin_u = read_float(value);
            parsed.texture_origin_v = read_float(value + 4);
            break;
        case TLV_STATIC_LAYOUT:
            rec.static_layout = (menu_theme_static_layout_t) {
                read_u16(value), read_u16(value + 2), read_u16(value + 4), read_u16(value + 6),
                read_float(value + 8), read_float(value + 12),
            };
            break;
        case TLV_STATIC_PALETTE:
            if (length < 7 || value[6] > THEME_PALETTE_MAX_RANGES || length != 7 + value[6] * 5) goto invalid;
            rec.static_palette.x = read_u16(value);
            rec.static_palette.y = read_u16(value + 2);
            rec.static_palette.count = read_u16(value + 4);
            rec.static_palette.range_count = value[6];
            for (int i = 0; i < value[6]; i++) {
                const uint8_t *range = value + 7 + i * 5;
                rec.static_palette.ranges[i] = (menu_theme_palette_range_t) {
                    range[0], range[1], read_u16(range + 2), range[4],
                };
            }
            break;
        }
    }
    int rows = header[5];
    bool layout = rec.seen & (1u << TLV_STATIC_LAYOUT);
    bool valid = terminated && name && family && (rec.seen & (1u << TLV_COLORS))
        && strcasecmp(name, "Custom") != 0
        && parsed.background_mode >= THEME_BACKGROUND_DUAL_UV
        && parsed.background_mode <= THEME_BACKGROUND_STATIC_IMAGE
        && (layout || (png.width <= THEME_MAX_WIDTH && png.height <= THEME_MAX_HEIGHT))
        && parameters_valid(&parsed, rec.static_overlay)
        && (parsed.background_mode == THEME_BACKGROUND_STATIC_IMAGE
            ? static_valid(&png, rows, &rec)
            : !(rec.seen & ((1u << TLV_STATIC_LAYOUT) | (1u << TLV_STATIC_PALETTE)))
              && background_valid(&png, rows, parsed.background_mode, &rec))
        && border_valid(&png, rows, &rec)
        && (!(rec.seen & (1u << TLV_PARAMETER_ANIMATION)) || (rec.parameter_animation.frame_count >= 1
            && rec.parameter_region.height == 1
            && animation_valid(&png, &rec.parameter_region, &rec.parameter_animation, rows, false)));
    if (!valid) goto invalid;
    /* Keep only what asset decoding reads as RGBA8: everything above a type-11
     * static image (cropped to the artwork's columns), or the whole PNG. */
    if (decoded) {
        int keep_rows = layout ? rec.static_layout.y : png.height;
        int keep_columns = layout ? artwork_right(&rec) : png.width;
        if (!png_reserve(&png, keep_rows, keep_columns)) goto invalid;
    }
    menu_theme_package_t *package = calloc(1, sizeof(*package));
    if (!package || !(package->path = strdup(path))) {
        free(package); free(name); free(family); png_free(&png); return false;
    }
    package->static_overlay = rec.static_overlay;
    package->static_layout = rec.static_layout;
    package->static_palette = rec.static_palette;
    package->family = family;
    package->metadata_rows = header[5];
    package->regions = rec.regions;
    memcpy(package->animations, rec.animations, sizeof(rec.animations));
    package->parameter_region = rec.parameter_region;
    package->parameter_mask = rec.parameter_mask;
    package->parameter_animation = rec.parameter_animation;
    parsed.name = name;
    parsed.package = package;
    if (rec.regions.border.width) {
        rec.border.asset_name = "external";
        package->border = rec.border;
        parsed.border_style = THEME_BORDER_PLAIN;
    }
    *theme = parsed;
    if (decoded) *decoded = png;
    else png_free(&png);
    return true;
invalid:
    free(name); free(family); png_free(&png);
    return false;
}

typedef struct {
    menu_theme_t *items;
    int count;
    int capacity;
} discovered_themes_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint16_t source_count;
} theme_index_header_t;

/* One per listed source; the name and family are stored only when valid. */
typedef struct __attribute__((packed)) {
    int64_t mtime;
    uint16_t name_length;
    uint16_t path_length;
    uint16_t family_length;
    uint8_t background_mode;
    uint8_t valid;
} theme_index_record_t;

static void descriptor_free(menu_theme_t *descriptor) {
    free((char *)descriptor->name);
    if (descriptor->package) {
        free(descriptor->package->path);
        free(descriptor->package->family);
        free(descriptor->package);
    }
}

static bool discovered_add(discovered_themes_t *themes, const menu_theme_t *descriptor) {
    for (int i = 0; i < themes->count; i++) {
        if (strcasecmp(themes->items[i].name, descriptor->name) == 0) {
            descriptor_free(&themes->items[i]);
            themes->items[i] = *descriptor;
            return true;
        }
    }
    if (themes->count == themes->capacity) {
        int capacity = themes->capacity ? themes->capacity * 2 : 16;
        menu_theme_t *items = realloc(themes->items, capacity * sizeof(items[0]));
        if (!items) return false;
        themes->items = items;
        themes->capacity = capacity;
    }
    themes->items[themes->count++] = *descriptor;
    return true;
}

typedef struct {
    external_theme_progress_t report;
    void *data;
    int done, total;
} scan_progress_t;

/* One .png found while walking the theme directories, in scan order. */
typedef struct {
    char *path;
    int64_t mtime;
    bool valid;
    menu_theme_t descriptor; /* Owned, and only meaningful, while valid. */
} theme_source_t;

typedef struct {
    theme_source_t *items;
    int count;
    int capacity;
} theme_sources_t;

static void sources_free(theme_sources_t *sources) {
    for (int i = 0; i < sources->count; i++) {
        free(sources->items[i].path);
        if (sources->items[i].valid) descriptor_free(&sources->items[i].descriptor);
    }
    free(sources->items);
    *sources = (theme_sources_t){ 0 };
}

static void sources_add(theme_sources_t *sources, const char *path) {
    if (sources->count == sources->capacity) {
        int capacity = sources->capacity ? sources->capacity * 2 : 16;
        theme_source_t *items = realloc(sources->items, capacity * sizeof(items[0]));
        if (!items) return;
        sources->items = items;
        sources->capacity = capacity;
    }
    struct stat st;
    char *copy = strdup(path);
    if (!copy) return;
    sources->items[sources->count++] = (theme_source_t){ .path = copy, .mtime = stat(path, &st) == 0 ? st.st_mtime : -1 };
}

static void list_directory(const char *path, theme_sources_t *sources) {
    dir_t entry;
    if (dir_findfirst(path, &entry) != 0) return;
    do {
        size_t length = strlen(entry.d_name);
        if (entry.d_type == DT_DIR || length < 4 || strcasecmp(entry.d_name + length - 4, ".png")) continue;
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s%s%s", path,
                 path[strlen(path) - 1] == '/' ? "" : "/", entry.d_name);
        sources_add(sources, full_path);
    } while (dir_findnext(path, &entry) == 0);
}

/* The single directory walk of a scan; later sources shadow earlier ones by name. */
static void sources_list(const char *storage_prefix, theme_sources_t *sources) {
    list_directory("rom:/themes", sources);
    if (storage_prefix && strncmp(storage_prefix, "rom:/", 5)) {
        char path[256];
        snprintf(path, sizeof(path), "%smenu/themes", storage_prefix);
        list_directory(path, sources);
    }
}

/* Decode every listed source, recording which are valid packages. */
static void sources_parse(theme_sources_t *sources, scan_progress_t *progress) {
    for (int i = 0; i < sources->count; i++) {
        theme_source_t *source = &sources->items[i];
        source->valid = parse_package(source->path, &source->descriptor, NULL);
        if (progress && progress->report) progress->report(++progress->done, progress->total, progress->data);
    }
}

/* Move the valid descriptors into `themes`, which takes ownership of them. */
static void sources_collect(theme_sources_t *sources, discovered_themes_t *themes) {
    for (int i = 0; i < sources->count; i++) {
        theme_source_t *source = &sources->items[i];
        if (!source->valid) continue;
        if (!discovered_add(themes, &source->descriptor)) descriptor_free(&source->descriptor);
        source->descriptor = (menu_theme_t){ 0 };
    }
}

static void theme_index_path(char path[512], const char *storage_prefix) {
    snprintf(path, 512, "%smenu/cache/theme.index", storage_prefix);
}

static char *index_text_read(FILE *f, int length) {
    char *text = calloc(length + 1, 1);
    if (text && length && fread(text, length, 1, f) != 1) { free(text); return NULL; }
    return text;
}

/* The index holds one record per listed source, valid or not, so any change
 * to the set of files or to a file's mtime (including a repaired invalid PNG)
 * invalidates it. Shadowed names are resolved by discovered_add() as in a scan. */
static bool theme_index_load(const char *storage_prefix, const theme_sources_t *sources,
                             discovered_themes_t *themes) {
    char index_path[512];
    theme_index_path(index_path, storage_prefix);
    FILE *f = fopen(index_path, "rb");
    if (!f) return false;
    theme_index_header_t header;
    if (fread(&header, sizeof(header), 1, f) != 1 || header.magic != THEME_INDEX_MAGIC ||
        header.version != THEME_INDEX_VERSION || header.source_count != sources->count ||
        header.count != header.source_count) {
        fclose(f);
        return false;
    }
    const menu_theme_t *fallback = theme_get_by_name("N64");
    for (int i = 0; i < header.count; i++) {
        theme_index_record_t record;
        if (fread(&record, sizeof(record), 1, f) != 1 || !record.path_length || record.path_length >= 512 || record.valid > 1) goto fail;
        if (record.valid && (!record.name_length || record.name_length > 63 ||
                !record.family_length || record.family_length > 63 ||
                record.background_mode < THEME_BACKGROUND_DUAL_UV || record.background_mode > THEME_BACKGROUND_STATIC_IMAGE))
            goto fail;
        if (!record.valid && (record.name_length || record.family_length)) goto fail;
        char *name = index_text_read(f, record.name_length);
        char *path = index_text_read(f, record.path_length);
        char *family = index_text_read(f, record.family_length);
        struct stat st;
        bool ok = name && path && family && !memchr(path, 0, record.path_length)
            && stat(path, &st) == 0 && (int64_t)st.st_mtime == record.mtime
            && (!record.valid || (text_valid((uint8_t *)name, record.name_length)
                && text_valid((uint8_t *)family, record.family_length) && strcasecmp(name, "Custom") != 0));
        menu_theme_package_t *package = ok && record.valid ? calloc(1, sizeof(*package)) : NULL;
        if (!ok || (record.valid && !package)) {
            free(name); free(path); free(family); free(package); goto fail;
        }
        if (!record.valid) { free(name); free(path); free(family); continue; }
        *package = (menu_theme_package_t){ .path = path, .family = family, .indexed_only = true };
        menu_theme_t descriptor = *fallback;
        descriptor.name = name;
        descriptor.package = package;
        descriptor.background_mode = record.background_mode;
        if (!discovered_add(themes, &descriptor)) { descriptor_free(&descriptor); goto fail; }
    }
    fclose(f);
    return true;
fail:
    fclose(f);
    for (int i = 0; i < themes->count; i++) descriptor_free(&themes->items[i]);
    free(themes->items);
    *themes = (discovered_themes_t){ 0 };
    return false;
}

static void theme_index_save(const char *storage_prefix, const theme_sources_t *sources) {
    char index_path[512];
    theme_index_path(index_path, storage_prefix);
    FILE *f = fopen(index_path, "wb");
    if (!f) return;
    theme_index_header_t header = {
        THEME_INDEX_MAGIC, THEME_INDEX_VERSION, sources->count, sources->count
    };
    bool ok = fwrite(&header, sizeof(header), 1, f) == 1;
    for (int i = 0; ok && i < sources->count; i++) {
        const theme_source_t *source = &sources->items[i];
        const menu_theme_t *theme = &source->descriptor;
        if (source->mtime < 0 || (source->valid && !theme->package)) { ok = false; break; }
        theme_index_record_t record = {
            .mtime = source->mtime,
            .name_length = source->valid ? strlen(theme->name) : 0,
            .path_length = strlen(source->path),
            .family_length = source->valid ? strlen(theme->package->family) : 0,
            .background_mode = source->valid ? theme->background_mode : 0,
            .valid = source->valid,
        };
        ok = fwrite(&record, sizeof(record), 1, f) == 1 &&
             (!source->valid || fwrite(theme->name, record.name_length, 1, f) == 1) &&
             fwrite(source->path, record.path_length, 1, f) == 1 &&
             (!source->valid || fwrite(theme->package->family, record.family_length, 1, f) == 1);
    }
    fclose(f);
    if (!ok) remove(index_path);
}

void external_themes_init(const char *storage_prefix, external_theme_progress_t progress, void *data) {
    if (themes_initialized) return;
    if (!theme_registry_init()) return;

    theme_sources_t sources = { 0 };
    discovered_themes_t themes = { 0 };
    sources_list(storage_prefix, &sources);
    if (!theme_index_load(storage_prefix, &sources, &themes)) {
        scan_progress_t scan = { progress, data, 0, sources.count };
        sources_parse(&sources, &scan);
        theme_index_save(storage_prefix, &sources);
        sources_collect(&sources, &themes);
    }
    sources_free(&sources);
    if (!theme_registry_extend(themes.items, themes.count)) {
        for (int i = 0; i < themes.count; i++) descriptor_free(&themes.items[i]);
    } else {
        themes_initialized = true;
    }
    free(themes.items);
}

bool external_theme_load_selected(const char *path, const char *name) {
    if (!path || !path[0]) return false;
    if (!theme_registry_init()) return false;
    menu_theme_t descriptor;
    if (!parse_package(path, &descriptor, NULL)) return false;
    if (name && strcasecmp(name, "Custom") == 0) {
        bool copied = theme_custom_copy_from(&descriptor);
        descriptor_free(&descriptor);
        return copied;
    }
    if (name && name[0] && strcasecmp(name, descriptor.name) != 0) {
        descriptor_free(&descriptor);
        return false;
    }
    if (!theme_registry_extend(&descriptor, 1)) {
        descriptor_free(&descriptor);
        return false;
    }
    return true;
}

bool external_theme_prepare(const menu_theme_t *theme) {
    if (!theme || !theme->package || !theme->package->indexed_only) return true;
    menu_theme_t *entry = theme_registry_entry(theme);
    if (!entry) return false;
    menu_theme_t parsed = { 0 };
    if (!parse_package(entry->package->path, &parsed, NULL) ||
        strcasecmp(entry->name, parsed.name) != 0) {
        if (parsed.package) descriptor_free(&parsed);
        return false;
    }
    descriptor_free(entry);
    *entry = parsed;
    return true;
}

/* A package decode in progress. external_theme_asset() starts one and
 * external_theme_poll() advances it in short slices from the main loop, so
 * choosing another theme abandons it instead of waiting for it to finish. */
typedef enum { JOB_NONE, JOB_OPEN, JOB_ARTWORK, JOB_STATIC, JOB_FINISH } job_stage_t;
static struct {
    job_stage_t stage;
    const menu_theme_t *theme;
    menu_theme_t parsed;    /* Valid from JOB_ARTWORK. */
    decoded_png_t png;
    int row;                /* Static image rows converted. */
    uint64_t requested;     /* When the theme was chosen. */
} job;
/* The theme whose package last failed to decode, so it is not retried every frame. */
static const menu_theme_t *failed_theme;

/* While a package decodes, frames render at about 30 Hz so the decode gets
 * most of the CPU. Decoding only starts once the cursor rests (below), and
 * a press still reaches the menu within one slice. */
#define JOB_SLICE_MS 25
#define JOB_ROWS_PER_STEP 1
/* Opening a package takes longer than a frame, so wait until the cursor rests
 * on a theme; holding a direction then scrolls as fast as it would without. */
#define JOB_SETTLE_MS 50

static void job_cancel(void) {
    if (job.stage == JOB_NONE) return;
    if (job.parsed.package) descriptor_free(&job.parsed);
    png_free(&job.png);
    memset(&job, 0, sizeof(job));
}

static void asset_free(void) {
    job_cancel();
    /* Only a finished asset is drawn, so only it can still be in use by the RDP. */
    if (asset_theme) rspq_wait();
    if (asset.layer0_frames) for (int i = 0; i < asset.layer0_frame_count; i++)
        if (asset.layer0_frames[i].buffer) surface_free(&asset.layer0_frames[i]);
    if (asset.layer1_frames) for (int i = 0; i < asset.layer1_frame_count; i++)
        if (asset.layer1_frames[i].buffer) surface_free(&asset.layer1_frames[i]);
    free(asset.layer0_frames);
    free(asset.layer1_frames);
    free(asset.parameter_frames);
    if (asset.layer0_blend.buffer) surface_free(&asset.layer0_blend);
    if (asset.layer1_blend.buffer) surface_free(&asset.layer1_blend);
    if (asset.border.buffer) surface_free(&asset.border);
    if (asset.static_background.buffer) surface_free(&asset.static_background);
    memset(&asset, 0, sizeof(asset));
    asset_theme = NULL;
}

typedef struct { uint16_t rgba16; uint32_t count; } color_count_t;
typedef struct {
    const menu_theme_rect_t *base;
    const menu_theme_animation_t *animation;
} region_sequence_t;

static uint16_t rgba16(const uint8_t *p) {
    color_t c=theme_hue_shift_color(RGBA32(p[0],p[1],p[2],p[3]),decode_hue_shift);
    return ((c.r >> 3) << 11) | ((c.g >> 3) << 6) | ((c.b >> 3) << 1) | (c.a >= 128);
}

static int palette_build(const decoded_png_t *png, const region_sequence_t *sequences, int sequence_count,
                         uint16_t palette[16]) {
    color_count_t *colors = calloc(4096, sizeof(*colors));
    if (!colors) return -1;
    for (int n = 0; n < sequence_count; n++) {
        int frames = sequences[n].animation
            ? animation_frame_count(sequences[n].animation) : 1;
        for (int frame = 0; frame < frames; frame++) {
            menu_theme_rect_t region;
            if (sequences[n].animation)
                animation_region(sequences[n].base, sequences[n].animation, frame, &region);
            else region = *sequences[n].base;
            for (int y = 0; y < region.height; y++) for (int x = 0; x < region.width; x++) {
                uint16_t value = rgba16(&png->rgba[((region.y + y) * png->stride + region.x + x) * 4]);
                int slot = ((uint32_t)value * 40503u) & 4095;
                int probes = 0;
                while (probes < 4096 && colors[slot].count && colors[slot].rgba16 != value) {
                    slot = (slot + 1) & 4095;
                    probes++;
                }
                if (probes == 4096) continue;
                if (colors[slot].count) colors[slot].count++;
                else colors[slot] = (color_count_t) { value, 1 };
            }
        }
    }
    int dense = 0;
    for (int i = 0; i < 4096; i++)
        if (colors[i].count) colors[dense++] = colors[i];
    int count = dense;
    int used = count < 16 ? count : 16;
    for (int i = 0; i < used; i++) {
        int best = i;
        for (int j = i + 1; j < count; j++)
            if (colors[j].count > colors[best].count
                    || (colors[j].count == colors[best].count
                        && colors[j].rgba16 < colors[best].rgba16)) best = j;
        if (best != i) { color_count_t t = colors[i]; colors[i] = colors[best]; colors[best] = t; }
    }
    for (int i = 0; i < used; i++) palette[i] = colors[i].rgba16;
    /* Preserve both alpha classes even when a small antialiased feature would
     * otherwise fall below the sixteen most common colors. */
    for (int alpha = 0; alpha <= 1; alpha++) {
        int best = -1;
        bool present = false;
        for (int i = 0; i < count; i++) {
            if ((colors[i].rgba16 & 1) == alpha
                    && (best < 0 || colors[i].count > colors[best].count
                        || (colors[i].count == colors[best].count
                            && colors[i].rgba16 < colors[best].rgba16))) best = i;
            if (i < used && (palette[i] & 1) == alpha) present = true;
        }
        if (!present && best >= 0 && used) palette[used - 1] = colors[best].rgba16;
    }
    free(colors);
    return used;
}

static int color_distance(uint16_t a, uint16_t b) {
    int ar=(a>>11)&31, ag=(a>>6)&31, ab=(a>>1)&31, aa=a&1;
    int br=(b>>11)&31, bg=(b>>6)&31, bb=(b>>1)&31, ba=b&1;
    if (aa != ba) return 100000;
    return (ar-br)*(ar-br) + (ag-bg)*(ag-bg) + (ab-bb)*(ab-bb);
}

static surface_t quantize(const decoded_png_t *png, const menu_theme_rect_t *r, uint16_t *palette, int colors) {
    if (colors <= 0) return (surface_t) { 0 };
    surface_t surface = surface_alloc(FMT_CI4, r->width, r->height);
    if (!surface.buffer) return surface;
    memset(surface.buffer, 0, surface.stride * surface.height);
    for (int y = 0; y < r->height; y++) for (int x = 0; x < r->width; x++) {
        uint16_t value = rgba16(&png->rgba[((r->y + y) * png->stride + r->x + x) * 4]);
        int best = 0, distance = color_distance(value, palette[0]);
        for (int i = 1; i < colors; i++) { int d = color_distance(value, palette[i]); if (d < distance) { best=i; distance=d; } }
        uint8_t *byte = (uint8_t *)surface.buffer + y * surface.stride + x / 2;
        if (x & 1) *byte = (*byte & 0xF0) | best; else *byte = (*byte & 0x0F) | (best << 4);
    }
    return surface;
}

static surface_t convert_rgba16(const decoded_png_t *png, int x0, int y0, int width, int height) {
    surface_t surface = surface_alloc(FMT_RGBA16, width, height);
    if (!surface.buffer) return surface;
    for (int y = 0; y < height; y++) {
        uint16_t *row = (uint16_t *)((uint8_t *)surface.buffer + y * surface.stride);
        for (int x = 0; x < width; x++)
            row[x] = rgba16(&png->rgba[((y0 + y) * png->stride + x0 + x) * 4]);
    }
    return surface;
}

static surface_t *convert_frames(const decoded_png_t *png, const menu_theme_rect_t *base,
                                 const menu_theme_animation_t *animation, bool direct_color,
                                 uint16_t palette[16], int colors) {
    int count = animation_frame_count(animation);
    surface_t *frames = calloc(count, sizeof(*frames));
    if (!frames) return NULL;
    for (int frame = 0; frame < count; frame++) {
        menu_theme_rect_t region;
        animation_region(base, animation, frame, &region);
        frames[frame] = direct_color ? convert_rgba16(png, region.x, region.y, region.width, region.height)
                                     : quantize(png, &region, palette, colors);
        if (!frames[frame].buffer) return frames;
    }
    return frames;
}

static bool decode_layer(const decoded_png_t *png, const menu_theme_rect_t *region,
                         const menu_theme_animation_t *animation, surface_t **frames,
                         int *count, surface_t *blend) {
    *count = animation_frame_count(animation);
    *frames = convert_frames(png, region, animation, region_is_rgba16(region),
                             asset.texture_palette, asset.texture_colors);
    if (!*frames || !(*frames)[*count - 1].buffer) return false;
    if (!(animation->flags & THEME_ANIMATION_INTERPOLATE)) return true;
    *blend = surface_alloc(FMT_RGBA16, region->width, region->height);
    return blend->buffer != NULL;
}

/* Convert static image rows [y, y + count) into `surface`. A type-11 image
 * streams from the PNG: gray index pixels into CI8 with a type-12 palette,
 * otherwise RGBA5551 colors. Without a layout, the kept rows below the
 * metadata are the image. */
static bool static_rows(decoded_png_t *png, const menu_theme_package_t *package,
                        surface_t *surface, int y, int count) {
    const menu_theme_static_layout_t *layout = &package->static_layout;
    const menu_theme_static_palette_t *palette = &package->static_palette;
    for (int end = y + count; y < end && y < surface->height; y++) {
        const uint8_t *source;
        if (layout->width) {
            if (!png_next_row(png)) return false;
            source = png->row + layout->x * 4;
        } else source = png->rgba + (package->metadata_rows + y) * png->stride * 4;
        uint8_t *row = (uint8_t *)surface->buffer + y * surface->stride;
        for (int x = 0; x < surface->width; x++, source += 4) {
            if (!palette->count) {
                ((uint16_t *)row)[x] = rgba16(source);
                continue;
            }
            if (source[0] >= palette->count || source[1] != source[0] || source[2] != source[0]
                    || source[3] != 255) return false;
            row[x] = source[0];
        }
    }
    return true;
}

const uint16_t *external_theme_static_tlut(external_theme_asset_t *asset,
                                           const menu_theme_static_palette_t *palette, float ticks) {
    /* The RDP loads a TLUT when it reaches the draw, which may be a frame or
     * two after this call, so rotate through buffers rather than reuse one. */
    asset->static_tlut_slot = (asset->static_tlut_slot + 1) % 4;
    uint16_t *tlut = asset->static_tlut[asset->static_tlut_slot];
    memcpy(tlut, asset->static_palette, palette->count * sizeof(uint16_t));
    for (int r = 0; r < palette->range_count; r++) {
        const menu_theme_palette_range_t *range = &palette->ranges[r];
        float steps = ticks / range->step_ticks;
        int step = (int)floorf(steps);
        int weight = (range->flags & THEME_PALETTE_RANGE_BLEND) ? (int)((steps - step) * 256.0f) : 0;
        int direction = (range->flags & THEME_PALETTE_RANGE_REVERSE) ? 1 : -1;
        for (int i = 0; i < range->length; i++) {
            int a = ((i + direction * step) % range->length + range->length) % range->length;
            int b = ((i + direction * (step + 1)) % range->length + range->length) % range->length;
            uint16_t ca = asset->static_palette[range->start + a];
            uint16_t cb = asset->static_palette[range->start + b];
            uint16_t value = 1;
            for (int shift = 1; shift <= 11; shift += 5) {
                int ia = (ca >> shift) & 31, ib = (cb >> shift) & 31;
                value |= ((ia * 256 + (ib - ia) * weight + 128) >> 8) << shift;
            }
            tlut[range->start + i] = value;
        }
    }
    data_cache_hit_writeback(tlut, palette->count * sizeof(uint16_t));
    return tlut;
}

/* Advance the decode by one short step. False when the package is invalid. */
static bool job_step(void) {
    const menu_theme_package_t *package = job.parsed.package;
    switch (job.stage) {
    case JOB_OPEN:
        if (!parse_package(job.theme->package->path, &job.parsed, &job.png)) return false;
        job.stage = JOB_ARTWORK;
        return true;
    case JOB_ARTWORK:
        if (!png_keep_rows(&job.png, JOB_ROWS_PER_STEP)) return false;
        if (job.png.rows < job.png.capacity) return true;
        if (job.parsed.background_mode != THEME_BACKGROUND_STATIC_IMAGE) {
            job.stage = JOB_FINISH;
            return true;
        }
        if (package->static_layout.width) {
            const menu_theme_static_palette_t *palette = &package->static_palette;
            for (int i = 0; i < palette->count; i++)
                asset.static_palette[i] = rgba16(&job.png.rgba[(palette->y * job.png.stride + palette->x + i) * 4]);
            asset.static_background = surface_alloc(palette->count ? FMT_CI8 : FMT_RGBA16,
                package->static_layout.width, package->static_layout.height);
        } else {
            asset.static_background = surface_alloc(FMT_RGBA16, job.png.width,
                job.png.height - package->metadata_rows);
        }
        if (!asset.static_background.buffer) return false;
        job.stage = JOB_STATIC;
        return true;
    case JOB_STATIC:
        if (!static_rows(&job.png, package, &asset.static_background, job.row, JOB_ROWS_PER_STEP)) return false;
        job.row += JOB_ROWS_PER_STEP;
        if (job.row >= asset.static_background.height) job.stage = JOB_FINISH;
        return true;
    case JOB_FINISH:
        break;
    default:
        return false;
    }
    /* The rest works on the kept artwork, which is small. */
    png_close(&job.png);
    const menu_theme_t *theme = job.theme;
    const menu_theme_regions_t *regions = &package->regions;
    const menu_theme_animation_t *animations = package->animations;
    const menu_theme_animation_t *parameter_animation = &package->parameter_animation;
    /* No parameter animation means use each texture animation's current frame. */
    asset.layer0_parameter_frame = asset.layer1_parameter_frame = THEME_FRAME_AUTOMATIC;
    if (package->parameter_region.width) {
        asset.parameter_frame_count = animation_frame_count(parameter_animation);
        asset.parameter_frames = malloc(asset.parameter_frame_count * sizeof(parameter_frame_t));
        for (int frame = 0; asset.parameter_frames && frame < asset.parameter_frame_count; frame++) {
            menu_theme_rect_t region;
            animation_region(&package->parameter_region, parameter_animation, frame, &region);
            parameter_frame_t *destination = &asset.parameter_frames[frame];
            if (!parameter_frame_read(&job.png, &region, package->parameter_mask, &job.parsed, destination)
                    || !parameter_frame_valid(destination, animations)) {
                free(asset.parameter_frames);
                asset.parameter_frames = NULL;
            }
        }
    }
    bool use_border = theme_has_package_border(theme);
    if (use_border) {
        const region_sequence_t border_sequence = { &regions->border, NULL };
        asset.border_colors = palette_build(&job.png, &border_sequence, 1, asset.border_palette);
        if (asset.border_colors > 0)
            asset.border = quantize(&job.png, &regions->border, asset.border_palette, asset.border_colors);
    }
    bool valid_asset = (!use_border || asset.border.buffer)
        && (job.parsed.background_mode != THEME_BACKGROUND_STATIC_IMAGE || asset.static_background.buffer)
        && (!package->parameter_region.width || asset.parameter_frames);
    if (job.parsed.background_mode == THEME_BACKGROUND_SINGLE_UV
            || job.parsed.background_mode == THEME_BACKGROUND_DUAL_UV || package->static_overlay) {
        bool dual = job.parsed.background_mode == THEME_BACKGROUND_DUAL_UV;
        asset.shared_layers = memcmp(&regions->layer0, &regions->layer1, sizeof(menu_theme_rect_t)) == 0
            && memcmp(&animations[0], &animations[1], sizeof(animations[0])) == 0
            && !(package->parameter_mask & THEME_PARAM_TEXTURE_FRAMES);
        if (!region_is_rgba16(&regions->layer0)) {
            const region_sequence_t sequences[] = {
                { &regions->layer0, &animations[0] }, { &regions->layer1, &animations[1] }
            };
            asset.texture_colors = palette_build(&job.png, sequences, dual ? 2 : 1, asset.texture_palette);
        }
        valid_asset = valid_asset && decode_layer(&job.png, &regions->layer0, &animations[0],
            &asset.layer0_frames, &asset.layer0_frame_count, &asset.layer0_blend);
        if (dual && !asset.shared_layers)
            valid_asset = valid_asset && decode_layer(&job.png, &regions->layer1, &animations[1],
                &asset.layer1_frames, &asset.layer1_frame_count, &asset.layer1_blend);
    }
    job_cancel();
    if (!valid_asset) return false;
    /* TLUT uploads DMA from RDRAM; these CPU-built palettes are cached, unlike
     * surface_alloc pixel buffers. Write back before exposing them to the RDP. */
    data_cache_hit_writeback(asset.texture_palette, sizeof(asset.texture_palette));
    data_cache_hit_writeback(asset.border_palette, sizeof(asset.border_palette));
    asset_theme = theme;
    return true;
}

const external_theme_asset_t *external_theme_asset(const menu_theme_t *theme) {
    if (!theme || !theme->package) return NULL;
    if (asset_theme == theme) return &asset;
    if ((job.stage != JOB_NONE && job.theme == theme) || failed_theme == theme) return NULL;
    asset_free();
    failed_theme = NULL;
    decode_hue_shift = theme_is_custom(theme) ? theme_custom_hue_shift() : 0;
    job.theme = theme;
    job.stage = JOB_OPEN;
    job.requested = get_ticks();
    return NULL;
}

const external_theme_asset_t *external_theme_asset_wait(const menu_theme_t *theme) {
    const external_theme_asset_t *loaded = external_theme_asset(theme);
    /* Nothing is being scrolled past, so start without the settling delay. */
    if (job.stage == JOB_OPEN && job.theme == theme) job.requested -= TICKS_FROM_MS(JOB_SETTLE_MS);
    while (!loaded && job.stage != JOB_NONE && job.theme == theme) {
        external_theme_poll();
        loaded = external_theme_asset(theme);
    }
    return loaded;
}

void external_theme_poll(void) {
    if (job.stage == JOB_NONE
            || (job.stage == JOB_OPEN && get_ticks() - job.requested < TICKS_FROM_MS(JOB_SETTLE_MS))) return;
    uint64_t deadline = get_ticks() + TICKS_FROM_MS(JOB_SLICE_MS);
    while (job.stage != JOB_NONE && get_ticks() < deadline) {
        const menu_theme_t *theme = job.theme;
        if (!job_step()) {
            asset_free();
            failed_theme = theme;
        }
    }
}

static int byte_lerp(int a, int b, float weight) {
    return (int)floorf(a + (b - a) * weight + 0.5f);
}

void external_theme_animate(menu_theme_t *theme, external_theme_asset_t *asset,
                            float *ticks, float elapsed_ticks) {
    if (!theme || !theme->package || !asset || !asset->parameter_frames) return;
    const menu_theme_animation_t *animation = &theme->package->parameter_animation;
    *ticks = theme_animation_advance(*ticks, elapsed_ticks, animation);
    int current = theme_animation_frame(animation, *ticks);
    int next = theme_animation_frame(animation, *ticks + animation->frame_ticks);
    float weight = 0.0f;
    if ((animation->flags & THEME_ANIMATION_INTERPOLATE) && animation->frame_count > 1)
        weight = fmodf(*ticks, animation->frame_ticks) / animation->frame_ticks;
    const parameter_frame_t *a = &asset->parameter_frames[current];
    const parameter_frame_t *b = &asset->parameter_frames[next];
    uint32_t mask = theme->package->parameter_mask;
    for (int color = 0; color < THEME_COLOR_COUNT; color++) {
        if (!(mask & THEME_PARAM_BIT(color))) continue;
        color_t ca = a->colors[color], cb = b->colors[color];
        color_t value = RGBA32(byte_lerp(ca.r, cb.r, weight), byte_lerp(ca.g, cb.g, weight),
                               byte_lerp(ca.b, cb.b, weight), byte_lerp(ca.a, cb.a, weight));
        theme->colors[color] = theme_is_custom(theme)
            ? theme_hue_shift_color(value, theme_custom_hue_shift()) : value;
    }
    for (int i = 0; i < THEME_PARAM_FLOAT_COUNT; i++) {
        int channel = THEME_PARAM_GRADIENT_SPEED + i;
        if (!(mask & THEME_PARAM_BIT(channel))) continue;
        float av = a->parameters[i], bv = b->parameters[i];
        /* Scales interpolate logarithmically so zooming appears uniform. */
        theme->parameters[i] = channel == THEME_PARAM_LAYER0_SCALE || channel == THEME_PARAM_LAYER1_SCALE
            ? expf(logf(av) + (logf(bv) - logf(av)) * weight)
            : av + (bv - av) * weight;
    }
    for (int layer = 0; layer < 2; layer++)
        if (mask & THEME_PARAM_BIT(THEME_PARAM_LAYER0_ALPHA + layer))
            theme->layer_alpha[layer] = byte_lerp(a->layer_alpha[layer], b->layer_alpha[layer], weight);
    const parameter_frame_t *selector = weight < 0.5f ? a : b;
    asset->layer0_parameter_frame = mask & THEME_PARAM_BIT(THEME_PARAM_LAYER0_FRAME)
        ? selector->layer_frame[0] : THEME_FRAME_AUTOMATIC;
    asset->layer1_parameter_frame = mask & THEME_PARAM_BIT(THEME_PARAM_LAYER1_FRAME)
        ? selector->layer_frame[1] : THEME_FRAME_AUTOMATIC;
}

void external_theme_invalidate(void) {
    asset_free();
    failed_theme = NULL;
}

void external_themes_close(void) {
    asset_free();
    failed_theme = NULL;
    theme_registry_close();
    themes_initialized = false;
}
