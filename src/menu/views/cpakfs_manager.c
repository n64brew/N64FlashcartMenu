#include <stdbool.h>
#include <stdio.h>
#include <libdragon.h>
#include "views.h"
#include "../sound.h"
#include "../fonts.h"
#include <errno.h>
#include <dir.h>
#include "utils/fs.h"
#include "utils/cpakfs_utils.h"
#include "../ui_components/constants.h"
#include "../../utils/utils.h"

/** @brief Number of note rows the settings pane shows at once. */
#define CPAK_PANE_NOTES 3

#define MAX_STRING_LENGTH 62

#define MEMPAK_BANK_SIZE 32768

#define CPAK_EXTENSION ".pak"   
#define CPAK_NOTE_EXTENSION ".paknote"

static bool use_rtc;
static int note_scroll;
static char string_datetime_cpak[26];
static char failure_message_note[255];

static int16_t controller_selected;
static int16_t index_selected;

static bool mounted[4] = { false, false, false, false };
static bool has_pak[4] = { false, false, false, false };
static bool corrupted[4] = { false, false, false, false };
static cpakfs_stats_t stats_per_port[4];

static bool has_mem;
static bool corrupted_pak;
static bool unmounted;
static bool ctr_p_data_loop; // to avoid repopulating the list multiple times
static cpakfs_stats_t cpakfs_stats;
static dir_t dir_entry;

static bool process_complete_full_dump;
static bool process_complete_note_dump;
static bool process_complete_format;
static bool process_complete_delete;
static bool error_message_displayed;

static char controller_pak_name_notes[MAX_NUM_NOTES][MAX_STRING_LENGTH];
static char controller_pak_name_notes_bank_size[MAX_NUM_NOTES][6]; // "(XXX)" = 5 chars + \0

static cpakfs_path_strings_t cpakfs_path_strings[MAX_NUM_NOTES];

static bool show_complete_dump_confirm_message;
static bool show_single_note_dump_confirm_message;
static bool show_single_note_delete_confirm_message;
static bool show_format_controller_pak_confirm_message;
static bool show_complete_write_confirm_message;
static bool show_single_note_write_info_message;

static bool start_complete_dump;
static bool start_single_note_dump;
static bool start_single_note_delete;
static bool start_format_controller_pak;

static char * CPAK_PATH = "sd:/cpak_saves";
static char * CPAK_NOTES_PATH = "sd:/cpak_saves/notes";

static int note_count (void);

static void reset_vars(){
    has_mem = false;
    corrupted_pak = false;
    ctr_p_data_loop = false;
    show_complete_dump_confirm_message = false;
    show_single_note_dump_confirm_message = false;
    show_single_note_delete_confirm_message = false;
    show_format_controller_pak_confirm_message = false;
    show_complete_write_confirm_message = false;
    show_single_note_write_info_message = false;
    start_complete_dump = false;
    start_single_note_dump = false;
    start_single_note_delete = false;
    start_format_controller_pak = false;
    process_complete_full_dump = false;
    process_complete_note_dump = false;
    process_complete_format = false;
    process_complete_delete = false;
    error_message_displayed = false;
}

