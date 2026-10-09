#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include "../jpeg_decoder.h"
#include "../audio_player.h"
#include "../png_decoder.h"
#include "../sound.h"
#include "../path.h"
#include "../ui_components/constants.h"
#include "utils/fs.h"
#include "views.h"


#define SEEK_SECONDS            (5)
#define SEEK_SECONDS_FAST       (60)
#define COVER_ART_MAX_SIZE      (238)
#define CONTENT_TOP_OFFSET      (54)
#define HEADER_UNIT_HEIGHT      (46)  /* title + ticker + tech line */
#define HEADER_BASELINE_OFFSET  (12)
#define HEADER_LINE_SPACING     (16)
#define CONTENT_BOTTOM_PAD      (8)
#define QUEUE_MAX_VISIBLE       (15)
#define QUEUE_TEXT_WIDTH         (280)
#define TICKER_SCROLL_SPEED     (0.5f)
#define COVER_SCAN_MAX_ATTEMPTS (20)
#define COVER_ART_BUDGET_MAX    (400)

typedef enum {
    PLAYBACK_NORMAL,
    PLAYBACK_LOOP,
    PLAYBACK_REPEAT_ONE,
    PLAYBACK_SHUFFLE,
    PLAYBACK_PARTY,
    PLAYBACK_MODE_COUNT,
} playback_mode_t;

static const char *playback_mode_name[] = {
    [PLAYBACK_NORMAL]     = "Normal",
    [PLAYBACK_LOOP]       = "Loop",
    [PLAYBACK_REPEAT_ONE] = "Repeat One",
    [PLAYBACK_SHUFFLE]    = "Shuffle",
    [PLAYBACK_PARTY]      = "Party",
};

typedef enum {
    PLAYER_LOADING,
    PLAYER_READY,
} player_state_t;

static player_state_t player_state = PLAYER_LOADING;
static int loading_step = 0;

static playback_mode_t playback_mode = PLAYBACK_NORMAL;
static bool advance_failed = false;
static bool seek_busy = false;  /* one seek per frame, prevents cascade */
static float ticker_offset = 0.0f;

#define QUEUE_LINE_HEIGHT   (16)
#define QUEUE_GAP           (12)
#define QUEUE_ARROW         "\xE2\x96\xB6 "  /* "▶ " UTF-8 */

/* Shuffle state: array of browser indices that point to music files */
static int *shuffle_list = NULL;
static int shuffle_count = 0;
static int shuffle_pos = 0;

/* Cover art state */
static bool cover_art_expected = false;
static bool cover_first_load = true;
static char cover_art_source[256] = "";  /* path of currently loaded art */

typedef enum {
    COVER_IDLE,
    COVER_LOADING_JPEG,
    COVER_LOADING_PNG,
} cover_state_t;

static cover_state_t cover_state = COVER_IDLE;
static surface_t *cover_image = NULL;

/* Cached blit parameters, computed once per loaded image */
static int cover_disp_size;
static int cover_s0;
static int cover_t0;

/* Lazy ID3 metadata cache for queue display */
typedef struct {
    int browser_idx;
    char title[ID3_FIELD_MAX];
    int track_number;
    bool loaded;
} queue_cache_entry_t;

static queue_cache_entry_t *queue_cache = NULL;
static int queue_cache_count = 0;

/* Music file index map (browser indices of ENTRY_TYPE_MUSIC files) */
static int *music_indices = NULL;
static int music_count = 0;

/* Directory scan state for async cover art search */
static const char *cover_image_extensions[] = { "png", "jpg", "jpeg", NULL };
static const char *preferred_cover_names[] = { "cover", "folder", "front", "album", "art", NULL };
static path_t *cover_dir = NULL;
static dir_t cover_dir_entry;
static bool cover_dir_scan_active = false;

static void try_next_cover_source(void);

/** Cancel any in-progress cover art decode. */
static void abort_cover_decode (void) {
    if (cover_state == COVER_LOADING_JPEG) jpeg_decoder_abort();
    if (cover_state == COVER_LOADING_PNG) png_decoder_abort();
    cover_state = COVER_IDLE;
}

/** Build the music file index map and allocate the lazy ID3 cache. */
static void build_music_index_map (menu_t *menu) {
    free(music_indices); music_indices = NULL;
    free(queue_cache); queue_cache = NULL;
    music_count = 0;
    queue_cache_count = 0;

    /* Count music files */
    int count = 0;
    for (int i = 0; i < menu->browser.entries; i++) {
        if (menu->browser.list[i].type == ENTRY_TYPE_MUSIC) count++;
    }
    if (count == 0) return;

    music_indices = malloc(count * sizeof(int));
    queue_cache = calloc(count, sizeof(queue_cache_entry_t));
    if (!music_indices || !queue_cache) {
        free(music_indices); music_indices = NULL;
        free(queue_cache); queue_cache = NULL;
        return;
    }

    for (int i = 0; i < menu->browser.entries; i++) {
        if (menu->browser.list[i].type == ENTRY_TYPE_MUSIC) {
            music_indices[music_count] = i;
            queue_cache[music_count].browser_idx = i;
            music_count++;
        }
    }
    queue_cache_count = music_count;
}

