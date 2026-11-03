#include "theme.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "external_theme.h"

#define C(r, g, b) RGBA32((r), (g), (b), 0xFF)
#define P(r, g, b) RGBA32((r), (g), (b), 0xC0)
/* Colors are listed in menu_theme_color_t order. Gradient speed is in cycles
 * per second and warp is the fraction of the screen the corners drift. */
#define THEME_GRAD(v_name, v_style, v_speed, v_warp, ...) \
    { .name=(v_name), .colors={__VA_ARGS__}, .background_mode=THEME_BACKGROUND_VERTEX_GRADIENT, \
      .border_style=(v_style), .gradient_speed=(v_speed), .gradient_warp=(v_warp) }
#define THEME_SOLID(v_name, v_style, v_bg, ...) \
    { .name=(v_name), .colors={v_bg, __VA_ARGS__, v_bg, v_bg, v_bg, v_bg}, .background_mode=THEME_BACKGROUND_SOLID, \
      .border_style=(v_style) }
/* Four-color palettes, dark to light, given as (r,g,b) triples; the panel is a translucent c1. */
#define THEME_4C_MUTED(v_name, v_style, v_speed, v_warp, c0, c1, c2, c3, muted) \
    THEME_GRAD(v_name, v_style, v_speed, v_warp, C c0, P c1, C c3, C(250,250,250), C muted, C c3, C c2, C c1, C c2, C c0, \
      C(90,215,137), C(246,185,60), C c0, C c2, C c1, C c3)
#define THEME_4C(v_name, v_style, v_speed, v_warp, c0, c1, c2, c3) \
    THEME_4C_MUTED(v_name, v_style, v_speed, v_warp, c0, c1, c2, c3, c2)
#define THEME_CAT(v_name) { .name=(v_name), .background_mode=THEME_BACKGROUND_CATEGORY }

#define CUSTOM_THEME THEME_GRAD("Custom", THEME_BORDER_SOFT, .045f, .16f, C(8,12,28), P(24,38,63), C(104,224,255), C(248,252,255), C(154,184,202), C(65,190,235), C(33,112,159), C(18,55,91), C(45,137,181), C(2,5,15), C(90,215,137), C(246,185,60), C(7,15,48), C(53,122,214), C(104,51,168), C(37,218,189))