static void get_rtc_time(char* formatted_time) {
    time_t t = time(NULL);

    struct tm tm = *localtime(&t);

    sprintf(formatted_time, "%04d-%02d-%02d_%02d%02d%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void free_controller_pak_name_notes() {
    for (int i = 0; i < MAX_NUM_NOTES; ++i) {
        controller_pak_name_notes[i][0] = '\0';
        controller_pak_name_notes_bank_size[i][0] = '\0';
        cpakfs_path_strings[i].gamecode[0] = '\0';
        cpakfs_path_strings[i].pubcode[0] = '\0';
        cpakfs_path_strings[i].filename[0] = '\0';
        cpakfs_path_strings[i].ext[0] = '\0';
    }
}

static void check_accessories(int controller) {
    bool was_present = has_pak[controller];

    joypad_accessory_type_t acc = joypad_get_accessory_type(controller);
    bool present = (acc == JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK);
    has_pak[controller] = present;

    if (!present) {
        if (mounted[controller]) {
            cpakfs_unmount(controller);
            mounted[controller] = false;
        }
        corrupted[controller] = false;
        memset(&stats_per_port[controller], 0, sizeof(stats_per_port[controller]));

        if (was_present) {
            free_controller_pak_name_notes();
            ctr_p_data_loop = false;
        }

        has_mem       = false;
        corrupted_pak = false;
        memset(&cpakfs_stats, 0, sizeof(cpakfs_stats));
        return;
    }

    if (!mounted[controller]) {
        corrupted[controller] = (mount_cpakfs(controller) < 0);
        if (!corrupted[controller]) {
            cpakfs_get_stats(controller, &stats_per_port[controller]);
            mounted[controller] = true;
        } else {
            mounted[controller] = false;
        }
    } else {
        if (!corrupted[controller]) {
            cpakfs_get_stats(controller, &stats_per_port[controller]);
        }
    }

    has_mem       = has_pak[controller];
    corrupted_pak = corrupted[controller];
    cpakfs_stats  = stats_per_port[controller];

    if (!was_present && present) {
        free_controller_pak_name_notes();
        ctr_p_data_loop = false;
    }
}

static void format_controller_pak () {
    snprintf(failure_message_note, sizeof(failure_message_note), " ");
    int res = cpakfs_format(controller_selected, false);
    if (res < 0) {
        snprintf(failure_message_note, sizeof(failure_message_note), "Unable to format Controller Pak on controller %d!\nError code: %d", controller_selected + 1, res);
        error_message_displayed = true;
    }
    reset_vars();
    cpakfs_unmount(controller_selected);
    mounted[controller_selected] = false;
    has_pak[controller_selected] = false;
    corrupted[controller_selected] = false; 
    process_complete_format = true;
}

static void active_single_note_delete_message(menu_t *menu, void *arg) {
    show_single_note_delete_confirm_message = true;
}

static void active_format_controller_pak_message(menu_t *menu, void *arg) {
    show_format_controller_pak_confirm_message = true;
}

static void active_restore_controller_pak_message(menu_t *menu, void *arg) {
    show_complete_write_confirm_message = true;
}

static void active_restore_controller_pak_note_message(menu_t *menu, void *arg) {
    show_single_note_write_info_message = true;
}

static component_context_menu_t options_context_menu = {
    .list = {
        { .text = "Format Controller Pak", .action = active_format_controller_pak_message },
        { .text = "Delete single note", .action = active_single_note_delete_message },
        { .text = "Restore a dump to the Controller Pak", .action = active_restore_controller_pak_message },
        { .text = "Restore a note to the Controller Pak", .action = active_restore_controller_pak_note_message },
        COMPONENT_CONTEXT_MENU_LIST_END,
    }
};

static void write_note_name_info_list(int16_t controller, int index, char* entry_name) {
    char filename_cpak[256];
    snprintf(filename_cpak, sizeof(filename_cpak), "%s%s", CPAK_MOUNT_ARRAY[controller], entry_name);
    int size = get_block_size_from_fs_path(filename_cpak);

    if (size < 0) {
        snprintf(controller_pak_name_notes_bank_size[index], sizeof(controller_pak_name_notes_bank_size[index]), " ");
    } else {
        snprintf(controller_pak_name_notes_bank_size[index], sizeof(controller_pak_name_notes_bank_size[index]), "%-3.3d", size);
    }
    snprintf(controller_pak_name_notes[index], MAX_STRING_LENGTH, "%s", entry_name);
    parse_cpakfs_fullname(entry_name, &cpakfs_path_strings[index]);
}

static void populate_list_cpakfs() {  
    if (has_mem && !ctr_p_data_loop) {
        
        free_controller_pak_name_notes();

        if (dir_findfirst(CPAK_MOUNT_ARRAY[controller_selected], &dir_entry) >= 0) {
            
            write_note_name_info_list(controller_selected, 0, dir_entry.d_name);

            int i = 1;     
            while(dir_findnext(CPAK_MOUNT_ARRAY[controller_selected], &dir_entry) == 0) {
                
                write_note_name_info_list(controller_selected, i, dir_entry.d_name);
            
                i++;
                if (i >= MAX_NUM_NOTES) break;

                ctr_p_data_loop = true;
            }
        }
    }
}

static void dump_complete_cpak(int port) {
    snprintf(failure_message_note, sizeof(failure_message_note), " ");

    
    int banks = cpak_probe_banks(port);
    if (banks < 1) {
        // Fallback to 1 bank if probing not available; or show error.
        banks = 1;
    }

    get_rtc_time(string_datetime_cpak);
    char complete_filename[200];
    snprintf(complete_filename, sizeof(complete_filename), "%s/CPAK_%s%s", CPAK_PATH, string_datetime_cpak, CPAK_EXTENSION);

    FILE *fp = fopen(complete_filename, "wb");
    if (!fp) {
        snprintf(failure_message_note, sizeof(failure_message_note), "Failed to open file for writing: %s\n", complete_filename);
        error_message_displayed = true;
        return;
    }

    uint8_t *bankbuf = scratch_malloc(MEMPAK_BANK_SIZE);
    bool used_scratch = true;
    if (!bankbuf) {
        used_scratch = false;
        bankbuf = malloc(MEMPAK_BANK_SIZE);
    }
    if (!bankbuf) {
        snprintf(failure_message_note, sizeof(failure_message_note), "Memory allocation failed!");
        error_message_displayed = true;
        fclose(fp);
        return;
    }

    for (int b = 0; b < banks; ++b) {
        int rd = cpak_read((joypad_port_t)port, (uint8_t)b, 0, bankbuf, MEMPAK_BANK_SIZE);
        if (rd < 0 || rd != MEMPAK_BANK_SIZE) {
            snprintf(failure_message_note, sizeof(failure_message_note), "Failed to read Controller Pak bank %d (err=%d)", b, (rd < 0) ? errno : -1);
            error_message_displayed = true;
            if (used_scratch) {
                scratch_free(bankbuf);
            } else {
                free(bankbuf);
            }
            fclose(fp);
            return;
        }

        size_t wr = fwrite(bankbuf, 1, MEMPAK_BANK_SIZE, fp);
        if (wr != MEMPAK_BANK_SIZE) {
            snprintf(failure_message_note, sizeof(failure_message_note), "Failed to write data to file: %s", complete_filename);
            error_message_displayed = true;
            if (used_scratch) {
                scratch_free(bankbuf);
            } else {
                free(bankbuf);
            }
            fclose(fp);
            return;
        }
    }

    if (used_scratch) {
        scratch_free(bankbuf);
    } else {
        free(bankbuf);
    }
    fclose(fp);
    process_complete_full_dump = true;
}

static void dump_single_note(int _port, int16_t selected_index) {
    snprintf(failure_message_note, sizeof(failure_message_note), " ");
    FILE *fSource, *fDump;
    char filename_note[256];

    get_rtc_time(string_datetime_cpak);

    snprintf(filename_note, sizeof(filename_note), "%s%s", CPAK_MOUNT_ARRAY[controller_selected], controller_pak_name_notes[selected_index]);

    fSource = fopen(filename_note, "rb");
    if (fSource == NULL) {
        snprintf(failure_message_note, sizeof(failure_message_note), "No note found in controller %d at slot %d!", controller_selected + 1, selected_index + 1);
        error_message_displayed = true;
        return;
    }

    char sanitized_note_name[MAX_STRING_LENGTH];
    cpakfs_sanitize_fat_filename(sanitized_note_name, controller_pak_name_notes[selected_index], sizeof(sanitized_note_name));
    snprintf(filename_note, sizeof(filename_note), "%s/%s_%s%s", CPAK_NOTES_PATH, sanitized_note_name, string_datetime_cpak, CPAK_NOTE_EXTENSION);

    fDump = fopen(filename_note, "wb");
    if (fDump == NULL) {
        snprintf(failure_message_note, sizeof(failure_message_note), "Unable to create dump file: %s", filename_note);
        fclose(fSource);
        error_message_displayed = true;
        return;
    }

    char buffer[4096];
    size_t bytesRead;

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), fSource)) > 0) {
        size_t bytesWritten = fwrite(buffer, 1, bytesRead, fDump);
        if (bytesWritten < bytesRead) {
            fclose(fSource);
            fclose(fDump);
            snprintf(failure_message_note, sizeof(failure_message_note), "Write error while copying to destination!");
            error_message_displayed = true;
            return;
        }
    }

    fclose(fSource);
    fclose(fDump);
    process_complete_note_dump = true;

}

