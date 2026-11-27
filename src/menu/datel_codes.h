/**
 * @file datel_codes.h
 * @brief Datel Codes
 * @ingroup menu 
 */

#ifndef DATEL_CODES_H__
#define DATEL_CODES_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "path.h"

// The maximum number of cheat codes to support is dependent on the available space in the N64 RAM.
// However, for simplicity, we will hardcode it to 64 for now, as this number of codes should handle most games (keeping it low allows us to collect caveats).
// The Krikzz official menu only supports upto 34 codes, so this is already an improvement.
#define MAX_CHEAT_CODES (64) // Maximum number of cheat codes to support. This can be changed later if needed.
#define MAX_CHEAT_CODE_ARRAYLIST_SIZE (MAX_CHEAT_CODES * 2 + 2) // Maximum size of the cheat code list (address/value pairs + two trailing zeros)

/** @brief Cheat file code Structure. */
typedef struct {
    uint32_t address; /**< Cheat address */
    uint16_t value; /**< Cheat value */
    bool enabled; /**< Cheat enabled flag */
    char description[32]; /**< Cheat description */
} cheat_file_code_t;


/** @brief Cheat code loading enum */
typedef enum {
    CHEAT_FILE_LOAD_OK,
    CHEAT_FILE_LOAD_ERR_MEMORY_ALLOC_FAIL,
    CHEAT_FILE_LOAD_ERR_FILE_OPEN_FAIL,
    CHEAT_FILE_LOAD_ERR_FILE_STAT_FAIL,
    CHEAT_FILE_LOAD_ERR_FILE_EMPTY,
    CHEAT_FILE_LOAD_ERR_FILE_TOO_BIG,
    CHEAT_FILE_LOAD_ERR_FILE_CONTENTS_ALLOC_FAIL,
    CHEAT_FILE_LOAD_ERR_FILE_READ_FAIL,
    CHEAT_FILE_LOAD_ERR_FILE_CLOSE_FAIL,
    CHEAT_FILE_LOAD_ERR_UNKNOWN
} cheat_file_load_err_t;


/**
 * @brief Generate a cheats array containing enabled cheats as address/value pairs.
 *        The last two entries will always be zero.
 * 
 * @param cheats_in Input array of type cheat_file_code_t.
 * @param cheats_out Output array (must be at least (MAX_CHEAT_CODES * 2 + 2) in size).
 * @return Number of address/value pairs written (including the trailing zeros).
 */
size_t generate_enabled_cheats_array(cheat_file_code_t *cheats_in, uint32_t *cheats_out);

/** * @brief Get the cheat codes.
 * 
 * @return Pointer to the array of cheat codes.
 */
cheat_file_code_t *get_cheat_codes(void);

/** * @brief Set the cheat codes.
 * 
 * @param cheats Pointer to the array of cheat codes.
 */
void set_cheat_codes(cheat_file_code_t *cheats);

/** * @brief Load cheats from a file.
 * 
 * @param path Path to the file containing cheat codes.
 * @return true if the file was opened, read, and parsed successfully, false on I/O error.
 */
bool load_cheats_from_file(char *path);

/**
 * @brief Load the cheat codes for a ROM's cheat file into memory, if one exists.
 *
 * Looks for a sibling file next to the ROM with a ".datel" extension, falling back to
 * ".datel.txt" if that's not found. Clears the in-memory cheat list if neither exists,
 * so stale cheats from a previously viewed ROM are never carried over.
 *
 * @param rom_path Path to the ROM file.
 */
void load_cheats_for_rom_path(path_t *rom_path);

/**
 * @brief Check whether the in-memory cheat list is already current for this ROM: either
 * freshly loaded from its cheat file, or already synced by a prior
 * load_cheats_for_rom_path() call for this exact ROM (e.g. from visiting the Datel Code
 * Editor, which always loads fresh from file on entry).
 *
 * @param rom_path Path to the ROM file.
 * @return true if the in-memory cheat list is already current for this ROM.
 */
bool cheats_loaded_for_rom_path(path_t *rom_path);

/** * @brief Save cheats to a file.
 * 
 * @param path Path to the file where cheat codes should be saved.
 */
void save_cheats_to_file(char *path);

#endif /* DATEL_CODES_H__ */