/* Built-in entries, copied into the registry before external packages are added. */
static const menu_theme_t builtin_themes[] = {
    THEME_CAT("Simple"),
    THEME_SOLID("Black and White", THEME_BORDER_PLAIN, C(0,0,0), C(0,0,0), C(255,255,255), C(255,255,255), C(160,160,160), C(255,255,255), C(72,72,72), C(0,0,0), C(48,48,48), C(0,0,0), C(255,255,255), C(255,255,255)),
    THEME_GRAD("N64", THEME_BORDER_PLAIN, .065f, .20f, C(4,7,13), P(27,34,45), C(236,240,247), C(255,255,255), C(160,168,181), C(59,124,245), C(72,84,106), C(50,60,75), C(91,105,126), C(0,0,0), C(112,255,112), C(255,153,0), C(36,92,224), C(229,54,67), C(44,181,89), C(246,193,48)),
    THEME_GRAD("Ocean", THEME_BORDER_PLAIN, .055f, .16f, C(2,12,39), P(5,38,66), C(75,224,255), C(238,252,255), C(137,190,211), C(35,201,238), C(12,112,151), C(8,52,81), C(17,112,150), C(0,9,25), C(86,255,170), C(255,205,73), C(1,8,38), C(8,92,151), C(9,43,91), C(37,224,210)),
    THEME_4C("Dawn", THEME_BORDER_SOFT, .038f, .14f, (20,21,61), (113,65,112), (239,133,121), (255,218,151)),
    THEME_GRAD("Sunset", THEME_BORDER_PLAIN, .045f, .18f, C(28,7,38), P(73,31,39), C(255,190,75), C(255,245,219), C(204,154,128), C(244,113,77), C(145,70,62), C(78,37,50), C(178,82,64), C(14,5,17), C(125,220,134), C(255,194,68), C(35,7,66), C(255,79,112), C(112,25,91), C(255,190,66)),
    THEME_GRAD("Pastel", THEME_BORDER_SOFT, .035f, .14f, C(38,31,69), P(78,64,102), C(202,191,255), C(255,248,253), C(208,190,215), C(255,163,189), C(139,108,155), C(93,76,117), C(118,196,184), C(17,12,32), C(140,231,171), C(255,209,103), C(114,102,192), C(255,161,190), C(111,198,191), C(242,207,255)),
    THEME_GRAD("Aurora", THEME_BORDER_DOUBLE, .075f, .22f, C(3,12,35), P(8,39,63), C(94,255,204), C(240,255,251), C(139,190,196), C(48,224,170), C(17,118,115), C(12,56,82), C(34,151,138), C(1,5,17), C(99,255,184), C(255,216,89), C(3,8,38), C(31,218,169), C(78,72,201), C(104,255,188)),
    THEME_4C("Twilight", THEME_BORDER_DOUBLE, .044f, .18f, (7,10,38), (41,37,91), (124,61,130), (244,114,130)),
    THEME_4C("Radioactive", THEME_BORDER_NODES, .071f, .20f, (4,13,2), (22,52,5), (103,167,12), (205,255,49)),
    THEME_GRAD("Game Rental", THEME_BORDER_DOUBLE, .040f, .13f, C(2,18,61), P(7,43,105), C(255,218,35), C(255,255,245), C(178,194,215), C(255,211,22), C(18,83,166), C(8,49,119), C(235,188,13), C(0,5,26), C(78,224,144), C(255,196,34), C(1,16,73), C(14,77,172), C(247,193,11), C(255,235,86)),
    THEME_4C("Arctic Night", THEME_BORDER_FACETS, .031f, .13f, (1,13,32), (9,51,78), (58,151,168), (180,244,224)),
    THEME_4C("Rose Gold", THEME_BORDER_DOUBLE, .027f, .10f, (43,20,26), (116,62,68), (206,135,125), (250,211,181)),
    THEME_4C("Mint Cream", THEME_BORDER_SOFT, .024f, .09f, (18,47,43), (66,126,107), (151,211,172), (235,248,215)),
    THEME_4C("Peach Soda", THEME_BORDER_BEADS, .052f, .18f, (55,18,34), (150,55,67), (247,126,96), (255,215,146)),
    THEME_4C("Lavender Fog", THEME_BORDER_SOFT, .021f, .08f, (25,22,50), (75,67,111), (147,135,177), (224,215,239)),
    THEME_4C("Tropical Punch", THEME_BORDER_BEADS, .066f, .22f, (17,18,65), (205,35,112), (255,121,72), (255,226,73)),
    THEME_4C("Deep Ocean", THEME_BORDER_DOUBLE, .029f, .12f, (0,7,24), (1,34,66), (3,94,119), (31,190,174)),
    THEME_4C("Monochrome Red", THEME_BORDER_PLAIN, .046f, .15f, (24,0,4), (80,7,15), (164,24,34), (248,92,92)),
    THEME_4C("Emerald City", THEME_BORDER_DOUBLE, .049f, .17f, (0,17,12), (4,65,42), (20,151,90), (130,239,155)),
    THEME_4C("Electric Blue", THEME_BORDER_NODES, .074f, .22f, (0,6,34), (0,45,105), (8,126,220), (73,231,255)),
    THEME_CAT("Hardware"),
    THEME_4C("White GBA", THEME_BORDER_PLAIN, .040f, .13f, (30,34,43), (132,135,143), (222,223,226), (250,250,248)),
    THEME_4C("Clear GBC", THEME_BORDER_PLAIN, .040f, .13f, (46,52,55), (112,123,120), (184,194,184), (241,245,228)),
    THEME_4C("Super Famicom", THEME_BORDER_PLAIN, .040f, .13f, (49,51,54), (120,122,119), (194,193,184), (237,235,220)),
    THEME_GRAD("Platinum GameCube", THEME_BORDER_PLAIN, .032f, .10f, C(31,33,35), P(66,69,72), C(187,188,189), C(250,251,251), C(206,208,210), C(147,151,153), C(100,106,106), C(66,69,72), C(122,127,128), C(8,8,11), C(79,198,127), C(235,177,64), C(66,69,72), C(147,151,153), C(100,106,106), C(187,188,189)),
    THEME_4C("Silver GBA", THEME_BORDER_PLAIN, .040f, .13f, (46,56,68), (112,126,135), (178,194,203), (229,235,237)),
    THEME_4C("North American SNES", THEME_BORDER_PLAIN, .040f, .13f, (42,39,51), (91,87,105), (158,156,164), (194,184,218)),
    THEME_GRAD("Charcoal N64", THEME_BORDER_PLAIN, .035f, .10f, C(9,9,10), P(35,35,37), C(194,194,197), C(244,244,244), C(151,151,154), C(66,115,190), C(76,76,81), C(52,52,56), C(89,89,94), C(2,2,2), C(61,184,86), C(234,176,49), C(16,18,18), C(90,85,89), C(56,74,69), C(145,146,150)),
    THEME_GRAD("Smoke Black N64", THEME_BORDER_PLAIN, .030f, .09f, C(5,7,9), P(25,31,35), C(157,177,184), C(239,244,245), C(130,145,150), C(89,126,140), C(54,71,78), C(43,52,57), C(72,91,99), C(1,2,3), C(77,194,124), C(230,169,61), C(2,3,5), C(53,66,73), C(13,18,22), C(135,157,166)),
    THEME_GRAD("Jet Black GameCube", THEME_BORDER_PLAIN, .028f, .08f, C(11,11,24), P(29,34,37), C(133,132,137), C(251,252,252), C(179,177,185), C(92,93,96), C(41,46,49), C(29,34,37), C(59,63,66), C(2,2,5), C(75,194,125), C(228,169,62), C(11,11,24), C(41,46,49), C(29,34,37), C(92,93,96)),
    THEME_4C_MUTED("Black GBA", THEME_BORDER_PLAIN, .040f, .13f, (16,19,22), (48,54,57), (70,76,79), (119,126,128), (190,198,201)),
    THEME_GRAD("Watermelon Red N64", THEME_BORDER_FACETS, .050f, .17f, C(40,3,14), P(91,17,39), C(255,150,179), C(255,240,245), C(205,139,157), C(233,47,99), C(143,22,60), C(105,25,48), C(187,36,78), C(14,0,4), C(102,223,136), C(255,188,55), C(66,26,40), C(175,46,83), C(116,32,61), C(220,102,128)),
    THEME_4C("Berry GBC", THEME_BORDER_PLAIN, .040f, .13f, (55,0,25), (133,0,55), (216,12,86), (255,91,136)),
    THEME_4C("Milky Pink GBA", THEME_BORDER_PLAIN, .040f, .13f, (33,32,40), (128,68,99), (179,101,137), (218,143,178)),
    THEME_GRAD("Daiei Hawks N64", THEME_BORDER_PLAIN, .045f, .14f, C(10,6,3), P(36,24,15), C(245,117,28), C(255,246,230), C(170,151,133), C(230,83,13), C(103,52,18), C(48,35,25), C(170,60,9), C(2,1,0), C(87,205,123), C(255,178,43), C(3,2,1), C(42,30,20), C(128,38,3), C(247,112,23)),
    THEME_GRAD("Fire Orange N64", THEME_BORDER_FACETS, .052f, .17f, C(37,13,1), P(82,34,8), C(255,177,83), C(255,244,220), C(202,157,119), C(241,100,22), C(143,57,10), C(98,43,14), C(190,76,14), C(13,4,0), C(112,217,117), C(255,184,45), C(35,8,0), C(235,70,8), C(100,30,2), C(255,185,71)),
    THEME_GRAD("Spice Orange GameCube", THEME_BORDER_PLAIN, .050f, .16f, C(35,11,1), P(100,67,56), C(243,187,150), C(252,250,248), C(210,164,132), C(233,123,46), C(181,83,29), C(100,67,56), C(234,142,79), C(12,4,0), C(105,216,122), C(255,188,45), C(100,67,56), C(233,123,46), C(181,83,29), C(243,187,150)),
    THEME_4C("Orange GBA", THEME_BORDER_PLAIN, .040f, .13f, (30,35,42), (151,65,31), (224,122,61), (246,174,113)),
    THEME_GRAD("Pikachu Orange/Blue", THEME_BORDER_NODES, .058f, .18f, C(28,13,9), P(71,38,24), C(137,210,241), C(255,244,220), C(190,165,140), C(246,117,37), C(128,72,48), C(91,54,40), C(90,166,205), C(10,4,3), C(90,211,133), C(255,190,43), C(35,12,4), C(235,91,18), C(43,119,178), C(153,220,244)),
    THEME_GRAD("Gold N64", THEME_BORDER_DOUBLE, .034f, .11f, C(31,20,5), P(76,54,15), C(246,205,85), C(255,247,218), C(197,170,112), C(218,163,42), C(130,94,20), C(92,69,25), C(172,126,27), C(11,7,1), C(89,198,112), C(255,190,54), C(28,15,2), C(166,111,17), C(81,53,7), C(255,221,113)),
    THEME_4C("Gold GBA", THEME_BORDER_PLAIN, .040f, .13f, (48,54,65), (133,116,88), (183,168,143), (218,205,180)),
    THEME_4C("Dandelion GBC", THEME_BORDER_PLAIN, .040f, .13f, (70,43,0), (188,128,4), (245,190,31), (255,226,103)),
    THEME_GRAD("Pikachu Blue/Yellow", THEME_BORDER_NODES, .060f, .19f, C(3,28,65), P(10,66,111), C(255,222,37), C(255,250,211), C(171,191,189), C(255,207,20), C(26,109,169), C(15,81,132), C(225,48,43), C(1,10,25), C(78,216,130), C(255,197,26), C(2,24,66), C(25,111,181), C(224,38,40), C(255,220,29)),
    THEME_GRAD("Jungle Green N64", THEME_BORDER_FACETS, .050f, .16f, C(2,23,9), P(8,58,25), C(117,239,143), C(239,255,238), C(128,189,140), C(38,195,83), C(17,110,48), C(13,74,34), C(31,147,66), C(0,8,3), C(89,242,125), C(255,203,69), C(12,54,31), C(66,163,81), C(28,143,78), C(115,182,101)),
    THEME_4C("Teal GBC", THEME_BORDER_PLAIN, .040f, .13f, (0,42,55), (0,111,139), (10,166,194), (104,222,229)),
    THEME_GRAD("Clear Blue/White N64", THEME_BORDER_FACETS, .040f, .13f, C(12,35,49), P(47,78,92), C(224,248,255), C(255,255,255), C(180,204,212), C(64,165,210), C(72,125,150), C(58,88,101), C(183,222,233), C(3,13,19), C(86,208,138), C(242,180,56), C(9,38,60), C(80,179,221), C(123,190,214), C(246,252,250)),
    THEME_GRAD("Clear Blue N64", THEME_BORDER_FACETS, .043f, .14f, C(4,21,38), P(15,52,79), C(194,240,255), C(248,253,255), C(155,190,207), C(91,186,233), C(42,104,148), C(25,68,100), C(65,142,189), C(1,8,16), C(91,221,145), C(255,198,70), C(2,18,39), C(48,143,211), C(8,57,98), C(203,244,255)),
    THEME_GRAD("Ice Blue N64", THEME_BORDER_FACETS, .050f, .16f, C(2,24,39), P(9,55,76), C(154,237,255), C(242,253,255), C(139,190,205), C(43,190,232), C(22,112,148), C(14,72,94), C(39,147,187), C(0,9,16), C(80,229,151), C(255,199,68), C(1,20,42), C(30,168,220), C(8,68,108), C(174,244,255)),
    THEME_4C("Milky Blue GBA", THEME_BORDER_PLAIN, .040f, .13f, (30,36,45), (86,91,132), (126,131,170), (171,174,209)),
    THEME_GRAD("Midnight Blue N64", THEME_BORDER_PLAIN, .037f, .12f, C(2,8,30), P(11,29,67), C(132,172,244), C(240,246,255), C(129,148,181), C(48,91,189), C(24,51,115), C(19,39,82), C(39,72,148), C(0,2,11), C(74,206,133), C(246,184,61), C(0,3,20), C(21,44,120), C(4,14,55), C(101,144,231)),
    THEME_GRAD("Indigo GameCube", THEME_BORDER_PLAIN, .042f, .14f, C(24,21,39), P(70,51,112), C(162,150,201), C(250,252,253), C(213,206,219), C(139,122,194), C(84,65,135), C(51,40,90), C(116,101,167), C(10,5,46), C(88,216,143), C(255,192,61), C(51,40,90), C(139,122,194), C(32,22,81), C(96,77,152)),
    THEME_4C_MUTED("Violet GBA", THEME_BORDER_PLAIN, .040f, .13f, (29,33,46), (54,42,109), (84,66,139), (135,116,191), (205,198,231)),
    THEME_4C_MUTED("Grape GBC", THEME_BORDER_PLAIN, .040f, .13f, (21,10,61), (65,39,133), (103,69,181), (160,130,224), (211,199,244)),
    THEME_GRAD("Grape Purple N64", THEME_BORDER_FACETS, .047f, .16f, C(22,7,37), P(60,24,82), C(213,160,255), C(250,242,255), C(178,146,196), C(155,73,211), C(88,42,123), C(71,36,92), C(122,57,164), C(7,2,13), C(92,221,141), C(255,191,64), C(17,4,35), C(126,43,184), C(55,18,91), C(220,156,255)),
    THEME_4C("Atomic Purple GBC", THEME_BORDER_PLAIN, .040f, .13f, (32,25,51), (88,72,119), (147,126,174), (213,201,225)),
    /* Package families are inserted here as they are discovered. */
    THEME_CAT("Custom"),
    CUSTOM_THEME,
};

