/**
 * @file audio_player.c
 * @brief Audio player with MP3 and FLAC support
 * @ingroup ui_components
 *
 * Format is detected by file extension. MP3 uses minimp3,
 * FLAC uses dr_flac.
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <libdragon.h>

#include "audio_player.h"
#include "id3_parser.h"
#include "sound.h"
#include "utils/fs.h"
#include "utils/utils.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include <minimp3/minimp3_ex.h>
#include <minimp3/minimp3.h>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO  /* we use our own file I/O callbacks */
#include <dr_libs/dr_flac.h>

#define SEEK_PREDECODE_FRAMES   (5)
#define FLAC_READ_FRAMES       (1152)  /* samples per wave_read iteration */
#define FLAC_IO_BUFFER_SIZE     (8 * 1024)


typedef enum {
    AUDIO_FORMAT_MP3,
    AUDIO_FORMAT_FLAC,
} audio_format_t;

/** Wrapper for dr_flac I/O callbacks. */
typedef struct {
    FILE *f;
    id3_metadata_t *meta;
    int flags;  /* ID3_FLAG_* controls art extraction */
} flac_io_t;

/** @brief Per-track file and decoder state. */
typedef struct {
    bool loaded;
    audio_format_t format;

    /* Common state */
    FILE *f;
    size_t file_size;
    volatile long file_pos;  /* updated in wave_read, read from main thread */
    int channels;
    int samplerate;
    int seek_predecode_frames;
    float duration;
    float bitrate;
    id3_metadata_t metadata;

    /* MP3-specific */
    size_t data_start;
    uint8_t buffer[16 * 1024];
    uint8_t *buffer_ptr;
    size_t buffer_left;
    mp3dec_t dec;
    mp3dec_frame_info_t info;

    /* FLAC-specific */
    flac_io_t *flac_io;  /* heap-allocated; drflac stores this pointer internally */
    drflac *flac;
    drflac_uint64 total_pcm_frames;
    volatile drflac_uint64 current_pcm_frame;
    int downsample;    /* 1 = native, 2 = halve, 4 = quarter, etc. */
    int native_rate;   /* original sample rate before downsampling */
    char *filebuf;     /* stdio buffer for FLAC, reduces SD card round-trips */
} audio_track_t;

/** @brief Player state. */
typedef struct {
    audio_track_t current;
    waveform_t wave;
} mp3player_t;

static mp3player_t *p = NULL;


/* --- dr_flac I/O and metadata callbacks --- */

static size_t drflac_read_cb (void *userdata, void *buf, size_t bytes) {
    flac_io_t *io = (flac_io_t *)userdata;
    return fread(buf, 1, bytes, io->f);
}

static drflac_bool32 drflac_seek_cb (void *userdata, int offset, drflac_seek_origin origin) {
    flac_io_t *io = (flac_io_t *)userdata;
    int whence;
    switch (origin) {
        case DRFLAC_SEEK_SET: whence = SEEK_SET; break;
        case DRFLAC_SEEK_END: whence = SEEK_END; break;
        default:              whence = SEEK_CUR; break;
    }
    return fseek(io->f, offset, whence) == 0;
}

static drflac_bool32 drflac_tell_cb (void *userdata, drflac_int64 *pCursor) {
    flac_io_t *io = (flac_io_t *)userdata;
    long pos = ftell(io->f);
    if (pos < 0) return DRFLAC_FALSE;
    *pCursor = (drflac_int64)pos;
    return DRFLAC_TRUE;
}


/* --- FLAC Vorbis comment metadata parsing --- */

static void flac_parse_vorbis_comment (const char *comment, id3_metadata_t *meta) {
    if (strncasecmp(comment, "TITLE=", 6) == 0) {
        strncpy(meta->title, comment + 6, ID3_FIELD_MAX - 1);
        meta->title[ID3_FIELD_MAX - 1] = '\0';
    } else if (strncasecmp(comment, "ARTIST=", 7) == 0) {
        strncpy(meta->artist, comment + 7, ID3_FIELD_MAX - 1);
        meta->artist[ID3_FIELD_MAX - 1] = '\0';
    } else if (strncasecmp(comment, "ALBUM=", 6) == 0) {
        strncpy(meta->album, comment + 6, ID3_FIELD_MAX - 1);
        meta->album[ID3_FIELD_MAX - 1] = '\0';
    } else if (strncasecmp(comment, "DATE=", 5) == 0) {
        strncpy(meta->year, comment + 5, sizeof(meta->year) - 1);
        meta->year[4] = '\0';  /* just the year portion */
    } else if (strncasecmp(comment, "TRACKNUMBER=", 12) == 0) {
        meta->track_number = atoi(comment + 12);
    }
}

