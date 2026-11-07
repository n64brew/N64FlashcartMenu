#include <libdragon.h>

#include "actions.h"


#define ACTIONS_REPEAT_DELAY    (8)


static int dir_repeat_delay;
static joypad_8way_t last_dir = JOYPAD_8WAY_NONE;


static void actions_clear (menu_t *menu) {
    menu->actions.go_up = false;
    menu->actions.go_down = false;
    menu->actions.go_left = false;
    menu->actions.go_right = false;
    menu->actions.go_fast = false;

    menu->actions.enter = false;
    menu->actions.back = false;
    menu->actions.options = false;
    menu->actions.settings = false;
    menu->actions.context = false;
    menu->actions.tab_left = false;
    menu->actions.tab_right = false;
}

static void actions_update_direction (menu_t *menu) {
    joypad_8way_t held_dir = JOYPAD_8WAY_NONE;
    joypad_8way_t fast_dir = JOYPAD_8WAY_NONE;

    JOYPAD_PORT_FOREACH (i) {
        held_dir = joypad_get_direction(i, JOYPAD_2D_DPAD | JOYPAD_2D_STICK);
        fast_dir = joypad_get_direction(i, JOYPAD_2D_C);
        if (held_dir != JOYPAD_8WAY_NONE || fast_dir != JOYPAD_8WAY_NONE) {
            break;
        }
    }

    if (fast_dir != JOYPAD_8WAY_NONE) {
        held_dir = fast_dir;
        menu->actions.go_fast = true;
    }

    joypad_8way_t final_dir = held_dir;

    if ((last_dir != held_dir) && (last_dir == JOYPAD_8WAY_NONE)) {
        dir_repeat_delay = ACTIONS_REPEAT_DELAY;
    } else if (dir_repeat_delay > 0) {
        final_dir = JOYPAD_8WAY_NONE;
    }

    switch (final_dir) {
        case JOYPAD_8WAY_NONE:
            break;
        case JOYPAD_8WAY_RIGHT:
            menu->actions.go_right = true;
            break;
        case JOYPAD_8WAY_UP_RIGHT:
            menu->actions.go_up = true;
            menu->actions.go_right = true;
            break;
        case JOYPAD_8WAY_UP:
            menu->actions.go_up = true;
            break;
        case JOYPAD_8WAY_UP_LEFT:
            menu->actions.go_up = true;
            menu->actions.go_left = true;
            break;
        case JOYPAD_8WAY_LEFT:
            menu->actions.go_left = true;
            break;
        case JOYPAD_8WAY_DOWN_LEFT:
            menu->actions.go_down = true;
            menu->actions.go_left = true;
            break;
        case JOYPAD_8WAY_DOWN:
            menu->actions.go_down = true;
            break;
        case JOYPAD_8WAY_DOWN_RIGHT:
            menu->actions.go_down = true;
            menu->actions.go_right = true;
            break;
    }

    if (dir_repeat_delay > 0) {
        dir_repeat_delay -= 1;
    }

    last_dir = held_dir;
}

static void actions_update_buttons (menu_t *menu) {    
    joypad_buttons_t pressed = {0};

    JOYPAD_PORT_FOREACH (i) {
        pressed = joypad_get_buttons_pressed(i);
        if (pressed.raw) {
            break;
        }
    }

    if (pressed.a) {
        menu->actions.enter = true;
    } else if (pressed.b) {
        menu->actions.back = true;
    } else if (pressed.r) {
        menu->actions.options = true;
        menu->actions.tab_right = true;
    } else if (pressed.start) {
        menu->actions.settings = true;
    } else if (pressed.l) {
        menu->actions.tab_left = true;
    } else if (pressed.z) {
        menu->actions.context = true;
    }
}


void actions_init (void) {
    JOYPAD_PORT_FOREACH (port) {
        joypad_set_rumble_active(port, false);
    }
}

void actions_update (menu_t *menu) {
    joypad_poll();

    actions_clear(menu);
    actions_update_direction(menu);
    actions_update_buttons(menu);
}