static void delete_single_note(int _port, unsigned short selected_index) {
    snprintf(failure_message_note, sizeof(failure_message_note), " ");
    char filename_note[256];

    snprintf(filename_note, sizeof(filename_note), "%s%s", CPAK_MOUNT_ARRAY[controller_selected], controller_pak_name_notes[selected_index]);

    if (!file_exists(filename_note)) {
        snprintf(failure_message_note, sizeof(failure_message_note), "No note found in controller %d at slot %d!", controller_selected + 1, selected_index + 1);
        error_message_displayed = true;
        return;
    }

    remove(filename_note);

    if (file_exists(filename_note)) {
        snprintf(failure_message_note, sizeof(failure_message_note), "Failed to delete file: %s", filename_note);
        error_message_displayed = true;
        return;
    }  

    reset_vars();
    cpakfs_unmount(controller_selected);
    mounted[controller_selected] = false;
    has_pak[controller_selected] = false;
    corrupted[controller_selected] = false; 
    unmounted = true;
    process_complete_delete = true;
}

static bool is_one_of_process_complete() {
    return process_complete_full_dump 
    || process_complete_note_dump 
    || process_complete_format 
    || process_complete_delete
    || error_message_displayed;
}


static void process (menu_t *menu) {

    if (is_one_of_process_complete()) {
     
        if(process_complete_full_dump && menu->actions.enter) {
            sound_play_effect(SFX_ENTER);
            process_complete_full_dump = false;
            return;
        }

        if(process_complete_note_dump && menu->actions.enter) {
            sound_play_effect(SFX_ENTER);
            process_complete_note_dump = false;
            return;
        }

        if(process_complete_format && menu->actions.enter) {
            sound_play_effect(SFX_ENTER);
            process_complete_format = false;
            return;
        }

        if(process_complete_delete && menu->actions.enter) {
            sound_play_effect(SFX_ENTER);
            process_complete_delete = false;
            return;
        }

        if(error_message_displayed && menu->actions.enter) {
            sound_play_effect(SFX_ENTER);
            error_message_displayed = false;
            return;
        }
        
    } else {

        if (ui_components_context_menu_process(menu, &options_context_menu)) {
            return;
        }

        if (!show_complete_dump_confirm_message && 
            !show_complete_write_confirm_message && 
            !show_single_note_write_info_message &&
            !show_single_note_dump_confirm_message &&
            !show_single_note_delete_confirm_message &&
            !show_format_controller_pak_confirm_message) {
            if(menu->actions.go_left) {
                sound_play_effect(SFX_SETTING);
                controller_selected = ((controller_selected - 1) + 4) % 4;
                reset_vars();
            } else if (menu->actions.go_right) {
                sound_play_effect(SFX_SETTING);
                controller_selected = ((controller_selected + 1) + 4) % 4;
                reset_vars();
            } else if (menu->actions.settings && has_mem) {
                sound_play_effect(SFX_SETTING);
                ui_components_context_menu_show(&options_context_menu);
            }
        }

        check_accessories(controller_selected);

        populate_list_cpakfs();

        if (has_mem && !corrupted_pak) {

            // Pressing A : dump the Controller Pak
            if (menu->actions.enter && 
                use_rtc && 
                cpakfs_stats.pages.used > 0 &&
                !show_complete_dump_confirm_message && 
                !show_complete_write_confirm_message &&
                !show_single_note_write_info_message &&
                !show_single_note_dump_confirm_message &&
                !show_single_note_delete_confirm_message &&
                !show_format_controller_pak_confirm_message) {
                sound_play_effect(SFX_ENTER);
                show_complete_dump_confirm_message = true;
                return;
            } 

            // Pressing Z: dump a single note
            else if (menu->actions.context &&
                use_rtc && 
                note_count() > 0 &&
                !show_complete_write_confirm_message &&
                !show_single_note_write_info_message &&
                !show_complete_dump_confirm_message &&
                !show_single_note_dump_confirm_message &&
                !show_single_note_delete_confirm_message &&
                !show_format_controller_pak_confirm_message) {
                sound_play_effect(SFX_ENTER);
                show_single_note_dump_confirm_message = true;
                return;
            }

            if (show_complete_dump_confirm_message && 
                !show_complete_write_confirm_message &&
                !show_single_note_write_info_message &&
                !show_single_note_dump_confirm_message &&
                !show_single_note_delete_confirm_message &&
                !show_format_controller_pak_confirm_message) {
                if (menu->actions.enter) {
                    sound_play_effect(SFX_ENTER);
                    show_complete_dump_confirm_message = false;
                    start_complete_dump = true;
                } else if (menu->actions.back) {
                    sound_play_effect(SFX_EXIT);
                    show_complete_dump_confirm_message = false;
                }
                return;
            } else if (show_complete_write_confirm_message &&
                !show_single_note_write_info_message &&
                !show_complete_dump_confirm_message &&
                !show_single_note_dump_confirm_message &&
                !show_single_note_delete_confirm_message &&
                !show_format_controller_pak_confirm_message) {
                if (menu->actions.back) {
                    sound_play_effect(SFX_EXIT);
                    show_complete_write_confirm_message = false;                    
                }
                return;
            } else if (show_single_note_write_info_message &&
                !show_complete_write_confirm_message &&
                !show_complete_dump_confirm_message &&
                !show_single_note_dump_confirm_message &&
                !show_single_note_delete_confirm_message &&
                !show_format_controller_pak_confirm_message) {
                if (menu->actions.back) {
                    sound_play_effect(SFX_EXIT);
                    show_single_note_write_info_message = false;                    
                }
                return;
            } else if (show_single_note_dump_confirm_message && 
                !show_complete_dump_confirm_message &&
                !show_complete_write_confirm_message &&
                !show_single_note_write_info_message &&
                !show_single_note_delete_confirm_message &&
                !show_format_controller_pak_confirm_message) {
                if (menu->actions.enter) {
                    sound_play_effect(SFX_ENTER);
                    show_single_note_dump_confirm_message = false;
                    start_single_note_dump = true;
                } else if (menu->actions.back) {
                    sound_play_effect(SFX_EXIT);
                    show_single_note_dump_confirm_message = false;                    
                } else if (menu->actions.go_up) {
                    sound_play_effect(SFX_CURSOR);
                    index_selected = dec_index_note(index_selected);
                } else if (menu->actions.go_down) {
                    sound_play_effect(SFX_CURSOR);
                    index_selected = inc_index_note(index_selected);
                }
                return;
            }  else if (show_single_note_delete_confirm_message && 
                !show_complete_dump_confirm_message &&
                !show_complete_write_confirm_message &&
                !show_single_note_write_info_message &&
                !show_single_note_dump_confirm_message &&
                !show_format_controller_pak_confirm_message) {
                if (menu->actions.enter) {
                    show_single_note_delete_confirm_message = false;
                    sound_play_effect(SFX_ENTER);
                    start_single_note_delete = true;
                } else if (menu->actions.back) {
                    show_single_note_delete_confirm_message = false;
                    sound_play_effect(SFX_EXIT);
                } else if (menu->actions.go_left) {
                    sound_play_effect(SFX_CURSOR);
                    index_selected = dec_index_note(index_selected);
                } else if (menu->actions.go_right) {
                    sound_play_effect(SFX_CURSOR);
                    index_selected = inc_index_note(index_selected);
                }
                return;
            } else if (show_format_controller_pak_confirm_message && 
                !show_complete_dump_confirm_message &&
                !show_complete_write_confirm_message &&
                !show_single_note_write_info_message &&
                !show_single_note_dump_confirm_message &&
                !show_single_note_delete_confirm_message) {
                if (menu->actions.enter) {
                    sound_play_effect(SFX_ENTER);
                    show_format_controller_pak_confirm_message = false;
                    start_format_controller_pak = true;
                } else if (menu->actions.back) {
                    sound_play_effect(SFX_EXIT);
                    show_format_controller_pak_confirm_message = false;                    
                } 
                return;
            }
        } else if (has_mem && corrupted_pak) {

            if (!show_format_controller_pak_confirm_message) {
                if (menu->actions.enter) {
                    sound_play_effect(SFX_ENTER);
                    show_format_controller_pak_confirm_message = true;
                }
            } else {
                if (menu->actions.enter) {
                    sound_play_effect(SFX_ENTER);
                    show_format_controller_pak_confirm_message = false;
                    start_format_controller_pak = true;
                } else if (menu->actions.back) {
                    sound_play_effect(SFX_EXIT);
                    show_format_controller_pak_confirm_message = false;
                } 
            }
        }
    }
}



