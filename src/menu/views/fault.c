#include <stdio.h>

#include "views.h"

static const char *firmware_message = (
    "Minimum supported firmware versions:\n"
    "64drive: 2.05+\n"
    "EverDrive-64: ???+\n"
    "SummerCart64: 2.17.0+"
);


/**
 * @brief Stop startup after any flashcart error, using only the built-in console.
 *
 * A failed probe or DMA may leave PI unavailable. Error messages must use retained
 * state, and no filesystem assets, USB logging, controls, or normal menu work may run.
 */
void view_fault_startup (flashcart_err_t error) {
    console_init();
    console_set_debug(false);
    console_set_render_mode(RENDER_MANUAL);
    printf("N64FlashcartMenu\n\nUNRECOVERABLE ERROR\n\n%s\n\n", flashcart_convert_error_message(error));
    if (error == FLASHCART_ERR_OUTDATED) {
        printf("%s\n\n", firmware_message);
    }
    printf("Power-cycle the console before trying again.\n");
    for (;;) {
        console_render();
    }
}


static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    rdpq_clear(RGBA32(0x7F, 0x00, 0x00, 0xFF));


    ui_components_messagebox_draw(
        "UNRECOVERABLE ERROR\n"
        "\n"
        "%s\n"
        "\n"
        "%s",
        flashcart_convert_error_message(menu->flashcart_err),
        (menu->flashcart_err == FLASHCART_ERR_OUTDATED) ? firmware_message : ""
    );

    rdpq_detach_show();
}


void view_fault_init (menu_t *menu) {
    // Nothing to initialize (yet)
}

void view_fault_display (menu_t *menu, surface_t *display) {
    draw(menu, display);
}