#define BUILTIN_COUNT ((int)(sizeof(builtin_themes) / sizeof(builtin_themes[0])))

/* Registry: built-ins followed by external packages inserted into their families. */
static menu_theme_t *themes;
static int theme_count;
static const menu_theme_t *current_theme;
/* Per-frame copy of current_theme that parameter animation may change. */
static menu_theme_t rendered_theme;
/* Unshifted Custom colors: the copied theme with any color edits. */
static color_t custom_base_colors[THEME_COLOR_COUNT];
static char custom_base_name[64] = "Custom";
static int custom_hue_shift;
/* The border region authored by Custom's package while a built-in style replaces it. */
static menu_theme_rect_t custom_package_border;

static menu_theme_t *custom_theme(void) { return (menu_theme_t *)theme_get_by_name("Custom"); }
const menu_theme_t *theme_catalog(void) { return themes; }
int theme_catalog_count(void) { return theme_count; }
const menu_theme_t *theme_get(void) { return current_theme ? &rendered_theme : NULL; }
const menu_theme_t *theme_selected(void) { return current_theme; }
const menu_theme_t *theme_custom(void) { return custom_theme(); }

bool theme_is_category(const menu_theme_t *theme) {
    return theme && theme->background_mode == THEME_BACKGROUND_CATEGORY;
}

