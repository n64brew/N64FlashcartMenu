/**
 * @file settings_tab.c
 * @brief The Settings tab.
 * @ingroup view
 *
 * This view owns nothing but the layout: a rail of categories on the left and
 * the selected category's pane on the right. Every pane is implemented by the
 * module that owns the underlying feature, so no logic is duplicated here.
 */

#include <stdbool.h>

#include "../sound.h"
#include "../ui_components.h"
#include "../ui_components/constants.h"
#include "views.h"

/** @brief Index of the Settings tab in the common tab bar. */
#define SETTINGS_TAB_INDEX  (4)

/** @brief Height of a single entry in the category rail. */
#define RAIL_ENTRY_HEIGHT   (37)

static const settings_pane_t *const panes[] = {
    &settings_pane_menu,
    &settings_pane_theme,
    &settings_pane_controller_pak,
    &settings_pane_time,
    &settings_pane_information,
    &settings_pane_flashcart,
    &settings_pane_n64,
};

#define PANE_COUNT ((int) (sizeof(panes) / sizeof(panes[0])))

static int selected_pane;
static bool pane_focused;
/** @brief Pane to focus on the next entry, or -1 for normal category navigation. */
static int pending_pane = -1;


/**
 * @brief Give up focus on the current pane and return to the category rail.
 */
static void leave_pane (menu_t *menu) {
    if (panes[selected_pane]->leave) {
        panes[selected_pane]->leave(menu);
    }
    pane_focused = false;
}

/**
 * @brief Hand focus to the current pane.
 */
static void enter_pane (menu_t *menu) {
    pane_focused = true;
    if (panes[selected_pane]->enter) {
        panes[selected_pane]->enter(menu);
    }
    sound_play_effect(SFX_ENTER);
}

static void process (menu_t *menu) {
    bool tabs_blocked = pane_focused
        && panes[selected_pane]->blocks_tabs
        && panes[selected_pane]->blocks_tabs(menu);

    if (!tabs_blocked && (menu->actions.tab_left || menu->actions.tab_right)) {
        if (pane_focused) {
            leave_pane(menu);
        }
        menu->next_mode = menu->actions.tab_left ? MENU_MODE_FAVORITE :
                          menu->grid_enabled ? MENU_MODE_GRID : MENU_MODE_BROWSER;
        sound_play_effect(SFX_CURSOR);
        return;
    }

    if (pane_focused) {
        /* A pane without a process callback is read only, so B always leaves. */
        if (!panes[selected_pane]->process) {
            if (menu->actions.back) {
                leave_pane(menu);
                sound_play_effect(SFX_EXIT);
            }
            return;
        }

        if (!panes[selected_pane]->process(menu)) {
            leave_pane(menu);
            sound_play_effect(SFX_EXIT);
        }
        return;
    }

    if (menu->actions.go_up && selected_pane > 0) {
        selected_pane--;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_down && selected_pane < PANE_COUNT - 1) {
        selected_pane++;
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.enter || menu->actions.go_right) {
        enter_pane(menu);
    }
}

/**
 * @brief Draw the category rail and the divider separating it from the pane.
 */
static void rail_draw (void) {
    int y = SETTINGS_PANE_Y0 + 4;

    for (int i = 0; i < PANE_COUNT; i++) {
        bool selected = (i == selected_pane);

        if (selected) {
            ui_components_box_draw(
                SETTINGS_RAIL_X0 + 4, y, SETTINGS_RAIL_X1 - 4, y + RAIL_ENTRY_HEIGHT - 4,
                pane_focused ? TAB_ACTIVE_BACKGROUND_COLOR : FILE_LIST_HIGHLIGHT_COLOR
            );
        }

        if (selected && !pane_focused)
            ui_components_focus_draw(SETTINGS_RAIL_X0 + 4, y,
                SETTINGS_RAIL_X1 - 4, y + RAIL_ENTRY_HEIGHT - 4);

        ui_components_text_draw(
            SETTINGS_RAIL_X0 + 8, y + 11, SETTINGS_RAIL_X1 - SETTINGS_RAIL_X0 - 16, SETTINGS_ROW_HEIGHT,
            selected ? STL_DEFAULT : STL_GRAY, ALIGN_LEFT, WRAP_ELLIPSES, panes[i]->label
        );

        y += RAIL_ENTRY_HEIGHT;
    }

    ui_components_box_draw(
        SETTINGS_RAIL_X1, SETTINGS_PANE_Y0, SETTINGS_RAIL_X1 + 3, SETTINGS_PANE_Y1, BORDER_COLOR
    );
}

/**
 * @brief Draw one action bar hint, falling back to the rail's own hints.
 */
static void hint_draw (menu_t *menu, settings_hint_t slot, rdpq_align_t align) {
    static const char *const rail_hints[] = { "A: Open\n", "D-Pad: Choose\n◀L Tab R▶", NULL };
    const char *text = NULL;

    if (pane_focused && panes[selected_pane]->hint) {
        text = panes[selected_pane]->hint(menu, slot);
    } else if (!pane_focused) {
        text = rail_hints[slot];
    }

    ui_components_actions_bar_text_draw(STL_DEFAULT, align, VALIGN_TOP, "%s", text ? text : "\n");
}

static void draw (menu_t *menu, surface_t *display) {
    rdpq_attach(display, NULL);

    ui_components_background_draw();
    ui_components_tabs_common_draw(menu, SETTINGS_TAB_INDEX);
    ui_components_layout_draw_tabbed();

    rail_draw();
    panes[selected_pane]->draw(menu, pane_focused);

    hint_draw(menu, SETTINGS_HINT_LEFT, ALIGN_LEFT);
    hint_draw(menu, SETTINGS_HINT_CENTER, ALIGN_CENTER);
    hint_draw(menu, SETTINGS_HINT_RIGHT, ALIGN_RIGHT);

    if (pane_focused && panes[selected_pane]->overlay) {
        panes[selected_pane]->overlay(menu);
    }

    rdpq_detach_show();
}


void view_settings_open_pane (menu_t *menu, const settings_pane_t *pane) {
    for (int i = 0; i < PANE_COUNT; i++) {
        if (panes[i] == pane) {
            pending_pane = i;
            menu->next_mode = MENU_MODE_SETTINGS;
            return;
        }
    }
}

void view_settings_init (menu_t *menu) {
    pane_focused = false;
    if (pending_pane >= 0) {
        selected_pane = pending_pane;
        pending_pane = -1;
        enter_pane(menu);
    }
}

void view_settings_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}
