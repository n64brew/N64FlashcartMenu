#include "views.h"
#include "../sound.h"


static void process (menu_t *menu) {
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
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
    // Dismissing an error always leads back to the browser (see process() above), so any
    // pending History/Favorites load target must be abandoned here too; otherwise a mode
    // that preserves it across a round-trip (e.g. the ROM info screen handing a
    // description off to the text viewer) could leave it set after an error diverts away
    // from that round-trip, causing a later, unrelated browser selection to be ignored in
    // favor of the stale entry.
    menu->load.load_history_id = -1;
    menu->load.load_favorite_id = -1;
    menu->next_mode = MENU_MODE_ERROR;
    menu->error_message = error_message;
}