bool theme_is_custom(const menu_theme_t *theme) {
    return theme && !theme_is_category(theme) && strcmp(theme->name, "Custom") == 0;
}

bool theme_is_custom_base(const menu_theme_t *theme) {
    return theme && theme->package && !theme_is_custom(theme)
        && (theme->background_mode == THEME_BACKGROUND_SINGLE_UV
            || theme->background_mode == THEME_BACKGROUND_DUAL_UV
            || theme->background_mode == THEME_BACKGROUND_STATIC_IMAGE);
}

bool theme_has_package_border(const menu_theme_t *theme) {
    return theme->package && theme->package->regions.border.width && theme->package->regions.border.height;
}

const char *theme_category(const menu_theme_t *theme) {
    const char *category = NULL;
    for (int i = 0; i < theme_count; i++) {
        if (theme_is_category(&themes[i])) category = themes[i].name;
        else if (&themes[i] == theme) return category;
    }
    return NULL;
}

const menu_theme_t *theme_get_by_name(const char *name) {
    for (int i = 0; name && i < theme_count; i++) {
        if (!theme_is_category(&themes[i]) && strcasecmp(name, themes[i].name) == 0)
            return &themes[i];
    }
    return NULL;
}

static void render_reset(void) {
    rendered_theme = current_theme ? *current_theme : (menu_theme_t){0};
}