static void flac_metadata_callback (void *userdata, drflac_metadata *metadata) {
    flac_io_t *io = (flac_io_t *)userdata;
    id3_metadata_t *meta = io->meta;

    if (metadata->type == DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) {
        drflac_vorbis_comment_iterator iter;
        drflac_init_vorbis_comment_iterator(&iter,
            metadata->data.vorbis_comment.commentCount,
            metadata->data.vorbis_comment.pComments);

        drflac_uint32 len;
        const char *comment;
        while ((comment = drflac_next_vorbis_comment(&iter, &len)) != NULL) {
            /* Copy to a null-terminated buffer since comment may not be terminated */
            char buf[ID3_FIELD_MAX + 32];
            size_t copy_len = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
            memcpy(buf, comment, copy_len);
            buf[copy_len] = '\0';
            flac_parse_vorbis_comment(buf, meta);
        }
        meta->has_metadata = (meta->title[0] || meta->artist[0] || meta->album[0]);
    }

    /* Extract embedded cover art (PICTURE block, type 3 = front cover) */
    if (metadata->type == DRFLAC_METADATA_BLOCK_TYPE_PICTURE &&
        (io->flags & ID3_FLAG_EXTRACT_ART) && !meta->has_cover_art) {
        drflac_uint32 pic_type = metadata->data.picture.type;
        drflac_uint32 pic_size = metadata->data.picture.pictureDataSize;
        const void  *pic_data = metadata->data.picture.pPictureData;

        /* Accept front cover (3), or any type if no front cover found */
        if (pic_data && pic_size > 0 &&
            (pic_type == DRFLAC_PICTURE_TYPE_COVER_FRONT || pic_type == DRFLAC_PICTURE_TYPE_OTHER)) {

            bool is_png = metadata->data.picture.mime &&
                          strstr(metadata->data.picture.mime, "png");

            /* Only allocate if we have enough free memory (keep 20% headroom) */
            heap_stats_t heap;
            sys_get_heap_stats(&heap);
            size_t free_mem = (size_t)(heap.total - heap.used);
            if (pic_size > (size_t)(free_mem * 0.8f)) return;

            uint8_t *buf = malloc(pic_size);
            if (!buf) return;
            memcpy(buf, pic_data, pic_size);

            meta->has_cover_art = true;
            meta->cover_art_is_png = is_png;
            meta->cover_art_size = pic_size;
            meta->cover_art_data = buf;
        }
    }
}


/* --- MP3 track helpers --- */

static void mp3_reset_decoder (audio_track_t *t) {
    mp3dec_init(&t->dec);
    t->seek_predecode_frames = 0;
    t->buffer_ptr = t->buffer;
    t->buffer_left = 0;
}

static void mp3_fill_buffer (audio_track_t *t) {
    if (feof(t->f)) return;
    if (t->buffer_left >= ALIGN(MAX_FREE_FORMAT_FRAME_SIZE, FS_SECTOR_SIZE)) return;

    if ((t->buffer_ptr != t->buffer) && (t->buffer_left > 0)) {
        memmove(t->buffer, t->buffer_ptr, t->buffer_left);
        t->buffer_ptr = t->buffer;
    }

    t->buffer_left += fread(t->buffer + t->buffer_left, 1, sizeof(t->buffer) - t->buffer_left, t->f);
}

