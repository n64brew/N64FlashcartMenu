/**
 * @file sound.c
 * @brief Sound component implementation
 * @ingroup ui_components
 */

#include <stdbool.h>
#include <string.h>
#include <libdragon.h>
#include "utils/fs.h"
#include "audio_player.h"
#include "sound.h"

#define DEFAULT_FREQUENCY   (44100)
#define NUM_BUFFERS         (4)
#define NUM_CHANNELS        (16)

static wav64_t sfx_cursor, sfx_error, sfx_enter, sfx_exit, sfx_setting, bgm;

static bool sound_initialized = false;
static bool sfx_enabled = false;
static bool bgm_enabled = false;
static bool sfx_opened = false;
static bool bgm_opened = false;
static char bgm_path[256] = "";
static bool bgm_path_valid = false;

/**
 * @brief Reconfigure the sound system with the specified frequency.
 * 
 * @param frequency The audio frequency.
 */
static void sound_reconfigure (int frequency) {
    if ((frequency > 0) && (audio_get_frequency() != frequency)) {
        
        sound_deinit();

        audio_init(frequency, NUM_BUFFERS);
        mixer_init(NUM_CHANNELS);

        // Attempt to initialize wav64 compression level 1
        wav64_init_compression(1);

        // Ensure SFX channel can play standard 44.1 kHz effects even if the
        // global mixer/sample rate was reconfigured to a lower value for MP3.
        mixer_ch_set_limits(SOUND_SFX_CHANNEL, 16, DEFAULT_FREQUENCY, 0);

        // Initialize MP3/audioplayer mixer
        audioplayer_mixer_init();
        sound_initialized = true;

        if (sfx_enabled) {
            sound_init_sfx();
        }
        if (bgm_enabled) {
            sound_init_bgm();
            wav64_play(&bgm, SOUND_BGM_CHANNEL);
        }
    }
}

/**
 * @brief Initialize the default sound system.
 */
void sound_init_default (void) {
    sound_reconfigure(DEFAULT_FREQUENCY);
}

/**
 * @brief Initialize the sound system for MP3 playback.
 */
void sound_init_audioplayer_playback (void) {
    // Temporarily disable BGM so it won't be restarted during audio reconfiguration.
    // BGM will be re-enabled when sound_init_default() is called on exit.
    bool bgm_was_enabled = bgm_enabled;
    bgm_enabled = false;
    mixer_ch_stop(SOUND_BGM_CHANNEL);
    sound_reconfigure(audioplayer_get_samplerate());
    bgm_enabled = bgm_was_enabled;
}

/**
 * @brief Initialize the sound effects.
 */
void sound_init_sfx (void) {
    mixer_ch_set_vol(SOUND_SFX_CHANNEL, 0.5f, 0.5f);
    wav64_open(&sfx_cursor, "rom:/menu/sounds/cursorsound.wav64");
    wav64_open(&sfx_exit, "rom:/menu/sounds/back.wav64");
    wav64_open(&sfx_setting, "rom:/menu/sounds/settings.wav64");
    wav64_open(&sfx_enter, "rom:/menu/sounds/enter.wav64");
    wav64_open(&sfx_error, "rom:/menu/sounds/error.wav64");
    sfx_enabled = true;
    sfx_opened = true;
}

/**
 * @brief Configure the custom background music path.
 *
 * Resolves and caches whether a custom BGM file exists at the given path so
 * sound_init_bgm() can reuse it across audio reconfigurations without
 * re-checking the SD card each time. Call this once before sound_init_bgm().
 */
void sound_set_bgm_path (const char *custom_bgm_path) {
    bgm_path[0] = '\0';
    bgm_path_valid = false;

    if (custom_bgm_path != NULL
        && strlen(custom_bgm_path) < sizeof(bgm_path)
        && file_exists((char *) custom_bgm_path)) {
        strncpy(bgm_path, custom_bgm_path, sizeof(bgm_path) - 1);
        bgm_path[sizeof(bgm_path) - 1] = '\0';
        bgm_path_valid = true;
    }
}

