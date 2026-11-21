/**
 * @file ed64_pro.h
 * @brief ed64_pro flashcart support
 * @ingroup flashcart 
 */

#ifndef FLASHCART_ED64_PRO_H__
#define FLASHCART_ED64_PRO_H__


#include "../flashcart.h"


/**
 * @addtogroup ed64_pro
 * @{
 */

bool ed64_pro_init_sdfs(const char *storage_prefix, int max_files);
bool ed64_pro_detected();
flashcart_t *ed64_pro_get_flashcart (void);

/** @} */ /* ed64_pro */
    

#endif
