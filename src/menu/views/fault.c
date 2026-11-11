#include <stdio.h>

#include "views.h"

static const char *firmware_message = (
    "Minimum supported firmware versions:\n"
    "64drive: 2.05+\n"
    "EverDrive-64 V/X series: ???+\n"
    "EverDrive-64 Pro: ???+\n"
    "SummerCart64: 2.17.0+"
);


/**
 * @brief Initialize the fault view using only the built-in console.
 *
 * A failed probe or DMA may leave PI unavailable. Error messages must use retained
 * state, and no filesystem assets, USB logging, controls, or normal menu work may run.
 */
void view_fault_init (menu_t *menu) {
    console_init();
    console_set_debug(false);
    console_set_render_mode(RENDER_MANUAL);
    printf("N64FlashcartMenu\n\nUNRECOVERABLE ERROR\n\n%s\n\n", flashcart_convert_error_message(menu->flashcart_err));
    if (menu->flashcart_err == FLASHCART_ERR_OUTDATED) {
        printf("%s\n\n", firmware_message);
    }
    printf("Power-cycle the console before trying again.\n");
}

void view_fault_display (menu_t *menu, surface_t *display) {
    console_render();
}
