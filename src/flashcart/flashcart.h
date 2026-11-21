/**
 * @file flashcart.h
 * @brief Flashcart Subsystem
 * @ingroup flashcart 
 */

#ifndef FLASHCART_H__
#define FLASHCART_H__

#include <stdbool.h>
#include <stdint.h>

/** @brief Flashcart error enumeration */
typedef enum {
    FLASHCART_OK, /**< No error */
    FLASHCART_ERR_OUTDATED, /**< Outdated firmware error */
    FLASHCART_ERR_SD_CARD, /**< SD card error */
    FLASHCART_ERR_BBFS, /**< BBFS error */
    FLASHCART_ERR_ARGS, /**< Argument error */
    FLASHCART_ERR_LOAD, /**< Load error */
    FLASHCART_ERR_INT, /**< Internal error */
    FLASHCART_ERR_FUNCTION_NOT_SUPPORTED, /**< Function not supported error */
} flashcart_err_t;

/** @brief List of optional supported flashcart features */
typedef enum {
    FLASHCART_FEATURE_64DD, /**< 64DD support */
    FLASHCART_FEATURE_RTC, /**< Real-time clock support */
    FLASHCART_FEATURE_USB, /**< USB support */
    FLASHCART_FEATURE_AUTO_CIC, /**< Automatic CIC detection */
    FLASHCART_FEATURE_AUTO_REGION, /**< Automatic region detection */
    FLASHCART_FEATURE_DIAGNOSTIC_DATA, /**< Diagnostic data support */
    FLASHCART_FEATURE_BIOS_UPDATE_FROM_MENU, /**< BIOS update from menu support */
    FLASHCART_FEATURE_SAVE_WRITEBACK, /**< Save writeback support */
    FLASHCART_FEATURE_ROM_REBOOT_FAST /**< Fast ROM reboot support */
} flashcart_features_t;

/** @brief Flashcart save type enumeration */
typedef enum {
    FLASHCART_SAVE_TYPE_NONE, /**< No save type */
    FLASHCART_SAVE_TYPE_EEPROM_4KBIT, /**< EEPROM 4Kbit */
    FLASHCART_SAVE_TYPE_EEPROM_16KBIT, /**< EEPROM 16Kbit */
    FLASHCART_SAVE_TYPE_SRAM_256KBIT, /**< SRAM 256Kbit */
    FLASHCART_SAVE_TYPE_SRAM_BANKED, /**< SRAM Banked */
    FLASHCART_SAVE_TYPE_SRAM_1MBIT, /**< SRAM 1Mbit */
    FLASHCART_SAVE_TYPE_FLASHRAM_1MBIT, /**< FlashRAM 1Mbit */
    FLASHCART_SAVE_TYPE_FLASHRAM_PKST2, /**< FlashRAM PKST2 */
    __FLASHCART_SAVE_TYPE_END /**< End of save types */
} flashcart_save_type_t;

/** @brief Flashcart save type enumeration */
typedef enum {
    /** @brief The flashcart will reboot into the menu on soft reboot (using the RESET button) */
    FLASHCART_REBOOT_MODE_MENU,
    /** @brief The flashcart will reboot into the previous ROM on soft reboot (using the RESET button) */
    FLASHCART_REBOOT_MODE_ROM,
} flashcart_reboot_mode_t;

/** @brief TV type to configure on the flashcart for fast reboot */
typedef enum {
    FLASHCART_TV_TYPE_PAL = 0,         /**< PAL */
    FLASHCART_TV_TYPE_NTSC = 1,        /**< NTSC */
    FLASHCART_TV_TYPE_MPAL = 2,        /**< MPAL */
    FLASHCART_TV_TYPE_PASSTHROUGH = 3, /**< Use console's own TV type */
} flashcart_tv_type_t;

