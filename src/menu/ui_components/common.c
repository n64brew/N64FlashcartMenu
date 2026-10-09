/**
 * @file common.c
 * @brief Common UI components implementation
 * @ingroup ui_components
 */

#include <stdarg.h>

#include "../ui_components.h"
#include "../fonts.h"
#include "constants.h"

/**
 * @brief Draw a box with the specified color.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 * @param color The color of the box.
 */
void ui_components_box_draw (int x0, int y0, int x1, int y1, color_t color) {
    rdpq_mode_push();
        rdpq_set_mode_fill(color);
        rdpq_fill_rectangle(x0, y0, x1, y1);
    rdpq_mode_pop();
}

/* Two opaque contrasting outlines keep focus visible over every palette and image. */
void ui_components_focus_draw(int x0, int y0, int x1, int y1) {
    rdpq_mode_push();
    for (int inset = 0; inset < 4; inset += 2) {
        rdpq_set_mode_fill(inset ? RGBA32(255, 255, 255, 255) : RGBA32(0, 0, 0, 255));
        rdpq_fill_rectangle(x0, y0, x1, y0 + 2);
        rdpq_fill_rectangle(x0, y1 - 2, x1, y1);
        rdpq_fill_rectangle(x0, y0 + 2, x0 + 2, y1 - 2);
        rdpq_fill_rectangle(x1 - 2, y0 + 2, x1, y1 - 2);
        x0 += 2; y0 += 2; x1 -= 2; y1 -= 2;
    }
    rdpq_mode_pop();
}

static void ui_components_panel_draw(int x0, int y0, int x1, int y1) {
    color_t panel = theme_get()->panel;
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_set_prim_color(panel);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        /* RGB dithering happens after the blender, so RGBA16 quantizes the
         * composed panel-over-background color instead of banding the result.
         * RGBA32 preserves the blended color directly. */
        rdpq_mode_dithering(DITHER_SQUARE_NONE);
        rdpq_fill_rectangle(x0, y0, x1, y1);
    rdpq_mode_pop();
}

/**
 * @brief Draw a border with the specified color.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 * @param color The color of the border.
 */
static void ui_components_border_draw_internal (int x0, int y0, int x1, int y1, color_t color) {
    rdpq_mode_push();
        rdpq_set_mode_fill(color);
        rdpq_fill_rectangle(x0 - BORDER_THICKNESS, y0 - BORDER_THICKNESS, x1 + BORDER_THICKNESS, y0);
        rdpq_fill_rectangle(x0 - BORDER_THICKNESS, y1, x1 + BORDER_THICKNESS, y1 + BORDER_THICKNESS);
        rdpq_fill_rectangle(x0 - BORDER_THICKNESS, y0, x0, y1);
        rdpq_fill_rectangle(x1, y0, x1 + BORDER_THICKNESS, y1);
    rdpq_mode_pop();
}

/**
 * @brief Draw a border with the default border color.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 */
void ui_components_border_draw (int x0, int y0, int x1, int y1) {
    ui_components_border_draw_internal(x0, y0, x1, y1, BORDER_COLOR);
}

/**
 * @brief Draw the layout with tabs.
 */
void ui_components_layout_draw_tabbed (void) {
    ui_components_panel_draw(VISIBLE_AREA_X0, VISIBLE_AREA_Y0 + TAB_HEIGHT + BORDER_THICKNESS,
        VISIBLE_AREA_X1, VISIBLE_AREA_Y1);
    ui_components_border_draw(
        VISIBLE_AREA_X0,
        VISIBLE_AREA_Y0 + TAB_HEIGHT + BORDER_THICKNESS,
        VISIBLE_AREA_X1,
        VISIBLE_AREA_Y1
    );

    ui_components_box_draw(
        VISIBLE_AREA_X0,
        LAYOUT_ACTIONS_SEPARATOR_Y,
        VISIBLE_AREA_X1,
        LAYOUT_ACTIONS_SEPARATOR_Y + BORDER_THICKNESS,
        BORDER_COLOR
    );
}

/**
 * @brief Draw the layout.
 */
void ui_components_layout_draw (void) {
    ui_components_panel_draw(VISIBLE_AREA_X0, VISIBLE_AREA_Y0, VISIBLE_AREA_X1, VISIBLE_AREA_Y1);
    ui_components_border_draw(
        VISIBLE_AREA_X0,
        VISIBLE_AREA_Y0,
        VISIBLE_AREA_X1,
        VISIBLE_AREA_Y1
    );
    ui_components_box_draw(
        VISIBLE_AREA_X0,
        LAYOUT_ACTIONS_SEPARATOR_Y,
        VISIBLE_AREA_X1,
        LAYOUT_ACTIONS_SEPARATOR_Y + BORDER_THICKNESS,
        BORDER_COLOR
    );
}

