/**
 * @file rom_info.h
 * @brief N64 ROM Database.
 * @note Only works with N64 ROM's by checking the first 4096 bytes of the file.
 * @ingroup menu 
 */

#ifndef ROM_INFO_H__
#define ROM_INFO_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "path.h"

#define ROM_METADATA_MAX_SCREENSHOTS 8

/** @brief ROM error enumeration. */
typedef enum {
    ROM_OK,                /**< No error */
    ROM_ERR_LOAD_IO,       /**< Load I/O error */
    ROM_ERR_SAVE_IO,       /**< Save I/O error */
    ROM_ERR_NO_FILE,       /**< No file error */
} rom_err_t;

/** @brief ROM endian enumeration. */
typedef enum {
    ENDIANNESS_BIG,        /**< Big Endian */
    ENDIANNESS_LITTLE,     /**< Little Endian */
    ENDIANNESS_BYTE_SWAP,  /**< Byte Swapped Endian */
} rom_endianness_t;

/** @brief ROM media type enumeration. */
typedef enum {
    N64_CART = 'N',                /**< Stand alone Cartridge program */
    N64_DISK  = 'D',               /**< Stand alone Disk Drive program */
    N64_CART_EXPANDABLE = 'C',     /**< Cartridge program that could use an extra Disk Drive program */
    N64_DISK_EXPANDABLE = 'E',     /**< Disk Drive program that could use an extra Cartridge program */
    N64_ALECK64  = 'Z'             /**< Aleck64 program */
} rom_category_type_t;

/** @brief ROM market region & language type enumeration. */
typedef enum {
    MARKET_JAPANESE_MULTI = 'A',   /**< Japanese and "English" languages */
    MARKET_BRAZILIAN  = 'B',       /**< Brazilian (Portuguese) language */
    MARKET_CHINESE = 'C',          /**< Chinese language */
    MARKET_GERMAN = 'D',           /**< German language */
    MARKET_NORTH_AMERICA  = 'E',   /**< North American "English" language */
    MARKET_FRENCH = 'F',           /**< French language */
    MARKET_GATEWAY64_NTSC = 'G',   /**< NTSC Gateway 64 */
    MARKET_DUTCH = 'H',            /**< Dutch language */
    MARKET_ITALIAN = 'I',          /**< Italian language */
    MARKET_JAPANESE = 'J',         /**< Japanese language */
    MARKET_KOREAN = 'K',           /**< Korean language */
    MARKET_GATEWAY64_PAL = 'L',    /**< PAL Gateway 64 */
    MARKET_CANADIAN = 'N',         /**< Canada region (English and French) language */
    MARKET_EUROPEAN_BASIC = 'P',   /**< European market and languages (must include English) */
    MARKET_SPANISH = 'S',          /**< Spanish language */
    MARKET_AUSTRALIAN = 'U',       /**< Australian (English) language */
    MARKET_SCANDINAVIAN = 'W',     /**< Scandinavian (Swedish, Norwegian, Finnish, etc.) languages */
    MARKET_OTHER_X = 'X',          /**< Undefined region and TBD language(s) */
    MARKET_OTHER_Y = 'Y',          /**< European region and language(s) */
    MARKET_OTHER_Z = 'Z'           /**< Undefined region and TBD language(s) */
} rom_destination_type_t;

/** @brief ROM CIC type enumeration. */
typedef enum {
    ROM_CIC_TYPE_UNKNOWN = 0,       /**< No known CIC type detected */
    ROM_CIC_TYPE_5101 = 5101,       /**< Aleck64 CIC-5101 */
    ROM_CIC_TYPE_5167 = 5167,       /**< 64DD ROM conversion CIC-5167 */
    ROM_CIC_TYPE_6101 = 6101,       /**< NTSC CIC-6101 */
    ROM_CIC_TYPE_7102 = 7102,       /**< PAL CIC-7102 */
    ROM_CIC_TYPE_x102 = 6102,       /**< NTSC CIC-6102 / PAL CIC-7101 */
    ROM_CIC_TYPE_x103 = 6103,       /**< NTSC CIC-6103 / PAL CIC-7103 */
    ROM_CIC_TYPE_x105 = 6105,       /**< NTSC CIC-6105 / PAL CIC-7105 */
    ROM_CIC_TYPE_x106 = 6106,       /**< NTSC CIC-6106 / PAL CIC-7106 */
    ROM_CIC_TYPE_8301 = 8301,       /**< NDDJ0 64DD IPL */
    ROM_CIC_TYPE_8302 = 8302,       /**< NDDJ1 64DD IPL */
    ROM_CIC_TYPE_8303 = 8303,       /**< NDDJ2 64DD IPL */
    ROM_CIC_TYPE_8401 = 8401,       /**< NDXJ0 64DD IPL */
    ROM_CIC_TYPE_8501 = 8501,       /**< NDDE0 64DD IPL */
    ROM_CIC_TYPE_AUTOMATIC = -1,    /**< Guess CIC from IPL3 */
} rom_cic_type_t;