/** Get the display name for a queue entry, lazily parsing ID3 if needed. */
static const char *queue_entry_name (menu_t *menu, int queue_idx, char *buf, size_t buf_size) {
    if (queue_idx < 0 || queue_idx >= queue_cache_count || !queue_cache) {
        snprintf(buf, buf_size, "???");
        return buf;
    }

    queue_cache_entry_t *c = &queue_cache[queue_idx];

    /* Lazy parse: one track per frame max to avoid hitches */
    if (!c->loaded) {
        int bidx = c->browser_idx;
        entry_t *e = &menu->browser.list[bidx];
        path_t *path = path_clone_push(menu->browser.directory, e->name);

        FILE *f = fopen(path_get(path), "rb");
        if (f) {
            struct stat st;
            if (fstat(fileno(f), &st) == 0) {
                id3_metadata_t meta;
                id3_parse(f, st.st_size, &meta, 0);  /* text only, no APIC */
                if (meta.has_metadata && meta.title[0]) {
                    strncpy(c->title, meta.title, ID3_FIELD_MAX - 1);
                    c->title[ID3_FIELD_MAX - 1] = '\0';
                }
                c->track_number = meta.track_number;
            }
            fclose(f);
        }
        path_free(path);
        c->loaded = true;
    }

    /* Build display string: "03. Title" or "Title" or filename */
    if (c->title[0]) {
        if (c->track_number > 0) {
            snprintf(buf, buf_size, "%02d. %s", c->track_number, c->title);
        } else {
            snprintf(buf, buf_size, "%s", c->title);
        }
    } else {
        /* Fall back to filename without extension */
        entry_t *e = &menu->browser.list[c->browser_idx];
        strncpy(buf, e->name, buf_size - 1);
        buf[buf_size - 1] = '\0';
        char *dot = strrchr(buf, '.');
        if (dot) *dot = '\0';
    }
    return buf;
}

/** Compute and cache blit parameters for the loaded cover_image. */
static void cover_cache_blit_params (void) {
    int iw = cover_image->width;
    int ih = cover_image->height;
    cover_s0 = (iw > ih) ? (iw - ih) / 2 : 0;
    cover_t0 = (ih > iw) ? (ih - iw) / 2 : 0;

    /* Position: centered horizontally, between ticker and seekbar */
    int text_bottom = VISIBLE_AREA_Y0 + CONTENT_TOP_OFFSET;
    int bar_top = SEEKBAR_Y - BORDER_THICKNESS;
    int avail_h = bar_top - text_bottom - 16;
    int crop = (iw < ih) ? iw : ih;
    cover_disp_size = (avail_h < COVER_ART_MAX_SIZE) ? avail_h : COVER_ART_MAX_SIZE;
    if (cover_disp_size > crop) cover_disp_size = crop;
}

/** Get the memory budget for cover art decode, based on free heap. */
static int cover_art_budget_size (void) {
    heap_stats_t heap;
    sys_get_heap_stats(&heap);
    size_t budget = (size_t)((heap.total - heap.used) * 0.8f);
    int dim = (int)sqrtf((float)(budget / 2));
    if (dim < 16) dim = 16;
    /* Clamp upward if heap can actually support the preferred cover art size */
    size_t min_surface = (size_t)COVER_ART_MAX_SIZE * COVER_ART_MAX_SIZE * 2;
    if (dim < COVER_ART_MAX_SIZE && budget > min_surface) dim = COVER_ART_MAX_SIZE;
    if (dim > COVER_ART_BUDGET_MAX) dim = COVER_ART_BUDGET_MAX;
    return dim;
}

/** Check if a PNG file's dimensions fit the memory budget.
 *  Reads only the 24-byte header. Returns true if safe to decode. */
static bool png_dimensions_ok (const char *path, int max_dim) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t hdr[24];
    size_t rd = fread(hdr, 1, 24, f);
    fclose(f);
    if (rd < 24) return false;
    /* PNG header: bytes 16-19 = width, 20-23 = height (big-endian) */
    if (memcmp(hdr, "\x89PNG", 4) != 0) return false;
    int w = (hdr[16] << 24) | (hdr[17] << 16) | (hdr[18] << 8) | hdr[19];
    int h = (hdr[20] << 24) | (hdr[21] << 16) | (hdr[22] << 8) | hdr[23];
    return (w <= max_dim && h <= max_dim);
}

static void cover_art_cb (jpeg_err_t err, surface_t *image, void *data) {
    cover_state = COVER_IDLE;
    if (err == JPEG_OK && image) {
        if (cover_image) {
            surface_free(cover_image);
            free(cover_image);
        }
        cover_image = image;
        cover_cache_blit_params();
    } else {
        try_next_cover_source();
    }
}

static void cover_art_png_cb (png_err_t err, surface_t *image, void *data) {
    cover_state = COVER_IDLE;
    if (err == PNG_OK && image) {
        if (cover_image) {
            surface_free(cover_image);
            free(cover_image);
        }
        cover_image = image;
        cover_cache_blit_params();
    } else {
        try_next_cover_source();
    }
}

/** Try to load a cover art file. Returns true if decode started. */
static const char *jpeg_extensions[] = { "jpg", "jpeg", NULL };
static const char *png_extensions[] = { "png", NULL };

/** Read a file into a heap buffer. Returns NULL on failure or if too large. */
static uint8_t *slurp_file (const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size_t sz = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz == 0 || sz > 2 * 1024 * 1024) { fclose(f); return NULL; }
    uint8_t *buf = malloc(sz);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, sz, f) != sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_size = sz;
    return buf;
}

