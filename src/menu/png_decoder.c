/**
 * @file png_decoder.c
 * @brief PNG Decoder component implementation
 * @ingroup ui_components
 */

#include <stdio.h>
#include <libdragon.h>
#include <libspng/spng/spng.h>
#include "png_decoder.h"
#include "utils/fs.h"

/** @brief PNG File Information Structure. */
typedef struct {
    FILE *f; /**< File pointer */
    uint8_t *mem_buf; /**< Heap buffer for in-memory decode (freed on deinit) */
    size_t mem_buf_size; /**< Size of mem_buf */
    spng_ctx *ctx; /**< SPNG context */
    struct spng_ihdr ihdr; /**< SPNG image header */
    surface_t *image; /**< Image surface */
    uint8_t *row_buffer; /**< Row buffer */
    int dst_w, dst_h; /**< Output dimensions (may be scaled down) */
    int decoded_rows; /**< Number of decoded rows */
    png_callback_t *callback; /**< Callback function */
    void *callback_data; /**< Callback data */
} png_decoder_t;

static png_decoder_t *decoder;

/**
 * @brief Deinitialize the PNG decoder.
 * 
 * @param free_image Flag indicating whether to free the image.
 */
static void png_decoder_deinit (bool free_image) {
    if (decoder != NULL) {
        if (decoder->f) fclose(decoder->f);
        free(decoder->mem_buf);
        if (decoder->ctx != NULL) {
            spng_ctx_free(decoder->ctx);
        }
        if ((decoder->image != NULL) && free_image) {
            surface_free(decoder->image);
            free(decoder->image);
        }
        if (decoder->row_buffer != NULL) {
            free(decoder->row_buffer);
        }
        free(decoder);
        decoder = NULL;
    }
}

/** Shared setup after the PNG source has been configured.
 *  Parses the header, picks output dimensions, allocates the surface. */
static png_err_t png_decoder_setup (int max_width, int max_height) {
    if (spng_set_crc_action(decoder->ctx, SPNG_CRC_USE, SPNG_CRC_USE) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_INT;
    }

    /* Allow large source PNGs — we scale down to fit available memory.
     * Cap at 4096 to reject corrupt headers without blocking real images. */
    if (spng_set_image_limits(decoder->ctx, 4096, 4096) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_INT;
    }

    if (decoder->f) {
        if (spng_set_png_file(decoder->ctx, decoder->f) != SPNG_OK) {
            png_decoder_deinit(false);
            return PNG_ERR_INT;
        }
    } else {
        if (spng_set_png_buffer(decoder->ctx, decoder->mem_buf, decoder->mem_buf_size) != SPNG_OK) {
            png_decoder_deinit(false);
            return PNG_ERR_INT;
        }
    }

    size_t image_size;

    if (spng_decoded_image_size(decoder->ctx, SPNG_FMT_RGBA8, &image_size) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_BAD_FILE;
    }

    if (spng_decode_image(decoder->ctx, NULL, image_size, SPNG_FMT_RGBA8,
                         SPNG_DECODE_PROGRESSIVE | SPNG_DECODE_TRNS) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_BAD_FILE;
    }

    if (spng_get_ihdr(decoder->ctx, &decoder->ihdr) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_BAD_FILE;
    }

    /* Start at caller's max dimensions (or source size if smaller),
     * then scale down by powers of 2 until it fits in available memory. */
    int src_w = decoder->ihdr.width;
    int src_h = decoder->ihdr.height;
    decoder->dst_w = (src_w < max_width) ? src_w : max_width;
    decoder->dst_h = (src_h < max_height) ? src_h : max_height;

    /* Preserve aspect ratio */
    if (src_w * decoder->dst_h > src_h * decoder->dst_w) {
        decoder->dst_h = (src_h * decoder->dst_w) / src_w;
    } else {
        decoder->dst_w = (src_w * decoder->dst_h) / src_h;
    }

    while (decoder->dst_w > 1 && decoder->dst_h > 1) {
        size_t row_size  = (size_t)src_w * 4;
        size_t surf_size = (size_t)decoder->dst_w * decoder->dst_h * 2;

        heap_stats_t heap;
        sys_get_heap_stats(&heap);
        size_t available = heap.total - heap.used;

        if (row_size + surf_size + 64 * 1024 <= available) break;

        decoder->dst_w /= 2;
        decoder->dst_h /= 2;
    }
    if (decoder->dst_w < 1) decoder->dst_w = 1;
    if (decoder->dst_h < 1) decoder->dst_h = 1;

    decoder->image = calloc(1, sizeof(surface_t));
    if (decoder->image == NULL) {
        png_decoder_deinit(false);
        return PNG_ERR_OUT_OF_MEM;
    }

    *decoder->image = surface_alloc(FMT_RGBA16, decoder->dst_w, decoder->dst_h);
    if (decoder->image->buffer == NULL) {
        png_decoder_deinit(true);
        return PNG_ERR_OUT_OF_MEM;
    }

    if ((decoder->row_buffer = malloc(src_w * 4)) == NULL) {
        png_decoder_deinit(true);
        return PNG_ERR_OUT_OF_MEM;
    }

    decoder->decoded_rows = 0;
    return PNG_OK;
}

