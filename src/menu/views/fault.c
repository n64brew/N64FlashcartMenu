#include <stdio.h>

#include "views.h"


void view_fault_init (menu_t *menu) {
    console_init();
    console_set_debug(false);

    const char *firmware_message =
        ("Minimum supported firmware versions:\n"
         "64drive: 2.05+\n"
         "EverDrive-64 V/X series: ???+\n"
         "EverDrive-64 Pro: 26.1014+\n"
         "SummerCart64: 2.17.0+");

    printf(
        "UNRECOVERABLE ERROR\n"
        "\n"
        "%s\n"
        "\n"
        "%s",
        flashcart_convert_error_message(menu->flashcart_err),
        (menu->flashcart_err == FLASHCART_ERR_OUTDATED) ? firmware_message : ""
    );

    printf("\nPower-cycle the console before trying again.\n");
}


void view_fault_display (menu_t *menu, surface_t *display) {
    console_render();
}
