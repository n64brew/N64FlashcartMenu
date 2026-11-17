/* Host layout regression. The runner includes production ticker/SDK code.
 * Font metrics come from the SDK's existing built-in Monogram font; GPU
 * submission is replaced by a baseline check, so this is not a render test. */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rdpq_paragraph.h>
#include <rdpq_font.h>
#include <rdpq_text.h>
#include "rdpq_font_internal.h"
#include "menu/fonts.h"
#include "rdpq_font_builtin.c"

/* Emit the SDK's inline metrics implementation, as rdpq_font.c does. */
extern inline void __rdpq_font_glyph_metrics(const rdpq_font_t *, int16_t,
    float *, int8_t *, int8_t *, bool *, uint8_t *);

#undef assertf
#define assertf(condition, ...) do { if (!(condition)) { \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); exit(7); \
} } while (0)
#define UNLIKELY(x) (x)
#define fm_ceilf ceilf
#define fm_truncf truncf

static rdpq_font_t font;
static float expected_baseline;
static unsigned renders;

const rdpq_font_t *rdpq_text_get_font(uint8_t id) {
    assert(id == FNT_DEFAULT);
    return &font;
}

/* The font64 file uses big-endian, 32-bit offsets; host pointers may be 64-bit.
 * Decode only its real ranges/glyph metrics, leaving image atlases unused. */
static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void font_init(void) {
    const unsigned char *data = __fontdb_monogram;
    assert(memcmp(data, "FNT\x0b", 4) == 0);
    font.point_size = be32(data + 8);
    font.ascent = (int32_t)be32(data + 12);
    font.descent = (int32_t)be32(data + 16);
    font.line_gap = (int32_t)be32(data + 20);
    font.num_ranges = be32(data + 36);
    font.num_glyphs = be32(data + 40);
    font.num_atlases = be32(data + 44);
    assert(font.num_ranges == 1 && be32(data + 48) == 0);
    font.ranges = calloc(font.num_ranges, sizeof(*font.ranges));
    font.glyphs = calloc(font.num_glyphs, sizeof(*font.glyphs));
    assert(font.ranges && font.glyphs);
    const unsigned char *range = data + be32(data + 72);
    font.ranges[0] = (range_t){be32(range), be32(range + 4), (int32_t)be32(range + 8)};
    assert(font.ranges[0].first_glyph >= 0);
    const unsigned char *glyphs = data + be32(data + 80);
    for (uint32_t i = 0; i < font.num_glyphs; i++) {
        const unsigned char *g = glyphs + i * 8;
        font.glyphs[i] = (glyph_t){.xadvance = g[0], .xoff = (int8_t)g[1],
            .yoff = (int8_t)g[2], .xoff2 = (int8_t)g[3], .yoff2 = (int8_t)g[4],
            .s = g[5], .t = g[6], .natlas = g[7] >> 2, .ntile = g[7] & 3};
    }
}

static int16_t __rdpq_font_glyph_sparse(const rdpq_font_t *unused, uint32_t codepoint) {
    (void)unused; (void)codepoint;
    assert(!"built-in fixture unexpectedly contains a sparse range");
    return -1;
}

void rdpq_paragraph_render(const rdpq_paragraph_t *layout, float x, float y) {
    (void)x;
    assert(fabsf(y + layout->advance_y - expected_baseline) < 0.001f);
    renders++;
}

#include "file_list_ticker_under_test.inc"

static void check(const char *name, int expected_glyphs, int expected_width) {
    assert(file_list_ticker_layout_build(name, STL_DEFAULT));
    int glyphs = 0;
    for (size_t i = 0; i < file_list_ticker_chunk_count; i++) {
        const rdpq_paragraph_t *p = file_list_ticker_chunks[i].layout;
        assert(p->nlines <= 1 && p->advance_x <= FILE_ENTRY_TICKER_CHUNK_WIDTH);
        for (int j = 0; j < p->nchars; j++) assert(p->chars[j].x < 2048 && p->chars[j].y < 2048);
        glyphs += p->nchars;
    }
    assert(glyphs == expected_glyphs);
    assert(file_list_ticker_name_width == expected_width);
    expected_baseline = 50;
    file_list_ticker_render(0, 50, 0, 328);
    file_list_ticker_render(-300, 50, 0, 328);
    file_list_ticker_layout_free();
    assert(file_list_ticker_chunks == NULL && file_list_ticker_chunk_count == 0);
}

int main(int argc, char **argv) {
    (void)argv;
    font_init();
    char long_name[12001];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    if (argc > 1) {
        /* Positive control: exercise the actual former unbounded SDK call. */
        int bytes = 400;
        rdpq_paragraph_t *p = rdpq_paragraph_build(NULL, FNT_DEFAULT, long_name, &bytes);
        rdpq_paragraph_free(p);
        return 1;
    }
    for (int repeat = 0; repeat < 10; repeat++) {
        check("short.z64", 9, 54);
        check(long_name, 12000, 72000);
        check("", 0, 0);
        check("     ", 0, 30);
        check("literal^FF$FF.z64", 17, 102);
        char unicode[1801] = {0};
        for (int i = 0; i < 300; i++) strcat(unicode, "£¢¥");
        check(unicode, 900, 5400);
        char boundary[160];
        memset(boundary, 'x', 127);
        strcpy(boundary + 127, "£日本語🙂.z64");
        check(boundary, 132, 792); /* Unsupported glyphs are skipped by this font. */
        char tabs[67], blanks[67], nbsp[134] = {0};
        memset(tabs, '\t', 65); strcpy(tabs + 65, "A");
        memset(blanks, ' ', 65); strcpy(blanks + 65, "A");
        for (int i = 0; i < 65; i++) strcat(nbsp, "\xC2\xA0");
        strcat(nbsp, "A");
        check(tabs, 1, 65 * 32 + 6);
        check(blanks, 1, 66 * 6);
        check(nbsp, 1, 66 * 6);
        /* A custom font can use the full uint8 advance range. */
        for (uint32_t i = 0; i < font.num_glyphs; i++) font.glyphs[i].xadvance = 255;
        check(tabs, 1, 65 * 256 + 255);
        check(blanks, 1, 66 * 255);
        check(nbsp, 1, 66 * 255);
        check(long_name, 12000, 12000 * 255);
        check(unicode, 900, 900 * 255);
        for (uint32_t i = 0; i < font.num_glyphs; i++) font.glyphs[i].xadvance = 6;
    }
    assert(renders > 0);
    free(font.ranges);
    free(font.glyphs);
    puts("PASS: actual SDK paragraphs and production ticker preserve bounded ASCII/UTF-8 layouts, whitespace, literals, baseline and cleanup");
    return 0;
}