/**
 * @brief Whether a dialog owned by this pane is currently on screen.
 *
 * While one is open the pane keeps focus, so B dismisses the dialog rather
 * than returning to the category rail.
 */
static bool modal_open (void) {
    return is_one_of_process_complete()
        || options_context_menu.row_selected >= 0
        || show_complete_dump_confirm_message
        || show_single_note_dump_confirm_message
        || show_single_note_delete_confirm_message
        || show_format_controller_pak_confirm_message
        || show_complete_write_confirm_message
        || show_single_note_write_info_message
        || start_complete_dump
        || start_single_note_dump
        || start_single_note_delete
        || start_format_controller_pak;
}

/**
 * @brief Run whichever long running Pak operation was requested last frame.
 *
 * The request is raised while the "Working..." dialog is on screen so that the
 * dialog is visible for the duration of the operation.
 *
 * @return true when an operation ran.
 */
static bool execute_pending (void) {
    if (start_complete_dump) {
        start_complete_dump = false;
        if (cpakfs_stats.pages.used <= 0) {
            snprintf(
                failure_message_note,
                sizeof(failure_message_note),
                "No data found on Controller Pak on controller %d!",
                controller_selected + 1
            );
            error_message_displayed = true;
        } else {
            dump_complete_cpak(controller_selected);
        }
        return true;
    }
    if (start_single_note_dump) {
        start_single_note_dump = false;
        dump_single_note(controller_selected, index_selected);
        return true;
    }
    if (start_single_note_delete) {
        start_single_note_delete = false;
        delete_single_note(controller_selected, index_selected);
        return true;
    }
    if (start_format_controller_pak) {
        start_format_controller_pak = false;
        format_controller_pak();
        return true;
    }
    return false;
}