/** @brief ROM save type enumeration. */
typedef enum {
    SAVE_TYPE_NONE = 0,             /**< No expected save type */
    SAVE_TYPE_EEPROM_4KBIT = 1,     /**< EEPROM 4Kbit */
    SAVE_TYPE_EEPROM_16KBIT = 2,    /**< EEPROM 16Kbit */
    SAVE_TYPE_SRAM_256KBIT = 3,     /**< SRAM 256Kbit */
    SAVE_TYPE_SRAM_BANKED = 4,      /**< SRAM Banked */
    SAVE_TYPE_SRAM_1MBIT = 5,       /**< SRAM 1Mbit */
    SAVE_TYPE_FLASHRAM_1MBIT = 6,   /**< FlashRAM 1Mbit */
    SAVE_TYPE_FLASHRAM_PKST2 = 7,   /**< FlashRAM PKST2 */
    SAVE_TYPE_AUTOMATIC = -1,       /**< Automatic save type detection */
} rom_save_type_t;

/** @brief ROM TV type enumeration. */
typedef enum {
    ROM_TV_TYPE_PAL = 0,            /**< PAL TV type */
    ROM_TV_TYPE_NTSC = 1,           /**< NTSC TV type */
    ROM_TV_TYPE_MPAL = 2,           /**< MPAL TV type */
    ROM_TV_TYPE_UNKNOWN = 3,        /**< Unknown TV type */
    ROM_TV_TYPE_AUTOMATIC = -1,     /**< Automatic TV type detection */
} rom_tv_type_t;

/** @brief ROM memory requirements enumeration. */
typedef enum {
    EXPANSION_PAK_UNSPECIFIED,      /**< Unspecified use of 4-8MB of memory */
    EXPANSION_PAK_UNUSED,           /**< Only uses 4MB of memory */
    EXPANSION_PAK_RECOMMENDED,      /**< Recommends 8MB of memory */
    EXPANSION_PAK_REQUIRED,         /**< Requires 8MB of memory */
    EXPANSION_PAK_FAULTY,           /**< Faulty with 8MB of memory */
} rom_expansion_pak_t;

/** @brief ROM Information Structure. */
typedef struct {
    rom_endianness_t endianness;    /**< The file endian */
    float clock_rate;               /**< The clock rate defined in the ROM's header */
    int32_t boot_address;          /**< The boot address defined in the ROM's header */

    struct {
        uint8_t version;            /**< The SDK version defined in the ROM's header */
        char revision;              /**< The SDK revision defined in the ROM's header */
    } libultra;

    int64_t check_code;            /**< The check code defined in the ROM's header */
    char title[20];                 /**< The title defined in the ROM's header */

    union {
        char game_code[4];          /**< The game code defined in the ROM's header */
        struct {
            rom_category_type_t category_code : 8; /**< The game media type */
            char unique_code[2];                   /**< The game unique identifier */
            rom_destination_type_t destination_code : 8; /**< The game region and or market */
        };
    };

    uint8_t version;                /**< The ROM version defined in the ROM's header */
    rom_cic_type_t cic_type;        /**< The CIC type required by the ROM */
    rom_save_type_t save_type;      /**< The save type required by the ROM */
    rom_tv_type_t tv_type;          /**< The TV type required by the ROM */

    struct {
        bool cic;                   /**< Override CIC type */
        rom_cic_type_t cic_type;    /**< CIC type */
        bool save;                  /**< Override save type */
        rom_save_type_t save_type;  /**< Save type */
        bool tv;                    /**< Override TV type */
        rom_tv_type_t tv_type;      /**< TV type */
    } boot_override;                /**< Overrides the auto-detected CIC/save/TV types during ROM boot */

    struct {
        bool controller_pak;        /**< Supports Controller Pak */
        bool rumble_pak;            /**< Supports Rumble Pak */
        bool transfer_pak;          /**< Supports Transfer Pak */
        bool voice_recognition_unit; /**< Supports Voice Recognition Unit */
        bool real_time_clock;       /**< Supports Real Time Clock */
        bool disk_conversion;       /**< Supports Disk Conversion */
        bool combo_rom_disk_game;   /**< Supports Combo ROM/Disk Game */
        rom_expansion_pak_t expansion_pak; /**< Expansion Pak requirements */
    } features;                     /**< The supported ROM accessories */

    struct {
        bool cheats_enabled;        /**< Cheats enabled */
        bool patches_enabled;       /**< Patches enabled */
        bool clear_rdram_enabled;   /**< Zero RDRAM before boot (workaround for ROMs with incomplete BSS init) */
    } settings;                     /**< The ROM settings */

    struct {
        char *name;                 /**< The game release name */
        char *author;               /**< The game author or developer */
        char *release_date;         /**< The game release date */
        char *osi_license;          /**< The game OSI license type */
        char *website;              /**< The game official website URL */
        uint32_t age_rating;        /**< The minimum game age rating */
        uint32_t num_players;       /**< The number of players supported */
        char *short_description;    /**< The short game description */
        char *long_description_fn;  /**< The long game description file name */
        char *metadata_zip_path;    /**< ZIP containing metadata images, if any */
        char *boxart[6];            /**< Spec-defined boxart filenames: front, back, top, bottom, left, right */
        char *cartart[2];           /**< Spec-defined boxart filenames: front, back */
        char *screenshots[ROM_METADATA_MAX_SCREENSHOTS]; /**< Spec-defined screenshot filenames */
        uint8_t screenshot_count;   /**< Number of screenshot filenames */
        bool size_limit_exceeded;   /**< Metadata was skipped because metadata.ini exceeded size cap */
    } meta;                         /**< The ROM metadata */
} rom_info_t;

