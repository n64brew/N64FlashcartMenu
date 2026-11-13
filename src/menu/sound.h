/**
 * @file sound.h
 * @brief Menu Sound
 * @ingroup menu
 */

#ifndef SOUND_H__
#define SOUND_H__

#include <stdbool.h>

#define SOUND_SFX_CHANNEL           (0) /**< First Channel of sound effects 0-7 (8 in total) */
#define SOUND_BGM_CHANNEL           (8) /**< First Channel for background music 8-9 (2 in total [stereo]) */
#define SOUND_MP3_PLAYER_CHANNEL    (10) /**< First Channel for MP3 player sound 10-15 (6 in total [surround sound possible]) */


/**
 * @brief Enumeration of available sound effects for menu interactions.
 * 
 * This enumeration defines the different sound effects that can be used
 * for menu interactions.
 */
typedef enum {
    SFX_CURSOR,  /**< Sound effect for cursor movement */
    SFX_ERROR,   /**< Sound effect for error */
    SFX_ENTER,   /**< Sound effect for entering a menu */
    SFX_EXIT,    /**< Sound effect for exiting a menu */
    SFX_SETTING, /**< Sound effect for changing a setting */
} sound_effect_t;

/**
 * @brief Initialize the default sound system.
 * 
 * This function initializes the default sound system, setting up
 * necessary resources and configurations.
 */
void sound_init_default(void);

/**
 * @brief Initialize the Audioplayer playback system.
 * 
 * This function initializes the audioplayer playback system, preparing it
 * for playing audio files.
 */
void sound_init_audioplayer_playback(void);

/**
 * @brief Initialize the sound effects system.
 * 
 * This function initializes the sound effects system, setting up
 * necessary resources and configurations for playing sound effects.
 */
void sound_init_sfx(void);

/**
 * @brief Enable or disable sound effects.
 * 
 * @param enable True to enable sound effects, false to disable.
 */
void sound_use_sfx(bool enable);

/**
 * @brief Configure the custom background music path.
 *
 * Call this once (e.g. during menu init) before sound_init_bgm(). If the
 * path doesn't exist, sound_init_bgm() falls back to the built-in track.
 *
 * @param custom_bgm_path Path to the custom bgm file, or NULL to clear it.
 */
void sound_set_bgm_path(const char *custom_bgm_path);

/**
 * @brief Initialize the background music system.
 * 
 * This function initializes the background music system, setting up
 * necessary resources and configurations for playing background music.
 * Opens the custom BGM configured via sound_set_bgm_path(), or the
 * built-in default track if none was configured or found.
 */
void sound_init_bgm(void);


/**
 * @brief Enable or disable background music.
 * 
 * @param enable True to enable background music, false to disable.
 */
void sound_use_bgm(bool enable);

/**
 * @brief Play a specified sound effect.
 * 
 * @param sfx The sound effect to play, as defined in sound_effect_t.
 */
void sound_play_effect(sound_effect_t sfx);

/**
 * @brief Deinitialize the sound system.
 * 
 * This function deinitializes the sound system, releasing any resources
 * that were allocated.
 */
void sound_deinit(void);

/**
 * @brief Poll the sound system.
 * 
 * This function polls the sound system, updating its state as necessary.
 */
void sound_poll(void);

#endif /* SOUND_H__ */
