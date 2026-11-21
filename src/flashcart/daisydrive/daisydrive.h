/**
 * @file daisydrive.h
 * @brief daisydrive flashcart support
 * @ingroup flashcart 
 */

#ifndef FLASHCART_DAISSYDRIVE_H__
#define FLASHCART_DAISSYDRIVE_H__


#include "../flashcart.h"


/**
 * @addtogroup daisydrive
 * @{
 */

bool daisydrive_init_sdfs(const char *storage_prefix, int max_files);
bool daisydrive_detected();
flashcart_t *daisydrive_get_flashcart (void);

/** @} */ /* daisydrive */


#endif
