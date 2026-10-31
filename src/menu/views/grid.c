/**
 * @file grid.c
 * @brief Persistent, artwork-first ROM grid.
 * @ingroup view
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdragon.h>
#include <miniz.h>

#include "../fonts.h"
#include "../ini_parser.h"
#include "../path.h"
#include "../png_decoder.h"
#include "../sound.h"
#include "../ui_components/constants.h"
#include "utils/fs.h"
#include "views.h"

#define GRID_CACHE_MAGIC        0x47524944U /* GRID */
#define GRID_CACHE_VERSION      2
#define GRID_CACHE_FILE         "menu/cache/grid.index"
#define GRID_MAX_ENTRIES        512
#define GRID_MAX_PATH           1023
#define GRID_NAME_LENGTH        96
#define GRID_UNKNOWN_DATE       INT32_MAX
#define GRID_ZIP_INI_MAX_SIZE   (64 * 1024)
#define GRID_ZIP_ART_MAX_SIZE   (2 * 1024 * 1024)

#define GRID_COLUMNS            4
#define GRID_ROWS               3
#define GRID_PAGE_ENTRIES       (GRID_COLUMNS * GRID_ROWS)
#define GRID_TILE_WIDTH         134
#define GRID_TILE_HEIGHT        108
#define GRID_TILE_GAP_X         7
#define GRID_TILE_GAP_Y         5
#define GRID_START_X            40
#define GRID_START_Y            61
#define GRID_SCROLLBAR_X        603
#define GRID_SCROLLBAR_Y        GRID_START_Y
#define GRID_SCROLLBAR_WIDTH    6
#define GRID_SCROLLBAR_HEIGHT   (3 * GRID_TILE_HEIGHT + 2 * GRID_TILE_GAP_Y)
#define GRID_ART_WIDTH          GRID_TILE_WIDTH
#define GRID_ART_HEIGHT         88
#define GRID_ART_DECODE_SIZE    158
#define GRID_HOLD_SECONDS       0.35f
#define GRID_DECODE_ROWS_PER_FRAME 64
#define GRID_MARQUEE_PAUSE_SECONDS 1.0f
#define GRID_MARQUEE_PIXELS_PER_SECOND 60.0f
#define GRID_ART_CACHE_BYTES    (2 * 1024 * 1024)
#define GRID_ART_CACHE_ENTRIES  64
#define GRID_PROGRESS_DELAY_US  150000
#define GRID_PROGRESS_INTERVAL_US 33000

#define GRID_TILE_COLOR         (theme_get()->tab_inactive)
#define GRID_TILE_SELECTED      (theme_get()->highlight)
#define GRID_TILE_MOVING        (theme_get()->accent)
#define GRID_TILE_SHADOW        (theme_get()->shadow)
#define GRID_ART_PLACEHOLDER    (theme_get()->background)
#define GRID_ACCENT_COLOR       (theme_get()->border)

static const char *rom_extensions[] = { "z64", "n64", "v64", "rom", NULL };

/* Where an entry's metadata.ini and artwork came from, matching rom_config_load's order. */
typedef enum {
    GRID_META_DIRECTORY,
    GRID_META_SIDECAR,
    GRID_META_EMBEDDED,
} grid_meta_source_t;

typedef struct {
    char *path;
    char game_code[5];
    char title[21];
    char display_name[GRID_NAME_LENGTH];
    char author[GRID_NAME_LENGTH];
    int32_t release_date_key;
    uint8_t meta_source;
    bool embedded_meta;
    bool seen;
    float display_width;
} grid_entry_t;

/* Artwork for one slot of the current page; the slot determines its ROM. */
typedef struct {
    bool loading;
    bool no_art;
    bool from_cache;
    int16_t top_opaque_row;
    surface_t *image;
} grid_thumbnail_t;

typedef enum {
    GRID_SORT_TITLE,
    GRID_SORT_RELEASE_DATE,
} grid_sort_t;

typedef enum {
    GRID_GROUP_NONE,
    GRID_GROUP_AUTHOR,
} grid_group_t;

typedef enum {
    GRID_ART_BOXART,
    GRID_ART_GAMEPAK,
} grid_art_mode_t;

typedef struct {
    int32_t entry_index;
    int32_t group_count;
    char group_name[GRID_NAME_LENGTH];
} grid_view_item_t;

typedef struct {
    char *path;
    surface_t *image;
    grid_art_mode_t art_mode;
    int16_t top_opaque_row;
    size_t bytes;
    uint32_t last_used;
} grid_art_cache_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint8_t sort;
    uint8_t group;
    uint8_t art_mode;
    uint16_t scan_directory_length;
    uint32_t count;
} grid_cache_header_t;

typedef struct __attribute__((packed)) {
    uint16_t path_length;
    char game_code[4];
    char title[20];
    char display_name[GRID_NAME_LENGTH];
    char author[GRID_NAME_LENGTH];
    int32_t release_date_key;
    uint8_t meta_source;
} grid_cache_entry_t;

static struct {
    grid_entry_t *entries;
    int32_t count;
    int32_t capacity;
    int32_t selected;
    int32_t page_start;
    grid_view_item_t view[GRID_MAX_ENTRIES];
    int32_t view_count;
    grid_sort_t sort;
    grid_group_t group;
    grid_art_mode_t art_mode;
    char open_group[GRID_NAME_LENGTH];
    char *cache_path;
    /* Directory of the current entries; NULL unless they form a complete index. */
    char *scan_directory;
    const char *storage_prefix;
    bool initialized;
    bool scan_error;
    /* Files changed the library; sync before the index is next shown. */
    bool stale;
    /* Whether Grid has been shown, and so selected the last played ROM. */
    bool opened;
    bool a_pending;
    bool moving;
    long long a_start_ticks;
    int32_t marquee_selected;
    long long marquee_start_ticks;
    bool art_cache_enabled;
    size_t art_cache_bytes;
    uint32_t art_cache_clock;
    grid_art_cache_entry_t art_cache[GRID_ART_CACHE_ENTRIES];
    grid_thumbnail_t thumbnails[GRID_PAGE_ENTRIES];
} grid;

static void entry_free(grid_entry_t *entry) {
    free(entry->path);
    memset(entry, 0, sizeof(*entry));
}

static void entries_free(void) {
    for (int32_t i = 0; i < grid.count; i++) {
        entry_free(&grid.entries[i]);
    }
    free(grid.entries);
    grid.entries = NULL;
    grid.count = 0;
    grid.capacity = 0;
}

static grid_entry_t *entry_append(void) {
    if (grid.count >= GRID_MAX_ENTRIES) {
        return NULL;
    }
    if (grid.count == grid.capacity) {
        int32_t new_capacity = grid.capacity ? grid.capacity * 2 : 32;
        if (new_capacity > GRID_MAX_ENTRIES) {
            new_capacity = GRID_MAX_ENTRIES;
        }
        grid_entry_t *new_entries = realloc(grid.entries, new_capacity * sizeof(*new_entries));
        if (!new_entries) {
            return NULL;
        }
        grid.entries = new_entries;
        grid.capacity = new_capacity;
    }
    grid_entry_t *entry = &grid.entries[grid.count++];
    memset(entry, 0, sizeof(*entry));
    return entry;
}

static const char *basename_of(const char *path) {
    const char *name = strrchr(path, '/');
    return name ? name + 1 : path;
}

