/**
 * @file cart_load.h
 * @brief ROM/save loading functions
 * @ingroup menu
 */

#ifndef CART_LOAD_H__
#define CART_LOAD_H__

#include "disk_info.h"
#include "flashcart/flashcart.h"
#include "menu_state.h"
#include "rom_info.h"

#ifndef SAVE_DIRECTORY_NAME
#define SAVE_DIRECTORY_NAME "saves"
#endif

/** @brief Cart load state enumeration. */
typedef enum {
    /** @brief Returned no error. */
    CART_LOAD_OK,
    /** @brief Failed to load the ROM correctly. */
    CART_LOAD_ERR_ROM_LOAD_FAIL,
    /** @brief Failed to load the save correctly. */
    CART_LOAD_ERR_SAVE_LOAD_FAIL,
    /** @brief Failed to set the next boot mode. */
    CART_LOAD_ERR_BOOT_MODE_FAIL,
    /** @brief The 64DD is available for use. */
    CART_LOAD_ERR_64DD_PRESENT,
    /** @brief Failed to find the 64DD IPL (BIOS) file. */
    CART_LOAD_ERR_64DD_IPL_NOT_FOUND,
    /** @brief Failed to load the 64DD IPL (BIOS) file. */
    CART_LOAD_ERR_64DD_IPL_LOAD_FAIL,
    /** @brief Failed to find the 64DD disk. */
    CART_LOAD_ERR_64DD_DISK_LOAD_FAIL,
    /** @brief Failed to find the emulator required. */
    CART_LOAD_ERR_EMU_NOT_FOUND,
    /** @brief Failed to load the emulator required. */
    CART_LOAD_ERR_EMU_LOAD_FAIL,
    /** @brief Failed to load the emulator ROM. */
    CART_LOAD_ERR_EMU_ROM_LOAD_FAIL,
    /** @brief Failed to create the save sub-directory. */
    CART_LOAD_ERR_CREATE_SAVES_SUBDIR_FAIL,
    /** @brief There was not enough system memory available (expected an Expansion PAK). */
    CART_LOAD_ERR_EXP_PAK_NOT_FOUND,
    /** @brief An unexpected response. */
    CART_LOAD_ERR_FUNCTION_NOT_SUPPORTED,
} cart_load_err_t;

/** @brief Cart load type enumeration */
typedef enum {
    /** @brief The ROM is designed for a Nintendo Entertainment System or Famicom. */
    CART_LOAD_EMU_TYPE_NES,
    /** @brief The ROM is designed for a Super Nintendo Entertainment System or Super Famicom. */
    CART_LOAD_EMU_TYPE_SNES,
    /** @brief The ROM is designed for a Nintendo Gameboy. */
    CART_LOAD_EMU_TYPE_GAMEBOY,
    /** @brief The ROM is designed for a Nintendo Gameboy Color. */
    CART_LOAD_EMU_TYPE_GAMEBOY_COLOR,
    /** @brief The ROM is designed for a Sega 8Bit system (Game Gear or Master System). */
    CART_LOAD_EMU_TYPE_SEGA_GENERIC_8BIT,
    /** @brief The ROM is designed for a Fairchild Channel F system. */
    CART_LOAD_EMU_TYPE_FAIRCHILD_CHANNELF,
    /** @brief The ROM is designed for a Sinclair ZX Spectrum system. */
    CART_LOAD_EMU_TYPE_SINCLAIR_ZXSPECTRUM,
    /** @brief The ROM is designed for a Microsoft MSX system. */
    CART_LOAD_EMU_TYPE_MICROSOFT_MSX,
    /** @brief The ROM is designed for a SNK Neo-Geo system. */
    CART_LOAD_EMU_TYPE_SNK_NEO_GEO,
    /** @brief The ROM is designed for a development emulator. */
    CART_LOAD_EMU_TYPE_DEV,
} cart_load_emu_type_t;

/**
 * @brief Check if the 64DD is connected.
 * 
 * @return true if the 64DD is connected, false otherwise.
 */
bool is_64dd_connected (void);

/**
 * @brief Convert a cart load error code to a human-readable error message.
 * 
 * @param err The cart load error code.
 * @return char* The human-readable error message.
 */
char *cart_load_convert_error_message(cart_load_err_t err);

/**
 * @brief Load an N64 ROM and its save data.
 * 
 * @param menu Pointer to the menu structure.
 * @param progress Callback function for ROM load progress updates.
 * @param save_progress Callback invoked once when a new save file is being created, or NULL.
 * @return cart_load_err_t Error code.
 */
cart_load_err_t cart_load_n64_rom_and_save(menu_t *menu, flashcart_progress_callback_t progress, flashcart_progress_callback_t save_progress);

/**
 * @brief Load the 64DD IPL (BIOS) and disk.
 * 
 * @param menu Pointer to the menu structure.
 * @param progress Callback function for progress updates.
 * @return cart_load_err_t Error code.
 */
cart_load_err_t cart_load_64dd_ipl_and_disks(menu_t *menu, flashcart_progress_callback_t progress);

/**
 * @brief Load an emulator and its ROM.
 * 
 * @param menu Pointer to the menu structure.
 * @param emu_type The type of emulator to load.
 * @param progress Callback function for progress updates.
 * @return cart_load_err_t Error code.
 */
cart_load_err_t cart_load_emulator(menu_t *menu, cart_load_emu_type_t emu_type, flashcart_progress_callback_t progress);

#endif /* CART_LOAD_H__ */
