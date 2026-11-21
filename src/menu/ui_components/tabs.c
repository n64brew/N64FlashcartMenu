/**
 * @file tabs.c
 * @brief Implementation of the tabs UI component.
 * @ingroup ui_components
 */

#include "../ui_components.h"
#include "constants.h"

/**
 * @brief Common tab labels used for the main menu.
 */
static const char *tabs[] = {
    "Grid",
    "Files",
    "History",
    "Favorites",
    "Settings",
    NULL
};

/**
 * @brief Draw the common tabs used for the main menu.
 *
 * @param menu Pointer to the menu structure; Grid is omitted unless enabled.
 * @param selected Index of the currently selected tab, counting Grid as 0.
 */
void ui_components_tabs_common_draw(menu_t *menu, int selected)
{
    /* Grid is the first tab, so hiding it starts the list one later. */
    int first = menu->grid_enabled ? 0 : 1;
    uint8_t tabs_count = sizeof(tabs) / sizeof(tabs[0]) - 1 - first;
    ui_components_tabs_draw(&tabs[first], tabs_count, selected - first, (float) VISIBLE_AREA_WIDTH / tabs_count);
}
