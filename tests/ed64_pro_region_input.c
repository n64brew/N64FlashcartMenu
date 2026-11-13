/**
 * @file ed64_pro_region_input.c
 * @brief Test-only controller actions for capturing normal ROM detection and launch.
 *
 * Build only with tests/Makefile.region. Set the normal menu default_directory
 * to the fixture directory, disable autoload/Grid/first-run, and define
 * ED64_PRO_REGION_TEST_ROM as the fixture basename in REGION_TEST_CONFIG.
 * Any real menu action cancels automation until the next menu launch.
 */

#include <libdragon.h>
#include <string.h>

#include "menu/menu_state.h"

#ifndef ED64_PRO_REGION_INPUT_TEST
#error Build this helper with tests/Makefile.region
#endif

#ifndef ED64_PRO_REGION_TEST_ROM
#define ED64_PRO_REGION_TEST_ROM "region.z64"
#endif

#ifndef ED64_PRO_REGION_TEST_HOLD_MS
#define ED64_PRO_REGION_TEST_HOLD_MS 15000
#endif

/** @brief Test input phases; all transitions use the existing menu actions. */
typedef enum {
    REGION_INPUT_FIND_ROM,
    REGION_INPUT_WAIT_DETAILS,
    REGION_INPUT_SHOW_TV,
    REGION_INPUT_SHOW_CIC,
    REGION_INPUT_LAUNCH,
    REGION_INPUT_FINISHED,
} region_input_phase_t;

static region_input_phase_t phase;
static uint64_t phase_started_ms;
static uint64_t next_action_ms;

void __real_actions_update (menu_t *menu);

/** @brief Detect operator input so a controller can take over the test. */
static bool operator_action (menu_t *menu) {
    return menu->actions.go_up || menu->actions.go_down || menu->actions.go_left || menu->actions.go_right ||
           menu->actions.go_fast || menu->actions.enter || menu->actions.back || menu->actions.options ||
           menu->actions.settings || menu->actions.context || menu->actions.tab_left || menu->actions.tab_right;
}

/** @brief Capture TV and CIC screens, then launch through the ordinary ROM view. */
void __wrap_actions_update (menu_t *menu) {
    __real_actions_update(menu);
    if (phase == REGION_INPUT_FINISHED) {
        return;
    }
    if (menu->mode == MENU_MODE_NONE || menu->mode == MENU_MODE_STARTUP) {
        return;
    }
    if (operator_action(menu)) {
        phase = REGION_INPUT_FINISHED;
        return;
    }

    uint64_t now = get_ticks_ms();
    if (now < next_action_ms) {
        return;
    }
    if (phase == REGION_INPUT_FIND_ROM) {
        if (menu->mode != MENU_MODE_BROWSER) {
            phase = REGION_INPUT_FINISHED;
            return;
        }
        for (int32_t i = 0; i < menu->browser.entries; i++) {
            entry_t *entry = &menu->browser.list[i];
            if (entry->type != ENTRY_TYPE_ROM || strcmp(entry->name, ED64_PRO_REGION_TEST_ROM) != 0) {
                continue;
            }
            if (menu->browser.selected == i) {
                menu->actions.enter = true;
                phase = REGION_INPUT_WAIT_DETAILS;
            } else if (menu->browser.selected < i) {
                menu->actions.go_down = true;
            } else {
                menu->actions.go_up = true;
            }
            next_action_ms = now + 200;
            return;
        }
        phase = REGION_INPUT_FINISHED;
        debugf("[REGION TEST] Fixture not found: %s\n", ED64_PRO_REGION_TEST_ROM);
        return;
    }

    if (menu->mode != MENU_MODE_LOAD_ROM || !menu->load.rom_path ||
        strcmp(path_last_get(menu->load.rom_path), ED64_PRO_REGION_TEST_ROM) != 0 || menu->load_pending.rom_file) {
        phase = REGION_INPUT_FINISHED;
        return;
    }

    if (phase == REGION_INPUT_WAIT_DETAILS) {
        phase = REGION_INPUT_SHOW_TV;
        phase_started_ms = now;
    } else if (phase == REGION_INPUT_SHOW_TV && now - phase_started_ms >= ED64_PRO_REGION_TEST_HOLD_MS) {
        menu->actions.context = true;
        phase = REGION_INPUT_SHOW_CIC;
        phase_started_ms = now;
    } else if (phase == REGION_INPUT_SHOW_CIC && now - phase_started_ms >= ED64_PRO_REGION_TEST_HOLD_MS) {
        menu->actions.back = true;
        phase = REGION_INPUT_LAUNCH;
        phase_started_ms = now;
    } else if (phase == REGION_INPUT_LAUNCH && now - phase_started_ms >= 2000) {
        menu->actions.enter = true;
        phase = REGION_INPUT_FINISHED;
    }
}