/**
 * @brief Number of notes on the mounted Pak.
 */
static int note_count (void) {
    int count = 0;

    if (!has_mem || corrupted_pak) {
        return 0;
    }

    for (int i = 0; i < MAX_NUM_NOTES; i++) {
        if (cpakfs_path_strings[i].filename[0] != '\0') {
            count++;
        }
    }

    return count;
}

static void pane_leave (menu_t *menu) {
    unmount_all_cpakfs();
    reset_vars();

    for (int i = 0; i < 4; i++) {
        mounted[i] = false;
        has_pak[i] = false;
        corrupted[i] = false;
        memset(&stats_per_port[i], 0, sizeof(stats_per_port[i]));
    }
}

static bool pane_process (menu_t *menu) {
    if (execute_pending()) {
        return true;
    }

    if (menu->actions.back && !modal_open()) {
        return false;
    }

    if (!modal_open()) {
        int max_scroll = MAX(note_count() - CPAK_PANE_NOTES, 0);

        note_scroll = MIN(note_scroll, max_scroll);
        if (menu->actions.go_up && note_scroll > 0) {
            note_scroll--;
        } else if (menu->actions.go_down && note_scroll < max_scroll) {
            note_scroll++;
        }
    }

    process(menu);

    return true;
}

static void pane_draw (menu_t *menu, bool focused) {
    char controller[32];
    char accessory[48];
    char free_space[32];
    int port = focused ? controller_selected : 0;
    bool present = focused
        ? has_mem
        : joypad_get_accessory_type(port) == JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK;
    bool corrupted = focused && corrupted_pak;
    int y = SETTINGS_PANE_Y0 + 4;


    snprintf(controller, sizeof(controller), "Controller %d", port + 1);
    snprintf(
        accessory, sizeof(accessory), "%s",
        corrupted ? "Controller Pak (corrupted)" : format_accessory_name(joypad_get_accessory_type(port))
    );
    if (focused && present && !corrupted) {
        snprintf(
            free_space, sizeof(free_space), "%d / %d",
            cpakfs_stats.pages.total - cpakfs_stats.pages.used, cpakfs_stats.pages.total
        );
    } else {
        snprintf(free_space, sizeof(free_space), "--");
    }

    ui_components_settings_row_draw(y, "Controller", controller, focused);
    y += SETTINGS_ROW_HEIGHT;
    ui_components_settings_row_draw(y, "Accessory", accessory, false);
    y += SETTINGS_ROW_HEIGHT;
    ui_components_settings_row_draw(y, "Free blocks", free_space, false);
    y += SETTINGS_ROW_HEIGHT;

    if (!focused || !present || corrupted) {
        ui_components_text_draw(
            SETTINGS_PANE_X0 + 28, y + 16, SETTINGS_PANE_X1 - SETTINGS_PANE_X0 - 56, 86,
            corrupted ? STL_YELLOW : STL_GRAY, ALIGN_LEFT, WRAP_WORD,
            corrupted
                ? "The Pak filesystem is damaged. Press Start for formatting options."
                : "Insert a Controller Pak, then use Left / Right to choose its controller port."
        );
        return;
    }

    ui_components_text_draw(
        SETTINGS_PANE_X0 + 20, y + 8, SETTINGS_PANE_X1 - SETTINGS_PANE_X0 - 40, 24,
        STL_GRAY, ALIGN_LEFT, WRAP_NONE, use_rtc ? "NOTES" : "NOTES (No RTC)"
    );
    y += 34;

    int total = note_count();
    int shown = 0;
    int skipped = 0;

    for (int i = 0; (i < MAX_NUM_NOTES) && (shown < CPAK_PANE_NOTES); i++) {
        char slot[16];
        char details[96];

        if (cpakfs_path_strings[i].filename[0] == '\0') {
            continue;
        }
        if (skipped++ < note_scroll) {
            continue;
        }

        snprintf(slot, sizeof(slot), "Note %d", i + 1);
        ui_components_settings_row_draw(y, slot, cpakfs_path_strings[i].filename, false);
        y += SETTINGS_ROW_HEIGHT;
        snprintf(details, sizeof(details), "Code: %s  Ext.: %s  Blocks used: %s",
            cpakfs_path_strings[i].gamecode, cpakfs_path_strings[i].ext,
            controller_pak_name_notes_bank_size[i]);
        ui_components_text_draw(SETTINGS_PANE_X0 + 18, y + 4,
            SETTINGS_PANE_X1 - SETTINGS_PANE_X0 - 36, SETTINGS_ROW_HEIGHT,
            STL_GRAY, ALIGN_LEFT, WRAP_NONE, details);
        y += SETTINGS_ROW_HEIGHT;
        shown++;
    }

    if (shown == 0) {
        ui_components_text_draw(
            SETTINGS_PANE_X0 + 28, y, SETTINGS_PANE_X1 - SETTINGS_PANE_X0 - 56, 48,
            STL_GRAY, ALIGN_LEFT, WRAP_WORD, "This Pak has no notes."
        );
    } else if (total > CPAK_PANE_NOTES) {
        char range[32];

        snprintf(range, sizeof(range), "Notes %d-%d of %d", note_scroll + 1, note_scroll + shown, total);
        ui_components_text_draw(
            SETTINGS_PANE_X0 + 20, SETTINGS_PANE_Y1 - 26, SETTINGS_PANE_X1 - SETTINGS_PANE_X0 - 40, 24,
            STL_GRAY, ALIGN_RIGHT, WRAP_NONE, range
        );
    }
}