static int32_t entry_find(const char *path) {
    for (int32_t i = 0; i < grid.count; i++) {
        if (strcmp(grid.entries[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static int32_t entry_find_basename(const char *name) {
    for (int32_t i = 0; i < grid.count; i++) {
        if (strcmp(basename_of(grid.entries[i].path), name) == 0) {
            return i;
        }
    }
    return -1;
}

static int32_t view_find(int32_t entry_index) {
    for (int32_t i = 0; i < grid.view_count; i++) {
        if (grid.view[i].entry_index == entry_index) {
            return i;
        }
    }
    return -1;
}

static bool is_group(const grid_view_item_t *item) {
    return item->group_count > 1;
}

static void copy_filename_title(char title[21], const char *path) {
    const char *name = basename_of(path);
    const char *extension = strrchr(name, '.');
    int length = extension ? (int)(extension - name) : (int)strlen(name);
    snprintf(title, 21, "%.*s", length, name);
}

static void normalize_header(uint8_t dst[64], const uint8_t src[64]) {
    if (src[0] == 0x37 && src[1] == 0x80 && src[2] == 0x40 && src[3] == 0x12) {
        for (int i = 0; i < 64; i += 2) {
            dst[i] = src[i + 1];
            dst[i + 1] = src[i];
        }
    } else if (src[0] == 0x40 && src[1] == 0x12 && src[2] == 0x37 && src[3] == 0x80) {
        for (int i = 0; i < 64; i += 4) {
            dst[i] = src[i + 3];
            dst[i + 1] = src[i + 2];
            dst[i + 2] = src[i + 1];
            dst[i + 3] = src[i];
        }
    } else {
        memcpy(dst, src, 64);
    }
}

static bool read_header_id(const char *full_path, grid_entry_t *entry) {
    uint8_t raw[64];
    uint8_t header[64];
    FILE *f = fopen(full_path, "rb");
    if (!f) {
        return false;
    }
    setbuf(f, NULL);
    bool ok = fread(raw, sizeof(raw), 1, f) == 1;
    fclose(f);
    if (!ok) {
        return false;
    }
    normalize_header(header, raw);
    if (!(header[0] == 0x80 && header[1] == 0x37 && header[2] == 0x12 && header[3] == 0x40)) {
        return false;
    }
    memcpy(entry->game_code, &header[0x3B], 4);
    entry->game_code[4] = '\0';
    memcpy(entry->title, &header[0x20], 20);
    entry->title[20] = '\0';
    /* Same flag rom_config_load checks for a ZIP appended to the ROM. */
    entry->embedded_meta = header[0x38] & 1;
    return true;
}

static void read_header(const char *full_path, grid_entry_t *entry) {
    if (!read_header_id(full_path, entry)) {
        strcpy(entry->game_code, "????");
        copy_filename_title(entry->title, entry->path);
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (!isprint((unsigned char)entry->game_code[i])) {
            entry->game_code[i] = '?';
        }
    }
    for (int i = 0; i < 20; i++) {
        if (!isprint((unsigned char)entry->title[i])) {
            entry->title[i] = ' ';
        }
    }
    for (int i = 19; i >= 0 && entry->title[i] == ' '; i--) {
        entry->title[i] = '\0';
    }
    if (!entry->title[0]) {
        copy_filename_title(entry->title, entry->path);
    }
}

/* Same layout as boxart.c: homebrew uses its title, and the region may be omitted. */
static path_t *metadata_directory(const grid_entry_t *entry) {
    if (entry->game_code[0] == '?') {
        return NULL;
    }
    path_t *path = path_init(grid.storage_prefix, "menu/metadata");
    char code_path[32];
    if (entry->game_code[1] == 'E' && entry->game_code[2] == 'D') {
        snprintf(code_path, sizeof(code_path), "homebrew/%s", entry->title);
        path_push(path, code_path);
    } else {
        snprintf(code_path, sizeof(code_path), "%c/%c/%c/%c",
            entry->game_code[0], entry->game_code[1], entry->game_code[2], entry->game_code[3]);
        path_push(path, code_path);
        if (!directory_exists(path_get(path))) {
            path_pop(path);
        }
    }
    return path;
}

/* The ROM itself or its .meta companion, for entries whose metadata is in a ZIP. */
static path_t *metadata_zip_path(const grid_entry_t *entry) {
    if (entry->meta_source == GRID_META_DIRECTORY) {
        return NULL;
    }
    path_t *path = path_init(grid.storage_prefix, entry->path);
    if (entry->meta_source == GRID_META_SIDECAR) {
        path_ext_replace(path, "meta");
    }
    return path;
}

/* Returns a malloc'd, NUL-terminated copy of one file in a ZIP. */
static char *zip_read(const char *zip_path, const char *name, size_t max_size, size_t *size) {
    if (!name[0] || name[0] == '/' || strstr(name, "..")) {
        return NULL;
    }
    mz_zip_archive zip = {0};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) {
        return NULL;
    }
    char *data = NULL;
    mz_zip_archive_file_stat stat;
    mz_uint index = mz_zip_reader_locate_file(&zip, name, NULL, MZ_ZIP_FLAG_CASE_SENSITIVE);
    if (index != MZ_UINT32_MAX && mz_zip_reader_file_stat(&zip, index, &stat) &&
        stat.m_uncomp_size > 0 && stat.m_uncomp_size <= max_size &&
        (data = malloc((size_t)stat.m_uncomp_size + 1))) {
        if (mz_zip_reader_extract_to_mem(&zip, index, data, (size_t)stat.m_uncomp_size, 0)) {
            data[stat.m_uncomp_size] = '\0';
            *size = (size_t)stat.m_uncomp_size;
        } else {
            free(data);
            data = NULL;
        }
    }
    mz_zip_reader_end(&zip);
    return data;
}

static ini_t *zip_ini_load(const char *zip_path) {
    size_t size;
    char *data = zip_read(zip_path, "metadata.ini", GRID_ZIP_INI_MAX_SIZE, &size);
    if (!data) {
        return NULL;
    }
    ini_t *ini = ini_parse_buffer(data, size);
    free(data);
    return ini;
}

static ini_t *metadata_ini_load(grid_entry_t *entry) {
    path_t *full_path = path_init(grid.storage_prefix, entry->path);
    path_ext_replace(full_path, "meta");
    ini_t *metadata = file_exists(path_get(full_path)) ? zip_ini_load(path_get(full_path)) : NULL;
    path_free(full_path);
    if (metadata) {
        entry->meta_source = GRID_META_SIDECAR;
        return metadata;
    }

    entry->meta_source = GRID_META_DIRECTORY;
    path_t *path = metadata_directory(entry);
    if (path) {
        path_push(path, "metadata.ini");
        metadata = ini_load(path_get(path));
        path_free(path);
        if (metadata) {
            return metadata;
        }
    }

    if (entry->embedded_meta) {
        full_path = path_init(grid.storage_prefix, entry->path);
        metadata = zip_ini_load(path_get(full_path));
        path_free(full_path);
        if (metadata) {
            entry->meta_source = GRID_META_EMBEDDED;
        }
    }
    return metadata;
}

static void load_metadata(grid_entry_t *entry) {
    entry->display_width = 0.0f;
    snprintf(entry->display_name, sizeof(entry->display_name), "%s", entry->title);
    snprintf(entry->author, sizeof(entry->author), "%s", "Unknown");
    entry->release_date_key = GRID_UNKNOWN_DATE;

    ini_t *metadata = metadata_ini_load(entry);
    if (!metadata) {
        return;
    }

    const char *name = ini_get_string(metadata, "meta", "name", "");
    if (name[0]) {
        snprintf(entry->display_name, sizeof(entry->display_name), "%s", name);
    }
    const char *author = ini_get_string(metadata, "meta", "author", "");
    if (author[0]) {
        snprintf(entry->author, sizeof(entry->author), "%s", author);
    }
    const char *release_date = ini_get_string(metadata, "meta", "release-date", "");
    int year, month, day;
    if (sscanf(release_date, "%4d-%2d-%2d", &year, &month, &day) == 3) {
        entry->release_date_key = year * 10000 + month * 100 + day;
    }
    ini_free(metadata);

    /* The grid is title-oriented; database region/revision tags belong in details. */
    char *qualifier = strrchr(entry->display_name, '(');
    size_t name_length = strlen(entry->display_name);
    if (qualifier && qualifier > entry->display_name && qualifier[-1] == ' ' &&
        entry->display_name[name_length - 1] == ')') {
        qualifier[-1] = '\0';
    }
}

static int utf8_glyph_bytes(const char *text) {
    unsigned char first = (unsigned char)text[0];
    if ((first & 0x80) == 0) return 1;
    if ((first & 0xE0) == 0xC0 && text[1]) return 2;
    if ((first & 0xF0) == 0xE0 && text[1] && text[2]) return 3;
    if ((first & 0xF8) == 0xF0 && text[1] && text[2] && text[3]) return 4;
    return 1;
}

static float title_width(grid_entry_t *entry) {
    if (entry->display_width <= 0.0f) {
        int bytes = strlen(entry->display_name);
        rdpq_paragraph_t *layout = rdpq_paragraph_build(
            &(rdpq_textparms_t){ .wrap = WRAP_NONE, .char_spacing = -1 },
            FNT_DEFAULT, entry->display_name, &bytes);
        if (layout) {
            entry->display_width = layout->bbox.x1 - layout->bbox.x0;
            rdpq_paragraph_free(layout);
        }
    }
    return entry->display_width;
}

static float marquee_offset(float overflow) {
    float travel_seconds = overflow / GRID_MARQUEE_PIXELS_PER_SECOND;
    float cycle_seconds = 2.0f * (GRID_MARQUEE_PAUSE_SECONDS + travel_seconds);
    float elapsed = (float)(timer_ticks() - grid.marquee_start_ticks) / (float)TICKS_PER_SECOND;
    float phase = fmodf(elapsed, cycle_seconds);

    if (phase < GRID_MARQUEE_PAUSE_SECONDS) return 0.0f;
    phase -= GRID_MARQUEE_PAUSE_SECONDS;
    if (phase < travel_seconds) return phase * GRID_MARQUEE_PIXELS_PER_SECOND;
    phase -= travel_seconds;
    if (phase < GRID_MARQUEE_PAUSE_SECONDS) return overflow;
    phase -= GRID_MARQUEE_PAUSE_SECONDS;
    return overflow - phase * GRID_MARQUEE_PIXELS_PER_SECOND;
}

static void two_line_title(const char *source, char *output, size_t output_size) {
    enum { MAX_LINE_GLYPHS = 24 };
    snprintf(output, output_size, "%s", source);

    int total_glyphs = 0;
    for (const char *cursor = source; *cursor; cursor += utf8_glyph_bytes(cursor)) {
        total_glyphs++;
    }
    if (total_glyphs <= MAX_LINE_GLYPHS) {
        return;
    }

    int glyph = 0;
    int best_glyph = -1;
    size_t best_byte = 0;
    int best_balance = 0x7FFFFFFF;
    for (const char *cursor = source; *cursor; cursor += utf8_glyph_bytes(cursor), glyph++) {
        if (*cursor != ' ' || glyph > MAX_LINE_GLYPHS || total_glyphs - glyph - 1 > MAX_LINE_GLYPHS) {
            continue;
        }
        int balance = abs(glyph - (total_glyphs - glyph - 1));
        if (balance < best_balance) {
            best_balance = balance;
            best_glyph = glyph;
            best_byte = (size_t)(cursor - source);
        }
    }
    if (best_glyph >= 0 && best_byte < output_size - 1) {
        output[best_byte] = '\n';
    }
}

static bool cache_load(const char *scan_directory) {
    FILE *f = fopen(grid.cache_path, "rb");
    if (!f) {
        return false;
    }
    setbuf(f, NULL);

    grid_cache_header_t header;
    size_t directory_length = strlen(scan_directory);
    char *cached_directory = NULL;
    bool ok = fread(&header, sizeof(header), 1, f) == 1 &&
        header.magic == GRID_CACHE_MAGIC && header.version == GRID_CACHE_VERSION &&
        header.count <= GRID_MAX_ENTRIES && header.scan_directory_length == directory_length &&
        (cached_directory = malloc(directory_length + 1)) &&
        fread(cached_directory, directory_length, 1, f) == 1;
    if (ok) {
        cached_directory[directory_length] = '\0';
        ok = strcmp(cached_directory, scan_directory) == 0;
    }

    for (uint32_t i = 0; ok && i < header.count; i++) {
        grid_cache_entry_t cached;
        grid_entry_t *entry = NULL;
        ok = fread(&cached, sizeof(cached), 1, f) == 1 &&
            cached.path_length > 0 && cached.path_length <= GRID_MAX_PATH &&
            (entry = entry_append()) &&
            (entry->path = malloc(cached.path_length + 1)) &&
            fread(entry->path, cached.path_length, 1, f) == 1;
        if (!ok) {
            break;
        }
        entry->path[cached.path_length] = '\0';
        memcpy(entry->game_code, cached.game_code, 4);
        memcpy(entry->title, cached.title, 20);
        memcpy(entry->display_name, cached.display_name, sizeof(cached.display_name));
        entry->display_name[sizeof(entry->display_name) - 1] = '\0';
        memcpy(entry->author, cached.author, sizeof(cached.author));
        entry->author[sizeof(entry->author) - 1] = '\0';
        entry->release_date_key = cached.release_date_key;
        entry->meta_source = cached.meta_source <= GRID_META_EMBEDDED ? cached.meta_source : GRID_META_DIRECTORY;
    }
    fclose(f);

    if (!ok) {
        free(cached_directory);
        entries_free();
        return false;
    }
    free(grid.scan_directory);
    grid.scan_directory = cached_directory;
    grid.sort = header.sort == GRID_SORT_RELEASE_DATE ? GRID_SORT_RELEASE_DATE : GRID_SORT_TITLE;
    grid.group = header.group == GRID_GROUP_AUTHOR ? GRID_GROUP_AUTHOR : GRID_GROUP_NONE;
    grid.art_mode = header.art_mode == GRID_ART_GAMEPAK ? GRID_ART_GAMEPAK : GRID_ART_BOXART;
    return true;
}

static grid_cache_header_t cache_header(void) {
    return (grid_cache_header_t){
        .magic = GRID_CACHE_MAGIC,
        .version = GRID_CACHE_VERSION,
        .sort = grid.sort,
        .group = grid.group,
        .art_mode = grid.art_mode,
        .scan_directory_length = strlen(grid.scan_directory),
        .count = grid.count,
    };
}

static bool cache_save(void) {
    if (!grid.cache_path || !grid.scan_directory) return false;
    FILE *f = fopen(grid.cache_path, "wb");
    if (!f) {
        debugf("[GRID] Could not write index: %s\n", grid.cache_path);
        return false;
    }
    setbuf(f, NULL);
    grid_cache_header_t header = cache_header();
    bool ok = fwrite(&header, sizeof(header), 1, f) == 1 &&
              fwrite(grid.scan_directory, header.scan_directory_length, 1, f) == 1;
    for (int32_t i = 0; ok && i < grid.count; i++) {
        grid_entry_t *entry = &grid.entries[i];
        size_t path_length = strlen(entry->path);
        grid_cache_entry_t cached = {
            .path_length = path_length,
            .release_date_key = entry->release_date_key,
            .meta_source = entry->meta_source,
        };
        memcpy(cached.game_code, entry->game_code, 4);
        memcpy(cached.title, entry->title, 20);
        memcpy(cached.display_name, entry->display_name, sizeof(cached.display_name));
        memcpy(cached.author, entry->author, sizeof(cached.author));
        ok = fwrite(&cached, sizeof(cached), 1, f) == 1 &&
             fwrite(entry->path, path_length, 1, f) == 1;
    }
    if (fclose(f)) {
        ok = false;
    }
    if (!ok) {
        debugf("[GRID] Failed while saving index\n");
    }
    return ok;
}

/* Sorting, grouping, and artwork live in the header, so only it is rewritten. */
static bool cache_save_preferences(void) {
    if (!grid.cache_path || !grid.scan_directory) return false;
    FILE *f = fopen(grid.cache_path, "r+b");
    if (!f) {
        return cache_save();
    }
    setbuf(f, NULL);
    grid_cache_header_t header = cache_header();
    bool ok = fwrite(&header, sizeof(header), 1, f) == 1;
    if (fclose(f) || !ok) {
        debugf("[GRID] Failed while saving preferences\n");
        return false;
    }
    return true;
}

static void art_cache_evict(grid_art_cache_entry_t *entry) {
    grid.art_cache_bytes -= entry->bytes;
    free(entry->path);
    if (entry->image) {
        surface_free(entry->image);
        free(entry->image);
    }
    memset(entry, 0, sizeof(*entry));
}

static void art_cache_clear(void) {
    for (int i = 0; i < GRID_ART_CACHE_ENTRIES; i++) {
        art_cache_evict(&grid.art_cache[i]);
    }
    grid.art_cache_clock = 0;
    grid.art_cache_enabled = false;
}

static bool art_cache_is_visible(const grid_art_cache_entry_t *cached) {
    for (int slot = 0; slot < GRID_PAGE_ENTRIES; slot++) {
        if (grid.thumbnails[slot].from_cache && grid.thumbnails[slot].image == cached->image) {
            return true;
        }
    }
    return false;
}

static surface_t *art_cache_get(const char *path, grid_art_mode_t art_mode, int16_t *top_opaque_row) {
    if (!grid.art_cache_enabled) {
        return NULL;
    }
    for (int i = 0; i < GRID_ART_CACHE_ENTRIES; i++) {
        grid_art_cache_entry_t *entry = &grid.art_cache[i];
        if (entry->path && entry->art_mode == art_mode && strcmp(entry->path, path) == 0) {
            entry->last_used = ++grid.art_cache_clock;
            *top_opaque_row = entry->top_opaque_row;
            return entry->image;
        }
    }
    return NULL;
}

static bool art_cache_put(const char *path, grid_art_mode_t art_mode,
    surface_t *image, int16_t top_opaque_row) {
    if (!grid.art_cache_enabled || !image) {
        return false;
    }
    size_t bytes = (size_t)image->stride * image->height;
    if (bytes > GRID_ART_CACHE_BYTES) {
        return false;
    }

    /* Evict hidden images, least recently used first, until a free slot has room. */
    int available;
    for (;;) {
        available = -1;
        int lru = -1;
        for (int i = 0; i < GRID_ART_CACHE_ENTRIES; i++) {
            grid_art_cache_entry_t *entry = &grid.art_cache[i];
            if (!entry->image) {
                if (available < 0) available = i;
            } else if (!art_cache_is_visible(entry) &&
                       (lru < 0 || entry->last_used < grid.art_cache[lru].last_used)) {
                lru = i;
            }
        }
        if (available >= 0 && grid.art_cache_bytes + bytes <= GRID_ART_CACHE_BYTES) {
            break;
        }
        if (lru < 0) {
            return false;
        }
        art_cache_evict(&grid.art_cache[lru]);
    }

    char *path_copy = strdup(path);
    if (!path_copy) {
        return false;
    }
    grid.art_cache[available] = (grid_art_cache_entry_t){
        .path = path_copy,
        .image = image,
        .art_mode = art_mode,
        .top_opaque_row = top_opaque_row,
        .bytes = bytes,
        .last_used = ++grid.art_cache_clock,
    };
    grid.art_cache_bytes += bytes;
    return true;
}

static void art_cache_enable_if_possible(void) {
    if (grid.art_cache_enabled) {
        return;
    }
    heap_stats_t heap;
    sys_get_heap_stats(&heap);
    grid.art_cache_enabled = heap.total - heap.used > GRID_ART_CACHE_BYTES;
}

static bool scan_is_menu_rom(const char *full_path) {
    const char *name = basename_of(full_path);
    const char *extension = strrchr(name, '.');
    size_t stem_length = extension ? (size_t)(extension - name) : strlen(name);
    return stem_length == sizeof("sc64menu") - 1 &&
           strncasecmp(name, "sc64menu", stem_length) == 0;
}

typedef struct {
    /* Paths in the library that are not in the index yet. */
    char **added;
    int32_t added_count;
    int32_t reads;
    int32_t total;
    menu_t *menu;
    uint64_t start_us;
    uint64_t drawn_us;
} grid_scan_t;

static int scan_callback(const char *full_path, dir_t *info, void *data) {
    grid_scan_t *scan = data;
    if (info->d_type == DT_DIR) {
        return DIR_WALK_SKIPDIR;
    }
    /* System files such as macOS "._" companions and other menus are never games. */
    if (info->d_type != DT_REG || !file_has_extensions((char *)full_path, rom_extensions) ||
        scan_is_menu_rom(full_path) || view_browser_path_is_hidden(full_path)) {
        return DIR_WALK_CONTINUE;
    }

    const char *relative = strip_fs_prefix((char *)full_path);
    size_t path_length = strlen(relative);
    if (path_length == 0 || path_length > GRID_MAX_PATH) {
        return DIR_WALK_CONTINUE;
    }

    int32_t existing = entry_find(relative);
    if (existing >= 0) {
        grid.entries[existing].seen = true;
    } else if (scan->added_count < GRID_MAX_ENTRIES) {
        char *path = strdup(relative);
        if (!path) {
            return DIR_WALK_ABORT;
        }
        scan->added[scan->added_count++] = path;
    }
    return DIR_WALK_CONTINUE;
}

/* Long scans show their progress in place of the grid. */
static void draw_scan_progress(grid_scan_t *scan) {
    uint64_t now = get_ticks_us();
    if (now - scan->start_us < GRID_PROGRESS_DELAY_US || now - scan->drawn_us < GRID_PROGRESS_INTERVAL_US) {
        return;
    }
    surface_t *display = display_try_get();
    if (!display) {
        return;
    }
    scan->drawn_us = now;
    sound_poll();
    char message[32];
    snprintf(message, sizeof(message), "Scanning ROMs %ld/%ld", (long)scan->reads, (long)scan->total);
    rdpq_attach(display, NULL);
    ui_components_background_draw();
    ui_components_tabs_common_draw(scan->menu, 0);
    ui_components_layout_draw_tabbed();
    ui_components_loader_draw((float)scan->reads / scan->total, message);
    rdpq_detach_show();
}

static void read_entry(grid_scan_t *scan, grid_entry_t *entry) {
    path_t *full_path = path_init(grid.storage_prefix, entry->path);
    read_header(path_get(full_path), entry);
    path_free(full_path);
    load_metadata(entry);
    scan->reads++;
    draw_scan_progress(scan);
}

static int compare_entries(const void *left, const void *right) {
    const grid_entry_t *a = left;
    const grid_entry_t *b = right;
    int result = strcasecmp(a->display_name, b->display_name);
    return result ? result : strcasecmp(a->path, b->path);
}

static int compare_view_games(const void *left, const void *right) {
    const grid_entry_t *a = &grid.entries[((const grid_view_item_t *)left)->entry_index];
    const grid_entry_t *b = &grid.entries[((const grid_view_item_t *)right)->entry_index];
    if (grid.sort == GRID_SORT_RELEASE_DATE && a->release_date_key != b->release_date_key) {
        return a->release_date_key < b->release_date_key ? -1 : 1;
    }
    return compare_entries(a, b);
}

/* Metadata uses | for multiple credits; the first is the stable grouping key. */
static void primary_author(const grid_entry_t *entry, char author[GRID_NAME_LENGTH]) {
    const char *start = entry->author;
    while (*start && isspace((unsigned char)*start)) start++;
    const char *end = start;
    while (*end && *end != '|') end++;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    size_t length = end - start;
    if (length >= GRID_NAME_LENGTH) length = GRID_NAME_LENGTH - 1;
    memcpy(author, start, length);
    author[length] = '\0';
}

static bool author_is_groupable(const char *author) {
    return author[0] && strcasecmp(author, "Unknown") != 0;
}

static int compare_grouped_view(const void *left, const void *right) {
    const grid_view_item_t *a = left;
    const grid_view_item_t *b = right;
    if (is_group(a) != is_group(b)) return is_group(a) ? -1 : 1;
    if (is_group(a)) return strcasecmp(a->group_name, b->group_name);
    return compare_view_games(left, right);
}

/*
 * Ungrouped title order is the manual order of grid.entries, so the view is
 * the identity mapping there; rearranging relies on that.
 */
static void rebuild_view(void) {
    grid.view_count = 0;
    if (grid.open_group[0]) {
        for (int32_t i = 0; i < grid.count; i++) {
            char author[GRID_NAME_LENGTH];
            primary_author(&grid.entries[i], author);
            if (strcasecmp(author, grid.open_group) == 0) {
                grid.view[grid.view_count++] = (grid_view_item_t){ .entry_index = i };
            }
        }
        qsort(grid.view, grid.view_count, sizeof(grid.view[0]), compare_view_games);
        return;
    }

    if (grid.group == GRID_GROUP_NONE) {
        for (int32_t i = 0; i < grid.count; i++) {
            grid.view[grid.view_count++] = (grid_view_item_t){ .entry_index = i };
        }
        if (grid.sort == GRID_SORT_RELEASE_DATE) {
            qsort(grid.view, grid.view_count, sizeof(grid.view[0]), compare_view_games);
        }
        return;
    }

    for (int32_t i = 0; i < grid.count; i++) {
        char author[GRID_NAME_LENGTH];
        primary_author(&grid.entries[i], author);
        if (!author_is_groupable(author)) {
            grid.view[grid.view_count++] = (grid_view_item_t){ .entry_index = i };
            continue;
        }
        grid_view_item_t *group = NULL;
        for (int32_t j = 0; j < grid.view_count; j++) {
            if (grid.view[j].group_count && strcasecmp(grid.view[j].group_name, author) == 0) {
                group = &grid.view[j];
                break;
            }
        }
        if (!group) {
            group = &grid.view[grid.view_count++];
            *group = (grid_view_item_t){ .entry_index = i };
            snprintf(group->group_name, sizeof(group->group_name), "%s", author);
        }
        group->group_count++;
        grid_view_item_t candidate = { .entry_index = i };
        if (compare_view_games(&candidate, group) < 0) {
            group->entry_index = i;
        }
    }
    qsort(grid.view, grid.view_count, sizeof(grid.view[0]), compare_grouped_view);
}

static void clamp_selection(void) {
    if (grid.selected >= grid.view_count) {
        grid.selected = grid.view_count ? grid.view_count - 1 : 0;
    }
}

static bool walk_library(menu_t *menu, grid_scan_t *scan) {
    path_t *root = path_init(grid.storage_prefix, menu->settings.grid_directory);
    int result = dir_walk(path_get(root), scan_callback, scan);
    path_free(root);
    if (result != 0) {
        debugf("[GRID] Directory scan stopped with result %d\n", result);
    }
    return result == 0;
}

/*
 * Updates the index from the library directory. Only added ROMs are read and
 * removed ones dropped, which costs little more than listing the directory.
 * The index is left untouched if the directory can't be listed.
 */
static void refresh_index(menu_t *menu) {
    if (!grid.scan_directory || strcmp(grid.scan_directory, menu->settings.grid_directory) != 0) {
        entries_free();
        free(grid.scan_directory);
        grid.scan_directory = NULL;
        grid.open_group[0] = '\0';
        grid.selected = 0;
    }
    grid.stale = false;
    for (int32_t i = 0; i < grid.count; i++) {
        grid.entries[i].seen = false;
    }

    grid_scan_t scan = { .menu = menu, .start_us = get_ticks_us() };
    scan.added = malloc(GRID_MAX_ENTRIES * sizeof(*scan.added));
    grid.scan_error = !scan.added || !walk_library(menu, &scan);
    if (!grid.scan_error) {
        bool changed = !grid.scan_directory;
        int32_t survivors = 0;
        for (int32_t i = 0; i < grid.count; i++) {
            if (!grid.entries[i].seen) {
                entry_free(&grid.entries[i]);
                changed = true;
            } else {
                if (survivors != i) {
                    grid.entries[survivors] = grid.entries[i];
                    memset(&grid.entries[i], 0, sizeof(grid.entries[i]));
                }
                survivors++;
            }
        }
        grid.count = survivors;

        int32_t additions = scan.added_count;
        if (additions > GRID_MAX_ENTRIES - survivors) {
            additions = GRID_MAX_ENTRIES - survivors;
        }
        scan.total = additions;
        /* Existing entries keep their manual order; new ones follow by title. */
        for (int32_t i = 0; i < additions; i++) {
            grid_entry_t *entry = entry_append();
            if (!entry) {
                break;
            }
            entry->path = scan.added[i];
            scan.added[i] = NULL;
            read_entry(&scan, entry);
            changed = true;
        }
        if (grid.count - survivors > 1) {
            qsort(&grid.entries[survivors], grid.count - survivors, sizeof(*grid.entries), compare_entries);
        }

        grid.scan_directory = strdup(menu->settings.grid_directory);
        if (changed) {
            cache_save();
        }
        debugf("[GRID] Sync: %ld ROMs, %ld read in %lu ms\n", (long)grid.count, (long)scan.reads,
               (unsigned long)((get_ticks_us() - scan.start_us) / 1000));
    }
    if (scan.added) {
        for (int32_t i = 0; i < scan.added_count; i++) {
            free(scan.added[i]);
        }
        free(scan.added);
    }

    rebuild_view();
    if (!grid.view_count && grid.open_group[0]) {
        grid.open_group[0] = '\0';
        rebuild_view();
    }
    clamp_selection();
}

static char *art_path(const grid_entry_t *entry, grid_art_mode_t art_mode) {
    path_t *path = metadata_directory(entry);
    if (!path) {
        return NULL;
    }
    path_push(path, art_mode == GRID_ART_GAMEPAK ? "gamepak_front.png" : "boxart_front.png");
    char *result = file_exists(path_get(path)) ? strdup(path_get(path)) : NULL;
    path_free(path);
    return result;
}

/* ZIP metadata names its artwork in metadata.ini rather than by fixed filenames. */
static char *zip_art_read(const char *zip_path, grid_art_mode_t art_mode, size_t *size) {
    ini_t *metadata = zip_ini_load(zip_path);
    if (!metadata) {
        return NULL;
    }
    const char *name = ini_get_string(metadata, art_mode == GRID_ART_GAMEPAK ? "cartart" : "boxart", "front", "");
    char *data = zip_read(zip_path, name, GRID_ZIP_ART_MAX_SIZE, size);
    ini_free(metadata);
    return data;
}

static grid_view_item_t *slot_item(int slot) {
    int32_t view_index = grid.page_start + slot;
    return view_index < grid.view_count ? &grid.view[view_index] : NULL;
}

static int16_t image_right_edge_top_opaque_row(const surface_t *image) {
    if (!image || surface_get_format(image) != FMT_RGBA16) {
        return -1;
    }
    for (int y = 0; y < image->height; y++) {
        const uint16_t *row = (const uint16_t *)((const uint8_t *)image->buffer + y * image->stride);
        if (row[image->width - 1] & 1) {
            return y;
        }
    }
    return -1;
}

static void thumbnail_callback(png_err_t error, surface_t *image, void *callback_data) {
    grid_thumbnail_t *thumbnail = callback_data;
    thumbnail->loading = false;
    if (error == PNG_OK) {
        grid_entry_t *entry = &grid.entries[slot_item(thumbnail - grid.thumbnails)->entry_index];
        thumbnail->top_opaque_row = grid.art_mode == GRID_ART_GAMEPAK
            ? image_right_edge_top_opaque_row(image) : -1;
        thumbnail->from_cache = art_cache_put(entry->path, grid.art_mode,
            image, thumbnail->top_opaque_row);
        thumbnail->image = image;
    } else {
        thumbnail->no_art = true;
    }
}

/* The decoder's callback targets a thumbnail slot, which must not move under it. */
static void abort_thumbnail_load(void) {
    for (int i = 0; i < GRID_PAGE_ENTRIES; i++) {
        if (grid.thumbnails[i].loading) {
            png_decoder_abort();
            grid.thumbnails[i].loading = false;
            return;
        }
    }
}

static void thumbnails_clear(void) {
    rspq_wait();
    abort_thumbnail_load();
    for (int i = 0; i < GRID_PAGE_ENTRIES; i++) {
        grid_thumbnail_t *thumbnail = &grid.thumbnails[i];
        if (thumbnail->image && !thumbnail->from_cache) {
            surface_free(thumbnail->image);
            free(thumbnail->image);
        }
        *thumbnail = (grid_thumbnail_t){ .top_opaque_row = -1 };
    }
}

static void prepare_page(void) {
    thumbnails_clear();
    grid.page_start = (grid.selected / GRID_PAGE_ENTRIES) * GRID_PAGE_ENTRIES;
}

static void queue_thumbnail(void) {
    for (int slot = 0; slot < GRID_PAGE_ENTRIES; slot++) {
        if (grid.thumbnails[slot].loading) {
            return;
        }
    }

    int selected_slot = grid.selected - grid.page_start;
    for (int priority = -1; priority < GRID_PAGE_ENTRIES; priority++) {
        int slot = priority < 0 ? selected_slot : priority;
        if (slot < 0 || slot >= GRID_PAGE_ENTRIES || (priority >= 0 && slot == selected_slot)) {
            continue;
        }
        grid_thumbnail_t *thumbnail = &grid.thumbnails[slot];
        grid_view_item_t *item = slot_item(slot);
        if (!item || thumbnail->image || thumbnail->no_art) {
            continue;
        }
        grid_entry_t *entry = &grid.entries[item->entry_index];
        surface_t *cached = art_cache_get(entry->path, grid.art_mode, &thumbnail->top_opaque_row);
        if (cached) {
            thumbnail->image = cached;
            thumbnail->from_cache = true;
            continue;
        }
        png_err_t error;
        path_t *zip_path = metadata_zip_path(entry);
        if (zip_path) {
            size_t size;
            char *data = zip_art_read(path_get(zip_path), grid.art_mode, &size);
            path_free(zip_path);
            if (!data) {
                thumbnail->no_art = true;
                continue;
            }
            error = png_decoder_start_mem(data, size, GRID_ART_DECODE_SIZE, GRID_ART_DECODE_SIZE,
                thumbnail_callback, thumbnail);
            if (error != PNG_OK) {
                free(data);
            }
        } else {
            char *path = art_path(entry, grid.art_mode);
            if (!path) {
                thumbnail->no_art = true;
                continue;
            }
            error = png_decoder_start(path, GRID_ART_DECODE_SIZE, GRID_ART_DECODE_SIZE,
                thumbnail_callback, thumbnail);
            free(path);
        }
        if (error == PNG_OK) {
            thumbnail->loading = true;
        } else if (error != PNG_ERR_BUSY) {
            thumbnail->no_art = true;
        }
        return;
    }
}

/* The menu's actions map C buttons to fast scrolling; the grid uses them to sort. */
static joypad_buttons_t buttons_pressed(void) {
    JOYPAD_PORT_FOREACH (port) {
        joypad_buttons_t pressed = joypad_get_buttons_pressed(port);
        if (pressed.raw) {
            return pressed;
        }
    }
    return (joypad_buttons_t){ 0 };
}

static bool a_held(void) {
    JOYPAD_PORT_FOREACH (port) {
        /* get_buttons_held excludes the first pressed frame; current state does not. */
        if (joypad_get_buttons(port).a) {
            return true;
        }
    }
    return false;
}

static int32_t navigation_target(menu_t *menu) {
    if (!grid.view_count) {
        return -1;
    }
    if (menu->actions.go_fast) return grid.selected;
    int32_t target = grid.selected;
    int column = grid.selected % GRID_COLUMNS;
    if (menu->actions.go_left && column > 0) {
        target--;
    } else if (menu->actions.go_right && column < GRID_COLUMNS - 1 && target + 1 < grid.view_count) {
        target++;
    } else if (menu->actions.go_up && target >= GRID_COLUMNS) {
        target -= GRID_COLUMNS;
    } else if (menu->actions.go_down && (target / GRID_COLUMNS + 1) * GRID_COLUMNS < grid.view_count) {
        target += GRID_COLUMNS;
        if (target >= grid.view_count) {
            target = grid.view_count - 1;
        }
    }
    return target;
}

static void move_item(void *items, size_t size, int32_t from, int32_t to) {
    _Static_assert(sizeof(grid_entry_t) >= sizeof(grid_thumbnail_t), "move_item buffer");
    uint8_t item[sizeof(grid_entry_t)];
    uint8_t *base = items;
    memcpy(item, base + from * size, size);
    if (from < to) {
        memmove(base + from * size, base + (from + 1) * size, (to - from) * size);
    } else {
        memmove(base + (to + 1) * size, base + to * size, (from - to) * size);
    }
    memcpy(base + to * size, item, size);
}

/* Moves the selected ROM in the manual order, shifting the ROMs in between. */
static void move_selected(int32_t to) {
    int32_t from = grid.selected;
    abort_thumbnail_load();
    move_item(grid.entries, sizeof(*grid.entries), from, to);
    grid.selected = to;
    rebuild_view();

    int32_t from_slot = from - grid.page_start;
    int32_t to_slot = to - grid.page_start;
    if (to_slot < 0 || to_slot >= GRID_PAGE_ENTRIES) {
        prepare_page();
        return;
    }
    /* Keep decoded artwork with its ROM instead of reloading the page. */
    move_item(grid.thumbnails, sizeof(*grid.thumbnails), from_slot, to_slot);
}

static void open_selected(menu_t *menu, bool launch) {
    if (!grid.view_count) {
        return;
    }
    grid_view_item_t *item = &grid.view[grid.selected];
    if (is_group(item)) {
        snprintf(grid.open_group, sizeof(grid.open_group), "%s", item->group_name);
        rebuild_view();
        grid.selected = 0;
        prepare_page();
        sound_play_effect(SFX_ENTER);
        return;
    }
    if (menu->load.rom_path) {
        rom_info_free_meta(&menu->load.rom_info);
        path_free(menu->load.rom_path);
    }
    menu->load.rom_path = path_init(menu->storage_prefix, grid.entries[item->entry_index].path);
    menu->load.from_grid = true;
    menu->load_pending.rom_file = launch;
    menu->next_mode = MENU_MODE_LOAD_ROM;
    sound_play_effect(SFX_ENTER);
}

static void close_group(void) {
    char closed_group[sizeof(grid.open_group)];
    snprintf(closed_group, sizeof(closed_group), "%s", grid.open_group);
    grid.open_group[0] = '\0';
    rebuild_view();
    grid.selected = 0;
    for (int32_t i = 0; i < grid.view_count; i++) {
        if (is_group(&grid.view[i]) && strcasecmp(grid.view[i].group_name, closed_group) == 0) {
            grid.selected = i;
            break;
        }
    }
    prepare_page();
}

static void select_history(menu_t *menu) {
    for (int i = 0; i < HISTORY_COUNT; i++) {
        bookkeeping_item_t *history = &menu->bookkeeping.history_items[i];
        if (history->bookkeeping_type != BOOKKEEPING_TYPE_ROM || !path_has_value(history->primary_path)) {
            continue;
        }
        const char *history_path = strip_fs_prefix(path_get(history->primary_path));
        int32_t entry_index = entry_find(history_path);
        if (entry_index < 0) {
            entry_index = entry_find_basename(basename_of(history_path));
        }
        if (entry_index >= 0) {
            int32_t view_index = view_find(entry_index);
            if (view_index >= 0) {
                grid.selected = view_index;
            }
            return;
        }
    }
}

/* Selects the ROM with this path, or the author group that contains it. */
static void select_entry_path(const char *path) {
    grid.selected = 0;
    for (int32_t i = 0; i < grid.view_count; i++) {
        grid_view_item_t *item = &grid.view[i];
        if (!is_group(item) && grid.entries[item->entry_index].path == path) {
            grid.selected = i;
            return;
        }
    }
    int32_t entry_index = 0;
    while (entry_index < grid.count && grid.entries[entry_index].path != path) entry_index++;
    if (entry_index == grid.count) {
        return;
    }
    char author[GRID_NAME_LENGTH];
    primary_author(&grid.entries[entry_index], author);
    for (int32_t i = 0; i < grid.view_count; i++) {
        if (is_group(&grid.view[i]) && strcasecmp(grid.view[i].group_name, author) == 0) {
            grid.selected = i;
            return;
        }
    }
}

static void process_sort_buttons(joypad_buttons_t pressed) {
    bool in_group = grid.open_group[0];
    bool changed;
    bool resorted = false;
    /* Entry paths are stable pointers, so they identify the selection across re-sorting. */
    const char *selected_path = grid.view_count ?
        grid.entries[grid.view[grid.selected].entry_index].path : NULL;
    if (pressed.c_up) {
        /* Title order is the manual arrangement; choosing it again re-sorts it. */
        if (in_group) {
            changed = grid.sort != GRID_SORT_TITLE;
        } else {
            if (grid.sort == GRID_SORT_TITLE && grid.group == GRID_GROUP_NONE) {
                thumbnails_clear();
                qsort(grid.entries, grid.count, sizeof(*grid.entries), compare_entries);
                resorted = true;
            }
            grid.group = GRID_GROUP_NONE;
            changed = true;
        }
        grid.sort = GRID_SORT_TITLE;
    } else if (pressed.c_left) {
        changed = grid.sort != GRID_SORT_RELEASE_DATE || (!in_group && grid.group != GRID_GROUP_NONE);
        grid.sort = GRID_SORT_RELEASE_DATE;
        if (!in_group) grid.group = GRID_GROUP_NONE;
    } else {
        changed = !in_group && grid.group != GRID_GROUP_AUTHOR;
        if (!in_group) grid.group = GRID_GROUP_AUTHOR;
    }
    if (changed) {
        rebuild_view();
        select_entry_path(selected_path);
        prepare_page();
        if (resorted) {
            cache_save();
        } else {
            cache_save_preferences();
        }
        sound_play_effect(SFX_SETTING);
    }
}

static void process(menu_t *menu) {
    joypad_buttons_t pressed = buttons_pressed();
    if (!grid.a_pending && (pressed.c_up || pressed.c_left || pressed.c_right)) {
        process_sort_buttons(pressed);
        return;
    }

    /* Held C buttons also report accelerated navigation, which the grid ignores. */
    if (menu->actions.go_fast && !grid.a_pending) {
        return;
    }

    if (menu->actions.context && !grid.a_pending) {
        grid.art_mode = grid.art_mode == GRID_ART_GAMEPAK ? GRID_ART_BOXART : GRID_ART_GAMEPAK;
        prepare_page();
        cache_save_preferences();
        sound_play_effect(SFX_SETTING);
        return;
    }

    bool can_rearrange = grid.sort == GRID_SORT_TITLE &&
                         grid.group == GRID_GROUP_NONE && !grid.open_group[0];
    if (menu->actions.enter && grid.view_count && !grid.a_pending) {
        if (!can_rearrange) {
            open_selected(menu, false);
            return;
        }
        grid.a_pending = true;
        grid.a_start_ticks = timer_ticks();
    }

    if (grid.a_pending) {
        if (a_held()) {
            int32_t target = navigation_target(menu);
            long long hold_ticks = (long long)(TICKS_PER_SECOND * GRID_HOLD_SECONDS);
            if (!grid.moving && (timer_ticks() - grid.a_start_ticks >= hold_ticks || target != grid.selected)) {
                grid.moving = true;
                sound_play_effect(SFX_SETTING);
            }
            if (grid.moving && target >= 0 && target != grid.selected) {
                move_selected(target);
                sound_play_effect(SFX_CURSOR);
            }
        } else {
            if (grid.moving) {
                cache_save();
                sound_play_effect(SFX_SETTING);
            } else {
                open_selected(menu, false);
            }
            grid.a_pending = false;
            grid.moving = false;
        }
        return;
    }

    int32_t target = navigation_target(menu);
    if (target >= 0 && target != grid.selected) {
        grid.selected = target;
        if ((grid.selected / GRID_PAGE_ENTRIES) * GRID_PAGE_ENTRIES != grid.page_start) {
            prepare_page();
        }
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.back) {
        if (grid.open_group[0]) {
            close_group();
        } else {
            menu->next_mode = MENU_MODE_BROWSER;
        }
        sound_play_effect(SFX_EXIT);
    } else if (menu->actions.tab_left) {
        menu->next_mode = MENU_MODE_SETTINGS;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.tab_right) {
        menu->next_mode = MENU_MODE_BROWSER;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.settings) {
        open_selected(menu, true);
    }
}

/* Cover the art bounds: flush edges are preferable to letterboxing. */
static float cover_scale(const surface_t *image) {
    float scale_x = GRID_ART_WIDTH / (float)image->width;
    float scale_y = GRID_ART_HEIGHT / (float)image->height;
    return scale_x > scale_y ? scale_x : scale_y;
}

static int thumbnail_right_shadow_y(const grid_thumbnail_t *thumbnail, int art_y) {
    if (grid.art_mode == GRID_ART_BOXART || !thumbnail->image) {
        return art_y;
    }
    if (thumbnail->top_opaque_row < 0) {
        return art_y + GRID_ART_HEIGHT;
    }
    float scale = cover_scale(thumbnail->image);
    float scaled_height = thumbnail->image->height * scale;
    float edge_y = art_y + (GRID_ART_HEIGHT - scaled_height) / 2.0f +
        thumbnail->top_opaque_row * scale;
    int footer_y = art_y + GRID_ART_HEIGHT;
    int shadow_y = (int)floorf(edge_y);
    return shadow_y < art_y ? art_y : shadow_y > footer_y ? footer_y : shadow_y;
}

static void draw_frame(int x, int y, bool selected, bool moving, bool grouped, int right_shadow_y) {
    color_t tile_color = moving ? GRID_TILE_MOVING : selected ? GRID_TILE_SELECTED :
        grouped ? GRID_ACCENT_COLOR : GRID_TILE_COLOR;
    const int footer_y = y + GRID_ART_HEIGHT;
    ui_components_box_draw(x + 4, footer_y + 4, x + GRID_TILE_WIDTH + 4,
        y + GRID_TILE_HEIGHT + 4, GRID_TILE_SHADOW);
    if (right_shadow_y < footer_y) {
        ui_components_box_draw(x + GRID_TILE_WIDTH, right_shadow_y + 4,
            x + GRID_TILE_WIDTH + 4, y + GRID_TILE_HEIGHT + 4, GRID_TILE_SHADOW);
    }
    if (selected) {
        ui_components_box_draw(x - 3, y - 3, x + GRID_TILE_WIDTH + 3,
            y + GRID_TILE_HEIGHT + 3, moving ? GRID_TILE_MOVING : GRID_ACCENT_COLOR);
    }
    ui_components_box_draw(x, footer_y, x + GRID_TILE_WIDTH, y + GRID_TILE_HEIGHT, tile_color);
}

static void draw_tile(int slot) {
    grid_thumbnail_t *thumbnail = &grid.thumbnails[slot];
    grid_view_item_t *item = slot_item(slot);
    if (!item) {
        return;
    }
    grid_entry_t *entry = &grid.entries[item->entry_index];
    int x = GRID_START_X + (slot % GRID_COLUMNS) * (GRID_TILE_WIDTH + GRID_TILE_GAP_X);
    int y = GRID_START_Y + (slot / GRID_COLUMNS) * (GRID_TILE_HEIGHT + GRID_TILE_GAP_Y);
    bool selected = grid.page_start + slot == grid.selected;
    bool grouped = is_group(item);
    draw_frame(x, y, selected, selected && grid.moving, grouped, thumbnail_right_shadow_y(thumbnail, y));

    if (thumbnail->image) {
        float scale = cover_scale(thumbnail->image);
        float width = thumbnail->image->width * scale;
        float height = thumbnail->image->height * scale;
        rdpq_mode_push();
            rdpq_set_mode_standard();
            rdpq_mode_alphacompare(1);
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_set_scissor(x, y, x + GRID_ART_WIDTH, y + GRID_ART_HEIGHT);
            rdpq_tex_blit(thumbnail->image,
                x + (GRID_ART_WIDTH - width) / 2.0f,
                y + (GRID_ART_HEIGHT - height) / 2.0f,
                &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale, .filtering = true });
            rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        rdpq_mode_pop();
    } else {
        ui_components_box_draw(x, y, x + GRID_ART_WIDTH, y + GRID_ART_HEIGHT, GRID_ART_PLACEHOLDER);
        rdpq_text_print(
            &(rdpq_textparms_t){
                .style_id = STL_GRAY,
                .width = GRID_ART_WIDTH,
                .height = GRID_ART_HEIGHT,
                .align = ALIGN_CENTER,
                .valign = VALIGN_CENTER,
                .wrap = WRAP_NONE,
            },
            FNT_DEFAULT, x, y, entry->game_code
        );
    }

    const int label_x = x + 5;
    const int label_y = y + GRID_TILE_HEIGHT - 19;
    const int label_width = GRID_TILE_WIDTH - 10;
    float width = grouped ? 0.0f : title_width(entry);
    if (selected && width > label_width) {
        rdpq_set_scissor(label_x, label_y, label_x + label_width, label_y + 20);
        rdpq_text_print(
            &(rdpq_textparms_t){
                .style_id = STL_DEFAULT,
                .width = (int)ceilf(width + 2.0f),
                .height = 20,
                .wrap = WRAP_NONE,
                .char_spacing = -1,
            },
            FNT_DEFAULT, label_x - marquee_offset(width - label_width), label_y, entry->display_name
        );
        rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    } else {
        rdpq_text_print(
            &(rdpq_textparms_t){
                .style_id = grouped ? STL_YELLOW : selected ? STL_DEFAULT : STL_BLUE,
                .width = label_width,
                .height = 20,
                .align = ALIGN_CENTER,
                .wrap = WRAP_ELLIPSES,
                .char_spacing = -1,
            },
            FNT_DEFAULT, label_x, label_y, grouped ? item->group_name : entry->display_name
        );
    }
    if (grouped) {
        rdpq_text_printf(
            &(rdpq_textparms_t){ .width = GRID_ART_WIDTH - 8, .align = ALIGN_RIGHT },
            FNT_LARGE_NUMBERS, x, y + 26, "%ld", (long)item->group_count
        );
    }
}

static void draw(menu_t *menu, surface_t *display) {
    rdpq_attach(display, NULL);
    ui_components_background_draw();
    ui_components_tabs_common_draw(menu, 0);
    ui_components_layout_draw_tabbed();

    grid_view_item_t *selected_item = grid.view_count ? &grid.view[grid.selected] : NULL;
    if (!selected_item) {
        ui_components_main_text_draw(
            grid.scan_error ? STL_RED : STL_BLUE,
            ALIGN_CENTER, VALIGN_CENTER,
            "%s\n%s\n\n"
            "Grid shows the ROMs directly inside this directory.\n"
            "To use another, open it in Files, press Z,\n"
            "and select Set current directory as Grid library.",
            grid.scan_error ? "Couldn't read the Grid library:" : "No N64 ROMs in the Grid library:",
            menu->settings.grid_directory
        );
    } else {
        if (grid.marquee_selected != grid.selected || grid.marquee_start_ticks == 0) {
            grid.marquee_selected = grid.selected;
            grid.marquee_start_ticks = timer_ticks();
        }
        for (int slot = 0; slot < GRID_PAGE_ENTRIES; slot++) {
            draw_tile(slot);
        }
        ui_components_scrollbar_draw(
            GRID_SCROLLBAR_X, GRID_SCROLLBAR_Y, GRID_SCROLLBAR_WIDTH, GRID_SCROLLBAR_HEIGHT,
            grid.selected / GRID_COLUMNS, (grid.view_count + GRID_COLUMNS - 1) / GRID_COLUMNS, GRID_ROWS
        );

        bool grouped = is_group(selected_item);
        char title[GRID_NAME_LENGTH];
        two_line_title(grouped ? selected_item->group_name : grid.entries[selected_item->entry_index].display_name,
            title, sizeof(title));
        ui_components_actions_bar_text_draw(STL_DEFAULT, ALIGN_CENTER, VALIGN_TOP, "%s", title);
    }

    if (grid.moving) {
        ui_components_actions_bar_text_draw(STL_DEFAULT, ALIGN_LEFT, VALIGN_TOP,
            "Release A: Save order\nStick/D-pad: Move tile");
    } else {
        ui_components_actions_bar_text_draw(STL_DEFAULT, ALIGN_LEFT, VALIGN_TOP, "%s\n%s",
            !selected_item ? "" :
            is_group(selected_item) ? "A: Expand" :
            grid.open_group[0] ? "A: Open | B: Back" : "A: Open",
            grid.group == GRID_GROUP_AUTHOR && !grid.open_group[0] ? "C: Group (Author)" :
            grid.sort == GRID_SORT_RELEASE_DATE ? "C: Sort (Date)" : "C: Sort (Title)");
    }
    ui_components_actions_bar_text_draw(
        STL_DEFAULT, ALIGN_RIGHT, VALIGN_TOP,
        grid.art_mode == GRID_ART_GAMEPAK ? "Z: Box Art\nStart: Launch" : "Z: Cartridges\nStart: Launch"
    );
    rdpq_detach_show();
}

/* Loads the cached index on first use; Grid syncs it before showing it. */
static void load_index(menu_t *menu) {
    if (grid.initialized) {
        return;
    }
    grid.initialized = true;
    grid.stale = true;
    grid.storage_prefix = menu->storage_prefix;
    path_t *cache = path_init(menu->storage_prefix, GRID_CACHE_FILE);
    grid.cache_path = strdup(path_get(cache));
    path_free(cache);
    uint64_t start_us = get_ticks_us();
    if (cache_load(menu->settings.grid_directory)) {
        rebuild_view();
        debugf("[GRID] Loaded %ld cached ROMs in %lu ms\n", (long)grid.count,
               (unsigned long)((get_ticks_us() - start_us) / 1000));
    }
}

static bool index_matches_library(menu_t *menu) {
    return grid.scan_directory && strcmp(grid.scan_directory, menu->settings.grid_directory) == 0;
}

void view_grid_init(menu_t *menu) {
    load_index(menu);
    /* Syncs after startup, after Files changed the library, or for a new library directory. */
    if (grid.stale || !index_matches_library(menu)) {
        thumbnails_clear();
        refresh_index(menu);
    }
    if (!grid.opened) {
        grid.opened = true;
        select_history(menu);
    }
    art_cache_enable_if_possible();
    grid.a_pending = false;
    grid.moving = false;
    prepare_page();
}

void view_grid_display(menu_t *menu, surface_t *display) {
    process(menu);
    queue_thumbnail();
    /* Decode a bounded batch, immediately starting the next image when ready. */
    for (int i = 0; i < GRID_DECODE_ROWS_PER_FRAME; i++) {
        png_decoder_poll();
        queue_thumbnail();
    }
    draw(menu, display);
    if (menu->next_mode != MENU_MODE_GRID) {
        thumbnails_clear();
        /* Details returns to Grid, so keep its artwork for then. */
        if (menu->next_mode != MENU_MODE_LOAD_ROM) {
            art_cache_clear();
        }
    }
}

const char *view_grid_cache_summary(menu_t *menu) {
    static char summary[24];
    load_index(menu);
    if (grid.scan_error) {
        return "Scan failed";
    }
    if (!index_matches_library(menu)) {
        return "Not scanned";
    }
    snprintf(summary, sizeof(summary), "%ld ROM%s", (long)grid.count, grid.count == 1 ? "" : "s");
    return summary;
}

void view_grid_clear_cache(menu_t *menu) {
    load_index(menu);
    remove(grid.cache_path);
    entries_free();
    free(grid.scan_directory);
    grid.scan_directory = NULL;
    grid.sort = GRID_SORT_TITLE;
    grid.group = GRID_GROUP_NONE;
    grid.art_mode = GRID_ART_BOXART;
    grid.open_group[0] = '\0';
    grid.selected = 0;
    grid.scan_error = false;
    grid.opened = false;
    rebuild_view();
}

void view_grid_library_changed(void) {
    grid.stale = true;
}

void view_grid_shutdown(void) {
    thumbnails_clear();
    art_cache_clear();
    entries_free();
    free(grid.cache_path);
    free(grid.scan_directory);
    memset(&grid, 0, sizeof(grid));
}
