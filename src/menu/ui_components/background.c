/**
 * @file background.c
 * @brief Implementation of the background UI component.
 * @ingroup ui_components
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ui_components.h"
#include "constants.h"
#include "utils/fs.h"

#define CACHE_METADATA_MAGIC    (0x424B4731)
#define BACKGROUND_CACHE_READ_CHUNK (32 * 1024)

/**
 * @brief Structure for background image cache metadata.
 */
typedef struct {
    uint32_t magic;    /**< Magic number for cache validation. */
    uint32_t width;    /**< Image width in pixels. */
    uint32_t height;   /**< Image height in pixels. */
    uint32_t size;     /**< Image buffer size in bytes. */
} cache_metadata_t;

/**
 * @brief Structure representing the background component.
 */
typedef struct {
    char *cache_location;      /**< Path to the cache file location. */
    surface_t decode_buffer;   /**< Reusable full-resolution decode buffer. */
    surface_t *image;          /**< Pointer to the loaded image surface. */
    rspq_block_t *image_display_list; /**< Display list for rendering the image. */
    FILE *cache_file;          /**< Cache being incrementally restored. */
    cache_metadata_t cache_metadata; /**< Metadata for the pending cache restore. */
    uint32_t cache_bytes_read;  /**< Bytes read from the pending cache. */
} component_background_t;

static component_background_t *background = NULL;

static void background_cache_cancel(component_background_t *c) {
    if (c->cache_file) {
        fclose(c->cache_file);
        c->cache_file = NULL;
    }
    c->cache_bytes_read = 0;
}

static void expand_cache_rows(component_background_t *c, const cache_metadata_t *metadata) {
    size_t row_size = metadata->width * 2;
    uint8_t *buffer = c->decode_buffer.buffer;
    for (uint32_t y = metadata->height; y-- > 0;) {
        memmove(buffer + y * c->decode_buffer.stride, buffer + y * row_size, row_size);
    }
}

/**
 * @brief Load background image from cache file if available.
 *
 * @param c Pointer to the background component structure.
 */
