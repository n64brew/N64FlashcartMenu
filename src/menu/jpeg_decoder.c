/**
 * @file jpeg_decoder.c
 * @brief JPEG decoder component implementation.
 * @ingroup ui_components
 */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <libdragon.h>
#include <jpeglib.h>
#include "jpeg_decoder.h"

#define SCANLINES_PER_POLL 1

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buf;
} jpeg_error_mgr_ex_t;

typedef struct {
    struct jpeg_decompress_struct cinfo;
    jpeg_error_mgr_ex_t jerr;
    FILE *f;
    uint8_t *mem_buf;
    size_t mem_buf_size;
    JSAMPLE *row_buf;
    surface_t *image;
    int src_w;
    int src_h;
    int dst_w;
    int dst_h;
    int comps;
    int scan_y;
    jpeg_callback_t *callback;
    void *callback_data;
    bool started;
} jpeg_decoder_t;

static jpeg_decoder_t *decoder;

static void jpeg_error_exit_ex (j_common_ptr cinfo) {
    jpeg_error_mgr_ex_t *err = (jpeg_error_mgr_ex_t *) cinfo->err;
    longjmp(err->setjmp_buf, 1);
}

static void jpeg_decoder_deinit (bool free_image, bool destroy_cinfo) {
    if (decoder == NULL) return;

    free(decoder->row_buf);
    if (decoder->started && destroy_cinfo) {
        jpeg_abort_decompress(&decoder->cinfo);
        jpeg_destroy_decompress(&decoder->cinfo);
    }
    if (decoder->f != NULL) fclose(decoder->f);
    free(decoder->mem_buf);
    if (free_image && decoder->image != NULL) {
        surface_free(decoder->image);
        free(decoder->image);
    }
    free(decoder);
    decoder = NULL;
}

static jpeg_err_t jpeg_decoder_setup (int max_width, int max_height) {
    if (max_width < 1 || max_height < 1) {
        jpeg_decoder_deinit(false, true);
        return JPEG_ERR_INT;
    }

    if (setjmp(decoder->jerr.setjmp_buf)) {
        jpeg_decoder_deinit(false, true);
        return JPEG_ERR_BAD_FILE;
    }

    jpeg_create_decompress(&decoder->cinfo);
    decoder->started = true;

    if (decoder->f != NULL) {
        jpeg_stdio_src(&decoder->cinfo, decoder->f);
    } else {
        jpeg_mem_src(&decoder->cinfo, decoder->mem_buf, decoder->mem_buf_size);
    }

    jpeg_read_header(&decoder->cinfo, TRUE);

    bool is_progressive = jpeg_has_multiple_scans(&decoder->cinfo);
    size_t coeff_cost = 0;
    if (is_progressive) {
        coeff_cost = (size_t) decoder->cinfo.image_width
                   * decoder->cinfo.image_height
                   * decoder->cinfo.num_components * sizeof(JCOEF);
    }

    decoder->cinfo.dct_method = JDCT_IFAST;
    bool fits = false;
    int start_denom = is_progressive ? 2 : 1;
    for (int denom = start_denom; denom <= 8; denom *= 2) {
        decoder->cinfo.scale_num = 1;
        decoder->cinfo.scale_denom = denom;
        jpeg_calc_output_dimensions(&decoder->cinfo);

        int out_w = (int) decoder->cinfo.output_width;
        int out_h = (int) decoder->cinfo.output_height;
        int dst_w = out_w;
        int dst_h = out_h;
        if (dst_w > max_width || dst_h > max_height) {
            if (out_w * max_height >= out_h * max_width) {
                dst_w = max_width;
                dst_h = (out_h * max_width) / out_w;
            } else {
                dst_h = max_height;
                dst_w = (out_w * max_height) / out_h;
            }
        }
        if (dst_w < 1) dst_w = 1;
        if (dst_h < 1) dst_h = 1;

        int comps = decoder->cinfo.output_components;
        size_t jpeg_cost = (size_t) out_w * comps * 12;
        size_t surf_size = (size_t) dst_w * dst_h * 2;
        size_t total_cost = jpeg_cost + surf_size + coeff_cost + 64 * 1024;

        heap_stats_t heap;
        sys_get_heap_stats(&heap);
        size_t available = heap.total - heap.used;
        if (total_cost <= available) {
            fits = true;
            break;
        }
    }

    if (!fits) {
        jpeg_decoder_deinit(false, true);
        return JPEG_ERR_OUT_OF_MEM;
    }

    jpeg_start_decompress(&decoder->cinfo);
    decoder->src_w = (int) decoder->cinfo.output_width;
    decoder->src_h = (int) decoder->cinfo.output_height;
    decoder->comps = (int) decoder->cinfo.output_components;
    decoder->dst_w = decoder->src_w;
    decoder->dst_h = decoder->src_h;
    if (decoder->dst_w > max_width || decoder->dst_h > max_height) {
        if (decoder->src_w * max_height >= decoder->src_h * max_width) {
            decoder->dst_w = max_width;
            decoder->dst_h = (decoder->src_h * max_width) / decoder->src_w;
        } else {
            decoder->dst_h = max_height;
            decoder->dst_w = (decoder->src_w * max_height) / decoder->src_h;
        }
    }
    if (decoder->dst_w < 1) decoder->dst_w = 1;
    if (decoder->dst_h < 1) decoder->dst_h = 1;

    decoder->image = calloc(1, sizeof(surface_t));
    if (decoder->image == NULL) {
        jpeg_decoder_deinit(false, true);
        return JPEG_ERR_OUT_OF_MEM;
    }
    *decoder->image = surface_alloc(FMT_RGBA16, decoder->dst_w, decoder->dst_h);
    if (decoder->image->buffer == NULL) {
        jpeg_decoder_deinit(true, true);
        return JPEG_ERR_OUT_OF_MEM;
    }

    decoder->row_buf = malloc((size_t) decoder->src_w * decoder->comps);
    if (decoder->row_buf == NULL) {
        jpeg_decoder_deinit(true, true);
        return JPEG_ERR_OUT_OF_MEM;
    }
    decoder->scan_y = 0;
    return JPEG_OK;
}