/**
 * @brief Draw a progress bar.
 * 
 * @param x0 The x-coordinate of the top-left corner.
 * @param y0 The y-coordinate of the top-left corner.
 * @param x1 The x-coordinate of the bottom-right corner.
 * @param y1 The y-coordinate of the bottom-right corner.
 * @param progress The progress value (0.0 to 1.0).
 */
void ui_components_progressbar_draw (int x0, int y0, int x1, int y1, float progress) {    
    float progress_width = progress * (x1 - x0);

    ui_components_box_draw(x0, y0, x0 + progress_width, y1, PROGRESSBAR_DONE_COLOR);
    ui_components_box_draw(x0 + progress_width, y0, x1, y1, PROGRESSBAR_BG_COLOR);
}

/**
 * @brief Draw a seek bar.
 * 
 * @param position The position value (0.0 to 1.0).
 */
void ui_components_seekbar_draw (float position) {
    int x0 = SEEKBAR_X;
    int y0 = SEEKBAR_Y;
    int x1 = SEEKBAR_X + SEEKBAR_WIDTH;
    int y1 = SEEKBAR_Y + SEEKBAR_HEIGHT;

    ui_components_border_draw(x0, y0, x1, y1);
    ui_components_progressbar_draw(x0, y0, x1, y1, position);
}

/**
 * @brief Draw a loader.
 * 
 * @param progress The progress value (0.0 to 1.0).
 * @param msg The message to display truncated to 30 characters.
 */
void ui_components_loader_draw (float progress, const char *msg) {
    int x0 = LOADER_X;
    int y0 = LOADER_Y;
    int x1 = LOADER_X + LOADER_WIDTH;
    int y1 = LOADER_Y + LOADER_HEIGHT;

    ui_components_border_draw(x0, y0, x1, y1);
    ui_components_progressbar_draw(x0, y0, x1, y1, progress);

    if (msg != NULL) {
        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_CENTER,
            "\n%.30s",
            msg
        );
    }
}

/**
 * @brief Draw a scrollbar.
 * 
 * @param x The x-coordinate of the top-left corner.
 * @param y The y-coordinate of the top-left corner.
 * @param width The width of the scrollbar.
 * @param height The height of the scrollbar.
 * @param position The current position.
 * @param items The total number of items.
 * @param visible_items The number of visible items.
 */
void ui_components_scrollbar_draw (int x, int y, int width, int height, int position, int items, int visible_items) {
    if (items <= 1 || items <= visible_items) {
        ui_components_box_draw(x, y, x + width, y + height, SCROLLBAR_INACTIVE_COLOR);
    } else {
        int scroll_height = (int) ((visible_items / (float) (items)) * height);
        float scroll_position = ((position / (float) (items - 1)) * (height - scroll_height));

        ui_components_box_draw(x, y, x + width, y + height, SCROLLBAR_BG_COLOR);
        ui_components_box_draw(x, y + scroll_position, x + width, y + scroll_position + scroll_height, SCROLLBAR_POSITION_COLOR);
    }
}

/**
 * @brief Draw a list scrollbar.
 * 
 * @param position The current position.
 * @param items The total number of items.
 * @param visible_items The number of visible items.
 */
void ui_components_list_scrollbar_draw (int position, int items, int visible_items) {
    ui_components_scrollbar_draw(
        LIST_SCROLLBAR_X,
        LIST_SCROLLBAR_Y,
        LIST_SCROLLBAR_WIDTH,
        LIST_SCROLLBAR_HEIGHT,
        position,
        items,
        visible_items
    );
}

/**
 * @brief Draw a dialog box.
 * 
 * @param width The width of the dialog box.
 * @param height The height of the dialog box.
 */
void ui_components_dialog_draw (int width, int height) {
    int x0 = DISPLAY_CENTER_X - (width / 2);
    int y0 = DISPLAY_CENTER_Y - (height / 2);
    int x1 = DISPLAY_CENTER_X + (width / 2);
    int y1 = DISPLAY_CENTER_Y + (height / 2);

    ui_components_border_draw(x0, y0, x1, y1);
    ui_components_box_draw(x0, y0, x1, y1, DIALOG_BG_COLOR);
}

/**
 * @brief Draw a message box with formatted text.
 * 
 * @param fmt The format string.
 * @param ... The format arguments.
 */