/** @brief Flashcart Disk Parameter Structure. */
typedef struct {
    bool development_drive; /**< Development drive flag */
    uint8_t disk_type; /**< Disk type */
    bool bad_system_area_lbas[24]; /**< Bad system area LBAs */
    uint8_t defect_tracks[16][12]; /**< Defect tracks */
} flashcart_disk_parameters_t;

/** @brief Flashcart Firmware version Structure. */
typedef struct {
    uint16_t major; /**< Major version */
    uint16_t minor; /**< Minor version */
    uint32_t revision; /**< Revision */
} flashcart_firmware_version_t;

/** @brief Flashcart progress callback type */
typedef void flashcart_progress_callback_t (float progress);

/** @brief Flashcart Structure */
typedef struct {
    /** @brief The flashcart initialization function */
    flashcart_err_t (*init) (void);
    /** @brief The flashcart de-initialization function */
    flashcart_err_t (*deinit) (void);
    /** @brief The flashcart feature function */
    bool (*has_feature) (flashcart_features_t feature);
    /** @brief The flashcart firmware version function */
    flashcart_firmware_version_t (*get_firmware_version) (void);
    /** @brief The flashcart ROM load function */
    flashcart_err_t (*load_rom) (char *rom_path, flashcart_progress_callback_t *progress);
    /** @brief The flashcart file load function */
    flashcart_err_t (*load_file) (char *file_path, uint32_t rom_offset, uint32_t file_offset);
    /** @brief The flashcart save file load function */
    flashcart_err_t (*load_save) (char *save_path);
    /** @brief The flashcart disk bios load function */
    flashcart_err_t (*load_64dd_ipl) (char *ipl_path, flashcart_progress_callback_t *progress);
    /** @brief The flashcart disk load function */
    flashcart_err_t (*load_64dd_disk) (char *disk_path, flashcart_disk_parameters_t *disk_parameters);
    /** @brief The flashcart disks load function */
    flashcart_err_t (*load_64dd_disks) (char *disk_path, flashcart_disk_parameters_t *disk_parameters, char **swap_disk_paths, int swap_disk_count);
    /** @brief Query hardware button state if supported by flashcart. */
    flashcart_err_t (*get_button_state) (bool *pressed);
    /** @brief Query realtime voltage/temperature diagnostic data if supported. */
    flashcart_err_t (*get_voltage_temperature) (uint16_t *voltage_mv, int16_t *temperature_deci_c);
    /** @brief The flashcart set save type function */
    flashcart_err_t (*set_save_type) (flashcart_save_type_t save_type);
    /** @brief The flashcart set save writeback function */
    flashcart_err_t (*set_save_writeback) (char *save_path);
    /** @brief The flashcart set boot mode function */
    flashcart_err_t (*set_next_boot_mode) (flashcart_reboot_mode_t boot_mode);
    /** @brief The flashcart set TV type function (optional, NULL if unsupported) */
    flashcart_err_t (*set_tv_type) (flashcart_tv_type_t tv_type);
} flashcart_t;

/**
 * @brief Convert a flashcart error code to a human-readable error message.
 * 
 * @param err The flashcart error code.
 * @return char* The human-readable error message.
 */
char *flashcart_convert_error_message (flashcart_err_t err);

/**
 * @brief Initialize the flashcart.
 * 
 * @param storage_prefix Pointer to the storage prefix.
 * @return flashcart_err_t Error code.
 */
flashcart_err_t flashcart_init (const char **storage_prefix);

/**
 * @brief Deinitialize the flashcart.
 * 
 * @return flashcart_err_t Error code.
 */
flashcart_err_t flashcart_deinit (void);

/**
 * @brief Check if the flashcart has a specific feature.
 * 
 * @param feature The flashcart feature to check.
 * @return bool True if the feature is supported, false otherwise.
 */
bool flashcart_has_feature (flashcart_features_t feature);

/**
 * @brief Get the flashcart firmware version.
 * 
 * @return flashcart_firmware_version_t The firmware version.
 */
flashcart_firmware_version_t flashcart_get_firmware_version (void);