static jpeg_err_t jpeg_decoder_begin (jpeg_callback_t *callback, void *callback_data) {
    if (decoder != NULL) return JPEG_ERR_BUSY;

    decoder = calloc(1, sizeof(jpeg_decoder_t));
    if (decoder == NULL) return JPEG_ERR_OUT_OF_MEM;
    decoder->callback = callback;
    decoder->callback_data = callback_data;
    decoder->cinfo.err = jpeg_std_error(&decoder->jerr.pub);
    decoder->jerr.pub.error_exit = jpeg_error_exit_ex;
    return JPEG_OK;
}

jpeg_err_t jpeg_decoder_start (char *path, int max_width, int max_height,
                               jpeg_callback_t *callback, void *callback_data) {
    jpeg_err_t err = jpeg_decoder_begin(callback, callback_data);
    if (err != JPEG_OK) return err;

    decoder->f = fopen(path, "rb");
    if (decoder->f == NULL) {
        jpeg_decoder_deinit(false, true);
        return JPEG_ERR_NO_FILE;
    }
    return jpeg_decoder_setup(max_width, max_height);
}

jpeg_err_t jpeg_decoder_start_mem (void *buf, size_t buf_size, int max_width,
                                   int max_height, jpeg_callback_t *callback,
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
    return jpeg_decoder_setup(max_width, max_height);
}

void jpeg_decoder_poll (void) {
    if (decoder == NULL || !decoder->started) return;

    if (setjmp(decoder->jerr.setjmp_buf)) {
        jpeg_callback_t *callback = decoder->callback;
        void *callback_data = decoder->callback_data;
        jpeg_decoder_deinit(false, true);
        callback(JPEG_ERR_BAD_FILE, NULL, callback_data);
        return;
    }

    int lines_this_frame = 0;
    while ((int) decoder->cinfo.output_scanline < decoder->src_h
           && lines_this_frame < SCANLINES_PER_POLL) {
        jpeg_read_scanlines(&decoder->cinfo, &decoder->row_buf, 1);
        int dst_y = (decoder->scan_y * decoder->dst_h) / decoder->src_h;
        uint16_t *dst_row = (uint16_t *) ((uint8_t *) decoder->image->buffer
                                          + dst_y * decoder->image->stride);
        for (int dst_x = 0; dst_x < decoder->dst_w; dst_x++) {
            int src_x = (dst_x * decoder->src_w) / decoder->dst_w;
            uint8_t *pixel = decoder->row_buf + src_x * decoder->comps;
            uint8_t r = pixel[0];
            uint8_t g = decoder->comps > 1 ? pixel[1] : r;
            uint8_t b = decoder->comps > 2 ? pixel[2] : r;
            dst_row[dst_x] = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1;
        }
        decoder->scan_y++;
        lines_this_frame++;
    }

    if ((int) decoder->cinfo.output_scanline >= decoder->src_h) {
        jpeg_finish_decompress(&decoder->cinfo);
        surface_t *image = decoder->image;
        jpeg_callback_t *callback = decoder->callback;
        void *callback_data = decoder->callback_data;
        decoder->image = NULL;
        jpeg_decoder_deinit(false, true);
        callback(JPEG_OK, image, callback_data);
    }
}

void jpeg_decoder_abort (void) {
    jpeg_decoder_deinit(true, true);
}

float jpeg_decoder_get_progress (void) {
    if (decoder == NULL || decoder->src_h == 0) return 0.0f;
    return (float) decoder->scan_y / (float) decoder->src_h;
}