/** Extract a metadata image from the associated ZIP into a heap buffer. */
bool rom_info_extract_metadata_image(const rom_info_t *rom_info, const char *filename,
                                     uint8_t **data, size_t *size);

/**
 * @brief Get the CIC seed for the ROM.
 * 
 * @param rom_info Pointer to the ROM information structure
 * @param seed Pointer to the seed value
 * @return true if successful, false otherwise
 */
bool rom_info_get_cic_seed(rom_info_t *rom_info, uint8_t *seed);

/**
 * @brief Load ROM information from a file.
 * 
 * @param path Pointer to the path structure
 * @param rom_info Pointer to the ROM information structure
 * @return rom_err_t Error code
 */
rom_err_t rom_config_load(path_t *path, rom_info_t *rom_info);

/**
 * @brief Get the CIC type for the ROM.
 * 
 * @param rom_info Pointer to the ROM information structure
 * @return rom_cic_type_t CIC type
 */
rom_cic_type_t rom_info_get_cic_type(rom_info_t *rom_info);

/**
 * @brief Override the CIC type for the ROM.
 * 
 * @param path Pointer to the path structure
 * @param rom_info Pointer to the ROM information structure
 * @param cic_type CIC type to override
 * @return rom_err_t Error code
 */
rom_err_t rom_config_override_cic_type(path_t *path, rom_info_t *rom_info, rom_cic_type_t cic_type);

/**
 * @brief Get the save type for the ROM.
 * 
 * @param rom_info Pointer to the ROM information structure
 * @return rom_save_type_t Save type
 */
rom_save_type_t rom_info_get_save_type(rom_info_t *rom_info);

/**
 * @brief Override the save type for the ROM.
 * 
 * @param path Pointer to the path structure
 * @param rom_info Pointer to the ROM information structure
 * @param save_type Save type to override
 * @return rom_err_t Error code
 */
rom_err_t rom_config_override_save_type(path_t *path, rom_info_t *rom_info, rom_save_type_t save_type);

/**
 * @brief Get the TV type for the ROM.
 * 
 * @param rom_info Pointer to the ROM information structure
 * @return rom_tv_type_t TV type
 */
rom_tv_type_t rom_info_get_tv_type(rom_info_t *rom_info);

/**
 * `@brief` Free dynamically allocated metadata fields in ROM information structure.
 * 
 * `@param` rom_info Pointer to the ROM information structure
 * `@note` Only frees the meta struct fields, not the rom_info_t itself
 */
void rom_info_free_meta(rom_info_t *rom_info);

/**
 * @brief Override the TV type for the ROM.
 * 
 * @param path Pointer to the path structure
 * @param rom_info Pointer to the ROM information structure
 * @param tv_type TV type to override
 * @return rom_err_t Error code
 */
rom_err_t rom_config_override_tv_type(path_t *path, rom_info_t *rom_info, rom_tv_type_t tv_type);

/**
 * @brief Set the cheats setting for the ROM.
 * 
 * @param path Pointer to the path structure
 * @param rom_info Pointer to the ROM information structure
 * @param enabled True to enable cheats, false to disable
 * @return rom_err_t Error code
 */
rom_err_t rom_config_setting_set_cheats (path_t *path, rom_info_t *rom_info, bool enabled);

/**
 * @brief Set whether RDRAM should be zeroed before booting this ROM.
 *
 * @param path Pointer to the path structure
 * @param rom_info Pointer to the ROM information structure
 * @param enabled True to clear RDRAM before boot, false to skip
 * @return rom_err_t Error code
 */
rom_err_t rom_config_setting_set_clear_rdram (path_t *path, rom_info_t *rom_info, bool enabled);

#ifdef FEATURE_PATCHER_GUI_ENABLED
/**
 * @brief Set the patcher setting for the ROM.
 * 
 * @param path Pointer to the path structure
 * @param rom_info Pointer to the ROM information structure
 * @param enabled True to enable cheats, false to disable
 * @return rom_err_t Error code
 */
rom_err_t rom_config_setting_set_patches (path_t *path, rom_info_t *rom_info, bool enabled);
#endif // FEATURE_PATCHER_GUI_ENABLED

#endif // ROM_INFO_H__