static void mp3_calculate_duration (audio_track_t *t, int samples) {
    uint32_t frames;
    int delay, padding;

    long data_size = (t->file_size - t->data_start);
    if (mp3dec_check_vbrtag((const uint8_t *)(t->buffer_ptr), t->info.frame_bytes, &frames, &delay, &padding) > 0) {
        if (t->info.hz > 0 && frames > 0 && samples > 0) {
            t->duration = (frames * samples) / (float)(t->info.hz);
            t->bitrate = (data_size * 8) / t->duration;
        }
    } else {
        t->bitrate = t->info.bitrate_kbps * 1000;
        if (t->bitrate > 0) {
            t->duration = data_size / (t->bitrate / 8);
        }
    }
}


/* --- Track load/unload --- */

static audio_format_t detect_format (const char *path) {
    const char *ext = strrchr(path, '.');
    if (ext) {
        ext++;
        if (strcasecmp(ext, "flac") == 0) return AUDIO_FORMAT_FLAC;
    }
    return AUDIO_FORMAT_MP3;
}

static void track_unload (audio_track_t *t) {
    if (t->loaded) {
        t->loaded = false;
        if (t->format == AUDIO_FORMAT_FLAC) {
            if (t->flac) {
                drflac_close(t->flac);
                t->flac = NULL;
            }
            free(t->flac_io);
            t->flac_io = NULL;
            /* With DR_FLAC_NO_STDIO, drflac_close only frees its context.
             * We close the FILE handle ourselves below. */
        }
        if (t->f) {
            fclose(t->f);
            t->f = NULL;
        }
    }
    free(t->filebuf);
    t->filebuf = NULL;
    free(t->metadata.cover_art_data);
    t->metadata.cover_art_data = NULL;
}

static audioplayer_err_t track_load_mp3 (audio_track_t *t, int id3_flags) {
    id3_parse(t->f, t->file_size, &t->metadata, id3_flags);

    mp3_reset_decoder(t);

    while (!(feof(t->f) && t->buffer_left == 0)) {
        mp3_fill_buffer(t);

        if (ferror(t->f)) {
            fclose(t->f);
            t->f = NULL;
            return AUDIOPLAYER_ERR_IO;
        }

        size_t id3v2_skip = mp3dec_skip_id3v2((const uint8_t *)(t->buffer_ptr), t->buffer_left);
        if (id3v2_skip > 0) {
            if (fseek(t->f, (-t->buffer_left) + id3v2_skip, SEEK_CUR)) {
                fclose(t->f);
                t->f = NULL;
                return AUDIOPLAYER_ERR_IO;
            }
            mp3_reset_decoder(t);
            continue;
        }

        int samples = mp3dec_decode_frame(&t->dec, t->buffer_ptr, t->buffer_left, NULL, &t->info);
        if (samples > 0) {
            t->loaded = true;
            t->file_pos = ftell(t->f);
            t->data_start = t->file_pos - t->buffer_left + t->info.frame_offset;

            t->buffer_ptr += t->info.frame_offset;
            t->buffer_left -= t->info.frame_offset;

            t->channels = t->info.channels;
            t->samplerate = t->info.hz;
            mp3_calculate_duration(t, samples);

            return AUDIOPLAYER_OK;
        }

        t->buffer_ptr += t->info.frame_bytes;
        t->buffer_left -= t->info.frame_bytes;
    }

    fclose(t->f);
    t->f = NULL;
    return AUDIOPLAYER_ERR_INVALID_FILE;
}

