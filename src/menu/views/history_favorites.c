#include <stdarg.h>
#include "../bookkeeping.h"
#include "../fonts.h"
#include "../ui_components/constants.h"
#include "../sound.h"
#include "views.h"


typedef enum {
    BOOKKEEPING_TAB_CONTEXT_HISTORY,
    BOOKKEEPING_TAB_CONTEXT_FAVORITE,
    BOOKKEEPING_TAB_CONTEXT_NONE
} bookkeeping_tab_context_t;


static bookkeeping_tab_context_t tab_context = BOOKKEEPING_TAB_CONTEXT_NONE;
static int selected_item = -1;
static bookkeeping_item_t *item_list;
static uint16_t item_max = 0;


static void item_reset_selected(menu_t *menu) {
    selected_item = -1;

    for(uint16_t i=0; i<item_max; i++) {
        if(item_list[i].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            selected_item = i;
            break;
        }
    }  
}

static void item_move_next() {
    int last = selected_item;

    do
    {
        selected_item++;

        if(selected_item >= item_max) {
            selected_item = last;
            break;
        } else if(item_list[selected_item].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            sound_play_effect(SFX_CURSOR);
            break;
        }
    } while (true);  
}

static void item_move_previous() {
    int last = selected_item;
    do
    {
        selected_item--;

        if(selected_item < 0) {
            selected_item = last;
            break;
        } else if(item_list[selected_item].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            sound_play_effect(SFX_CURSOR);
            break;
        }
    } while (true);
}

static void process(menu_t *menu) {
    if(menu->actions.go_down) {
        item_move_next();   
    } else if(menu->actions.go_up) {
        item_move_previous();
    } else if(menu->actions.enter && selected_item != -1) {
                
        if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->load.load_favorite_id = selected_item;
            menu->load.load_history_id = -1;
        } else if(tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->load.load_history_id = selected_item;
            menu->load.load_favorite_id = -1;
        }           

        if(item_list[selected_item].bookkeeping_type == BOOKKEEPING_TYPE_DISK) {
            menu->next_mode = MENU_MODE_LOAD_DISK;
            sound_play_effect(SFX_ENTER);
        } else if(item_list[selected_item].bookkeeping_type == BOOKKEEPING_TYPE_ROM) {
            menu->next_mode = MENU_MODE_LOAD_ROM;
            sound_play_effect(SFX_ENTER);
        }
    } else if (menu->actions.tab_left) {
        if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->next_mode = MENU_MODE_HISTORY;
        } else if(tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->next_mode = MENU_MODE_BROWSER;
        }
        sound_play_effect(SFX_CURSOR);       
    } else if (menu->actions.tab_right) {
        if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->next_mode = MENU_MODE_SETTINGS;
        } else if(tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->next_mode = MENU_MODE_FAVORITE;
        }
        sound_play_effect(SFX_CURSOR);
    }else if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE && menu->actions.context && selected_item != -1) {
        bookkeeping_favorite_remove(&menu->bookkeeping, selected_item);
        item_reset_selected(menu);
        sound_play_effect(SFX_SETTING);
    }
}

static void draw_list(menu_t *menu, surface_t *display) {
    if(selected_item != -1) {
        float highlight_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT +  TEXT_OFFSET_VERTICAL + (selected_item * 19 * 2);

        ui_components_box_draw(
            VISIBLE_AREA_X0,
            highlight_y,
            VISIBLE_AREA_X0 + FILE_LIST_HIGHLIGHT_WIDTH + LIST_SCROLLBAR_WIDTH,
            highlight_y + 39,
            FILE_LIST_HIGHLIGHT_COLOR
        );
    }

    char buffer[1024];
    buffer[0] = 0;

    for(uint16_t i=0; i < item_max; i++) {   
        if(path_has_value(item_list[i].primary_path)) {
            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%d  : %s\n", (i+1), path_last_get(item_list[i].primary_path));
        } else {
            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "%d  : \n", (i+1));
        }

        if(path_has_value(item_list[i].secondary_path)) {
            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "     %s\n", path_last_get(item_list[i].secondary_path));
        } else {
            snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), "\n");
        }
    }

    int nbytes = strlen(buffer);
    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .width = VISIBLE_AREA_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = LAYOUT_ACTIONS_SEPARATOR_Y - OVERSCAN_HEIGHT - (TEXT_MARGIN_VERTICAL * 2),
            .align = ALIGN_LEFT,
            .valign = VALIGN_TOP,
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_OFFSET_VERTICAL,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT +  TEXT_OFFSET_VERTICAL,
        buffer,
        nbytes
    );           
}

static void draw(menu_t *menu, surface_t *display) {
    rdpq_attach(display, NULL);

    ui_components_background_draw();

    if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
        ui_components_tabs_common_draw(menu, 3);
    } else if(tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
        ui_components_tabs_common_draw(menu, 2);
    }

    ui_components_layout_draw_tabbed();

    draw_list(menu, display);

    if(selected_item != -1) {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Launch\n"
            "\n"
        );
        
        if(tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE && selected_item != -1) {
            ui_components_actions_bar_text_draw(
                STL_DEFAULT,
                ALIGN_RIGHT, VALIGN_TOP,
                "Z: Remove item\n"
                "\n"
            );
        }
    }

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "▼▲ Scroll | ◀L Tab R▶\n"
        "%s",
        ctime(&menu->current_time)
    );    

    rdpq_detach_show();   
}

void view_favorite_init (menu_t *menu) {
    tab_context = BOOKKEEPING_TAB_CONTEXT_FAVORITE;
    item_list = menu->bookkeeping.favorite_items;
    item_max = FAVORITES_COUNT;

    item_reset_selected(menu);
}

void view_favorite_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display); 
}

void view_history_init (menu_t *menu) {
    tab_context = BOOKKEEPING_TAB_CONTEXT_HISTORY;
    item_list = menu->bookkeeping.history_items;
    item_max = HISTORY_COUNT;

    item_reset_selected(menu);
}

void view_history_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display); 
}
