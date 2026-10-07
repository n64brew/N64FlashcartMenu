#include <libdragon.h>

#include "fonts.h"
#include "theme.h"
#include "utils/fs.h"

static rdpq_font_t *default_font;
static rdpq_font_t *large_numbers_font;

void fonts_apply_theme(void) {
    const menu_theme_t *theme = theme_get();
    if (large_numbers_font) {
        rdpq_font_style(large_numbers_font, STL_DEFAULT, &((rdpq_fontstyle_t) { .color = theme->warning }));
    }
    if (default_font) {
        rdpq_font_style(default_font, STL_DEFAULT, &((rdpq_fontstyle_t) { .color = theme->text }));
        rdpq_font_style(default_font, STL_GREEN, &((rdpq_fontstyle_t) { .color = theme->success }));
        rdpq_font_style(default_font, STL_BLUE, &((rdpq_fontstyle_t) { .color = theme->accent }));
        /* Directories use yellow and archives orange, so keep them apart. */
        rdpq_font_style(default_font, STL_YELLOW, &((rdpq_fontstyle_t) { .color = theme_hue_shift_color(theme->warning, 20) }));
        rdpq_font_style(default_font, STL_ORANGE, &((rdpq_fontstyle_t) { .color = theme->warning }));
        rdpq_font_style(default_font, STL_RED, &((rdpq_fontstyle_t) { .color = RGBA32(0xFF, 0x58, 0x58, 0xFF) }));
        rdpq_font_style(default_font, STL_GRAY, &((rdpq_fontstyle_t) { .color = theme->muted_text }));
    }
}

static void load_default_font (char *custom_font_path) {
    char *font_path = "rom:/menu/fonts/Firple-Bold.font64";

    if (custom_font_path != NULL && strlen(custom_font_path) > 0) {
        // Only check file_exists if custom_font_path is a valid filesystem path (not rom:/)
        if (strncmp(custom_font_path, "rom:/", 5) != 0 && file_exists(custom_font_path)) {
            font_path = custom_font_path;
        }
    }

    default_font = rdpq_font_load(font_path);
    large_numbers_font = rdpq_font_load("rom:/menu/fonts/digits/Firple-Bold.font64");
    rdpq_text_register_font(FNT_LARGE_NUMBERS, large_numbers_font);
    fonts_apply_theme();

    rdpq_text_register_font(FNT_DEFAULT, default_font);
}


void fonts_init (char *custom_font_path) {
    load_default_font(custom_font_path);
}
