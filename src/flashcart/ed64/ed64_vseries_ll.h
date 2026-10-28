/**
 * @file ed64_vseries_ll.h
 * @brief ED64 V series flashcart low level access
 * @ingroup flashcart
 */

#ifndef FLASHCART_ED64_VSERIES_LL_H__
#define FLASHCART_ED64_VSERIES_LL_H__

#include <stdint.h>
#include <stdbool.h>

#define PI_SAVE_ADDR    0xA8000000UL
#define ROM_ADDRESS     0xB0000000UL

typedef enum {
    SAVE_TYPE_NONE = 0,
    SAVE_TYPE_SRAM = 1,
    SAVE_TYPE_SRAM_128K = 2,
    SAVE_TYPE_EEPROM_4K = 3,
    SAVE_TYPE_EEPROM_16K = 4,
    SAVE_TYPE_FLASHRAM = 5,
} ed64_vseries_save_type_t;

void ed64_vseries_ll_set_sdcard_timing (void);
ed64_vseries_save_type_t ed64_vseries_ll_get_save_type (void);
void ed64_vseries_ll_set_save_type (ed64_vseries_save_type_t type);
void ed64_vseries_ll_get_sram (uint8_t *buffer, int size);
void ed64_vseries_ll_get_eeprom (uint8_t *buffer, int size);
void ed64_vseries_ll_get_fram (uint8_t *buffer, int size);
void ed64_vseries_ll_set_sram (uint8_t *buffer, int size);
void ed64_vseries_ll_set_eeprom (uint8_t *buffer, int size);
void ed64_vseries_ll_set_fram (uint8_t *buffer, int size);


/** @} */ /* ed64_vseries_ll */


#endif
