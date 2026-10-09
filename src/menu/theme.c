#include "theme.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define C(r, g, b) RGBA32((r), (g), (b), 0xFF)
#define P(r, g, b) RGBA32((r), (g), (b), 0xC0)
/* Colors are listed in menu_theme_color_t order. */
#define THEME_GRAD(v_name, ...) \
    { .name=(v_name), .colors={__VA_ARGS__}, .background_mode=THEME_BACKGROUND_VERTEX_GRADIENT }
#define THEME_SOLID(v_name, v_bg, ...) \
    { .name=(v_name), .colors={v_bg, __VA_ARGS__, v_bg, v_bg, v_bg, v_bg}, .background_mode=THEME_BACKGROUND_SOLID }
/* Four-color palettes, dark to light, given as (r,g,b) triples; the panel is a translucent c1. */
#define THEME_4C_MUTED(v_name, c0, c1, c2, c3, muted) \
    THEME_GRAD(v_name, C c0, P c1, C c3, C(250,250,250), C muted, C c3, C c2, C c1, C c2, C c0, \
      C(90,215,137), C(246,185,60), C c0, C c2, C c1, C c3)
#define THEME_4C(v_name, c0, c1, c2, c3) THEME_4C_MUTED(v_name, c0, c1, c2, c3, c2)
#define THEME_CAT(v_name) { .name=(v_name), .background_mode=THEME_BACKGROUND_CATEGORY }

#define CUSTOM_THEME THEME_GRAD("Custom", C(8,12,28), P(24,38,63), C(104,224,255), C(248,252,255), C(154,184,202), C(65,190,235), C(33,112,159), C(18,55,91), C(45,137,181), C(2,5,15), C(90,215,137), C(246,185,60), C(7,15,48), C(53,122,214), C(104,51,168), C(37,218,189))

