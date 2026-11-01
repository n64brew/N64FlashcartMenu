#ifndef JPEG_DECODER_H__
/** @file jpeg_decoder.h JPEG decoder component. */
#define JPEG_DECODER_H__

#include <stddef.h>
#include <surface.h>

/** @brief JPEG decoder error codes. */
typedef enum {
    JPEG_OK             =  0,
    JPEG_ERR_INT        = -1,
    JPEG_ERR_BUSY       = -2,
    JPEG_ERR_OUT_OF_MEM = -3,
    JPEG_ERR_NO_FILE    = -4,
    JPEG_ERR_BAD_FILE   = -5,
} jpeg_err_t;

typedef void jpeg_callback_t (jpeg_err_t err, surface_t *decoded_image, void *callback_data);

jpeg_err_t jpeg_decoder_start (char *path, int max_width, int max_height, jpeg_callback_t *callback, void *callback_data);
jpeg_err_t jpeg_decoder_start_mem (void *buf, size_t buf_size, int max_width, int max_height, jpeg_callback_t *callback, void *callback_data);
void jpeg_decoder_abort (void);
float jpeg_decoder_get_progress (void);
void jpeg_decoder_poll (void);

#endif /* JPEG_DECODER_H__ */