static bool try_cover_path (const char *path, int max_size) {
    bool is_jpeg = file_has_extensions((char *)path, jpeg_extensions);
    bool is_png  = file_has_extensions((char *)path, png_extensions);
    if (!is_jpeg && !is_png) return false;

    if (is_png && !png_dimensions_ok(path, max_size)) return false;

    /* Read entire file into memory to avoid SD card contention with audio */
    size_t buf_size;
    uint8_t *buf = slurp_file(path, &buf_size);
    if (!buf) return false;

    if (is_jpeg) {
        cover_state = COVER_LOADING_JPEG;
        jpeg_err_t err = jpeg_decoder_start_mem(buf, buf_size, max_size, max_size,
                                               cover_art_cb, NULL);
        if (err != JPEG_OK) {
            cover_state = COVER_IDLE;
            return false;
        }
    } else {
        cover_state = COVER_LOADING_PNG;
        png_err_t err = png_decoder_start_mem(buf, buf_size, max_size, max_size,
                                              cover_art_png_cb, NULL);
        if (err != PNG_OK) {
            cover_state = COVER_IDLE;
            free(buf);
            return false;
        }
    }
    return true;
}

/** Continue scanning the directory for the next image file. */
static void try_next_cover_source (void) {
    if (!cover_dir_scan_active || !cover_dir) return;

    int max_size = cover_art_budget_size();
    int attempts = 0;

    while (attempts < COVER_SCAN_MAX_ATTEMPTS) {
        if (cover_dir_entry.d_type == DT_REG &&
            file_has_extensions(cover_dir_entry.d_name, cover_image_extensions)) {
            path_t *candidate = path_clone_push(cover_dir, cover_dir_entry.d_name);

            if (dir_findnext(path_get(cover_dir), &cover_dir_entry) != 0) {
                cover_dir_scan_active = false;
            }

            if (try_cover_path(path_get(candidate), max_size)) {
                path_free(candidate);
                return;
            }
            path_free(candidate);
            attempts++;
        } else {
            if (dir_findnext(path_get(cover_dir), &cover_dir_entry) != 0) {
                cover_dir_scan_active = false;
                if (!cover_image) cover_art_expected = false;
                return;
            }
        }
    }

    cover_dir_scan_active = false;
    if (!cover_image) cover_art_expected = false;
}

/** Scan the directory for a cover image, preferring known filenames. */
static void scan_directory_for_cover (path_t *dir) {
    int max_size = cover_art_budget_size();

    /* First pass: look for preferred filenames (cover.jpg, folder.png, etc.) */
    for (const char **name = preferred_cover_names; *name; name++) {
        for (const char **ext = cover_image_extensions; *ext; ext++) {
            char filename[64];
            snprintf(filename, sizeof(filename), "%s.%s", *name, *ext);
            path_t *candidate = path_clone_push(dir, filename);

            struct stat st;
            if (stat(path_get(candidate), &st) == 0) {
                if (try_cover_path(path_get(candidate), max_size)) {
                    path_free(candidate);
                    return;
                }
            }
            path_free(candidate);
        }
    }

    /* Second pass: fall back to first image file found */
    if (cover_dir) { path_free(cover_dir); cover_dir = NULL; }
    cover_dir = path_clone(dir);
    if (dir_findfirst(path_get(cover_dir), &cover_dir_entry) == 0) {
        cover_dir_scan_active = true;
        try_next_cover_source();
    }
}

/** Find the path that cover art would come from (no decoding).
 *  Returns the path string in out_path, or empty string if none found. */
static void find_cover_art_source (path_t *directory, char *out_path, size_t out_size) {
    out_path[0] = '\0';

    const id3_metadata_t *meta = audioplayer_get_metadata();

    if (meta->has_cover_art) {
        if (meta->cover_art_data) {
            /* Hash the image data for the cache key. The buffer already
             * passed the memory check so it's reasonably sized, and this
             * only runs once per track change. */
            uint32_t hash = 5381;
            for (size_t i = 0; i < meta->cover_art_size; i++) {
                hash = ((hash << 5) + hash) ^ meta->cover_art_data[i];
            }
            snprintf(out_path, out_size, "embedded:%08lx",
                     (unsigned long)hash);
        } else {
            /* Buffer was already taken by a previous decode — reuse the
             * cached source key so we don't trigger a redundant reload. */
            strncpy(out_path, cover_art_source, out_size - 1);
            out_path[out_size - 1] = '\0';
        }
        return;
    }

    /* Check preferred filenames */
    for (const char **name = preferred_cover_names; *name; name++) {
        for (const char **ext = cover_image_extensions; *ext; ext++) {
            char filename[64];
            snprintf(filename, sizeof(filename), "%s.%s", *name, *ext);
            path_t *candidate = path_clone_push(directory, filename);
            struct stat st;
            if (stat(path_get(candidate), &st) == 0) {
                strncpy(out_path, path_get(candidate), out_size - 1);
                out_path[out_size - 1] = '\0';
                path_free(candidate);
                return;
            }
            path_free(candidate);
        }
    }

    /* Quick scan for any image file */
    dir_t dir_entry;
    if (dir_findfirst(path_get(directory), &dir_entry) == 0) {
        do {
            if (dir_entry.d_type == DT_REG &&
                file_has_extensions(dir_entry.d_name, cover_image_extensions)) {
                path_t *candidate = path_clone_push(directory, dir_entry.d_name);
                strncpy(out_path, path_get(candidate), out_size - 1);
                out_path[out_size - 1] = '\0';
                path_free(candidate);
                return;
            }
        } while (dir_findnext(path_get(directory), &dir_entry) == 0);
    }
}