/* Fixed catalog: entries and borrowed pointers remain valid for the menu's lifetime. */
static menu_theme_t themes[] = {
    THEME_CAT("Simple"),
    THEME_SOLID("Black and White", C(0,0,0), C(0,0,0), C(255,255,255), C(255,255,255), C(160,160,160), C(255,255,255), C(72,72,72), C(0,0,0), C(48,48,48), C(0,0,0), C(255,255,255), C(255,255,255)),
    THEME_GRAD("N64", C(4,7,13), P(27,34,45), C(236,240,247), C(255,255,255), C(160,168,181), C(59,124,245), C(72,84,106), C(50,60,75), C(91,105,126), C(0,0,0), C(112,255,112), C(255,153,0), C(36,92,224), C(229,54,67), C(44,181,89), C(246,193,48)),
    THEME_GRAD("Ocean", C(2,12,39), P(5,38,66), C(75,224,255), C(238,252,255), C(137,190,211), C(35,201,238), C(12,112,151), C(8,52,81), C(17,112,150), C(0,9,25), C(86,255,170), C(255,205,73), C(1,8,38), C(8,92,151), C(9,43,91), C(37,224,210)),
    THEME_4C("Dawn", (20,21,61), (113,65,112), (239,133,121), (255,218,151)),
    THEME_GRAD("Sunset", C(28,7,38), P(73,31,39), C(255,190,75), C(255,245,219), C(204,154,128), C(244,113,77), C(145,70,62), C(78,37,50), C(178,82,64), C(14,5,17), C(125,220,134), C(255,194,68), C(35,7,66), C(255,79,112), C(112,25,91), C(255,190,66)),
    THEME_GRAD("Pastel", C(38,31,69), P(78,64,102), C(202,191,255), C(255,248,253), C(208,190,215), C(255,163,189), C(139,108,155), C(93,76,117), C(118,196,184), C(17,12,32), C(140,231,171), C(255,209,103), C(114,102,192), C(255,161,190), C(111,198,191), C(242,207,255)),
    THEME_GRAD("Aurora", C(3,12,35), P(8,39,63), C(94,255,204), C(240,255,251), C(139,190,196), C(48,224,170), C(17,118,115), C(12,56,82), C(34,151,138), C(1,5,17), C(99,255,184), C(255,216,89), C(3,8,38), C(31,218,169), C(78,72,201), C(104,255,188)),
    THEME_4C("Twilight", (7,10,38), (41,37,91), (124,61,130), (244,114,130)),
    THEME_4C("Radioactive", (4,13,2), (22,52,5), (103,167,12), (205,255,49)),
    THEME_GRAD("Game Rental", C(2,18,61), P(7,43,105), C(255,218,35), C(255,255,245), C(178,194,215), C(255,211,22), C(18,83,166), C(8,49,119), C(235,188,13), C(0,5,26), C(78,224,144), C(255,196,34), C(1,16,73), C(14,77,172), C(247,193,11), C(255,235,86)),
    THEME_4C("Arctic Night", (1,13,32), (9,51,78), (58,151,168), (180,244,224)),
    THEME_4C("Rose Gold", (43,20,26), (116,62,68), (206,135,125), (250,211,181)),
    THEME_4C("Mint Cream", (18,47,43), (66,126,107), (151,211,172), (235,248,215)),
    THEME_4C("Peach Soda", (55,18,34), (150,55,67), (247,126,96), (255,215,146)),
    THEME_4C("Lavender Fog", (25,22,50), (75,67,111), (147,135,177), (224,215,239)),
    THEME_4C("Tropical Punch", (17,18,65), (205,35,112), (255,121,72), (255,226,73)),
    THEME_4C("Deep Ocean", (0,7,24), (1,34,66), (3,94,119), (31,190,174)),
    THEME_4C("Monochrome Red", (24,0,4), (80,7,15), (164,24,34), (248,92,92)),
    THEME_4C("Emerald City", (0,17,12), (4,65,42), (20,151,90), (130,239,155)),
    THEME_4C("Electric Blue", (0,6,34), (0,45,105), (8,126,220), (73,231,255)),
    THEME_CAT("Hardware"),
    THEME_4C("White GBA", (30,34,43), (132,135,143), (222,223,226), (250,250,248)),
    THEME_4C("Clear GBC", (46,52,55), (112,123,120), (184,194,184), (241,245,228)),
    THEME_4C("Super Famicom", (49,51,54), (120,122,119), (194,193,184), (237,235,220)),
    THEME_GRAD("Platinum GameCube", C(31,33,35), P(66,69,72), C(187,188,189), C(250,251,251), C(206,208,210), C(147,151,153), C(100,106,106), C(66,69,72), C(122,127,128), C(8,8,11), C(79,198,127), C(235,177,64), C(66,69,72), C(147,151,153), C(100,106,106), C(187,188,189)),
    THEME_4C("Silver GBA", (46,56,68), (112,126,135), (178,194,203), (229,235,237)),
    THEME_4C("North American SNES", (42,39,51), (91,87,105), (158,156,164), (194,184,218)),
    THEME_GRAD("Charcoal N64", C(9,9,10), P(35,35,37), C(194,194,197), C(244,244,244), C(151,151,154), C(66,115,190), C(76,76,81), C(52,52,56), C(89,89,94), C(2,2,2), C(61,184,86), C(234,176,49), C(16,18,18), C(90,85,89), C(56,74,69), C(145,146,150)),
    THEME_GRAD("Smoke Black N64", C(5,7,9), P(25,31,35), C(157,177,184), C(239,244,245), C(130,145,150), C(89,126,140), C(54,71,78), C(43,52,57), C(72,91,99), C(1,2,3), C(77,194,124), C(230,169,61), C(2,3,5), C(53,66,73), C(13,18,22), C(135,157,166)),
    THEME_GRAD("Jet Black GameCube", C(11,11,24), P(29,34,37), C(133,132,137), C(251,252,252), C(179,177,185), C(92,93,96), C(41,46,49), C(29,34,37), C(59,63,66), C(2,2,5), C(75,194,125), C(228,169,62), C(11,11,24), C(41,46,49), C(29,34,37), C(92,93,96)),
    THEME_4C_MUTED("Black GBA", (16,19,22), (48,54,57), (70,76,79), (119,126,128), (190,198,201)),
    THEME_GRAD("Watermelon Red N64", C(40,3,14), P(91,17,39), C(255,150,179), C(255,240,245), C(205,139,157), C(233,47,99), C(143,22,60), C(105,25,48), C(187,36,78), C(14,0,4), C(102,223,136), C(255,188,55), C(66,26,40), C(175,46,83), C(116,32,61), C(220,102,128)),
    THEME_4C("Berry GBC", (55,0,25), (133,0,55), (216,12,86), (255,91,136)),
    THEME_4C("Milky Pink GBA", (33,32,40), (128,68,99), (179,101,137), (218,143,178)),
    THEME_GRAD("Daiei Hawks N64", C(10,6,3), P(36,24,15), C(245,117,28), C(255,246,230), C(170,151,133), C(230,83,13), C(103,52,18), C(48,35,25), C(170,60,9), C(2,1,0), C(87,205,123), C(255,178,43), C(3,2,1), C(42,30,20), C(128,38,3), C(247,112,23)),
    THEME_GRAD("Fire Orange N64", C(37,13,1), P(82,34,8), C(255,177,83), C(255,244,220), C(202,157,119), C(241,100,22), C(143,57,10), C(98,43,14), C(190,76,14), C(13,4,0), C(112,217,117), C(255,184,45), C(35,8,0), C(235,70,8), C(100,30,2), C(255,185,71)),
    THEME_GRAD("Spice Orange GameCube", C(35,11,1), P(100,67,56), C(243,187,150), C(252,250,248), C(210,164,132), C(233,123,46), C(181,83,29), C(100,67,56), C(234,142,79), C(12,4,0), C(105,216,122), C(255,188,45), C(100,67,56), C(233,123,46), C(181,83,29), C(243,187,150)),
    THEME_4C("Orange GBA", (30,35,42), (151,65,31), (224,122,61), (246,174,113)),
    THEME_GRAD("Pikachu Orange/Blue", C(28,13,9), P(71,38,24), C(137,210,241), C(255,244,220), C(190,165,140), C(246,117,37), C(128,72,48), C(91,54,40), C(90,166,205), C(10,4,3), C(90,211,133), C(255,190,43), C(35,12,4), C(235,91,18), C(43,119,178), C(153,220,244)),
    THEME_GRAD("Gold N64", C(31,20,5), P(76,54,15), C(246,205,85), C(255,247,218), C(197,170,112), C(218,163,42), C(130,94,20), C(92,69,25), C(172,126,27), C(11,7,1), C(89,198,112), C(255,190,54), C(28,15,2), C(166,111,17), C(81,53,7), C(255,221,113)),
    THEME_4C("Gold GBA", (48,54,65), (133,116,88), (183,168,143), (218,205,180)),
    THEME_4C("Dandelion GBC", (70,43,0), (188,128,4), (245,190,31), (255,226,103)),
    THEME_GRAD("Pikachu Blue/Yellow", C(3,28,65), P(10,66,111), C(255,222,37), C(255,250,211), C(171,191,189), C(255,207,20), C(26,109,169), C(15,81,132), C(225,48,43), C(1,10,25), C(78,216,130), C(255,197,26), C(2,24,66), C(25,111,181), C(224,38,40), C(255,220,29)),
    THEME_GRAD("Jungle Green N64", C(2,23,9), P(8,58,25), C(117,239,143), C(239,255,238), C(128,189,140), C(38,195,83), C(17,110,48), C(13,74,34), C(31,147,66), C(0,8,3), C(89,242,125), C(255,203,69), C(12,54,31), C(66,163,81), C(28,143,78), C(115,182,101)),
    THEME_4C("Teal GBC", (0,42,55), (0,111,139), (10,166,194), (104,222,229)),
    THEME_GRAD("Clear Blue/White N64", C(12,35,49), P(47,78,92), C(224,248,255), C(255,255,255), C(180,204,212), C(64,165,210), C(72,125,150), C(58,88,101), C(183,222,233), C(3,13,19), C(86,208,138), C(242,180,56), C(9,38,60), C(80,179,221), C(123,190,214), C(246,252,250)),
    THEME_GRAD("Clear Blue N64", C(4,21,38), P(15,52,79), C(194,240,255), C(248,253,255), C(155,190,207), C(91,186,233), C(42,104,148), C(25,68,100), C(65,142,189), C(1,8,16), C(91,221,145), C(255,198,70), C(2,18,39), C(48,143,211), C(8,57,98), C(203,244,255)),
    THEME_GRAD("Ice Blue N64", C(2,24,39), P(9,55,76), C(154,237,255), C(242,253,255), C(139,190,205), C(43,190,232), C(22,112,148), C(14,72,94), C(39,147,187), C(0,9,16), C(80,229,151), C(255,199,68), C(1,20,42), C(30,168,220), C(8,68,108), C(174,244,255)),
    THEME_4C("Milky Blue GBA", (30,36,45), (86,91,132), (126,131,170), (171,174,209)),
    THEME_GRAD("Midnight Blue N64", C(2,8,30), P(11,29,67), C(132,172,244), C(240,246,255), C(129,148,181), C(48,91,189), C(24,51,115), C(19,39,82), C(39,72,148), C(0,2,11), C(74,206,133), C(246,184,61), C(0,3,20), C(21,44,120), C(4,14,55), C(101,144,231)),
    THEME_GRAD("Indigo GameCube", C(24,21,39), P(70,51,112), C(162,150,201), C(250,252,253), C(213,206,219), C(139,122,194), C(84,65,135), C(51,40,90), C(116,101,167), C(10,5,46), C(88,216,143), C(255,192,61), C(51,40,90), C(139,122,194), C(32,22,81), C(96,77,152)),
    THEME_4C_MUTED("Violet GBA", (29,33,46), (54,42,109), (84,66,139), (135,116,191), (205,198,231)),
    THEME_4C_MUTED("Grape GBC", (21,10,61), (65,39,133), (103,69,181), (160,130,224), (211,199,244)),
    THEME_GRAD("Grape Purple N64", C(22,7,37), P(60,24,82), C(213,160,255), C(250,242,255), C(178,146,196), C(155,73,211), C(88,42,123), C(71,36,92), C(122,57,164), C(7,2,13), C(92,221,141), C(255,191,64), C(17,4,35), C(126,43,184), C(55,18,91), C(220,156,255)),
    THEME_4C("Atomic Purple GBC", (32,25,51), (88,72,119), (147,126,174), (213,201,225)),
    THEME_CAT("Custom"),
    CUSTOM_THEME,
};