static audioplayer_err_t track_load_flac (audio_track_t *t, int id3_flags) {
    fseek(t->f, 0, SEEK_SET);

    /* Verify FLAC magic bytes */
    uint8_t magic[4];
    if (fread(magic, 1, 4, t->f) != 4 || memcmp(magic, "fLaC", 4) != 0) {
        fclose(t->f);
        t->f = NULL;
        return AUDIOPLAYER_ERR_INVALID_FILE;
    }
    fseek(t->f, 0, SEEK_SET);

    /* Heap-allocate the I/O wrapper because drflac stores
     * this pointer internally. */
    t->flac_io = malloc(sizeof(flac_io_t));
    if (!t->flac_io) {
        fclose(t->f);
        t->f = NULL;
        return AUDIOPLAYER_ERR_OUT_OF_MEM;
    }
    t->flac_io->f = t->f;
    t->flac_io->meta = &t->metadata;
    t->flac_io->flags = id3_flags;

    t->flac = drflac_open_with_metadata(drflac_read_cb, drflac_seek_cb,
                                         drflac_tell_cb, flac_metadata_callback,
                                         t->flac_io, NULL);
    if (!t->flac) {
        free(t->flac_io);
        t->flac_io = NULL;
        fclose(t->f);
        t->f = NULL;
        return AUDIOPLAYER_ERR_INVALID_FILE;
    }

    /* Reject unsupported channel counts */
    if (t->flac->channels > 2) {
        drflac_close(t->flac);
        t->flac = NULL;
        free(t->flac_io);
        t->flac_io = NULL;
        fclose(t->f);
        t->f = NULL;
        return AUDIOPLAYER_ERR_INVALID_FILE;
    }

    /* Downsample to 48kHz max. Higher rates demand more decoded samples
     * per mixer callback than the CPU can reliably produce. */
    t->native_rate = t->flac->sampleRate;
    t->downsample = 1;
    while (t->native_rate / t->downsample > 48000 && t->downsample < 8) {
        t->downsample *= 2;
    }

    t->loaded = true;
    t->channels = t->flac->channels;
    t->samplerate = t->native_rate / t->downsample;
    t->total_pcm_frames = t->flac->totalPCMFrameCount;
    t->current_pcm_frame = 0;

    if (t->native_rate > 0 && t->total_pcm_frames > 0) {
        t->duration = (float)t->total_pcm_frames / (float)t->native_rate;
        t->bitrate = (t->file_size * 8.0f) / t->duration;
    }

    t->metadata.has_metadata = (t->metadata.title[0] || t->metadata.artist[0] || t->metadata.album[0]);

    return AUDIOPLAYER_OK;
}

static audioplayer_err_t track_load (audio_track_t *t, char *path, int id3_flags) {
    track_unload(t);

    memset(t, 0, sizeof(*t));
    t->format = detect_format(path);

    if ((t->f = fopen(path, "rb")) == NULL) {
        return AUDIOPLAYER_ERR_IO;
    }

    /* FLAC benefits from buffered I/O since dr_flac does many small reads
     * during seek table scanning. MP3 manages its own read buffer. */
    if (t->format == AUDIO_FORMAT_FLAC) {
        t->filebuf = malloc(FLAC_IO_BUFFER_SIZE);
        if (t->filebuf) {
            setvbuf(t->f, t->filebuf, _IOFBF, FLAC_IO_BUFFER_SIZE);
        }
    } else {
        setbuf(t->f, NULL);
    }

    struct stat st;
    if (fstat(fileno(t->f), &st)) {
        fclose(t->f);
        t->f = NULL;
        return AUDIOPLAYER_ERR_IO;
    }
    t->file_size = st.st_size;

    if (t->format == AUDIO_FORMAT_FLAC) {
        return track_load_flac(t, id3_flags);
    } else {
        return track_load_mp3(t, id3_flags);
    }
}

static bool track_is_finished (audio_track_t *t) {
    if (!t->loaded) return false;
    if (t->format == AUDIO_FORMAT_FLAC) {
        return t->total_pcm_frames > 0
            && t->current_pcm_frame >= t->total_pcm_frames;
    }
    return feof(t->f) && (t->buffer_left == 0);
}


/* Fill the sample buffer with silence (used when track is unloaded or exhausted) */
static void write_silence (samplebuffer_t *sbuf, int wlen, int channels) {
    short *out = (short *)(samplebuffer_append(sbuf, wlen));
    memset(out, 0, wlen * sizeof(short) * channels);
}

/* --- Waveform read callback --- */

/* Decode into a cached stack buffer first, then memcpy to the uncached
 * samplebuffer. Writing directly to uncached RDRAM is significantly slower
 * on the VR4300 because each store bypasses the data cache. */