/** Start the cover art loading process for the current track. */
static void load_cover_art (path_t *directory) {
    /* Find what art source this track would use */
    char new_source[256];
    find_cover_art_source(directory, new_source, sizeof(new_source));

    /* For embedded art, find_cover_art_source already set new_source
     * to "embedded:<size>" — same album tracks typically have identical
     * embedded art (same size), different albums trigger a reload. */

    /* If same source as current and we already have an image, skip reload */
    if (cover_image && new_source[0] && strcmp(new_source, cover_art_source) == 0) {
        return;
    }

    /* Abort any in-progress decode */
    abort_cover_decode();
    cover_dir_scan_active = false;

    cover_art_expected = (new_source[0] != '\0');

    /* On first load or no art: free old image. Otherwise keep it visible
     * while the new one decodes (avoids blank flash between tracks). */
    if (cover_first_load || !cover_art_expected) {
        if (cover_image) {
            surface_free(cover_image);
            free(cover_image);
            cover_image = NULL;
        }
        cover_disp_size = 0;
    }

    if (!cover_art_expected) {
        cover_art_source[0] = '\0';
        return;
    }

    /* Store the new source so we can compare on next track change */
    strncpy(cover_art_source, new_source, sizeof(cover_art_source) - 1);
    cover_art_source[sizeof(cover_art_source) - 1] = '\0';

    const id3_metadata_t *meta = audioplayer_get_metadata();

    /* Try embedded cover art first (decode directly from memory buffer).
     * The decoder takes ownership of the buffer and frees it when done. */
    if (meta->has_cover_art && meta->cover_art_data) {
        int max_size = cover_art_budget_size();
        size_t buf_size;
        uint8_t *buf = audioplayer_take_cover_art(&buf_size);

        if (buf) {
            bool started = false;
            if (meta->cover_art_is_png) {
                cover_state = COVER_LOADING_PNG;
                started = (png_decoder_start_mem(buf, buf_size, max_size, max_size,
                                                 cover_art_png_cb, NULL) == PNG_OK);
            } else {
                cover_state = COVER_LOADING_JPEG;
                started = (jpeg_decoder_start_mem(buf, buf_size, max_size, max_size,
                                                  cover_art_cb, NULL) == JPEG_OK);
            }

            if (started) return;
            cover_state = COVER_IDLE;
            if (meta->cover_art_is_png) free(buf);
        }
    }

    /* Fall back to directory scan */
    scan_directory_for_cover(directory);

    /* If nothing is decoding at this point, give up */
    if (cover_state == COVER_IDLE) {
        cover_art_expected = false;
    }
}

/** Escape '$' and '^' characters that rdpq_text interprets as control codes.
 *  Doubles them ('$' -> '$$', '^' -> '^^') so they render as literals. */