png_err_t png_decoder_start (char *path, int max_width, int max_height, png_callback_t *callback, void *callback_data) {
    if (decoder != NULL) return PNG_ERR_BUSY;

    decoder = calloc(1, sizeof(png_decoder_t));
    if (decoder == NULL) return PNG_ERR_OUT_OF_MEM;

    decoder->callback = callback;
    decoder->callback_data = callback_data;

    if ((decoder->f = fopen(path, "rb")) == NULL) {
        png_decoder_deinit(false);
        return PNG_ERR_NO_FILE;
    }
    setbuf(decoder->f, NULL);

    if ((decoder->ctx = spng_ctx_new(SPNG_CTX_IGNORE_ADLER32)) == NULL) {
        png_decoder_deinit(false);
        return PNG_ERR_OUT_OF_MEM;
    }

    return png_decoder_setup(max_width, max_height);
}

png_err_t png_decoder_start_mem (void *buf, size_t buf_size, int max_width, int max_height, png_callback_t *callback, void *callback_data) {
    if (decoder != NULL) return PNG_ERR_BUSY;

    decoder = calloc(1, sizeof(png_decoder_t));
    if (decoder == NULL) {
        free(buf);
        return PNG_ERR_OUT_OF_MEM;
    }

    decoder->callback = callback;
    decoder->callback_data = callback_data;

    decoder->mem_buf = buf;
    decoder->mem_buf_size = buf_size;

    if ((decoder->ctx = spng_ctx_new(SPNG_CTX_IGNORE_ADLER32)) == NULL) {
        png_decoder_deinit(false);
        return PNG_ERR_OUT_OF_MEM;
    }

    return png_decoder_setup(max_width, max_height);
}

/**
 * @brief Abort the PNG decoding process.
 */
void png_decoder_abort (void) {
    png_decoder_deinit(true);
}

/**
 * @brief Get the progress of the PNG decoding process.
 * 
 * @return float Progress as a percentage.
 */
float png_decoder_get_progress (void) {
    if (!decoder) {
        return 0.0f;
    }

    return (float) (decoder->decoded_rows) / (decoder->ihdr.height);
}

/**
 * @brief Poll the PNG decoder to process the next row.
 */
void png_decoder_poll (void) {
    if (!decoder) {
        return;
    }

    enum spng_errno err;
    struct spng_row_info row_info;

    if ((err = spng_get_row_info(decoder->ctx, &row_info)) != SPNG_OK) {
        png_callback_t *callback = decoder->callback;
        void *callback_data = decoder->callback_data;
        png_decoder_deinit(true);
        callback(PNG_ERR_BAD_FILE, NULL, callback_data);
        return;
    }

    err = spng_decode_row(decoder->ctx, decoder->row_buffer, decoder->ihdr.width * 4);

    if (err == SPNG_OK || err == SPNG_EOI) {
        decoder->decoded_rows += 1;

        int src_w = decoder->ihdr.width;
        int src_h = decoder->ihdr.height;
        int dst_y = (row_info.row_num * decoder->dst_h) / src_h;

        if (dst_y < decoder->dst_h) {
            uint16_t *dst_row = (uint16_t *)((uint8_t *)decoder->image->buffer
                                             + dst_y * decoder->image->stride);
            for (int dx = 0; dx < decoder->dst_w; dx++) {
                int sx = (dx * src_w) / decoder->dst_w;
                uint8_t *p = decoder->row_buffer + sx * 4;
                uint8_t r = p[0] >> 3;
                uint8_t g = p[1] >> 3;
                uint8_t b = p[2] >> 3;
                uint8_t a = p[3] >= 128;
                dst_row[dx] = (r << 11) | (g << 6) | (b << 1) | a;
            }
        }
    }

    if (err == SPNG_EOI) {
        png_callback_t *callback = decoder->callback;
        void *callback_data = decoder->callback_data;
        surface_t *image = decoder->image;
        decoder->image = NULL;
        png_decoder_deinit(false);
        callback(PNG_OK, image, callback_data);
    } else if (err != SPNG_OK) {
        png_callback_t *callback = decoder->callback;
        void *callback_data = decoder->callback_data;
        png_decoder_deinit(true);
        callback(PNG_ERR_BAD_FILE, NULL, callback_data);
    }
}
