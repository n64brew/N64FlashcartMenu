/**
 * @file file_list.c
 * @brief Implementation of the file list UI component.
 * @ingroup ui_components
 */

#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "../ui_components.h"
#include "../fonts.h"
#include "constants.h"

#define FILE_ENTRY_TICKER_SCROLL_SPEED  (0.5f)
#define FILE_ENTRY_TICKER_START_DELAY   (20)
#define FILE_ENTRY_TICKER_CHUNK_WIDTH   (1024)
#define FILE_ENTRY_TICKER_CHUNK_BYTES   (128)

static float file_list_ticker_offset = 0.0f;
static int file_list_ticker_delay_frames = FILE_ENTRY_TICKER_START_DELAY;
static int file_list_ticker_selected = -1;
static char *file_list_ticker_name = NULL;
static bool file_list_ticker_widths_valid = false;
static const rdpq_font_t *file_list_ticker_font = NULL;
static menu_font_style_t file_list_ticker_style = STL_DEFAULT;
static int file_list_ticker_name_width = 0;
static int file_list_ticker_padding_width = 0;

typedef struct {
    rdpq_paragraph_t *layout;
    float x;
} file_list_ticker_chunk_t;

static file_list_ticker_chunk_t *file_list_ticker_chunks;
static size_t file_list_ticker_chunk_count;

static void file_list_ticker_layout_free(void) {
    for (size_t i = 0; i < file_list_ticker_chunk_count; i++) {
        rdpq_paragraph_free(file_list_ticker_chunks[i].layout);
    }
    free(file_list_ticker_chunks);
    file_list_ticker_chunks = NULL;
    file_list_ticker_chunk_count = 0;
    file_list_ticker_widths_valid = false;
}

/* Keep every paragraph below libdragon's 2048-pixel coordinate limit, even
 * when archive entry paths are much longer than ordinary filenames. */
static bool file_list_ticker_layout_build(const char *name, menu_font_style_t style) {
    file_list_ticker_layout_free();
    float x = 0;
    const rdpq_font_t *font = rdpq_text_get_font(FNT_DEFAULT);
    size_t remaining = strlen(name);
    while (remaining) {
        size_t bytes = remaining < FILE_ENTRY_TICKER_CHUNK_BYTES ? remaining : FILE_ENTRY_TICKER_CHUNK_BYTES;
        rdpq_paragraph_t *chunk;
        for (;;) {
            while (bytes && ((unsigned char)name[bytes] & 0xC0) == 0x80) bytes--;
            if (!bytes) {
                bytes = 1;
                while (bytes < remaining && ((unsigned char)name[bytes] & 0xC0) == 0x80) bytes++;
            }
            /* Blank glyphs bypass libdragon's width wrapping. End after the
             * first one so spaces/tabs cannot overflow before the next glyph. */
            for (size_t i = 0; i < bytes;) {
                uint32_t codepoint = (unsigned char)name[i++];
                if (codepoint >= 0xC0) {
                    codepoint &= codepoint < 0xE0 ? 0x1F : codepoint < 0xF0 ? 0x0F : 0x07;
                    while (i < bytes && ((unsigned char)name[i] & 0xC0) == 0x80) {
                        codepoint = (codepoint << 6) | (name[i++] & 0x3F);
                    }
                }
                rdpq_font_gmetrics_t metrics;
                if (codepoint == '\t' ||
                    (rdpq_font_get_glyph_metrics(font, codepoint, &metrics) && metrics.x1 == 0)) {
                    bytes = i;
                    break;
                }
            }
            rdpq_paragraph_builder_begin(
                &(rdpq_textparms_t) {
                    .style_id = style, .width = FILE_ENTRY_TICKER_CHUNK_WIDTH,
                    .height = FILE_ENTRY_TICKER_CHUNK_WIDTH, .wrap = WRAP_CHAR,
                }, FNT_DEFAULT, NULL
            );
            rdpq_paragraph_builder_span(name, bytes);
            chunk = rdpq_paragraph_builder_end();
            if (chunk->nlines <= 1 && chunk->advance_x <= FILE_ENTRY_TICKER_CHUNK_WIDTH) break;
            rdpq_paragraph_free(chunk);
            bytes /= 2;
        }
        file_list_ticker_chunk_t *grown = realloc(
            file_list_ticker_chunks, (file_list_ticker_chunk_count + 1) * sizeof(*grown)
        );
        if (!grown) {
            rdpq_paragraph_free(chunk);
            file_list_ticker_layout_free();
            return false;
        }
        file_list_ticker_chunks = grown;
        grown[file_list_ticker_chunk_count++] = (file_list_ticker_chunk_t) { .layout = chunk, .x = x };
        x += chunk->advance_x;
        name += bytes;
        remaining -= bytes;
    }
    file_list_ticker_name_width = (int)x;
    return true;
}