static void load_from_cache(component_background_t *c) {
    if (!c->cache_location || c->decode_buffer.buffer == NULL) {
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

    if (cache_metadata.magic != CACHE_METADATA_MAGIC
            || cache_metadata.width == 0 || cache_metadata.height == 0
            || cache_metadata.width > c->decode_buffer.width
            || cache_metadata.height > c->decode_buffer.height) {
        fclose(f);
        return;
    }

    uint32_t expected_size = cache_metadata.width * cache_metadata.height * 2;
    if (cache_metadata.size != expected_size) {
        fclose(f);
        return;
    }

    if (fread(c->decode_buffer.buffer, cache_metadata.size, 1, f) != 1) {
        fclose(f);
        return;
    }

    expand_cache_rows(c, &cache_metadata);

    c->image = calloc(1, sizeof(surface_t));
    if (c->image != NULL) {
        *c->image = surface_make(c->decode_buffer.buffer, FMT_RGBA16,
                                 cache_metadata.width, cache_metadata.height,
                                 c->decode_buffer.stride);
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
        .size = c->image->width * c->image->height * 2,
    };

    if (fwrite(&cache_metadata, sizeof(cache_metadata), 1, f) == 1) {
        for (int y = 0; y < c->image->height; y++) {
            uint8_t *row = (uint8_t *) c->image->buffer + y * c->image->stride;
            if (fwrite(row, c->image->width * 2, 1, f) != 1) break;
        }
    }

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

static void begin_cache_restore(component_background_t *c) {
    if (!c->cache_location || c->decode_buffer.buffer == NULL || c->image || c->cache_file) {
        return;
    }

    FILE *f = fopen(c->cache_location, "rb");
    if (!f) {
        return;
    }

    cache_metadata_t metadata;
    if (fread(&metadata, sizeof(metadata), 1, f) != 1
            || metadata.magic != CACHE_METADATA_MAGIC
            || metadata.width == 0 || metadata.height == 0
            || metadata.width > c->decode_buffer.width
            || metadata.height > c->decode_buffer.height
            || metadata.size != metadata.width * metadata.height * 2) {
        fclose(f);
        return;
    }

    c->cache_file = f;
    c->cache_metadata = metadata;
    c->cache_bytes_read = 0;
}

static void poll_cache_restore(component_background_t *c) {
    if (!c->cache_file) {
        return;
    }

    uint32_t remaining = c->cache_metadata.size - c->cache_bytes_read;
    size_t chunk_size = remaining < BACKGROUND_CACHE_READ_CHUNK
        ? remaining : BACKGROUND_CACHE_READ_CHUNK;
    uint8_t *buffer = c->decode_buffer.buffer;
    if (fread(buffer + c->cache_bytes_read, chunk_size, 1, c->cache_file) != 1) {
        debugf("[Background] Failed to read background cache\n");
        background_cache_cancel(c);
        return;
    }
    c->cache_bytes_read += chunk_size;
    if (c->cache_bytes_read < c->cache_metadata.size) {
        return;
    }

    cache_metadata_t metadata = c->cache_metadata;
    background_cache_cancel(c);
    expand_cache_rows(c, &metadata);

    c->image = calloc(1, sizeof(surface_t));
    if (!c->image) {
        debugf("[Background] Unable to allocate cached image surface\n");
        return;
    }
    *c->image = surface_make(c->decode_buffer.buffer, FMT_RGBA16,
                             metadata.width, metadata.height,
                             c->decode_buffer.stride);
    prepare_background(c);
}

/**
 * @brief Initialize the background component and load from cache.
 *
 * @param cache_location Path to the cache file location.
 */
void ui_components_background_init(char *cache_location) {
    if (!background) {
        background = calloc(1, sizeof(component_background_t));
        if (!background) {
            debugf("[Background] Unable to allocate component\n");
            return;
        }
        background->cache_location = strdup(cache_location);
        background->decode_buffer = surface_alloc(FMT_RGBA16, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        if (background->decode_buffer.buffer == NULL) {
            debugf("[Background] Unable to reserve decode buffer\n");
        }
        load_from_cache(background);
        prepare_background(background);
    }
}

/**
 * @brief Free the background component and its resources.
 */
void ui_components_background_free(void) {
    if (background) {
        background_cache_cancel(background);
        rspq_wait();
        if (background->image) {
            surface_free(background->image);
            free(background->image);
            background->image = NULL;
        }
        surface_free(&background->decode_buffer);
        if (background->image_display_list) {
            rspq_block_free(background->image_display_list);
            background->image_display_list = NULL;
        }
        if (background->cache_location) {
            free(background->cache_location);
        }
        free(background);
        background = NULL;
    }
}

void ui_components_background_clear(void) {
    if (!background) {
        return;
    }
    background_cache_cancel(background);
    if (background->cache_location) {
        remove(background->cache_location);
    }
    // Free image and display list but keep the struct and cache_location so
    // a new background can be set without rebooting
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

/**
 * @brief Replace the background image and update cache/display list.
 *
 * @param image Pointer to the new background image surface.
 */
void ui_components_background_replace_image(surface_t *image) {
    if (!background) {
        return;
    }
    background_cache_cancel(background);

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

/**
 * @brief Draw the background image or clear the screen if not available.
 */
void ui_components_background_draw(void) {
    if (background) {
        poll_cache_restore(background);
    }
    if (background && background->image_display_list) {
        rspq_block_run(background->image_display_list);
    } else {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
    }
}

surface_t *ui_components_background_get_image(void) {
    return background ? background->image : NULL;
}

void ui_components_background_reload(void) {
    if (!background || !background->cache_location) {
        return;
    }
    if (background->decode_buffer.buffer == NULL) {
        background->decode_buffer = surface_alloc(FMT_RGBA16, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        if (background->decode_buffer.buffer == NULL) {
            debugf("[Background] Unable to reserve decode buffer\n");
            return;
        }
    }
    if (!background->image && !background->cache_file) {
        begin_cache_restore(background);
    }
}

void ui_components_background_image_free_only(void) {
    if (!background) {
        return;
    }
    background_cache_cancel(background);
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

surface_t *ui_components_background_get_decode_buffer(void) {
    if (!background || background->decode_buffer.buffer == NULL) {
        return NULL;
    }
    return &background->decode_buffer;
}