void theme_render_update(external_theme_asset_t *asset, float *ticks, float elapsed_ticks) {
    render_reset();
    external_theme_animate(&rendered_theme, asset, ticks, elapsed_ticks);
}

void theme_set(const menu_theme_t *theme) {
    if (theme && !theme_is_category(theme)) {
        current_theme = theme;
        render_reset();
    }
}

void theme_set_by_name(const char *name) { theme_set(theme_get_by_name(name)); }

int theme_animation_frame(const menu_theme_animation_t *animation, float ticks) {
    int count = animation->frame_count > 1 ? animation->frame_count : 1;
    if (count == 1) return 0;
    int step = (int)floorf(ticks / animation->frame_ticks);
    int mode = animation->flags & THEME_ANIMATION_MODE_MASK;
    int frame;
    if (mode == THEME_ANIMATION_PING_PONG) {
        int period = count * 2 - 2;
        frame = step % period;
        if (frame >= count) frame = period - frame;
    } else if (mode == THEME_ANIMATION_ONCE) {
        frame = step < count ? step : count - 1;
    } else {
        frame = step % count;
    }
    return animation->flags & THEME_ANIMATION_REVERSED ? count - 1 - frame : frame;
}

float theme_animation_advance(float ticks, float elapsed,
                              const menu_theme_animation_t *animation) {
    int count = animation->frame_count > 1 ? animation->frame_count : 1;
    if (count == 1) return 0.0f;
    ticks += elapsed;
    int mode = animation->flags & THEME_ANIMATION_MODE_MASK;
    float limit = animation->frame_ticks
        * (float)(mode == THEME_ANIMATION_PING_PONG ? count * 2 - 2 : count);
    if (mode == THEME_ANIMATION_ONCE) return ticks < limit ? ticks : limit;
    return fmodf(ticks, limit);
}

typedef struct {
    menu_theme_color_t channel;
    menu_theme_color_t linked_channel;
} custom_color_descriptor_t;

static const custom_color_descriptor_t custom_colors[THEME_CUSTOM_COLOR_COUNT] = {
    { THEME_COLOR_BORDER, THEME_COLOR_ACCENT },
    { THEME_COLOR_PANEL, THEME_COLOR_COUNT },
    { THEME_COLOR_TEXT, THEME_COLOR_COUNT },
    { THEME_COLOR_HIGHLIGHT, THEME_COLOR_TAB_ACTIVE },
    { THEME_COLOR_GRADIENT_TL, THEME_COLOR_COUNT },
    { THEME_COLOR_GRADIENT_TR, THEME_COLOR_COUNT },
    { THEME_COLOR_GRADIENT_BL, THEME_COLOR_COUNT },
    { THEME_COLOR_GRADIENT_BR, THEME_COLOR_COUNT },
};

color_t theme_hue_shift_color(color_t color, int degrees) {
    degrees = ((degrees % 360) + 360) % 360;
    if (!degrees) return color;
    float r=color.r/255.0f, g=color.g/255.0f, b=color.b/255.0f;
    float maximum=fmaxf(r,fmaxf(g,b)), minimum=fminf(r,fminf(g,b));
    float delta=maximum-minimum;
    if (delta == 0.0f) return color;
    float hue = maximum==r ? fmodf((g-b)/delta,6.0f)
              : maximum==g ? (b-r)/delta+2.0f : (r-g)/delta+4.0f;
    hue=fmodf(hue*60.0f+degrees+360.0f,360.0f)/60.0f;
    float x=delta*(1.0f-fabsf(fmodf(hue,2.0f)-1.0f)), rr=0,gg=0,bb=0;
    switch ((int)hue) {
        case 0: rr=delta; gg=x; break; case 1: rr=x; gg=delta; break;
        case 2: gg=delta; bb=x; break; case 3: gg=x; bb=delta; break;
        case 4: rr=x; bb=delta; break; default: rr=delta; bb=x; break;
    }
    float m=maximum-delta;
    return RGBA32((uint8_t)((rr+m)*255+0.5f),(uint8_t)((gg+m)*255+0.5f),
                  (uint8_t)((bb+m)*255+0.5f),color.a);
}