static void pane_overlay (menu_t *menu) {
    ui_components_context_menu_draw(&options_context_menu);

    if (error_message_displayed) {
        ui_components_messagebox_draw(
            "Error: %s\n\n"
            "Press A to continue.",
            failure_message_note
        );
    } else if (process_complete_format) {
        ui_components_messagebox_draw(
            "Controller Pak formatted.\n\n"
            "Press A to continue."
        );
    } else if (process_complete_full_dump) {
        ui_components_messagebox_draw(
            "Pak saved to:\n"
            "%s\n\n"
            "Press A to continue.",
            CPAK_PATH
        );
    } else if (process_complete_note_dump) {
        ui_components_messagebox_draw(
            "Note saved to:\n"
            "%s/notes\n\n"
            "Press A to continue.",
            CPAK_PATH
        );
    } else if (process_complete_delete) {
        ui_components_messagebox_draw(
            "Note %d deleted from Controller Pak.\n\n"
            "Press A to continue.",
            index_selected + 1
        );
    } else if (show_complete_dump_confirm_message && !start_complete_dump) {
        ui_components_messagebox_draw(
            "Do you want to backup the Controller Pak?\n\n"
            "A: Yes        B: No"
        );
    } else if (show_single_note_dump_confirm_message && !start_single_note_dump) {
        ui_components_messagebox_draw(
            "Which note would you like to backup?\n\n"
            "Note selected: N.%-2.2d\n\n"
            "A: Select    B: No\n"
            "▼▲: Select note number",
            index_selected + 1
        );
    } else if (show_single_note_delete_confirm_message && !start_single_note_delete) {
        ui_components_messagebox_draw(
            "Which note would you like to delete?\n\n"
            "Note selected: N.%-2.2d\n\n"
            "A: Delete    B: No\n"
            "Left / Right: Select note number",
            index_selected + 1
        );
    } else if (show_format_controller_pak_confirm_message && !start_format_controller_pak) {
        ui_components_messagebox_draw(
            "Do you want to format the Controller pak?\n\n"
            "A: Yes        B: No"
        );
    } else if (show_complete_write_confirm_message) {
        ui_components_messagebox_draw(
            "To write a complete backup, browse to a file"
            " with the extension \".mpk\" or \".pak\" in the menu filebrowser.\n\n"
            "B: Back"
        );
    } else if (show_single_note_write_info_message) {
        ui_components_messagebox_draw(
            "To write a single note, browse to a file"
            " with the extension \".mpkn\" or \".paknote\" in the menu filebrowser.\n\n"
            "B: Back"
        );
    } else if (start_complete_dump || start_single_note_dump
            || start_single_note_delete || start_format_controller_pak) {
        ui_components_messagebox_draw("Working...");
    }
}