void ui_components_messagebox_draw (char *fmt, ...) {
    char buffer[512];
    size_t nbytes = sizeof(buffer);

    va_list va;
    va_start(va, fmt);
    char *formatted = vasnprintf(buffer, &nbytes, fmt, va);
    va_end(va);

    int paragraph_nbytes = nbytes;

    rdpq_paragraph_t *paragraph = rdpq_paragraph_build(&(rdpq_textparms_t) {
        .width = MESSAGEBOX_MAX_WIDTH,
        .height = VISIBLE_AREA_HEIGHT,
        .align = ALIGN_CENTER,
        .valign = VALIGN_CENTER,
        .wrap = WRAP_WORD,
        .line_spacing = TEXT_LINE_SPACING_ADJUST,
    }, FNT_DEFAULT, formatted, &paragraph_nbytes);

    if (formatted != buffer) {
        free(formatted);
    }

    ui_components_dialog_draw(
        paragraph->bbox.x1 - paragraph->bbox.x0 + MESSAGEBOX_MARGIN,
        paragraph->bbox.y1 - paragraph->bbox.y0 + MESSAGEBOX_MARGIN
    );

    rdpq_paragraph_render(paragraph, DISPLAY_CENTER_X - (MESSAGEBOX_MAX_WIDTH / 2), VISIBLE_AREA_Y0);

    rdpq_paragraph_free(paragraph);
}

/**
 * @brief Draw the main text with formatted content.
 * 
 * @param style The font style.
 * @param align The horizontal alignment.
 * @param valign The vertical alignment.
 * @param fmt The format string.
 * @param ... The format arguments.
 */
void ui_components_main_text_draw (menu_font_type_t style, rdpq_align_t align, rdpq_valign_t valign, char *fmt, ...) {
    char buffer[1024];
    size_t nbytes = sizeof(buffer);

    va_list va;
    va_start(va, fmt);
    char *formatted = vasnprintf(buffer, &nbytes, fmt, va);
    va_end(va);

    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .style_id = style,
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = LAYOUT_ACTIONS_SEPARATOR_Y - OVERSCAN_HEIGHT - (TEXT_MARGIN_VERTICAL * 2),
            .align = align,
            .valign = valign,
            .wrap = WRAP_WORD,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL,
        formatted,
        nbytes
    );

    if (formatted != buffer) {
        free(formatted);
    }
}

/**
 * @brief Draw the actions bar text with formatted content.
 * 
 * @param style The font style.
 * @param align The horizontal alignment.
 * @param valign The vertical alignment.
 * @param fmt The format string.
 * @param ... The format arguments.
 */
void ui_components_actions_bar_text_draw (menu_font_type_t style, rdpq_align_t align, rdpq_valign_t valign, char *fmt, ...) {
    char buffer[256];
    size_t nbytes = sizeof(buffer);

    va_list va;
    va_start(va, fmt);
    char *formatted = vasnprintf(buffer, &nbytes, fmt, va);
    va_end(va);

    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .style_id = style,
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = VISIBLE_AREA_Y1 - LAYOUT_ACTIONS_SEPARATOR_Y - BORDER_THICKNESS - (TEXT_MARGIN_VERTICAL * 2),
            .align = align,
            .valign = valign,
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        LAYOUT_ACTIONS_SEPARATOR_Y + BORDER_THICKNESS + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL,
        formatted,
        nbytes
    );

    if (formatted != buffer) {
        free(formatted);
    }
}

/**
 * @brief Draw a block of text inside an explicit rectangle.
 *
 * @param x The x-coordinate of the top-left corner.
 * @param y The y-coordinate of the top-left corner.
 * @param width Width of the rectangle.
 * @param height Height of the rectangle.
 * @param style Font style to draw with.
 * @param align Horizontal alignment within the rectangle.
 * @param wrap Wrapping mode for text that exceeds the rectangle.
 * @param text The text to draw.
 */
void ui_components_text_draw (int x, int y, int width, int height, menu_font_type_t style,
                              rdpq_align_t align, rdpq_textwrap_t wrap, const char *text) {
    rdpq_text_print(
        &(rdpq_textparms_t) {
            .style_id = style,
            .width = width,
            .height = height,
            .align = align,
            .valign = VALIGN_TOP,
            .wrap = wrap,
        },
        FNT_DEFAULT,
        x,
        y,
        text
    );
}

/**
 * @brief Draw a label/value row inside a settings pane.
 *
 * The label is left aligned and the value is right aligned, with the value
 * column starting at a fixed fraction of the pane width.
 *
 * @param y The y-coordinate of the top of the row.
 * @param label The row label.
 * @param value The row value.
 * @param selected Whether the row is highlighted.
 */