/* Integer HSV rotation for RGBA5551 images. Keep hue scaled by chroma
 * so conversion needs no floating-point arithmetic or per-channel division. */
uint16_t theme_hue_shift_rgba16(uint16_t value, int degrees) {
    if (!degrees) return value;
    int r = ((value >> 11) & 31) * 255 / 31;
    int g = ((value >> 6) & 31) * 255 / 31;
    int b = ((value >> 1) & 31) * 255 / 31;
    int maximum = r > g ? r : g;
    if (b > maximum) maximum = b;
    int minimum = r < g ? r : g;
    if (b < minimum) minimum = b;
    int delta = maximum - minimum;
    if (!delta) return value;

    int hue = maximum == r ? (g - b) * 60
            : maximum == g ? (b - r) * 60 + 120 * delta
            : (r - g) * 60 + 240 * delta;
    hue = (hue + degrees * delta + 360 * delta) % (360 * delta);
    int sector = hue / (60 * delta);
    int x = (60 * delta - abs(hue % (120 * delta) - 60 * delta) + 30) / 60;
    int channels[6][3] = {
        {delta, x, 0}, {x, delta, 0}, {0, delta, x},
        {0, x, delta}, {x, 0, delta}, {delta, 0, x},
    };
    return (((channels[sector][0] + minimum) >> 3) << 11)
         | (((channels[sector][1] + minimum) >> 3) << 6)
         | (((channels[sector][2] + minimum) >> 3) << 1) | (value & 1);
}


static void custom_apply_hue(void) {
    menu_theme_t *custom = custom_theme();
    for (int i = 0; i < THEME_COLOR_COUNT; i++)
        custom->colors[i] = theme_hue_shift_color(custom_base_colors[i], custom_hue_shift);
    render_reset();
}

static void theme_package_free(menu_theme_t *theme) {
    if (!theme->package) return;
    free((char *)theme->name);
    free(theme->package->path);
    free(theme->package->family);
    free(theme->package);
    theme->package = NULL;
}

/* Custom's package, and the authored border remembered for it, go together. */
static void custom_package_free(menu_theme_t *custom) {
    theme_package_free(custom);
    custom_package_border = (menu_theme_rect_t){0};
}

/* Replace Custom's settings; its colors come from the new entry's palette. */
static void custom_replace(const menu_theme_t *source, const char *base_name) {
    menu_theme_t *custom = custom_theme();
    external_theme_invalidate();
    custom_package_free(custom);
    *custom = *source;
    memcpy(custom_base_colors, source->colors, sizeof(custom_base_colors));
    snprintf(custom_base_name, sizeof(custom_base_name), "%s", base_name);
    custom_hue_shift = 0;
    custom_apply_hue();
}

void theme_custom_reset(void) {
    custom_replace(&builtin_themes[BUILTIN_COUNT - 1], "Custom");
}

color_t theme_custom_color(menu_theme_custom_color_t slot) {
    return slot >= 0 && slot < THEME_CUSTOM_COLOR_COUNT
        ? custom_theme()->colors[custom_colors[slot].channel] : C(255,255,255);
}

bool theme_custom_copy_from(const menu_theme_t *source) {
    if (!source || theme_is_category(source)) return false;
    if (theme_is_custom(source)) return true;
    if (!external_theme_prepare(source)) return false;

    menu_theme_t copy = *source;
    copy.name = "Custom";
    if (source->package) {
        /* Custom owns a copy of the package so it survives catalog changes. */
        copy.package = malloc(sizeof(*copy.package));
        if (!copy.package) return false;
        *copy.package = *source->package;
        copy.package->path = strdup(source->package->path);
        copy.package->family = strdup("Custom");
        copy.name = strdup("Custom");
        if (!copy.package->path || !copy.package->family || !copy.name) {
            theme_package_free(&copy);
            return false;
        }
    }
    custom_replace(&copy, source->name);
    return true;
}

const char *theme_custom_base_name(void) { return custom_base_name; }
int theme_custom_hue_shift(void) { return custom_hue_shift; }

void theme_custom_set_hue_shift(int degrees) {
    custom_hue_shift = ((degrees % 360) + 360) % 360;
    /* Package textures are hue shifted when decoded. */
    if (custom_theme()->package) external_theme_invalidate();
    custom_apply_hue();
}

bool theme_custom_has_background_image(void) {
    return custom_theme()->background_mode == THEME_BACKGROUND_CUSTOM_IMAGE;
}

void theme_custom_use_background_image(void) {
    menu_theme_t *custom = custom_theme();
    external_theme_invalidate();
    custom_package_free(custom);
    custom->name = "Custom";
    custom->background_mode = THEME_BACKGROUND_CUSTOM_IMAGE;
    render_reset();
}

void theme_custom_clear_background_image(void) {
    if (theme_custom_has_background_image()) theme_custom_clear_base();
}