static void file_list_ticker_render(float x, int y, int left, int right) {
    for (size_t i = 0; i < file_list_ticker_chunk_count; i++) {
        const file_list_ticker_chunk_t *chunk = &file_list_ticker_chunks[i];
        float chunk_x = x + chunk->x;
        if (chunk_x + chunk->layout->bbox.x1 < left || chunk_x + chunk->layout->bbox.x0 >= right) continue;
        rdpq_paragraph_render(chunk->layout, chunk_x, y - chunk->layout->advance_y);
    }
}

static rdpq_paragraph_t *file_list_layout_buffer;
static size_t file_list_layout_capacity;

static rdpq_paragraph_t *file_list_layout_get(size_t required_capacity) {
    if (required_capacity == 0) {
        required_capacity = 1;
    }

    if (required_capacity <= file_list_layout_capacity) {
        memset(file_list_layout_buffer, 0, sizeof(rdpq_paragraph_t));
        file_list_layout_buffer->capacity = required_capacity;
        return file_list_layout_buffer;
    }

    size_t new_capacity = file_list_layout_capacity > 0 ? file_list_layout_capacity : 64;
    while (new_capacity < required_capacity) {
        size_t prev = new_capacity;
        new_capacity += new_capacity / 2;
        if (new_capacity <= prev) {
            return NULL;
        }
    }

    if (new_capacity > (SIZE_MAX - sizeof(rdpq_paragraph_t)) / sizeof(rdpq_paragraph_char_t)) {
        return NULL;
    }

    size_t bytes = sizeof(rdpq_paragraph_t) + (sizeof(rdpq_paragraph_char_t) * new_capacity);
    rdpq_paragraph_t *grown = realloc(file_list_layout_buffer, bytes);
    if (!grown) {
        return NULL;
    }

    file_list_layout_buffer = grown;
    file_list_layout_capacity = new_capacity;

    memset(file_list_layout_buffer, 0, sizeof(rdpq_paragraph_t));
    file_list_layout_buffer->capacity = required_capacity;
    return file_list_layout_buffer;
}

void ui_components_file_list_free(void) {
    file_list_ticker_layout_free();
    free(file_list_layout_buffer);
    file_list_layout_buffer = NULL;
    file_list_layout_capacity = 0;
    free(file_list_ticker_name);
    file_list_ticker_name = NULL;
    file_list_ticker_selected = -1;
    file_list_ticker_widths_valid = false;
}

/**
 * @brief Format the file size into a human-readable string.
 *
 * @param buffer Buffer to store the formatted string.
 * @param size Size of the file in bytes.
 * @return Number of characters written to the buffer.
 */
static int format_file_size(char *buffer, int64_t size) {
    if (size < 0) {
        return sprintf(buffer, "unknown");
    } else if (size == 0) {
        return sprintf(buffer, "empty");
    } else if (size < 8 * 1024) {
        return sprintf(buffer, "%lld B", size);
    } else if (size < 4 * 1024 * 1024) {
        return sprintf(buffer, "%lld kB", size / 1024);
    } else if (size < 1 * 1024 * 1024 * 1024) {
        return sprintf(buffer, "%lld MB", size / 1024 / 1024);
    } else {
        return sprintf(buffer, "%lld GB", size / 1024 / 1024 / 1024);
    }
}