void ui_components_settings_row_draw (int y, const char *label, const char *value, bool selected) {
    int x0 = SETTINGS_PANE_X0 + 10;
    int x1 = SETTINGS_PANE_X1 - 10;
    int value_x = x0 + (((x1 - x0) * 55) / 100);

    if (selected) {
        ui_components_box_draw(x0, y, x1, y + SETTINGS_ROW_HEIGHT - 2, FILE_LIST_HIGHLIGHT_COLOR);
        ui_components_focus_draw(x0, y, x1, y + SETTINGS_ROW_HEIGHT - 2);
    }

    ui_components_text_draw(
        x0 + 8, y + 4, value_x - x0 - 16, SETTINGS_ROW_HEIGHT,
        STL_DEFAULT, ALIGN_LEFT, WRAP_ELLIPSES, label
    );
    ui_components_text_draw(
        value_x, y + 4, x1 - value_x - 8, SETTINGS_ROW_HEIGHT,
        selected ? STL_DEFAULT : STL_GRAY, ALIGN_RIGHT, WRAP_ELLIPSES, value
    );
}

/**
 * @brief Draw the tabs.
 *
 * @param text Array of tab text.
 * @param count Number of tabs.
 * @param selected Index of the selected tab.
 * @param width Width of each tab.
 */
void ui_components_tabs_draw(const char **text, int count, int selected, float width ) {
    float starting_x = VISIBLE_AREA_X0;

    float x = starting_x;
    float y = OVERSCAN_HEIGHT;    
    float height = TAB_HEIGHT;

    // first draw the tabs that are not selected
    for(int i=0;i< count;i++) {
        if(i != selected) {
            ui_components_box_draw(
                x,
                y,
                x + width,
                y + height,
                TAB_INACTIVE_BACKGROUND_COLOR
            );

            ui_components_border_draw_internal(
                x,
                y,
                x + width,
                y + height,
                TAB_INACTIVE_BORDER_COLOR
            );
        }
        x += width;
    }
    
    // draw the selected tab (so it shows up on top of the others)
    if(selected >= 0 && selected < count) {
        x = starting_x + (width * selected);

        ui_components_box_draw(
            x,
            y - TAB_SELECTED_LIFT,
            x + width,
            y + height,
            TAB_ACTIVE_BACKGROUND_COLOR
        );

        ui_components_border_draw_internal(
            x,
            y - TAB_SELECTED_LIFT,
            x + width,
            y + height,
            TAB_ACTIVE_BORDER_COLOR
        );
    }

    // write the text on the tabs
    rdpq_textparms_t tab_textparms = {
        .width = width,
        .height = 24,
        .align = ALIGN_CENTER,
        .wrap = WRAP_NONE
    };
    x = starting_x;
    for(int i=0;i< count;i++) {
        rdpq_text_print(
            &tab_textparms,
            FNT_DEFAULT,
            x,
            y,
            text[i]
        );
        x += width;
    }
}

void ui_component_value_editor(const char **header_text, const char **value_text, int count, int selected, float width_adjustment ) {
    float field_width = (VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2)) / width_adjustment;
    float starting_x = DISPLAY_CENTER_X - (field_width * count / 2.0f);

    float x = starting_x;
    float y = DISPLAY_CENTER_Y;    
    float height = TAB_HEIGHT;

    // first draw the values that are not selected
    for(int i=0;i< count;i++) {
        if(i != selected) {
            ui_components_box_draw(
                x,
                y,
                x + field_width,
                y + height + 24,
                TAB_INACTIVE_BACKGROUND_COLOR
            );
        }
        x += field_width;
    }
    
    // draw the selected value (so it shows up on top of the others)
    if(selected >= 0 && selected < count) {
        x = starting_x + (field_width * selected);

        ui_components_box_draw(
            x,
            y,
            x + field_width,
            y + height + 24,
            TAB_ACTIVE_BACKGROUND_COLOR
        );
    }

    // write the text on the value boxes
    rdpq_textparms_t value_textparms = {
        .width = field_width,
        .height = 24,
        .align = ALIGN_CENTER,
        .wrap = WRAP_NONE
    };
    x = starting_x;
    for(int i=0;i< count;i++) {
        rdpq_text_print(
            &value_textparms,
            FNT_DEFAULT,
            x,
            y,
            header_text[i]
        );

        rdpq_text_print(
            &value_textparms,
            FNT_DEFAULT,
            x,
            y + 24,
            value_text[i]
        );
        x += field_width;
    }

    // draw the border around the value boxes
    ui_components_border_draw (starting_x, y, x, y + height + 24);
}
