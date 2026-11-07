#include "views.h"
#include "../sound.h"


static void process (menu_t *menu) {
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = menu->load.from_grid ? MENU_MODE_GRID : MENU_MODE_BROWSER;
        menu->load.from_grid = false;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    if (menu->error_message) {
        ui_components_messagebox_draw(menu->error_message);
    } else {
        ui_components_messagebox_draw("Unspecified error");
    }

    rdpq_detach_show();
}

static void deinit (menu_t *menu) {
    menu->error_message = NULL;
    menu->flashcart_err = FLASHCART_OK;
}


void view_error_init (menu_t *menu) {
    if (menu->flashcart_err != FLASHCART_OK) {
        debugf(
            "Flashcart error [%d]: %s\n",
            menu->flashcart_err,
            flashcart_convert_error_message(menu->flashcart_err)
        );
    }
}

void view_error_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);

    if (menu->next_mode != MENU_MODE_ERROR) {
        deinit(menu);
    }
}

void menu_show_error (menu_t *menu, char *error_message) {
    sound_play_effect(SFX_ERROR);
    menu->next_mode = MENU_MODE_ERROR;
    menu->error_message = error_message;
}