/**
 * @brief Draw the file list UI component.
 *
 * @param list Pointer to the list of file entries.
 * @param entries Number of entries in the list.
 * @param selected Index of the currently selected entry.
 */
void ui_components_file_list_draw(entry_t *list, int entries, int selected) {
    int starting_position = 0;

    if (selected < 0 || selected >= entries) {
        file_list_ticker_selected = -1;
        free(file_list_ticker_name);
        file_list_ticker_name = NULL;
        file_list_ticker_layout_free();
    } else if (selected != file_list_ticker_selected ||
               !file_list_ticker_name ||
               strcmp(list[selected].name, file_list_ticker_name) != 0) {
        char *new_name = strdup(list[selected].name);
        free(file_list_ticker_name);
        file_list_ticker_name = new_name;
        file_list_ticker_selected = selected;
        file_list_ticker_offset = 0.0f;
        file_list_ticker_delay_frames = FILE_ENTRY_TICKER_START_DELAY;
        file_list_ticker_layout_free();
    }

    if (entries > LIST_ENTRIES && selected >= (LIST_ENTRIES / 2)) {
        starting_position = selected - (LIST_ENTRIES / 2);
        if (starting_position >= entries - LIST_ENTRIES) {
            starting_position = entries - LIST_ENTRIES;
        }
    }

    ui_components_list_scrollbar_draw(selected, entries, LIST_ENTRIES);

    if (entries == 0) {
        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n"
            "^%02X** empty directory **",
            STL_GRAY
        );
    } else {
        rdpq_paragraph_t *file_list_layout;
        rdpq_paragraph_t *layout;
        int visible_entries = entries - starting_position;
        if (visible_entries > LIST_ENTRIES) {
            visible_entries = LIST_ENTRIES;
        }

        size_t name_lengths[LIST_ENTRIES];
        size_t total_length = 1;
        int name_x = VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL + 10;
        int name_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL;
        int name_width = FILE_LIST_MAX_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2);
        menu_font_style_t selected_style = STL_DEFAULT;

        for (int i = 0; i < visible_entries; i++) {
            int entry_index = starting_position + i;
            size_t length = strlen(list[entry_index].name);
            name_lengths[i] = length;
            total_length += length;
        }

        file_list_layout = file_list_layout_get(total_length);
        if (!file_list_layout) {
            ui_components_main_text_draw(
                STL_DEFAULT,
                ALIGN_LEFT, VALIGN_TOP,
                "\n"
                "^%02X** out of memory **",
                STL_GRAY
            );
            return;
        }

        rdpq_paragraph_builder_begin(
            &(rdpq_textparms_t) {
                .width = name_width,
                .height = LAYOUT_ACTIONS_SEPARATOR_Y - VISIBLE_AREA_Y0  - (TEXT_MARGIN_VERTICAL * 2),
                .wrap = WRAP_ELLIPSES,
                .line_spacing = TEXT_LINE_SPACING_ADJUST,
            },
            FNT_DEFAULT,
            file_list_layout
        );

        for (int i = 0; i < visible_entries; i++) {
            int entry_index = starting_position + i;

            entry_t *entry = &list[entry_index];

            menu_font_style_t style;

            switch (entry->type) {
                case ENTRY_TYPE_DIR: style = STL_YELLOW; break;
                case ENTRY_TYPE_ROM: style = STL_DEFAULT; break;
                case ENTRY_TYPE_DISK: style = STL_DEFAULT; break;
                case ENTRY_TYPE_EMULATOR: style = STL_DEFAULT; break;
                case ENTRY_TYPE_SAVE: style = STL_GREEN; break;
                case ENTRY_TYPE_IMAGE: style = STL_BLUE; break;
                case ENTRY_TYPE_MUSIC: style = STL_BLUE; break;
                case ENTRY_TYPE_TEXT: style = STL_ORANGE; break;
                case ENTRY_TYPE_ARCHIVE: style = STL_ORANGE; break;
                case ENTRY_TYPE_ARCHIVED: style = STL_DEFAULT; break;
                case ENTRY_TYPE_ROM_PATCH: style = STL_GREEN; break;
                case ENTRY_TYPE_ROM_CHEAT: style = STL_GREEN; break;
                case ENTRY_TYPE_ROM_META: style = STL_GREEN; break;
                case ENTRY_TYPE_OTHER: style = STL_GRAY; break;
                default: style = STL_GRAY; break;
            }

            rdpq_paragraph_builder_style(style);

            if (entry_index == selected) {
                selected_style = style;
            }

            rdpq_paragraph_builder_span(entry->name, name_lengths[i]);

            if ((i + 1) >= visible_entries) {
                break;
            }

            rdpq_paragraph_builder_newline();
        }

        layout = rdpq_paragraph_builder_end();

        int highlight_height = (layout->bbox.y1 - layout->bbox.y0) / layout->nlines;
        int highlight_y = VISIBLE_AREA_Y0 + TAB_HEIGHT + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL + ((selected - starting_position) * highlight_height);

        ui_components_box_draw(
            FILE_LIST_HIGHLIGHT_X,
            highlight_y,
            FILE_LIST_HIGHLIGHT_X + FILE_LIST_HIGHLIGHT_WIDTH,
            highlight_y + highlight_height,
            FILE_LIST_HIGHLIGHT_COLOR
        );

        rdpq_paragraph_render(
            layout,
            name_x,
            name_y
        );

        if (selected >= starting_position && selected < starting_position + visible_entries) {
            const char *selected_name = list[selected].name;
            const char *wrap_pad = "     ";
            const rdpq_font_t *font = rdpq_text_get_font(FNT_DEFAULT);

            if (file_list_ticker_name && (!file_list_ticker_widths_valid ||
                strcmp(selected_name, file_list_ticker_name) != 0 ||
                font != file_list_ticker_font || selected_style != file_list_ticker_style)) {
                file_list_ticker_widths_valid = false;
                int padding_bytes = (int)strlen(wrap_pad);
                rdpq_paragraph_t *padding_layout = rdpq_paragraph_build(
                    &(rdpq_textparms_t) { .style_id = selected_style, .width = FILE_ENTRY_TICKER_CHUNK_WIDTH },
                    FNT_DEFAULT,
                    wrap_pad, &padding_bytes
                );

                if (padding_layout && file_list_ticker_layout_build(selected_name, selected_style)) {
                    file_list_ticker_padding_width = (int)padding_layout->advance_x;
                    file_list_ticker_font = font;
                    file_list_ticker_style = selected_style;
                    file_list_ticker_widths_valid = true;
                }

                if (padding_layout) rdpq_paragraph_free(padding_layout);
            }

            if (file_list_ticker_widths_valid && file_list_ticker_name_width > name_width) {
                int first_baseline = INT_MAX;
                for (int i = 0; i < layout->nchars; i++) {
                    if (layout->chars[i].y < first_baseline) {
                        first_baseline = layout->chars[i].y;
                    }
                }

                int line_step = layout->nlines > 1
                    ? (int)((layout->advance_y - first_baseline) / (layout->nlines - 1))
                    : 0;
                int row = selected - starting_position;
                int ticker_y = name_y + first_baseline + row * line_step;
                int cycle_width = file_list_ticker_name_width + file_list_ticker_padding_width;

                if (file_list_ticker_delay_frames > 0) {
                    file_list_ticker_delay_frames--;
                } else {
                    file_list_ticker_offset += FILE_ENTRY_TICKER_SCROLL_SPEED;
                }
                if ((int)file_list_ticker_offset >= cycle_width) {
                    file_list_ticker_offset -= (float)cycle_width;
                }

                ui_components_box_draw(
                    name_x, highlight_y,
                    name_x + name_width, highlight_y + highlight_height,
                    FILE_LIST_HIGHLIGHT_COLOR
                );
                rdpq_set_scissor(name_x, highlight_y, name_x + name_width, highlight_y + highlight_height);
                float ticker_x = name_x - (int)file_list_ticker_offset;
                file_list_ticker_render(ticker_x, ticker_y, name_x, name_x + name_width);
                file_list_ticker_render(ticker_x + cycle_width, ticker_y, name_x, name_x + name_width);
                rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            }
        }

        rdpq_paragraph_builder_begin(
            &(rdpq_textparms_t) {
                .width = VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
                .height = LAYOUT_ACTIONS_SEPARATOR_Y - VISIBLE_AREA_Y0  - (TEXT_MARGIN_VERTICAL * 2),
                .align = ALIGN_RIGHT,
                .wrap = WRAP_ELLIPSES,
                .line_spacing = TEXT_LINE_SPACING_ADJUST,
            },
            FNT_DEFAULT,
            NULL
        );

        char file_size[16];

        for (int i = 0; i < visible_entries; i++) {
            entry_t *entry = &list[starting_position + i];

            if (entry->type != ENTRY_TYPE_DIR) {
                rdpq_paragraph_builder_span(file_size, format_file_size(file_size, entry->size));
            }
            // else {
            //     rdpq_paragraph_builder_span("[DIR]", 5);
            // }

            switch (entry->type) {
                case ENTRY_TYPE_DIR: 
                    ui_components_sprite_draw(SPRITE_FILETYPE_FOLDER, VISIBLE_AREA_X0 + VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - TEXT_MARGIN_HORIZONTAL - 551, VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (i * highlight_height)); 
                break;
                case ENTRY_TYPE_ARCHIVE: 
                    ui_components_sprite_draw(SPRITE_FILETYPE_COMPRESSED, VISIBLE_AREA_X0 + VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - TEXT_MARGIN_HORIZONTAL - 551, VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (i * highlight_height));
                break;
                case ENTRY_TYPE_ROM: 
                    ui_components_sprite_draw(SPRITE_FILETYPE_N64ROM, VISIBLE_AREA_X0 + VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - TEXT_MARGIN_HORIZONTAL - 551, VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (i * highlight_height));
                break;
                case ENTRY_TYPE_DISK: 
                    ui_components_sprite_draw(SPRITE_FILETYPE_N64DISK, VISIBLE_AREA_X0 + VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - TEXT_MARGIN_HORIZONTAL - 551, VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (i * highlight_height));
                break;
                case ENTRY_TYPE_MUSIC: 
                    ui_components_sprite_draw(SPRITE_FILETYPE_MUSIC, VISIBLE_AREA_X0 + VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - TEXT_MARGIN_HORIZONTAL - 551, VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (i * highlight_height));
                break;
                case ENTRY_TYPE_TEXT: 
                    ui_components_sprite_draw(SPRITE_FILETYPE_TEXT, VISIBLE_AREA_X0 + VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - TEXT_MARGIN_HORIZONTAL - 551, VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (i * highlight_height));
                break;
                case ENTRY_TYPE_IMAGE: 
                    ui_components_sprite_draw(SPRITE_FILETYPE_IMAGE, VISIBLE_AREA_X0 + VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - TEXT_MARGIN_HORIZONTAL - 551, VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (i * highlight_height));
                break;
                case ENTRY_TYPE_SAVE: 
                    ui_components_sprite_draw(SPRITE_FILETYPE_SAVE, VISIBLE_AREA_X0 + VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - TEXT_MARGIN_HORIZONTAL - 551, VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (i * highlight_height));
                break;
                default: 
                    ui_components_sprite_draw(SPRITE_FILETYPE_UNKNOWN, VISIBLE_AREA_X0 + VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - TEXT_MARGIN_HORIZONTAL - 551, VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL + (i * highlight_height));
                break;
            }

            if ((i + 1) >= visible_entries) {
                break;
            }

            rdpq_paragraph_builder_newline();
        }

        layout = rdpq_paragraph_builder_end();

        rdpq_paragraph_render(
            layout,
            VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
            VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL
        );

        rdpq_paragraph_free(layout);
    }
}