/**
 * @brief Initialize the background music.
 *
 * Opens the custom BGM configured via sound_set_bgm_path(), or the built-in
 * default track if none was configured or found.
 */
void sound_init_bgm (void) {
    wav64_open(&bgm, bgm_path_valid ? bgm_path : "rom:/menu/sounds/bgm.wav64");
    wav64_set_loop(&bgm, true);
    mixer_ch_set_vol(SOUND_BGM_CHANNEL, 0.1f, 0.1f);
    bgm_opened = true;
}

/**
 * @brief Enable or disable sound effects.
 * 
 * @param state True to enable, false to disable.
 */
void sound_use_sfx(bool state) {
    sfx_enabled = state;
}

/**
 * @brief Enable or disable background music.
 * 
 * @param state True to enable, false to disable.
 */
void sound_use_bgm(bool state) {
    bgm_enabled = state;
    if (bgm_enabled) {
        wav64_play(&bgm, SOUND_BGM_CHANNEL);
    } else {
        mixer_ch_stop(SOUND_BGM_CHANNEL);
    }
}

/**
 * @brief Play a sound effect.
 * 
 * @param sfx The sound effect to play.
 */
void sound_play_effect(sound_effect_t sfx) {
    if(sfx_enabled) {
        switch (sfx) {
            case SFX_CURSOR:
                wav64_play(&sfx_cursor, SOUND_SFX_CHANNEL);
                if (joypad_get_rumble_supported(0)) {
                    joypad_set_rumble_active(0, true);
                    wait_ms(15);
                    joypad_set_rumble_active(0, false);
                }
                break;
            case SFX_EXIT:
                wav64_play(&sfx_exit, SOUND_SFX_CHANNEL);
                if (joypad_get_rumble_supported(0)) {
                    joypad_set_rumble_active(0, true);
                    wait_ms(40);
                    joypad_set_rumble_active(0, false);
                }
                break;
            case SFX_SETTING:
                wav64_play(&sfx_setting, SOUND_SFX_CHANNEL);
                if (joypad_get_rumble_supported(0)) {
                    joypad_set_rumble_active(0, true);
                    wait_ms(40);
                    joypad_set_rumble_active(0, false);
                }
                break;
            case SFX_ENTER:
                wav64_play(&sfx_enter, SOUND_SFX_CHANNEL);
                if (joypad_get_rumble_supported(0)) {
                    joypad_set_rumble_active(0, true);
                    wait_ms(40);
                    joypad_set_rumble_active(0, false);
                }
                break;
            case SFX_ERROR:
                wav64_play(&sfx_error, SOUND_SFX_CHANNEL);
                if (joypad_get_rumble_supported(0)) {
                    joypad_set_rumble_active(0, true);
                    wait_ms(50);
                    joypad_set_rumble_active(0, false);
                }
                break;
            default:
                if ( joypad_get_rumble_active(0)) {
                    joypad_set_rumble_active(0, false);
                }
                break;
        }
    }
}

/**
 * @brief Deinitialize the sound system.
 */
void sound_deinit (void) {
    if (sound_initialized) {
        if (sfx_opened) {
            wav64_close(&sfx_cursor);
            wav64_close(&sfx_exit);
            wav64_close(&sfx_setting);
            wav64_close(&sfx_enter);
            wav64_close(&sfx_error);
            sfx_opened = false;
        }
        if (bgm_opened) {
            wav64_close(&bgm);
            bgm_opened = false;
        }
        mixer_close();
        audio_close();
        sound_initialized = false;
    }
}

/**
 * @brief Poll the sound system to process audio playback.
 */
void sound_poll (void) {
    if (sound_initialized) {
        
        // Check whether one audio buffer is ready, otherwise wait for next
        // frame to perform mixing.
        mixer_try_play();
    }
}