static const menu_theme_t custom_defaults = CUSTOM_THEME;

#define DEFAULT_THEME (&themes[2]) // N64

static const menu_theme_t *current_theme = DEFAULT_THEME;
/* Unshifted source of the Custom theme: the copied preset with any color edits. */
static menu_theme_t custom_base = CUSTOM_THEME;
static int custom_hue_shift;

static menu_theme_t *custom_theme(void) { return &themes[theme_catalog_count() - 1]; }
const menu_theme_t *theme_catalog(void) { return themes; }
int theme_catalog_count(void) { return sizeof(themes) / sizeof(themes[0]); }
const menu_theme_t *theme_get(void) { return current_theme; }
const menu_theme_t *theme_custom(void) { return custom_theme(); }

bool theme_is_category(const menu_theme_t *theme) {
    return theme && theme->background_mode == THEME_BACKGROUND_CATEGORY;
}

bool theme_is_custom(const menu_theme_t *theme) { return theme == custom_theme(); }

const menu_theme_t *theme_get_by_name(const char *name) {
    for (int i = 0; name && i < theme_catalog_count(); i++) {
        if (!theme_is_category(&themes[i]) && strcasecmp(name, themes[i].name) == 0)
            return &themes[i];
    }
    return NULL;
}

void theme_set(const menu_theme_t *theme) {
    if (theme && !theme_is_category(theme)) current_theme = theme;
}