static void mp3player_wave_read (void *ctx, samplebuffer_t *sbuf, int wpos, int wlen, bool seeking) {
    audio_track_t *t = &p->current;

    if (!t->loaded) {
        write_silence(sbuf, wlen, p->wave.channels);
        return;
    }

    if (t->format == AUDIO_FORMAT_FLAC) {
        if (!t->flac) {
            write_silence(sbuf, wlen, p->wave.channels);
            return;
        }

        /* Fixed-size stack buffer. When downsampling, we reduce the output
         * frame count so the raw read (output * downsample) fits in the buffer. */
        int16_t pcm_buf[FLAC_READ_FRAMES * 2];  /* max 2 channels */
        int max_output = FLAC_READ_FRAMES / t->downsample;
        if (max_output < 1) max_output = 1;

        while (wlen > 0) {
            int to_read = (wlen < max_output) ? wlen : max_output;
            int raw_read = to_read * t->downsample;
            drflac_uint64 frames_read = drflac_read_pcm_frames_s16(t->flac, raw_read, pcm_buf);

            if (frames_read > 0) {
                int out_frames = frames_read / t->downsample;
                if (out_frames < 1) {
                    /* Not enough frames to produce output, skip */
                    t->current_pcm_frame += frames_read;
                    continue;
                }
                short *out = (short *)(samplebuffer_append(sbuf, out_frames));

                if (t->downsample == 1) {
                    memcpy(out, pcm_buf, out_frames * t->channels * sizeof(int16_t));
                } else {
                    /* Decimation: pick every Nth frame. Use pointer stride
                     * instead of multiply-per-sample to reduce ALU work. */
                    int stride = t->downsample * t->channels;
                    int16_t *src = pcm_buf;
                    if (t->channels == 2) {
                        for (int i = 0; i < out_frames; i++) {
                            out[0] = src[0];
                            out[1] = src[1];
                            out += 2;
                            src += stride;
                        }
                    } else {
                        for (int i = 0; i < out_frames; i++) {
                            *out++ = *src;
                            src += stride;
                        }
                    }
                }

                t->current_pcm_frame += frames_read;
                wlen -= out_frames;
            } else {
                /* FLAC track exhausted — fill remaining with silence. */
                write_silence(sbuf, wlen, t->channels);
                wlen = 0;
            }
        }
        return;
    }

    /* MP3 decode path */
    if (!t->f) {
        write_silence(sbuf, wlen, p->wave.channels);
        return;
    }

    int16_t pcm_buf[MINIMP3_MAX_SAMPLES_PER_FRAME];

    while (wlen > 0) {
        mp3_fill_buffer(t);
        t->file_pos = ftell(t->f);

        int samples = mp3dec_decode_frame(&t->dec, t->buffer_ptr, t->buffer_left, pcm_buf, &t->info);

        if (samples > 0) {
            int pcm_bytes = samples * t->info.channels * sizeof(int16_t);
            short *out = (short *)(samplebuffer_append(sbuf, samples));
            memcpy(out, pcm_buf, pcm_bytes);

            if (t->seek_predecode_frames > 0) {
                t->seek_predecode_frames -= 1;
                memset(out, 0, pcm_bytes);
            }

            wlen -= samples;
        }

        t->buffer_ptr += t->info.frame_bytes;
        t->buffer_left -= t->info.frame_bytes;

        if (t->info.frame_bytes == 0) {
            /* MP3 track exhausted — fill remaining with silence. */
            short *out = (short *)(samplebuffer_append(sbuf, wlen));
            memset(out, 0, wlen * sizeof(short) * t->channels);
            wlen = 0;
        }
    }
}


/* --- Public API --- */

void audioplayer_mixer_init (void) {
    mixer_ch_set_limits(SOUND_MP3_PLAYER_CHANNEL, 16, 48000, 0);
}

audioplayer_err_t audioplayer_init (void) {
    p = calloc(1, sizeof(mp3player_t));
    if (p == NULL) return AUDIOPLAYER_ERR_OUT_OF_MEM;

    p->wave = (waveform_t) {
        .name = "mp3player",
        .bits = 16,
        .channels = 2,
        .frequency = 44100,
        .len = WAVEFORM_MAX_LEN - 1,
        .loop_len = WAVEFORM_MAX_LEN - 1,
        .append_units = MINIMP3_MAX_SAMPLES_PER_FRAME / 2,
        .read = mp3player_wave_read,
        .ctx = p,
    };

    return AUDIOPLAYER_OK;
}