static void pane_enter (menu_t *menu) {
    ctr_p_data_loop = false;
    controller_selected = 0;
    note_scroll = 0;
    reset_vars();
    unmount_all_cpakfs();
    unmounted = true;

    for(int i = 0; i < 4; i++){
        mounted[i] = false;
        has_pak[i] = false;
        corrupted[i] = false;
        memset(&stats_per_port[i], 0, sizeof(stats_per_port[i]));
    }

    use_rtc = menu->current_time >= 0 ? true : false;

    directory_create(CPAK_PATH);
    directory_create(CPAK_NOTES_PATH);

    ui_components_context_menu_init(&options_context_menu);
    free_controller_pak_name_notes();
}

static bool pane_blocks_tabs (menu_t *menu) {
    (void) menu;
    return modal_open();
}

static const char *pane_hint (menu_t *menu, settings_hint_t slot) {
    switch (slot) {
        case SETTINGS_HINT_LEFT:
            if (has_mem && corrupted_pak) {
                return "A: Format Pak\nB: Categories";
            }
            return use_rtc && has_mem && !corrupted_pak && cpakfs_stats.pages.used > 0
                ? "A: Backup whole Pak\nB: Categories"
                : "B: Categories\n";
        case SETTINGS_HINT_CENTER:
            return note_count() > CPAK_PANE_NOTES
                ? "Left / Right: Controller\n▼▲ Notes | ◀L Tab R▶"
                : "Left / Right: Controller\n◀L Tab R▶";
        case SETTINGS_HINT_RIGHT:
            if (use_rtc && has_mem && !corrupted_pak && note_count() > 0) {
                return "Z: Backup a Note\nStart: Options";
            }
            return has_mem ? "\nStart: Options" : NULL;
        default: return NULL;
    }
}

const settings_pane_t settings_pane_controller_pak = {
    .label = "Controller",
    .enter = pane_enter,
    .leave = pane_leave,
    .process = pane_process,
    .draw = pane_draw,
    .overlay = pane_overlay,
    .blocks_tabs = pane_blocks_tabs,
    .hint = pane_hint,
};