void theme_set_by_name(const char *name) { theme_set(theme_get_by_name(name)); }

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
    for (int i = 0; i < THEME_COLOR_COUNT; i++)
        custom_theme()->colors[i] = theme_hue_shift_color(custom_base.colors[i], custom_hue_shift);
}

void theme_custom_reset(void) {
    *custom_theme() = custom_defaults;
    custom_base = custom_defaults;
    custom_hue_shift = 0;
}

color_t theme_custom_color(menu_theme_custom_color_t slot) {
    return slot >= 0 && slot < THEME_CUSTOM_COLOR_COUNT
        ? custom_theme()->colors[custom_colors[slot].channel] : C(255,255,255);
}

bool theme_custom_copy_from(const menu_theme_t *source) {
    if (!source || theme_is_category(source)) return false;
    if (theme_is_custom(source)) return true;
    custom_base = *source;
    *custom_theme() = *source;
    custom_theme()->name = "Custom";
    custom_theme()->background_mode = THEME_BACKGROUND_VERTEX_GRADIENT;
    custom_hue_shift = 0;
    return true;
}

const char *theme_custom_base_name(void) { return custom_base.name; }
int theme_custom_hue_shift(void) { return custom_hue_shift; }

void theme_custom_set_hue_shift(int degrees) {
    custom_hue_shift = ((degrees % 360) + 360) % 360;
    custom_apply_hue();
}