void audioplayer_deinit (void) {
    if (!p) return;
    audioplayer_unload();
    free(p);
    p = NULL;
}

audioplayer_err_t audioplayer_load (char *path) {
    if (!p) return AUDIOPLAYER_ERR_OUT_OF_MEM;
    if (p->current.loaded) {
        audioplayer_stop();
        track_unload(&p->current);
    }

    audioplayer_err_t err = track_load(&p->current, path, ID3_FLAG_EXTRACT_ART);
    if (err != AUDIOPLAYER_OK) return err;

    p->wave.channels = p->current.channels;
    p->wave.frequency = p->current.samplerate;

    return AUDIOPLAYER_OK;
}

void audioplayer_unload (void) {
    if (!p) return;
    audioplayer_stop();
    track_unload(&p->current);
}

audioplayer_err_t audioplayer_process (void) {
    if (!p || !p->current.loaded) return AUDIOPLAYER_OK;

    if (p->current.format == AUDIO_FORMAT_MP3 && p->current.f && ferror(p->current.f)) {
        audioplayer_unload();
        return AUDIOPLAYER_ERR_IO;
    }

    if (audioplayer_is_finished()) {
        audioplayer_stop();
    }

    return AUDIOPLAYER_OK;
}

bool audioplayer_is_playing (void) {
    return mixer_ch_playing(SOUND_MP3_PLAYER_CHANNEL);
}

bool audioplayer_is_finished (void) {
    if (!p || !p->current.loaded) return false;
    return track_is_finished(&p->current);
}

audioplayer_err_t audioplayer_play (void) {
    if (!p) return AUDIOPLAYER_ERR_OUT_OF_MEM;
    if (!p->current.loaded) return AUDIOPLAYER_ERR_NO_FILE;

    if (!audioplayer_is_playing()) {
        if (track_is_finished(&p->current)) {
            /* Restart from beginning */
            if (p->current.format == AUDIO_FORMAT_FLAC) {
                drflac_seek_to_pcm_frame(p->current.flac, 0);
                p->current.current_pcm_frame = 0;
            } else {
                if (fseek(p->current.f, p->current.data_start, SEEK_SET)) {
                    return AUDIOPLAYER_ERR_IO;
                }
                mp3_reset_decoder(&p->current);
            }
        }
        mixer_ch_play(SOUND_MP3_PLAYER_CHANNEL, &p->wave);
    }
    return AUDIOPLAYER_OK;
}

void audioplayer_stop (void) {
    if (audioplayer_is_playing()) {
        mixer_ch_stop(SOUND_MP3_PLAYER_CHANNEL);
    }
}

audioplayer_err_t audioplayer_toggle (void) {
    if (!p) return AUDIOPLAYER_ERR_OUT_OF_MEM;
    if (audioplayer_is_playing()) {
        audioplayer_stop();
    } else {
        return audioplayer_play();
    }
    return AUDIOPLAYER_OK;
}

void audioplayer_mute (bool mute) {
    float volume = mute ? 0.0f : 1.0f;
    mixer_ch_set_vol(SOUND_MP3_PLAYER_CHANNEL, volume, volume);
}

