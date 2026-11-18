/**
 * @file jpeg_decoder.c
 * @brief TJpgDec-backed JPEG decoder adapter.
 * @ingroup ui_components
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libdragon.h>
#include <coroutine.h>
#include "libs/tjpgd/tjpgd.h"
#include "jpeg_decoder.h"

#define JPEG_WORK_SIZE 4096
#define JPEG_COROUTINE_STACK_SIZE 4096
#define JPEG_BLOCKS_PER_POLL 8

typedef struct {
    JDEC jd;
    FILE *f;
    uint8_t *mem_buf;
    size_t mem_buf_size;
    size_t mem_buf_pos;
    void *work_buf;
    surface_t *image;
    surface_t *output_buffer;
    int dst_w;
    int dst_h;
    int decoded_w;
    int decoded_h;
    uint8_t scale;
    float progress;
    jpeg_callback_t *callback;
    void *callback_data;
    coroutine_t *coroutine;
    unsigned blocks_this_poll;
    bool prepared;
    JRESULT result;
} jpeg_decoder_t;

static jpeg_decoder_t *decoder;

static void jpeg_decode_coroutine (void *data);

static size_t jpeg_input (JDEC *jd, uint8_t *buffer, size_t size) {
    jpeg_decoder_t *state = jd->device;

    if (state->f != NULL) {
        if (buffer != NULL) return fread(buffer, 1, size, state->f);
        return fseek(state->f, (long) size, SEEK_CUR) == 0 ? size : 0;
    }

    if (state->mem_buf_pos > state->mem_buf_size) return 0;
    size_t available = state->mem_buf_size - state->mem_buf_pos;
    if (size > available) size = available;
    if (size == 0) return 0;
    if (buffer != NULL) {
        memcpy(buffer, state->mem_buf + state->mem_buf_pos, size);
    }
    state->mem_buf_pos += size;
    return size;
}

static void jpeg_decoder_deinit (bool free_image) {
    if (decoder == NULL) return;

    if (decoder->coroutine != NULL) coro_destroy(decoder->coroutine);
    if (decoder->f != NULL) fclose(decoder->f);
    free(decoder->mem_buf);
    free(decoder->work_buf);
    if (free_image && decoder->image != NULL) {
        surface_free(decoder->image);
        free(decoder->image);
    }
    free(decoder);
    decoder = NULL;
}

static int jpeg_output (JDEC *jd, void *bitmap, JRECT *rect) {
    jpeg_decoder_t *state = jd->device;
    int block_w = rect->right - rect->left + 1;
    int block_h = rect->bottom - rect->top + 1;
    const uint8_t *src = bitmap;

    if (state->output_buffer == NULL) {
        for (int y = 0; y < block_h; y++) {
            uint16_t *dst_row = (uint16_t *) ((uint8_t *) state->image->buffer
                                              + (rect->top + y) * state->image->stride);
            for (int x = 0; x < block_w; x++) {
                const uint8_t *pixel = src + (y * block_w + x) * 3;
                dst_row[rect->left + x] = ((pixel[0] >> 3) << 11)
                                        | ((pixel[1] >> 3) << 6)
                                        | ((pixel[2] >> 3) << 1) | 1;
            }
        }
    } else {
        for (int y = 0; y < block_h; y++) {
            int source_y = rect->top + y;
            int dest_y = (((source_y + 1) * state->dst_h) - 1) / state->decoded_h;
            if (dest_y >= state->dst_h
                    || (dest_y * state->decoded_h) / state->dst_h != source_y) {
                continue;
            }

            uint16_t *dst_row = (uint16_t *) ((uint8_t *) state->image->buffer
                                              + dest_y * state->image->stride);
            for (int x = 0; x < block_w; x++) {
                int source_x = rect->left + x;
                int dest_x = (((source_x + 1) * state->dst_w) - 1) / state->decoded_w;
                if (dest_x >= state->dst_w
                        || (dest_x * state->decoded_w) / state->dst_w != source_x) {
                    continue;
                }

                const uint8_t *pixel = src + (y * block_w + x) * 3;
                dst_row[dest_x] = ((pixel[0] >> 3) << 11)
                                | ((pixel[1] >> 3) << 6)
                                | ((pixel[2] >> 3) << 1) | 1;
            }
        }
    }

    state->progress = (float) (rect->bottom + 1) / state->decoded_h;
    state->blocks_this_poll++;
    bool at_end = rect->right + 1 >= state->decoded_w
               && rect->bottom + 1 >= state->decoded_h;
    if (!at_end && state->blocks_this_poll >= JPEG_BLOCKS_PER_POLL) {
        coro_yield();
    }
    return 1;
}

static void jpeg_decode_coroutine (void *data) {
    jpeg_decoder_t *state = data;
    state->result = jd_decomp(&state->jd, jpeg_output, state->scale);
}

static jpeg_err_t jpeg_decoder_setup (int max_width, int max_height) {
    if (max_width < 1 || max_height < 1) {
        jpeg_decoder_deinit(false);
        return JPEG_ERR_INT;
    }

    JRESULT result = jd_prepare(&decoder->jd, jpeg_input, decoder->work_buf,
                                JPEG_WORK_SIZE, decoder);
    if (result != JDR_OK) {
        jpeg_decoder_deinit(false);
        return (result == JDR_MEM1 || result == JDR_MEM2)
            ? JPEG_ERR_OUT_OF_MEM : JPEG_ERR_BAD_FILE;
    }

    bool fits = false;
    if (decoder->output_buffer != NULL) {
        int src_w = decoder->jd.width;
        int src_h = decoder->jd.height;
        decoder->dst_w = (src_w < max_width) ? src_w : max_width;
        decoder->dst_h = (src_h < max_height) ? src_h : max_height;
        if (src_w * decoder->dst_h > src_h * decoder->dst_w) {
            decoder->dst_h = (src_h * decoder->dst_w) / src_w;
        } else {
            decoder->dst_w = (src_w * decoder->dst_h) / src_h;
        }
        if (decoder->dst_w < 1) decoder->dst_w = 1;
        if (decoder->dst_h < 1) decoder->dst_h = 1;

        for (uint8_t scale = 3;; scale--) {
            int decoded_w = decoder->jd.width >> scale;
            int decoded_h = decoder->jd.height >> scale;
            if (decoded_w == 0 || decoded_h == 0) {
                if (scale == 0) break;
                continue;
            }
            if (decoded_w < 1) decoded_w = 1;
            if (decoded_h < 1) decoded_h = 1;
            if (decoded_w >= decoder->dst_w && decoded_h >= decoder->dst_h) {
                decoder->scale = scale;
                decoder->decoded_w = decoded_w;
                decoder->decoded_h = decoded_h;
                fits = true;
                break;
            }
            if (scale == 0) break;
        }
        if (decoder->output_buffer->buffer == NULL
                || surface_get_format(decoder->output_buffer) != FMT_RGBA16
                || decoder->output_buffer->stride < decoder->dst_w * 2
                || decoder->dst_w > decoder->output_buffer->width
                || decoder->dst_h > decoder->output_buffer->height) {
            fits = false;
        }
    } else {
        heap_stats_t heap;
        sys_get_heap_stats(&heap);
        size_t available = heap.total - heap.used;
        for (uint8_t scale = 0; scale <= 3; scale++) {
            int dst_w = decoder->jd.width >> scale;
            int dst_h = decoder->jd.height >> scale;
            if (dst_w < 1) dst_w = 1;
            if (dst_h < 1) dst_h = 1;
            if (dst_w > max_width || dst_h > max_height) continue;

            size_t surface_size = (size_t) dst_w * dst_h * 2;
            if (surface_size + 64 * 1024 <= available) {
                decoder->scale = scale;
                decoder->dst_w = dst_w;
                decoder->dst_h = dst_h;
                decoder->decoded_w = dst_w;
                decoder->decoded_h = dst_h;
                fits = true;
                break;
            }
        }
    }

    if (!fits) {
        jpeg_decoder_deinit(false);
        return JPEG_ERR_OUT_OF_MEM;
    }

    decoder->image = calloc(1, sizeof(surface_t));
    if (decoder->image == NULL) {
        jpeg_decoder_deinit(false);
        return JPEG_ERR_OUT_OF_MEM;
    }
    if (decoder->output_buffer != NULL) {
        *decoder->image = surface_make(decoder->output_buffer->buffer, FMT_RGBA16,
                                       decoder->dst_w, decoder->dst_h,
                                       decoder->output_buffer->stride);
    } else {
        *decoder->image = surface_alloc(FMT_RGBA16, decoder->dst_w, decoder->dst_h);
    }
    if (decoder->image->buffer == NULL) {
        jpeg_decoder_deinit(true);
        return JPEG_ERR_OUT_OF_MEM;
    }

    decoder->coroutine = coro_create(jpeg_decode_coroutine, decoder,
                                     JPEG_COROUTINE_STACK_SIZE);
    if (decoder->coroutine == NULL) {
        jpeg_decoder_deinit(true);
        return JPEG_ERR_OUT_OF_MEM;
    }
    decoder->prepared = true;
    return JPEG_OK;
}

static jpeg_err_t jpeg_decoder_begin (jpeg_callback_t *callback, void *callback_data) {
    if (decoder != NULL) return JPEG_ERR_BUSY;

    decoder = calloc(1, sizeof(jpeg_decoder_t));
    if (decoder == NULL) return JPEG_ERR_OUT_OF_MEM;
    decoder->callback = callback;
    decoder->callback_data = callback_data;
    decoder->work_buf = malloc(JPEG_WORK_SIZE);
    if (decoder->work_buf == NULL) {
        jpeg_decoder_deinit(false);
        return JPEG_ERR_OUT_OF_MEM;
    }
    return JPEG_OK;
}

static jpeg_err_t jpeg_decoder_start_file (char *path, int max_width, int max_height,
                                           surface_t *output_buffer,
                                           jpeg_callback_t *callback, void *callback_data) {
    jpeg_err_t err = jpeg_decoder_begin(callback, callback_data);
    if (err != JPEG_OK) return err;

    decoder->output_buffer = output_buffer;
    decoder->f = fopen(path, "rb");
    if (decoder->f == NULL) {
        jpeg_decoder_deinit(false);
        return JPEG_ERR_NO_FILE;
    }
    return jpeg_decoder_setup(max_width, max_height);
}

jpeg_err_t jpeg_decoder_start (char *path, int max_width, int max_height,
                               jpeg_callback_t *callback, void *callback_data) {
    return jpeg_decoder_start_file(path, max_width, max_height, NULL,
                                   callback, callback_data);
}

jpeg_err_t jpeg_decoder_start_into (char *path, int max_width, int max_height,
                                    surface_t *output_buffer,
                                    jpeg_callback_t *callback, void *callback_data) {
    if (output_buffer == NULL) return JPEG_ERR_OUT_OF_MEM;
    return jpeg_decoder_start_file(path, max_width, max_height, output_buffer,
                                   callback, callback_data);
}

static jpeg_err_t jpeg_decoder_start_mem_internal (void *buf, size_t buf_size,
                                                   int max_width, int max_height,
                                                   surface_t *output_buffer,
                                                   jpeg_callback_t *callback,
                                                   void *callback_data) {
    if (buf == NULL || buf_size == 0) {
        free(buf);
        return JPEG_ERR_BAD_FILE;
    }

    jpeg_err_t err = jpeg_decoder_begin(callback, callback_data);
    if (err != JPEG_OK) {
        free(buf);
        return err;
    }

    decoder->mem_buf = buf;
    decoder->mem_buf_size = buf_size;
    decoder->output_buffer = output_buffer;
    return jpeg_decoder_setup(max_width, max_height);
}

jpeg_err_t jpeg_decoder_start_mem (void *buf, size_t buf_size, int max_width,
                                   int max_height, jpeg_callback_t *callback,
                                   void *callback_data) {
    return jpeg_decoder_start_mem_internal(buf, buf_size, max_width, max_height,
                                           NULL, callback, callback_data);
}

jpeg_err_t jpeg_decoder_start_mem_into (void *buf, size_t buf_size, int max_width,
                                        int max_height, surface_t *output_buffer,
                                        jpeg_callback_t *callback,
                                        void *callback_data) {
    if (output_buffer == NULL) {
        free(buf);
        return JPEG_ERR_OUT_OF_MEM;
    }
    return jpeg_decoder_start_mem_internal(buf, buf_size, max_width, max_height,
                                           output_buffer, callback, callback_data);
}

void jpeg_decoder_poll (void) {
    if (decoder == NULL || !decoder->prepared) return;

    decoder->blocks_this_poll = 0;
    coro_resume(decoder->coroutine);
    if (!coro_finished(decoder->coroutine)) return;

    jpeg_err_t err = decoder->result == JDR_OK ? JPEG_OK
        : (decoder->result == JDR_MEM1 || decoder->result == JDR_MEM2)
            ? JPEG_ERR_OUT_OF_MEM : JPEG_ERR_BAD_FILE;

    surface_t *image = err == JPEG_OK ? decoder->image : NULL;
    jpeg_callback_t *callback = decoder->callback;
    void *callback_data = decoder->callback_data;
    if (err == JPEG_OK) decoder->image = NULL;
    jpeg_decoder_deinit(err != JPEG_OK);
    callback(err, image, callback_data);
}

void jpeg_decoder_abort (void) {
    jpeg_decoder_deinit(true);
}

float jpeg_decoder_get_progress (void) {
    return decoder == NULL ? 0.0f : decoder->progress;
}