bool theme_custom_has_background_image(void) {
    return custom_theme()->background_mode == THEME_BACKGROUND_CUSTOM_IMAGE;
}

void theme_custom_use_background_image(void) {
    custom_theme()->background_mode = THEME_BACKGROUND_CUSTOM_IMAGE;
}

void theme_custom_clear_background_image(void) {
    if (theme_custom_has_background_image())
        custom_theme()->background_mode = THEME_BACKGROUND_VERTEX_GRADIENT;
}

void theme_select_custom_image(void) {
    theme_custom_copy_from(current_theme);
    theme_custom_use_background_image();
    current_theme = custom_theme();
}

void theme_custom_get_palette(color_t colors[THEME_COLOR_COUNT]) {
    memcpy(colors, custom_base.colors, sizeof(custom_base.colors));
}

void theme_custom_set_palette(const color_t colors[THEME_COLOR_COUNT]) {
    memcpy(custom_base.colors, colors, sizeof(custom_base.colors));
    custom_apply_hue();
}

void theme_custom_set_color(menu_theme_custom_color_t slot, color_t color) {
    if (slot < 0 || slot >= THEME_CUSTOM_COLOR_COUNT) return;
    const custom_color_descriptor_t *descriptor = &custom_colors[slot];
    if (slot != THEME_CUSTOM_PANEL) color.a = 0xFF;
    color_t current = custom_theme()->colors[descriptor->channel];
    color_t base = custom_base.colors[descriptor->channel];
    /* Alpha-only edits must not round-trip RGB through the hue conversion. */
    if (color.r != current.r || color.g != current.g || color.b != current.b)
        base = theme_hue_shift_color(color, -custom_hue_shift);
    base.a = color.a;
    custom_base.colors[descriptor->channel] = base;
    if (descriptor->linked_channel != THEME_COLOR_COUNT)
        custom_base.colors[descriptor->linked_channel] = base;
    custom_apply_hue();
}