void theme_custom_clear_base(void) {
    menu_theme_t *custom = custom_theme();
    external_theme_invalidate();
    custom_package_free(custom);
    custom->name = "Custom";
    strcpy(custom_base_name, "Custom");
    if (custom->background_mode != THEME_BACKGROUND_VERTEX_GRADIENT) {
        const menu_theme_t *defaults = &builtin_themes[BUILTIN_COUNT - 1];
        custom->background_mode = THEME_BACKGROUND_VERTEX_GRADIENT;
        custom->gradient_speed = defaults->gradient_speed;
        custom->gradient_warp = defaults->gradient_warp;
    }
    render_reset();
}

void theme_select_custom_image(void) {
    theme_custom_copy_from(current_theme);
    theme_custom_use_background_image();
    theme_set(custom_theme());
}

void theme_custom_get_palette(color_t colors[THEME_COLOR_COUNT]) {
    memcpy(colors, custom_base_colors, sizeof(custom_base_colors));
}

void theme_custom_set_palette(const color_t colors[THEME_COLOR_COUNT]) {
    memcpy(custom_base_colors, colors, sizeof(custom_base_colors));
    custom_apply_hue();
}

void theme_custom_set_color(menu_theme_custom_color_t slot, color_t color) {
    if (slot < 0 || slot >= THEME_CUSTOM_COLOR_COUNT) return;
    const custom_color_descriptor_t *descriptor = &custom_colors[slot];
    menu_theme_t *custom = custom_theme();
    if (slot != THEME_CUSTOM_PANEL) color.a = 0xFF;
    color_t current = custom->colors[descriptor->channel];
    color_t base = custom_base_colors[descriptor->channel];
    /* Alpha-only edits must not round-trip RGB through the hue conversion. */
    if (color.r != current.r || color.g != current.g || color.b != current.b)
        base = theme_hue_shift_color(color, -custom_hue_shift);
    base.a = color.a;
    custom_base_colors[descriptor->channel] = base;
    /* An edited color replaces any authored animation of that channel. */
    if (custom->package) custom->package->parameter_mask &= ~THEME_PARAM_BIT(descriptor->channel);
    if (descriptor->linked_channel != THEME_COLOR_COUNT) {
        custom_base_colors[descriptor->linked_channel] = base;
        if (custom->package) custom->package->parameter_mask &= ~THEME_PARAM_BIT(descriptor->linked_channel);
    }
    custom_apply_hue();
}

menu_theme_border_style_t theme_custom_border_style(void) {
    return custom_theme()->border_style;
}

void theme_custom_set_border_style(menu_theme_border_style_t style) {
    if (style < THEME_BORDER_PLAIN || style >= THEME_BORDER_COUNT) return;
    menu_theme_t *custom = custom_theme();
    custom->border_style = style;
    if (custom->package) {
        if (theme_has_package_border(custom)) custom_package_border = custom->package->regions.border;
        custom->package->regions.border = (menu_theme_rect_t){0};
        external_theme_invalidate();
    }
    render_reset();
}

bool theme_custom_has_package_border(void) {
    const menu_theme_t *custom = custom_theme();
    return custom->package && (theme_has_package_border(custom) || custom_package_border.width);
}

void theme_custom_use_package_border(void) {
    menu_theme_t *custom = custom_theme();
    if (!custom->package || theme_has_package_border(custom) || !custom_package_border.width) return;
    custom->package->regions.border = custom_package_border;
    external_theme_invalidate();
    render_reset();
}

const menu_theme_border_descriptor_t *theme_border_descriptor(const menu_theme_t *theme) {
    static const menu_theme_border_descriptor_t borders[THEME_BORDER_COUNT] = {
        [THEME_BORDER_PLAIN]     = { "border-plain",     2, 3, 1 },
        [THEME_BORDER_DOUBLE]    = { "border-double",    2, 3, 1 },
        [THEME_BORDER_FACETS]    = { "border-facets",    2, 3, 1 },
        [THEME_BORDER_BEADS]     = { "border-beads",     2, 3, 1 },
        [THEME_BORDER_BRACKETS]  = { "border-brackets",  2, 3, 4 },
        [THEME_BORDER_STITCHES]  = { "border-stitches",  2, 3, 1 },
        [THEME_BORDER_NODES]     = { "border-nodes",     2, 3, 1 },
        [THEME_BORDER_SCALLOPS]  = { "border-scallops",  2, 3, 1 },
        [THEME_BORDER_SOFT]      = { "border-soft",      2, 3, 1 },
    };
    return theme_has_package_border(theme) ? &theme->package->border : &borders[theme->border_style];
}

static int category_index(const char *name) {
    for (int i = 0; name && i < theme_count; i++)
        if (theme_is_category(&themes[i]) && strcasecmp(themes[i].name, name) == 0) return i;
    return -1;
}

/* Families created for packages, before the Custom category. Beyond the
 * limit, and for the reserved family Custom, packages go under External. */
#define THEME_FAMILY_LIMIT 12
static char family_names[THEME_FAMILY_LIMIT][64];
static int family_count;

