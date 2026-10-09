/**
 * @file boxart.c
 * @brief Implementation of the boxart UI component.
 * @ingroup ui_components
 */

#include <stdlib.h>

#include "../ui_components.h"
#include "../path.h"
#include "../jpeg_decoder.h"
#include "../png_decoder.h"
#include "constants.h"
#include "utils/fs.h"

#define OLD_BOXART_DIRECTORY       "menu/boxart"
#define METADATA_BASE_DIRECTORY    "menu/metadata"
#define HOMEBREW_ID_SUBDIRECTORY   "homebrew"

/**
 * @brief PNG decoder callback for boxart image loading.
 *
 * Sets the loading flag to false and assigns the decoded image to the boxart component.
 *
 * @param err PNG decoder error code.
 * @param decoded_image Pointer to the decoded image surface.
 * @param callback_data Pointer to the boxart component (component_boxart_t *).
 */
static void png_decoder_callback(png_err_t err, surface_t *decoded_image, void *callback_data) {
    component_boxart_t *b = (component_boxart_t *)(callback_data);
    b->loading = false;
    b->image = decoded_image;
}

static void jpeg_decoder_callback(jpeg_err_t err, surface_t *decoded_image, void *callback_data) {
    component_boxart_t *b = (component_boxart_t *)(callback_data);
    b->loading = false;
    b->image = (err == JPEG_OK) ? decoded_image : NULL;
}

component_boxart_t *ui_components_boxart_init_mem(const char *filename, void *data, size_t size,
                                                  int max_width, int max_height) {
    component_boxart_t *b = calloc(1, sizeof(component_boxart_t));
    if (!b) {
        free(data);
        return NULL;
    }
    b->loading = true;

    jpeg_err_t jpeg_err = JPEG_ERR_BAD_FILE;
    png_err_t png_err = PNG_ERR_BAD_FILE;
    bool is_jpeg = file_has_extensions((char *) filename, (const char *[]) { "jpg", "jpeg", NULL });
    if (is_jpeg) {
        jpeg_err = jpeg_decoder_start_mem(data, size, max_width, max_height, jpeg_decoder_callback, b);
        if (jpeg_err == JPEG_OK) return b;
    } else {
        png_err = png_decoder_start_mem(data, size, max_width, max_height, png_decoder_callback, b);
        if (png_err == PNG_OK) return b;
        if (png_err == PNG_ERR_BUSY) free(data);
    }

    free(b);
    return NULL;
}

/**
 * @brief Initialize and load the boxart component for a game.
 *
 * Attempts to locate and load the appropriate boxart image for the given game code and ROM title.
 *
 * @param storage_prefix The storage prefix (e.g., SD card root).
 * @param game_code The 4-character game code.
 * @param rom_title Title of the ROM (may be NULL). If used, it is sanitized for filesystem safety.
 * @param current_image_view The current image view type (front, back, etc.).
 * @return Pointer to the initialized boxart component, or NULL on failure.
 */
component_boxart_t *ui_components_boxart_init(const char *storage_prefix, const char *game_code, const char *rom_title, file_image_type_t current_image_view) {
    component_boxart_t *b;
    char boxart_path[32];

    if ((b = calloc(1, sizeof(component_boxart_t))) == NULL) {
        return NULL;
    }

    b->loading = true;

    path_t *path = path_init(storage_prefix, METADATA_BASE_DIRECTORY);

    if (game_code[1] == 'E' && game_code[2] == 'D') {
        // This is using a homebrew ROM ID, use the title for the file name instead.
        // Create a null-terminated copy of the title for safe string operations
        char safe_title[21];  // 20 chars + null terminator
        memcpy(safe_title, rom_title, 20);
        safe_title[20] = '\0';
        
        snprintf(boxart_path, sizeof(boxart_path), HOMEBREW_ID_SUBDIRECTORY"/%s", safe_title);
        path_push(path, boxart_path);
    }
    else {
        snprintf(boxart_path, sizeof(boxart_path), "%c/%c/%c/%c", game_code[0], game_code[1], game_code[2], game_code[3]);
        path_push(path, boxart_path);

        if (!directory_exists(path_get(path))) { // Allow boxart to not specify the region code.
            path_pop(path);
        }

        if (!directory_exists(path_get(path))) { // Fallback to the old boxart directory if metadata directory doesn't exist.
            
            // TODO: As a deprecated path, eventually this should be removed.
            path_free(path);
            path = path_init(storage_prefix, OLD_BOXART_DIRECTORY);
            path_push(path, boxart_path);

            if (!directory_exists(path_get(path))) { // Allow boxart to not specify the region code.
                path_pop(path);
            }
        }
    }

    debugf("Boxart: Using path %s\n", boxart_path);

    if (directory_exists(path_get(path))) {
        switch (current_image_view) {
            case IMAGE_GAMEPAK_FRONT:
                path_push(path, "gamepak_front.png");
                break;
            case IMAGE_GAMEPAK_BACK:
                path_push(path, "gamepak_back.png");
                break;
            case IMAGE_BOXART_BACK:
                path_push(path, "boxart_back.png");
                break;
            case IMAGE_BOXART_LEFT:
                path_push(path, "boxart_left.png");
                break;
            case IMAGE_BOXART_RIGHT:
                path_push(path, "boxart_right.png");
                break;
            case IMAGE_BOXART_BOTTOM:
                path_push(path, "boxart_bottom.png");
                break;
            case IMAGE_BOXART_TOP:
                path_push(path, "boxart_top.png");
                break;
            default:
                path_push(path, "boxart_front.png");
                break;
        }

        if (file_exists(path_get(path))) { 
            if (png_decoder_start(path_get(path), BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX, png_decoder_callback, b) == PNG_OK) {
                path_free(path);
                return b;
            }
        }
    }
    // TODO: return default image.

    path_free(path);
    free(b);

    return NULL;
}

/**
 * @brief Free the boxart component and its resources.
 *
 * @param b Pointer to the boxart component.
 */
void ui_components_boxart_free(component_boxart_t *b) {
    if (b) {
        if (b->loading) {
            jpeg_decoder_abort();
            png_decoder_abort();
        }
        if (b->image) {
            rspq_wait();
            surface_free(b->image);
            free(b->image);
        }
        free(b);
    }
}

/**
 * @brief Draw the boxart image or a loading placeholder.
 *
 * Draws the loaded boxart image at the appropriate position, or a placeholder if not loaded.
 *
 * @param b Pointer to the boxart component.
 */
void ui_components_boxart_draw(component_boxart_t *b) {
    int box_x = BOXART_X;
    int box_y = BOXART_Y;
    if (b && b->image && b->image->width <= BOXART_WIDTH_MAX && b->image->height <= BOXART_HEIGHT_MAX) {
        rdpq_mode_push();
            rdpq_set_mode_copy(true);
            if (b->image->height == BOXART_HEIGHT_MAX) {
                box_x = BOXART_X_JP;
                box_y = BOXART_Y_JP;
            } else if (b->image->width == BOXART_WIDTH_DD && b->image->height == BOXART_HEIGHT_DD) {
                box_x = BOXART_X_DD;
                box_y = BOXART_Y_DD;
            }
            rdpq_tex_blit(b->image, box_x, box_y, NULL);
        rdpq_mode_pop();
    }
}