static void sanitize_rdpq_text (char *dst, const char *src, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 1; i++) {
        if ((src[i] == '$' || src[i] == '^') && j + 1 < dst_size - 1) {
            dst[j++] = src[i];
            dst[j++] = src[i];
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static char *convert_error_message (audioplayer_err_t err) {
    switch (err) {
        case AUDIOPLAYER_ERR_OUT_OF_MEM: return "Audio player failed due to insufficient memory";
        case AUDIOPLAYER_ERR_IO: return "I/O error during audio playback";
        case AUDIOPLAYER_ERR_NO_FILE: return "No audio file is loaded";
        case AUDIOPLAYER_ERR_INVALID_FILE: return "Invalid audio file";
        default: return "Unknown audio player error";
    }
}

static void format_time (char *buffer, size_t buf_size, float seconds) {
    int s = (int)seconds;
    if (s < 0) s = 0;
    if (s >= 3600) {
        snprintf(buffer, buf_size, "%02d:%02d:%02d", s / 3600, (s % 3600) / 60, s % 60);
    } else {
        snprintf(buffer, buf_size, "%02d:%02d", s / 60, s % 60);
    }
}


/* Build the shuffle list by collecting all music file indices, then Fisher-Yates shuffle. */
static void build_shuffle_list (menu_t *menu) {
    int count = menu->browser.entries;

    /* Count music files first */
    int n_music = 0;
    for (int i = 0; i < count; i++) {
        if (menu->browser.list[i].type == ENTRY_TYPE_MUSIC) {
            n_music++;
        }
    }

    free(shuffle_list);
    shuffle_list = NULL;

    shuffle_count = 0;
    shuffle_pos = 0;

    if (n_music == 0) return;

    shuffle_list = malloc(n_music * sizeof(int));
    if (!shuffle_list) return;

    /* Collect indices */
    for (int i = 0; i < count; i++) {
        if (menu->browser.list[i].type == ENTRY_TYPE_MUSIC) {
            shuffle_list[shuffle_count++] = i;
        }
    }

    /* Fisher-Yates shuffle, seeded from hardware entropy */
    srand(getentropy32());
    for (int i = shuffle_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = shuffle_list[i];
        shuffle_list[i] = shuffle_list[j];
        shuffle_list[j] = tmp;
    }
}

/** Try to load and play the track at the given browser index.
 *  Returns true on success. */
static bool try_play_index (menu_t *menu, int idx) {
    if (idx < 0 || idx >= menu->browser.entries) return false;

    entry_t *e = &menu->browser.list[idx];
    if (e->type != ENTRY_TYPE_MUSIC) return false;

    path_t *path = path_clone_push(menu->browser.directory, e->name);
    audioplayer_err_t err = audioplayer_load(path_get(path));
    path_free(path);
    if (err != AUDIOPLAYER_OK) return false;

    sound_init_audioplayer_playback();
    err = audioplayer_play();
    if (err != AUDIOPLAYER_OK) return false;

    menu->browser.selected = idx;
    menu->browser.entry = e;
    advance_failed = false;
    ticker_offset = 0.0f;

    load_cover_art(menu->browser.directory);
    return true;
}

/** Try to skip to the next (direction=1) or previous (direction=-1) music file.
 *  Behavior depends on the current playback mode. */
static bool try_skip_track (menu_t *menu, int direction) {
    int current = menu->browser.selected;
    int count = menu->browser.entries;

    if (playback_mode == PLAYBACK_REPEAT_ONE) {
        /* Reload and restart the current track */
        return try_play_index(menu, current);
    }

    if (playback_mode == PLAYBACK_SHUFFLE || playback_mode == PLAYBACK_PARTY) {
        if (!shuffle_list || shuffle_count == 0) return false;

        shuffle_pos += direction;

        /* Walked before the start */
        if (shuffle_pos < 0) {
            shuffle_pos = 0;
            return false;
        }

        /* Walked past the end */
        if (shuffle_pos >= shuffle_count) {
            if (playback_mode == PLAYBACK_PARTY) {
                build_shuffle_list(menu);
                shuffle_pos = 0;
            } else {
                shuffle_pos = shuffle_count - 1;
                return false;
            }
        }

        /* Try each remaining entry in the shuffle list, skip broken files */
        while (shuffle_pos < shuffle_count) {
            if (try_play_index(menu, shuffle_list[shuffle_pos])) return true;
            shuffle_pos++;
        }
        return false;
    }

    /* Normal and Loop: scan linearly */
    for (int idx = current + direction; idx >= 0 && idx < count; idx += direction) {
        if (try_play_index(menu, idx)) return true;
    }

    /* Loop mode wraps to the other end of the directory */
    if (playback_mode == PLAYBACK_LOOP) {
        int start = (direction > 0) ? 0 : count - 1;
        int stop = current;
        for (int idx = start; idx != stop; idx += direction) {
            if (try_play_index(menu, idx)) return true;
        }
    }

    return false;
}

static void process (menu_t *menu) {
    audioplayer_err_t err;

    /* Allow one seek per frame to prevent cascading I/O stalls */
    seek_busy = false;

    err = audioplayer_process();
    if (err != AUDIOPLAYER_OK) {
        menu_show_error(menu, convert_error_message(err));
        return;
    }

    /* Auto-advance to next track when current finishes */
    if (audioplayer_is_finished() && !advance_failed) {
        if (!try_skip_track(menu, 1)) {
            advance_failed = true;
        }
    }

    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    } else if (menu->actions.enter) {
        err = audioplayer_toggle();
        if (err != AUDIOPLAYER_OK) {
            menu_show_error(menu, convert_error_message(err));
        }
        sound_play_effect(SFX_ENTER);
    } else if (menu->actions.options) {
        /* R button: cycle playback mode */
        playback_mode = (playback_mode + 1) % PLAYBACK_MODE_COUNT;
        advance_failed = false;
        if (playback_mode == PLAYBACK_SHUFFLE || playback_mode == PLAYBACK_PARTY) {
            build_shuffle_list(menu);
            /* Anchor to the currently playing track so the next advance
             * moves past it rather than skipping shuffle_list[0]. */
            for (int i = 0; i < shuffle_count; i++) {
                if (shuffle_list[i] == menu->browser.selected) {
                    shuffle_pos = i;
                    break;
                }
            }
        }
    } else if (menu->actions.go_up || menu->actions.go_down) {
        try_skip_track(menu, menu->actions.go_up ? -1 : 1);
    } else if ((menu->actions.go_left || menu->actions.go_right) && !seek_busy) {
        int seconds = menu->actions.go_fast ? SEEK_SECONDS_FAST : SEEK_SECONDS;
        err = audioplayer_seek(menu->actions.go_left ? (-seconds) : seconds);
        if (err != AUDIOPLAYER_OK) {
            menu_show_error(menu, convert_error_message(err));
        }
        seek_busy = true;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    ui_components_layout_draw();

    ui_components_seekbar_draw(audioplayer_get_progress());

    const id3_metadata_t *meta = audioplayer_get_metadata();

    const char *display_title = (meta->has_metadata && meta->title[0])
        ? meta->title : menu->browser.entry->name;

    /* Center the title + ticker as a unit between top edge and art/content */
    int content_top_y = VISIBLE_AREA_Y0 + CONTENT_TOP_OFFSET;
    int bar_top_y = SEEKBAR_Y - BORDER_THICKNESS;
    int expected_art = cover_art_expected ? COVER_ART_MAX_SIZE : 0;
    if (cover_image && cover_disp_size > 0) expected_art = cover_disp_size;
    int art_top_approx = expected_art > 0
        ? content_top_y + ((bar_top_y - content_top_y - CONTENT_BOTTOM_PAD * 2 - expected_art) / 2)
        : content_top_y;

    int header_y = VISIBLE_AREA_Y0 + (art_top_approx - VISIBLE_AREA_Y0 - HEADER_UNIT_HEIGHT) / 2;

    int ticker_x = SEEKBAR_X + 8;
    int ticker_w = SEEKBAR_WIDTH - 16;
    int title_y = header_y + HEADER_BASELINE_OFFSET;
    int ticker_y = title_y + HEADER_LINE_SPACING;

    /* Song title (sanitized for rdpq control codes) */
    char safe_title[256];
    sanitize_rdpq_text(safe_title, display_title, sizeof(safe_title));
    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .style_id = STL_DEFAULT,
            .width = SEEKBAR_WIDTH,
            .align = ALIGN_CENTER,
        },
        FNT_DEFAULT,
        SEEKBAR_X, title_y,
        safe_title, strlen(safe_title)
    );

    char ticker_str[512] = "";
    {
        char *p = ticker_str;
        size_t remaining = sizeof(ticker_str);
        const char *sep = " \xC2\xB7 "; /* " · " UTF-8 middle dot */
        bool need_sep = false;
        if (meta->artist[0]) {
            int n = snprintf(p, remaining, "%s", meta->artist);
            if (n < 0 || (size_t)n >= remaining) { remaining = 0; } else { p += n; remaining -= n; }
            need_sep = true;
        }
        if (meta->album[0] && remaining > 0) {
            int n = snprintf(p, remaining, "%s%s", need_sep ? sep : "", meta->album);
            if (n < 0 || (size_t)n >= remaining) { remaining = 0; } else { p += n; remaining -= n; }
            need_sep = true;
        }
        if (meta->year[0] && remaining > 0) {
            int n = snprintf(p, remaining, "%s%s", need_sep ? sep : "", meta->year);
            if (n < 0 || (size_t)n >= remaining) { remaining = 0; } else { p += n; remaining -= n; }
        }
    }

    char safe_ticker[512];
    sanitize_rdpq_text(safe_ticker, ticker_str, sizeof(safe_ticker));
    size_t ticker_len = strlen(safe_ticker);

    if (ticker_len > 0) {
        /* Measure actual pixel width */
        rdpq_textmetrics_t metrics = rdpq_text_printn(
            &(rdpq_textparms_t) { .style_id = STL_DEFAULT },
            FNT_DEFAULT,
            -1000, -1000,
            safe_ticker, ticker_len
        );
        int full_width = (int)metrics.advance_x;

        if (full_width <= ticker_w) {
            /* Fits on screen, center it */
            rdpq_text_printn(
                &(rdpq_textparms_t) {
                    .style_id = STL_DEFAULT,
                    .width = ticker_w,
                    .align = ALIGN_CENTER,
                },
                FNT_DEFAULT,
                ticker_x, ticker_y,
                safe_ticker, ticker_len
            );
        } else {
            /* Too wide, scroll it */
            const char *wrap_pad = "     ";
            char scroll_str[576];
            snprintf(scroll_str, sizeof(scroll_str), "%s%s%s%s", safe_ticker, wrap_pad, safe_ticker, wrap_pad);
            size_t scroll_len = strlen(scroll_str);

            int cycle_width = full_width + (int)rdpq_text_printn(
                &(rdpq_textparms_t) { .style_id = STL_DEFAULT },
                FNT_DEFAULT, -1000, -1000,
                wrap_pad, strlen(wrap_pad)
            ).advance_x;

            ticker_offset += TICKER_SCROLL_SPEED;
            if ((int)ticker_offset >= cycle_width) {
                ticker_offset -= (float)cycle_width;
            }

            rdpq_set_scissor(SEEKBAR_X, ticker_y - 12, SEEKBAR_X + SEEKBAR_WIDTH, ticker_y + 4);

            rdpq_text_printn(
                &(rdpq_textparms_t) { .style_id = STL_DEFAULT },
                FNT_DEFAULT,
                ticker_x - (int)ticker_offset, ticker_y,
                scroll_str, scroll_len
            );

            rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        }
    }

    /* Hz / kbps / format line below the ticker, gray and centered */
    {
        char tech_str[64];
        int native_rate = audioplayer_get_native_samplerate();
        int playback_rate = audioplayer_get_samplerate();
        const char *ext = strrchr(menu->browser.entry->name, '.');
        const char *fmt = (ext && strcasecmp(ext, ".flac") == 0) ? "FLAC" : "MP3";
        if (native_rate > playback_rate) {
            snprintf(tech_str, sizeof(tech_str), "%dHz (%dHz) \xC2\xB7 %.0fkbps \xC2\xB7 %s",
                     playback_rate, native_rate,
                     (double)(audioplayer_get_bitrate() / 1000), fmt);
        } else {
            snprintf(tech_str, sizeof(tech_str), "%dHz \xC2\xB7 %.0fkbps \xC2\xB7 %s",
                     playback_rate,
                     (double)(audioplayer_get_bitrate() / 1000), fmt);
        }
        int tech_y = ticker_y + HEADER_LINE_SPACING;
        rdpq_text_printn(
            &(rdpq_textparms_t) {
                .style_id = STL_GRAY,
                .width = ticker_w,
                .align = ALIGN_CENTER,
            },
            FNT_DEFAULT,
            ticker_x, tech_y,
            tech_str, strlen(tech_str)
        );
    }

    /* Build the queue view using the pre-built music index map */
    int queue_count = 0;
    int queue_current = -1;
    int *queue_indices = NULL;

    if (playback_mode == PLAYBACK_SHUFFLE || playback_mode == PLAYBACK_PARTY) {
        queue_indices = shuffle_list;
        queue_count = shuffle_count;
        queue_current = shuffle_pos;
    } else {
        queue_indices = music_indices;
        queue_count = music_count;
        /* Find current track in the music index map */
        for (int i = 0; i < music_count; i++) {
            if (music_indices[i] == menu->browser.selected) {
                queue_current = i;
                break;
            }
        }
    }

    /* Compute the content area between ticker and seekbar */
    int content_top = VISIBLE_AREA_Y0 + CONTENT_TOP_OFFSET;
    int content_bottom = SEEKBAR_Y - BORDER_THICKNESS - CONTENT_BOTTOM_PAD;
    int content_h = content_bottom - content_top;

    /* Use expected size for layout even before image finishes decoding */
    int art_size = cover_art_expected ? COVER_ART_MAX_SIZE : 0;
    if (cover_image && cover_disp_size > 0) art_size = cover_disp_size;
    int max_queue_lines = content_h / QUEUE_LINE_HEIGHT;
    if (max_queue_lines < 1) max_queue_lines = 1;
    if (max_queue_lines > QUEUE_MAX_VISIBLE) max_queue_lines = QUEUE_MAX_VISIBLE;

    /* Determine visible queue window */
    int window_start = 0;
    int window_end = queue_count;

    if (queue_count > max_queue_lines && queue_current >= 0) {
        /* List doesn't fit, scroll with 2 previous tracks for context */
        int prev_context = (playback_mode == PLAYBACK_SHUFFLE || playback_mode == PLAYBACK_PARTY) ? 0 : 2;
        window_start = queue_current - prev_context;
        if (window_start < 0) window_start = 0;
        window_end = window_start + max_queue_lines;
        if (window_end > queue_count) {
            window_end = queue_count;
            window_start = queue_count - max_queue_lines;
            if (window_start < 0) window_start = 0;
        }
    }

    /* Calculate the queue text width */
    int queue_w = QUEUE_TEXT_WIDTH;

    /* Layout: art on left, queue on right, centered as a unit */
    int block_w;
    int art_x, art_y;
    int queue_x, queue_y;

    if (art_size > 0) {
        block_w = art_size + QUEUE_GAP + queue_w;
        int block_x = DISPLAY_CENTER_X - block_w / 2;
        art_x = block_x;
        art_y = content_top + (content_h - art_size) / 2;
        queue_x = block_x + art_size + QUEUE_GAP;
    } else {
        block_w = queue_w;
        queue_x = DISPLAY_CENTER_X - queue_w / 2;
    }

    int visible_queue_h = (window_end - window_start) * QUEUE_LINE_HEIGHT;

    if (art_size > 0) {
        if (visible_queue_h < art_size) {
            /* Fewer tracks than art height: center vertically beside art */
            queue_y = art_y + (art_size - visible_queue_h) / 2 + HEADER_BASELINE_OFFSET;
        } else {
            queue_y = art_y + HEADER_BASELINE_OFFSET;
        }
    } else {
        queue_y = content_top + (content_h - visible_queue_h) / 2;
    }

    /* Render cover art or loading placeholder */
    if (cover_image) {
        int crop = (cover_image->width < cover_image->height)
                   ? cover_image->width : cover_image->height;
        rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);
        rdpq_mode_blender(0);
        rdpq_tex_blit(cover_image, art_x, art_y, &(rdpq_blitparms_t){
            .s0 = cover_s0, .t0 = cover_t0,
            .width = crop, .height = crop,
            .scale_x = (float)cover_disp_size / crop,
            .scale_y = (float)cover_disp_size / crop,
            .filtering = true,
        });
        rdpq_mode_pop();
    } else if (cover_art_expected && art_size > 0 && cover_state != COVER_IDLE) {
        /* Show decode progress centered in art area */
        // float progress = (cover_state == COVER_LOADING_JPEG)
        //                ? jpeg_decoder_get_progress()
        //                : png_decoder_get_progress();
        float progress = (cover_state == COVER_LOADING_JPEG)
                       ? jpeg_decoder_get_progress()
                       : png_decoder_get_progress();
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", (int)(progress * 100));
        rdpq_text_printn(
            &(rdpq_textparms_t) {
                .style_id = STL_GRAY,
                .width = art_size,
                .align = ALIGN_CENTER,
            },
            FNT_DEFAULT,
            art_x, art_y + art_size / 2,
            buf, strlen(buf)
        );
    }

    /* Render queue list */
    if (queue_count > 0 && queue_indices) {
        for (int i = window_start; i < window_end; i++) {
            int bidx = queue_indices[i];

            /* Find the cache index for this browser index */
            int cache_idx = -1;
            for (int c = 0; c < queue_cache_count; c++) {
                if (queue_cache[c].browser_idx == bidx) {
                    cache_idx = c;
                    break;
                }
            }

            char name_buf[160];
            if (cache_idx >= 0) {
                queue_entry_name(menu, cache_idx, name_buf, sizeof(name_buf));
            } else {
                entry_t *e = &menu->browser.list[bidx];
                strncpy(name_buf, e->name, sizeof(name_buf) - 1);
                name_buf[sizeof(name_buf) - 1] = '\0';
                char *dot = strrchr(name_buf, '.');
                if (dot) *dot = '\0';
            }

            char safe_name[256];
            sanitize_rdpq_text(safe_name, name_buf, sizeof(safe_name));

            char line_buf[288];
            if (i == queue_current) {
                snprintf(line_buf, sizeof(line_buf), QUEUE_ARROW "%s", safe_name);
            } else {
                snprintf(line_buf, sizeof(line_buf), "  %s", safe_name);
            }

            int line_y = queue_y + (i - window_start) * QUEUE_LINE_HEIGHT;
            int style = (i == queue_current) ? STL_DEFAULT : STL_GRAY;

            rdpq_text_printn(
                &(rdpq_textparms_t) {
                    .style_id = style,
                    .width = queue_w,
                    .wrap = WRAP_ELLIPSES,
                },
                FNT_DEFAULT,
                queue_x, line_y,
                line_buf, strlen(line_buf)
            );
        }
    }

    /* Draw elapsed and duration times on the seekbar */
    char elapsed_str[16];
    char duration_str[16];
    float duration = audioplayer_get_duration();
    format_time(elapsed_str, sizeof(elapsed_str), duration * audioplayer_get_progress());
    format_time(duration_str, sizeof(duration_str), duration);

    int time_y = SEEKBAR_Y + 18;

    rdpq_text_printn(
        &(rdpq_textparms_t) { .style_id = STL_DEFAULT },
        FNT_DEFAULT,
        ticker_x, time_y,
        elapsed_str, strlen(elapsed_str)
    );

    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .style_id = STL_DEFAULT,
            .width = ticker_w,
            .align = ALIGN_RIGHT,
        },
        FNT_DEFAULT,
        ticker_x, time_y,
        duration_str, strlen(duration_str)
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "A: %s\n"
        "B: Exit\n",
        audioplayer_is_playing() ? "Pause" : audioplayer_is_finished() ? "Play again" : "Play"
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "◀ Rewind | Fast forward ▶\n"
        "▲ Prev track | Next track ▼\n"
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_RIGHT, VALIGN_TOP,
        "R: %s\n",
        playback_mode_name[playback_mode]
    );

    rdpq_detach_show();
}