audioplayer_err_t audioplayer_seek (int seconds) {
    if (!p) return AUDIOPLAYER_ERR_OUT_OF_MEM;
    if (!p->current.loaded) return AUDIOPLAYER_ERR_NO_FILE;

    if (p->current.format == AUDIO_FORMAT_FLAC) {
        /* Stop the mixer during FLAC seek to prevent wave_read and seek
         * from hitting the SD card simultaneously (single bus, I/O collision). */
        bool was_playing = audioplayer_is_playing();
        if (was_playing) mixer_ch_stop(SOUND_MP3_PLAYER_CHANNEL);

        drflac_int64 frame_offset = (drflac_int64)seconds * p->current.native_rate;
        drflac_int64 target = (drflac_int64)p->current.current_pcm_frame + frame_offset;
        if (target < 0) target = 0;
        if (target > (drflac_int64)p->current.total_pcm_frames) {
            target = p->current.total_pcm_frames;
        }
        if (!drflac_seek_to_pcm_frame(p->current.flac, (drflac_uint64)target)) {
            if (was_playing) mixer_ch_play(SOUND_MP3_PLAYER_CHANNEL, &p->wave);
            return AUDIOPLAYER_ERR_IO;
        }
        p->current.current_pcm_frame = (drflac_uint64)target;
        if (was_playing) mixer_ch_play(SOUND_MP3_PLAYER_CHANNEL, &p->wave);
        return AUDIOPLAYER_OK;
    }

    /* MP3 seek — stop the mixer to prevent wave_read from using the
     * file handle and buffer state while we mutate them. */
    bool was_playing = audioplayer_is_playing();
    if (was_playing) mixer_ch_stop(SOUND_MP3_PLAYER_CHANNEL);

    long bytes_to_move = (long)((p->current.bitrate * seconds) / 8);
    if (bytes_to_move == 0) {
        if (was_playing) mixer_ch_play(SOUND_MP3_PLAYER_CHANNEL, &p->wave);
        return AUDIOPLAYER_OK;
    }

    long position = (ftell(p->current.f) - p->current.buffer_left + bytes_to_move);
    if (position < (long)(p->current.data_start)) {
        position = p->current.data_start;
    }
    if (position > (long)(p->current.file_size)) {
        position = p->current.file_size;
    }

    if (fseek(p->current.f, position, SEEK_SET)) {
        if (was_playing) mixer_ch_play(SOUND_MP3_PLAYER_CHANNEL, &p->wave);
        return AUDIOPLAYER_ERR_IO;
    }

    mp3_reset_decoder(&p->current);
    mp3_fill_buffer(&p->current);
    p->current.file_pos = ftell(p->current.f);

    if (ferror(p->current.f)) {
        if (was_playing) mixer_ch_play(SOUND_MP3_PLAYER_CHANNEL, &p->wave);
        return AUDIOPLAYER_ERR_IO;
    }

    p->current.seek_predecode_frames = (position == p->current.data_start) ? 0 : SEEK_PREDECODE_FRAMES;

    if (was_playing) mixer_ch_play(SOUND_MP3_PLAYER_CHANNEL, &p->wave);
    return AUDIOPLAYER_OK;
}

float audioplayer_get_duration (void) {
    if (!p || !p->current.loaded) return 0.0f;
    return p->current.duration;
}

float audioplayer_get_bitrate (void) {
    if (!p || !p->current.loaded) return 0.0f;
    return p->current.bitrate;
}

int audioplayer_get_samplerate (void) {
    if (!p || !p->current.loaded) return 0;
    return p->current.samplerate;
}

int audioplayer_get_native_samplerate (void) {
    if (!p || !p->current.loaded) return 0;
    if (p->current.format == AUDIO_FORMAT_FLAC && p->current.native_rate > 0) {
        return p->current.native_rate;
    }
    return p->current.samplerate;
}

float audioplayer_get_progress (void) {
    if (!p || !p->current.loaded) return 0.0f;

    float progress;

    if (p->current.format == AUDIO_FORMAT_FLAC) {
        if (p->current.total_pcm_frames == 0) return 0.0f;
        progress = (float)p->current.current_pcm_frame / (float)p->current.total_pcm_frames;
    } else {
        long data_size = p->current.file_size - p->current.data_start;
        if (data_size <= 0) return 0.0f;
        long data_consumed = p->current.file_pos - p->current.buffer_left;
        long data_position = (data_consumed > p->current.data_start) ? (data_consumed - p->current.data_start) : 0;
        progress = data_position / (float)(data_size);
    }

    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    return progress;
}

const id3_metadata_t *audioplayer_get_metadata (void) {
    static const id3_metadata_t empty = {0};
    if (!p || !p->current.loaded) return &empty;
    return &p->current.metadata;
}

uint8_t *audioplayer_take_cover_art (size_t *size_out) {
    if (!p || !p->current.loaded || !p->current.metadata.cover_art_data) {
        if (size_out) *size_out = 0;
        return NULL;
    }
    uint8_t *buf = p->current.metadata.cover_art_data;
    if (size_out) *size_out = p->current.metadata.cover_art_size;
    p->current.metadata.cover_art_data = NULL;
    return buf;
}