/**
 * @brief Load a ROM onto the flashcart.
 * 
 * @param rom_path The path to the ROM file.
 * @param byte_swap Whether to byte swap the ROM.
 * @param progress Callback function for progress updates.
 * @return flashcart_err_t Error code.
 */
flashcart_err_t flashcart_load_rom (char *rom_path, bool byte_swap, flashcart_progress_callback_t *progress);

/**
 * @brief Load a file onto the flashcart.
 * 
 * @param file_path The path to the file.
 * @param rom_offset The ROM offset.
 * @param file_offset The file offset.
 * @return flashcart_err_t Error code.
 */
flashcart_err_t flashcart_load_file (char *file_path, uint32_t rom_offset, uint32_t file_offset);

/**
 * @brief Load a save file onto the flashcart.
 * 
 * @param save_path The path to the save file.
 * @param save_type The type of save.
 * @return flashcart_err_t Error code.
 */
flashcart_err_t flashcart_load_save (char *save_path, flashcart_save_type_t save_type);

/**
 * @brief Load the 64DD IPL (BIOS) onto the flashcart.
 * 
 * @param ipl_path The path to the IPL file.
 * @param progress Callback function for progress updates.
 * @return flashcart_err_t Error code.
 */
flashcart_err_t flashcart_load_64dd_ipl (char *ipl_path, flashcart_progress_callback_t *progress);

/**
 * @brief Load a 64DD disk onto the flashcart.
 * 
 * @param disk_path The path to the disk file.
 * @param disk_parameters Pointer to the disk parameters structure.
 * @return flashcart_err_t Error code.
 */
flashcart_err_t flashcart_load_64dd_disk (char *disk_path, flashcart_disk_parameters_t *disk_parameters);

/**
 * @brief Load 64DD disks into the flashcart.
 *
 * @param disk_path Path to the primary disk file.
 * @param disk_parameters Pointer to the disk parameters.
 * @param swap_disk_paths Array of paths to swap disk files.
 * @param swap_disk_count Number of swap disks.
 * @return flashcart_err_t Error code.
 */
flashcart_err_t flashcart_load_64dd_disks (char *disk_path, flashcart_disk_parameters_t *disk_parameters, char **swap_disk_paths, int swap_disk_count);

/**
 * @brief Check whether hardware button state can be queried on active flashcart.
 *
 * @return bool True if button-state query is supported.
 */
bool flashcart_has_button_state (void);

/**
 * @brief Get realtime hardware button state from active flashcart.
 *
 * @param pressed Output pointer receiving true when button is currently pressed.
 * @return flashcart_err_t Result code.
 */
flashcart_err_t flashcart_get_button_state (bool *pressed);

/**
 * @brief Check whether realtime voltage/temperature diagnostics are supported.
 *
 * @return bool True when voltage/temperature query is supported.
 */
bool flashcart_has_voltage_temperature (void);

/**
 * @brief Get realtime voltage and temperature diagnostics.
 *
 * @param voltage_mv Output voltage in millivolts.
 * @param temperature_deci_c Output temperature in deci-degrees Celsius.
 * @return flashcart_err_t Result code.
 */
flashcart_err_t flashcart_get_voltage_temperature (uint16_t *voltage_mv, int16_t *temperature_deci_c);

/**
* @brief Set the next boot mode for the flashcart.
* 
* @param boot_mode The boot mode to set.
* @return flashcart_err_t Error code.
*/
flashcart_err_t flashcart_set_next_boot_mode (flashcart_reboot_mode_t boot_mode);

/**
 * @brief Set the TV type for the next ROM boot.
 *
 * Only meaningful when fast reboot is enabled. Has no effect if the
 * flashcart does not support TV type configuration.
 *
 * @param tv_type The TV type to set.
 * @return flashcart_err_t Error code.
 */
flashcart_err_t flashcart_set_tv_type (flashcart_tv_type_t tv_type);

#endif /* FLASHCART_H__ */