static void deinit (void) {
    abort_cover_decode();
    if (cover_image) {
        surface_free(cover_image);
        free(cover_image);
        cover_image = NULL;
    }
    cover_disp_size = 0;
    cover_art_expected = false;
    cover_first_load = true;
    cover_art_source[0] = '\0';
    if (cover_dir) {
        path_free(cover_dir);
        cover_dir = NULL;
    }
    cover_dir_scan_active = false;

    free(shuffle_list); shuffle_list = NULL;
    shuffle_count = 0;

    free(music_indices); music_indices = NULL;
    free(queue_cache); queue_cache = NULL;
    music_count = 0;
    queue_cache_count = 0;

    // Close the current track's SD file handle before reconfiguring audio,
    // since sound_init_default() may reopen a custom BGM file from the SD
    // card and some filesystem drivers don't support two open SD handles.
    audioplayer_deinit();
    sound_init_default();
}


/** Draw the loading screen with progress bar. */
/* Progress weights per loading step. Caps at 95% so the bar
 * never sits at 100% waiting for async cover art decode. */
static const float loading_progress[] = {
    0.00f,  /* step 0: audioplayer_init */
    0.10f,  /* step 1: audioplayer_load */
    0.20f,  /* step 2: sound reconfigure */
    0.40f,  /* step 3: build music index */
    0.60f,  /* step 4: cover art load */
    0.80f,  /* step 5: cover art decode */
    0.95f,  /* step 6: play */
};

