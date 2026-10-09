/**
 * @file background.c
 * @brief Implementation of the background UI component.
 * @ingroup ui_components
 */

#include <stdio.h>
#include <stdlib.h>

#include "../ui_components.h"
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
    }
}

/**
 * @brief Free the background component and its resources.
 */
void ui_components_background_free(void) {
    if (!background) return;
    ui_components_background_image_free_only();
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
}

static float gradient_channel(float tl, float tr, float bl, float br, float u, float v) {
    float top = tl + (tr - tl) * u;
    float bottom = bl + (br - bl) * u;
    return top + (bottom - top) * v;
}

static void gradient_vertex(float vertex[6], const menu_theme_t *theme,
                            float x, float y, float u, float v) {
    vertex[0] = x;
    vertex[1] = y;
    vertex[2] = gradient_channel(theme->gradient_tl.r, theme->gradient_tr.r,
        theme->gradient_bl.r, theme->gradient_br.r, u, v) / 255.0f;
    vertex[3] = gradient_channel(theme->gradient_tl.g, theme->gradient_tr.g,
        theme->gradient_bl.g, theme->gradient_br.g, u, v) / 255.0f;
    vertex[4] = gradient_channel(theme->gradient_tl.b, theme->gradient_tr.b,
        theme->gradient_bl.b, theme->gradient_br.b, u, v) / 255.0f;
    vertex[5] = 1.0f;
}

static void draw_vertex_gradient(const menu_theme_t *theme) {
    float xs[3] = { 0, DISPLAY_WIDTH / 2, DISPLAY_WIDTH };
    float ys[3] = { 0, DISPLAY_HEIGHT / 2, DISPLAY_HEIGHT };
    float vertices[3][3][6];

    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            gradient_vertex(vertices[row][column], theme,
                xs[column], ys[row], column * 0.5f, row * 0.5f);
        }
    }

    rdpq_mode_push();
        rdpq_set_mode_standard();
        /* On RGBA16 displays, ordered RGB dithering breaks up otherwise
         * obvious Gouraud bands. RGBA32 targets simply preserve the result. */
        rdpq_mode_dithering(DITHER_SQUARE_NONE);
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
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

void ui_components_background_draw(void) {
    const menu_theme_t *theme = theme_get();
    if (theme->background_mode == THEME_BACKGROUND_CUSTOM_IMAGE && background && background->image_display_list) {
        rspq_block_run(background->image_display_list);
    } else if (theme->background_mode != THEME_BACKGROUND_SOLID) {
        draw_vertex_gradient(theme);
    } else {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
    }
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
