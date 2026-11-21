#include "utils/fs.h"
#include "../flashcart/flashcart.h"
#include "views.h"
#include <stdlib.h>
#include <string.h>


static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach_clear(d, NULL);
    rdpq_detach_show();
}


void view_startup_init (menu_t *menu) {
    path_t *metadata = path_init(menu->storage_prefix, "menu/metadata");
    menu->grid_enabled = directory_exists(path_get(metadata));
    path_free(metadata);

    if (flashcart_has_voltage_temperature()) {
        uint16_t voltage_mv;
        int16_t temperature_deci_c;

        if (flashcart_get_voltage_temperature(&voltage_mv, &temperature_deci_c) == FLASHCART_OK && voltage_mv < 3000) {
            menu_show_error(menu, "ERROR: Low voltage detected\n\n"
                                  "The flashcart reports a voltage less than 3.0V.\n"
                                  "\n"
                                  "COMMON CAUSES:\n"
                                  "- Faulty power supply\n"
                                  "- Clone Flashcart modifications."
                                  );
            return;
        }
    }

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    // FIXME: rather than use a controller button, would it be better to use the cart button?
    JOYPAD_PORT_FOREACH (port) {
        joypad_poll();
        joypad_buttons_t b_held = joypad_get_buttons_held(port);

        if (menu->settings.rom_autoload_enabled && b_held.start) {
            menu->settings.rom_autoload_enabled = false;
            // set_autoload_type() frees these, so they stay heap strings.
            free(menu->settings.rom_autoload_path);
            menu->settings.rom_autoload_path = strdup("");
            free(menu->settings.rom_autoload_filename);
            menu->settings.rom_autoload_filename = strdup("");
            settings_save(&menu->settings);
        }
    }
    if (menu->settings.rom_autoload_enabled) {
        menu->browser.directory = path_init(menu->storage_prefix, menu->settings.rom_autoload_path);
        menu->load.rom_path = path_clone_push(menu->browser.directory, menu->settings.rom_autoload_filename);
        menu->load_pending.rom_file = true;
        menu->next_mode = MENU_MODE_LOAD_ROM;

        return;
    }
#endif
    
    if (menu->settings.first_run) {
        menu->settings.first_run = false;
        settings_save(&menu->settings);
        menu->next_mode = MENU_MODE_CREDITS;
    }
    else {
        menu->next_mode = menu->settings.boot_into_grid && menu->grid_enabled ? MENU_MODE_GRID : MENU_MODE_BROWSER;
    }
}

void view_startup_display (menu_t *menu, surface_t *display) {
    draw(menu, display);
}