static void draw_loading_screen (surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();
    ui_components_layout_draw();

    int idx = loading_step;
    int max_idx = (int)(sizeof(loading_progress) / sizeof(loading_progress[0])) - 1;
    if (idx > max_idx) idx = max_idx;
    ui_components_loader_draw(loading_progress[idx], "Loading...");

    rdpq_detach_show();
}

/** Perform one loading step per frame. Returns true when fully loaded. */
static bool loading_tick (menu_t *menu) {
    switch (loading_step) {
        case 0: {
            audioplayer_err_t err = audioplayer_init();
            if (err != AUDIOPLAYER_OK) {
                menu_show_error(menu, convert_error_message(err));
                audioplayer_deinit();
                return false;
            }
            loading_step++;
            return false;
        }
        case 1: {
            path_t *path = path_clone_push(menu->browser.directory, menu->browser.entry->name);
            audioplayer_err_t err = audioplayer_load(path_get(path));
            path_free(path);
            if (err != AUDIOPLAYER_OK) {
                menu_show_error(menu, convert_error_message(err));
                audioplayer_deinit();
                return false;
            }
            loading_step++;
            return false;
        }
        case 2: {
            /* Reconfigure audio to the track's sample rate early, so the
             * hardware has settled well before playback starts. */
            sound_init_audioplayer_playback();
            loading_step++;
            return false;
        }
        case 3: {
            build_music_index_map(menu);
            loading_step++;
            return false;
        }
        case 4: {
            load_cover_art(menu->browser.directory);
            loading_step++;
            return false;
        }
        case 5: {
            /* Wait for cover art decode to finish before starting audio */
            bool art_done = (cover_image != NULL) || !cover_art_expected || (cover_state == COVER_IDLE);
            if (!art_done) return false;
            loading_step++;
            return false;
        }
        default: {
            audioplayer_mute(false);
            return true;
        }
    }
}

void view_music_player_init (menu_t *menu) {
    player_state = PLAYER_LOADING;
    loading_step = 0;
    playback_mode = PLAYBACK_NORMAL;
    advance_failed = false;

    /* Reset cover art state */
    abort_cover_decode();
    if (cover_image) {
        surface_free(cover_image);
        free(cover_image);
        cover_image = NULL;
    }
    cover_disp_size = 0;
    cover_art_expected = false;
    cover_first_load = true;
    cover_art_source[0] = '\0';
    cover_dir_scan_active = false;
}

void view_music_player_display (menu_t *menu, surface_t *display) {
    if (player_state == PLAYER_LOADING) {
        /* Poll cover art decoder if active */
        bool done = loading_tick(menu);
        draw_loading_screen(display);

        if (done) {
            audioplayer_err_t play_err = audioplayer_play();
            if (play_err != AUDIOPLAYER_OK) {
                menu_show_error(menu, convert_error_message(play_err));
            }
            player_state = PLAYER_READY;
            cover_first_load = false;
        }
        return;
    }

    process(menu);
    draw(menu, display);

    if (menu->next_mode != MENU_MODE_MUSIC_PLAYER) {
        deinit();
    }
}