static bool is_external_family(const char *name) { return strcasecmp(name, "External") == 0; }

static bool is_discovered_family(const menu_theme_t *theme) {
    if (!theme_is_category(theme)) return false;
    if (is_external_family(theme->name)) return true;
    for (int i = 0; i < family_count; i++)
        if (theme->name == family_names[i]) return true;
    return false;
}

/* Insert a family category alphabetically among the discovered ones, with
 * External last, all just before Custom. Capacity must already be reserved. */
static int add_family(const char *name) {
    int insertion = category_index("Custom");
    for (int i = 0; i < insertion; i++) {
        if (!is_discovered_family(&themes[i])) continue;
        if (is_external_family(themes[i].name)
                || (!is_external_family(name) && strcasecmp(name, themes[i].name) < 0)) { insertion = i; break; }
    }
    memmove(&themes[insertion + 1], &themes[insertion], (theme_count - insertion) * sizeof(themes[0]));
    themes[insertion] = (menu_theme_t) THEME_CAT("External");
    if (!is_external_family(name)) {
        snprintf(family_names[family_count], sizeof(family_names[0]), "%s", name);
        themes[insertion].name = family_names[family_count++];
    }
    theme_count++;
    return insertion;
}

/* Capacity for the descriptor and a new family category must already be reserved. */
static void register_external(const menu_theme_t *descriptor) {
    const menu_theme_t *existing = theme_get_by_name(descriptor->name);
    if (existing && existing->package && !existing->package->indexed_only && descriptor->package
            && !strcmp(existing->package->path, descriptor->package->path)) {
        /* The same package is already fully registered; discard the rediscovered descriptor. */
        menu_theme_t discarded = *descriptor;
        theme_package_free(&discarded);
        return;
    }
    if (existing) {
        int index = existing - themes;
        theme_package_free(&themes[index]);
        memmove(&themes[index], &themes[index + 1], (theme_count - index - 1) * sizeof(themes[0]));
        theme_count--;
    }

    const char *family = descriptor->package->family;
    bool reserved = strcasecmp(family, "Custom") == 0 || is_external_family(family);
    int category = reserved ? -1 : category_index(family);
    if (category < 0 && !reserved && family_count < THEME_FAMILY_LIMIT) category = add_family(family);
    if (category < 0) category = category_index("External");
    if (category < 0) category = add_family("External");

    /* Preserve the authored order of built-ins; alphabetize package themes. */
    int insertion = category + 1;
    while (insertion < theme_count && !theme_is_category(&themes[insertion])
            && !(themes[insertion].package && strcasecmp(descriptor->name, themes[insertion].name) < 0))
        insertion++;
    memmove(&themes[insertion + 1], &themes[insertion], (theme_count - insertion) * sizeof(themes[0]));
    themes[insertion] = *descriptor;
    theme_count++;
}

menu_theme_t *theme_registry_entry(const menu_theme_t *theme) {
    uintptr_t address = (uintptr_t)theme, first = (uintptr_t)themes;
    return theme && themes && address >= first && address < first + theme_count * sizeof(themes[0])
        ? &themes[theme - themes] : NULL;
}

bool theme_registry_init(void) {
    if (themes) return true;
    themes = malloc(sizeof(builtin_themes));
    if (!themes) return false;
    memcpy(themes, builtin_themes, sizeof(builtin_themes));
    theme_count = BUILTIN_COUNT;
    theme_custom_reset();
    theme_set_by_name("N64");
    return true;
}

bool theme_registry_extend(const menu_theme_t *external, int external_count) {
    if (!theme_registry_init()) return false;
    if (!external || external_count <= 0) return true;

    char current_name[64];
    snprintf(current_name, sizeof(current_name), "%s", current_theme->name);
    uintptr_t old_themes = (uintptr_t)themes;
    int old_index = current_theme - themes;
    uintptr_t old_package = (uintptr_t)current_theme->package;
    /* Reserve all descriptors and every family category they could still
     * create before transferring ownership. Registration itself cannot
     * allocate or fail. */
    int categories = THEME_FAMILY_LIMIT - family_count + (category_index("External") < 0);
    menu_theme_t *expanded = realloc(themes, (theme_count + external_count + categories) * sizeof(themes[0]));
    if (!expanded) return false;
    themes = expanded;
    for (int i = 0; i < external_count; i++) register_external(&external[i]);
    theme_set_by_name(current_name);
    /* The decoded asset is keyed by the theme's address, so it only survives
     * when the current entry is the same package at the same address. */
    if ((uintptr_t)themes != old_themes || current_theme - themes != old_index || (uintptr_t)current_theme->package != old_package)
        external_theme_invalidate();
    return true;
}

void theme_registry_close(void) {
    if (!themes) return;
    for (int i = 0; i < theme_count; i++) theme_package_free(&themes[i]);
    free(themes);
    themes = NULL;
    theme_count = 0;
    family_count = 0;
    current_theme = NULL;
    render_reset();
    custom_hue_shift = 0;
    strcpy(custom_base_name, "Custom");
}
