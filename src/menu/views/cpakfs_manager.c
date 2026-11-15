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

#define MAX_STRING_LENGTH 62

#define MEMPAK_BANK_SIZE 32768

#define CPAK_EXTENSION ".pak"   
#define CPAK_NOTE_EXTENSION ".paknote"

static bool use_rtc;
static char string_datetime_cpak[26];
static char failure_message_note[255];
static const char *controller_tabs[] = {
    "Controller 1",
    "Controller 2",
    "Controller 3",
    "Controller 4",
};

static int16_t controller_selected;
static int16_t index_selected;
static int note_count;

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
static char controller_pak_name_notes_bank_size[MAX_NUM_NOTES][6]; // Three-digit block count plus \0
static entry_t controller_note_entries[MAX_NUM_NOTES];
static char controller_note_names[MAX_NUM_NOTES][MAX_STRING_LENGTH + 32];

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
    note_count = 0;

    for (int i = 0; i < MAX_NUM_NOTES; ++i) {
        snprintf(controller_pak_name_notes[i], sizeof(controller_pak_name_notes[i]), " ");
        snprintf(controller_pak_name_notes_bank_size[i], sizeof(controller_pak_name_notes_bank_size[i]), " ");
        snprintf(cpakfs_path_strings[i].gamecode, sizeof(cpakfs_path_strings[i].gamecode), " ");
        snprintf(cpakfs_path_strings[i].pubcode, sizeof(cpakfs_path_strings[i].pubcode), " ");
        snprintf(cpakfs_path_strings[i].filename, sizeof(cpakfs_path_strings[i].filename), " ");
        snprintf(cpakfs_path_strings[i].ext, sizeof(cpakfs_path_strings[i].ext), " ");
    }
}

static void check_accessories(int controller) {
    bool was_present = has_pak[controller];

    joypad_accessory_type_t acc = joypad_get_accessory_type(controller);
    bool present = (acc == JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK);
    has_pak[controller] = present;

    if (!present) {
        show_single_note_dump_confirm_message = false;
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
    if (note_count == 0) {
        snprintf(failure_message_note, sizeof(failure_message_note), "No notes found on this Controller Pak.");
        error_message_displayed = true;
    } else {
        show_single_note_delete_confirm_message = true;
    }
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
        snprintf(controller_pak_name_notes_bank_size[index], sizeof(controller_pak_name_notes_bank_size[index]), "---");
    } else {
        snprintf(controller_pak_name_notes_bank_size[index], sizeof(controller_pak_name_notes_bank_size[index]), "%3.3d", size);
    }
    snprintf(controller_pak_name_notes[index], MAX_STRING_LENGTH, "%s", entry_name);
    parse_cpakfs_fullname(entry_name, &cpakfs_path_strings[index]);
    snprintf(
        controller_note_names[index],
        sizeof(controller_note_names[index]),
        "%02d. %-30s  %-4.4s  %-3.3s  %s",
        index + 1,
        cpakfs_path_strings[index].filename,
        cpakfs_path_strings[index].gamecode,
        cpakfs_path_strings[index].ext,
        controller_pak_name_notes_bank_size[index]
    );
    controller_note_entries[index] = (entry_t) {
        .name = controller_note_names[index],
        .type = ENTRY_TYPE_SAVE,
        .size = 0,
        .index = index,
    };
    note_count = index + 1;
}

static void populate_list_cpakfs() {
    if (has_mem && mounted[controller_selected] && !ctr_p_data_loop) {
        
        free_controller_pak_name_notes();

        int result = dir_findfirst(CPAK_MOUNT_ARRAY[controller_selected], &dir_entry);
        if (result == 0) {
            write_note_name_info_list(controller_selected, 0, dir_entry.d_name);

            int i = 1;
            while(dir_findnext(CPAK_MOUNT_ARRAY[controller_selected], &dir_entry) == 0) {
                write_note_name_info_list(controller_selected, i, dir_entry.d_name);
                i++;
                if (i >= MAX_NUM_NOTES) break;
            }

            ctr_p_data_loop = true;
        } else if (result == -1) {
            ctr_p_data_loop = true;
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

    surface_t *d = display_try_get();
    rdpq_attach(d, NULL);
    
    ui_components_messagebox_draw(
        "Backing up note:\n\n"
        "%s",
        controller_pak_name_notes[index_selected]
    );
    ui_components_loader_draw(0, "Saving Controller Pak note...");
    rdpq_detach_show();

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
            !show_single_note_dump_confirm_message &&
            !show_complete_write_confirm_message && 
            !show_single_note_write_info_message &&
            !show_single_note_delete_confirm_message &&
            !show_format_controller_pak_confirm_message) {
            if(menu->actions.go_left) {
                sound_play_effect(SFX_SETTING);
                controller_selected = ((controller_selected - 1) + 4) % 4;
                reset_vars();
                free_controller_pak_name_notes();
                index_selected = 0;
            } else if (menu->actions.go_right) {
                sound_play_effect(SFX_SETTING);
                controller_selected = ((controller_selected + 1) + 4) % 4;
                reset_vars();
                free_controller_pak_name_notes();
                index_selected = 0;
            } else if (menu->actions.go_up && note_count > 0) {
                int scroll_amount = menu->actions.go_fast ? 10 : 1;
                if (menu->settings.wrap_file_list_scrolling) {
                    index_selected = (index_selected - scroll_amount % note_count + note_count) % note_count;
                } else if (index_selected > 0) {
                    index_selected = (index_selected > scroll_amount) ? index_selected - scroll_amount : 0;
                }
                sound_play_effect(SFX_CURSOR);
            } else if (menu->actions.go_down && note_count > 0) {
                int scroll_amount = menu->actions.go_fast ? 10 : 1;
                if (menu->settings.wrap_file_list_scrolling) {
                    index_selected = (index_selected + scroll_amount) % note_count;
                } else if (index_selected < note_count - 1) {
                    index_selected += scroll_amount;
                    if (index_selected >= note_count) {
                        index_selected = note_count - 1;
                    }
                }
                sound_play_effect(SFX_CURSOR);
            } else if (menu->actions.back) {
                unmount_all_cpakfs();
                reset_vars();
                for(int i = 0; i < 4; i++){
                    mounted[i] = false;
                    has_pak[i] = false;
                    corrupted[i] = false;
                    memset(&stats_per_port[i], 0, sizeof(stats_per_port[i]));
                }
                sound_play_effect(SFX_EXIT);
                menu->next_mode = MENU_MODE_BROWSER;
            } else if (menu->actions.options && use_rtc && has_mem) {
                sound_play_effect(SFX_SETTING);
                ui_components_context_menu_show(&options_context_menu);
            }
        }

        check_accessories(controller_selected);

        populate_list_cpakfs();
        if (note_count == 0) {
            index_selected = 0;
        } else if (index_selected >= note_count) {
            index_selected = note_count - 1;
        }

        if (has_mem && !corrupted_pak) {

            // Pressing A backs up the selected note.
            if (menu->actions.enter && 
                use_rtc && 
                note_count > 0 &&
                !show_complete_dump_confirm_message && 
                !show_single_note_dump_confirm_message &&
                !show_complete_write_confirm_message &&
                !show_single_note_write_info_message &&
                !show_single_note_delete_confirm_message &&
                !show_format_controller_pak_confirm_message) {
                sound_play_effect(SFX_ENTER);
                show_single_note_dump_confirm_message = true;
                return;
            } 
            // Pressing L or Z backs up the whole Controller Pak.
            else if (menu->actions.lz_context && 
                use_rtc && 
                !show_complete_write_confirm_message &&
                !show_single_note_write_info_message &&
                !show_complete_dump_confirm_message &&
                !show_single_note_dump_confirm_message &&
                !show_single_note_delete_confirm_message &&
                !show_format_controller_pak_confirm_message) {
                sound_play_effect(SFX_ENTER);
                show_complete_dump_confirm_message = true;
                return;
            }

            if (show_complete_dump_confirm_message && 
                !show_single_note_dump_confirm_message &&
                !show_complete_write_confirm_message &&
                !show_single_note_write_info_message &&
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
                }
                return;
            } else if (show_complete_write_confirm_message &&
                !show_single_note_dump_confirm_message &&
                !show_single_note_write_info_message &&
                !show_complete_dump_confirm_message &&
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
            }  else if (show_single_note_delete_confirm_message && 
                !show_complete_dump_confirm_message &&
                !show_complete_write_confirm_message &&
                !show_single_note_dump_confirm_message &&
                !show_single_note_write_info_message &&
                !show_format_controller_pak_confirm_message) {
                if (menu->actions.enter) {
                    show_single_note_delete_confirm_message = false;
                    sound_play_effect(SFX_ENTER);
                    start_single_note_delete = true;
                } else if (menu->actions.back) {
                    show_single_note_delete_confirm_message = false;
                    sound_play_effect(SFX_EXIT);
                }
                return;
            } else if (show_format_controller_pak_confirm_message && 
                !show_complete_dump_confirm_message &&
                !show_complete_write_confirm_message &&
                !show_single_note_dump_confirm_message &&
                !show_single_note_write_info_message &&
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
                if (menu->actions.back) {
                    sound_play_effect(SFX_EXIT);
                    menu->next_mode = MENU_MODE_BROWSER;
                } else if (menu->actions.enter) {
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

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    char status_text[64];
    if (!use_rtc) {
        snprintf(status_text, sizeof(status_text), "No RTC");
    } else if (!has_mem) {
        snprintf(status_text, sizeof(status_text), "No Controller Pak");
    } else if (corrupted_pak) {
        snprintf(status_text, sizeof(status_text), "Controller Pak corrupted");
    } else {
        snprintf(
            status_text,
            sizeof(status_text),
            "%d/%d free blocks",
            cpakfs_stats.pages.total - cpakfs_stats.pages.used,
            cpakfs_stats.pages.total
        );
    }

    ui_components_tabs_draw(controller_tabs, 4, controller_selected, 132.0f);
    ui_components_layout_draw_tabbed();
    ui_components_file_list_draw(
        controller_note_entries,
        note_count,
        index_selected,
        MAX_NUM_NOTES,
        false,
        "No.  Name                          Code  Ext   Blocks",
        has_mem ? NULL : "Not Available"
    );

    ui_components_context_menu_draw(&options_context_menu);

    menu_font_type_t note_backup_style =
        (has_mem && !corrupted_pak && note_count > 0 && use_rtc) ? STL_DEFAULT : STL_GRAY;
    menu_font_type_t pak_backup_style =
        (has_mem && !corrupted_pak && use_rtc) ? STL_DEFAULT : STL_GRAY;

    ui_components_actions_bar_text_draw(note_backup_style,
        ALIGN_LEFT, VALIGN_TOP,
        "A: Backup note\n"
        "B: Exit"
    );

    ui_components_actions_bar_text_draw(pak_backup_style,
        ALIGN_RIGHT, VALIGN_TOP,
        "L|Z: Backup Pak"
    );

    ui_components_actions_bar_text_draw(
        (use_rtc && has_mem) ? STL_DEFAULT : STL_GRAY,
        ALIGN_RIGHT, VALIGN_TOP,
        "\n"
        "R: Options"
    );

    ui_components_actions_bar_text_draw(STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "◀ Tabs ▶ | ▼/▲: Notes\n"
        "%s",
        status_text
    );

    if (error_message_displayed) {
        ui_components_messagebox_draw(
            "Error: %s\n\n"
            "Press A to continue.",
            failure_message_note
        );   
    }

    if (process_complete_format) {
        ui_components_messagebox_draw(
            "Controller Pak formatted.\n\n"
            "Press A to continue."
        );   
    }

    if (process_complete_full_dump) {
        ui_components_messagebox_draw(
            "Pak saved to:\n"
            "%s\n\n"
            "Press A to continue.",
            CPAK_PATH
        );   
    }

    if (process_complete_note_dump) {
        ui_components_messagebox_draw(
            "Note saved to:\n"
            "%s/notes\n\n"
            "Press A to continue.",
            CPAK_PATH
        );   
    }

    if (process_complete_delete) {
        ui_components_messagebox_draw(
            "Note %d deleted from Controller Pak.\n\n"
            "Press A to continue.",
            index_selected + 1
        );   
    }

    if (show_complete_dump_confirm_message && 
        !start_complete_dump) {
        ui_components_messagebox_draw(
            "Do you want to backup the Controller Pak?\n\n"
            "A: Yes        B: No"
        );   
    } else if (show_single_note_dump_confirm_message &&
        !start_single_note_dump) {
        ui_components_messagebox_draw(
            "Do you want to back up this note?\n\n"
            "%s\n\n"
            "A: Yes        B: No",
            controller_pak_name_notes[index_selected]
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
    }

    if (show_single_note_delete_confirm_message &&
        !start_single_note_delete) {
        ui_components_messagebox_draw(
            "Delete this note?\n\n"
            "%s\n\n"
            "A: Yes        B: No",
            controller_pak_name_notes[index_selected]
        );
    }

    if (show_format_controller_pak_confirm_message && 
        !start_format_controller_pak) {
        ui_components_messagebox_draw(
            "Do you want to format the Controller pak?\n\n"
            "A: Yes        B: No"
        );   
    }

    if (start_complete_dump) {

        if (cpakfs_stats.pages.used <= 0) {
            rdpq_detach_show();
            snprintf(failure_message_note, sizeof(failure_message_note), "No data found on Controller Pak on controller %d!", controller_selected + 1);
            error_message_displayed = true;
            start_complete_dump = false;
            return;

        } else {
            ui_components_loader_draw(0, "Saving Controller Pak...");
            rdpq_detach_show();
            dump_complete_cpak(controller_selected);
            start_complete_dump = false;
            return;
        }
    }

    if (start_single_note_dump) {
        rdpq_detach_show();
        dump_single_note(controller_selected, index_selected);
        start_single_note_dump = false;
        return;
    }

    if (start_single_note_delete) {
        rdpq_detach_show();
        delete_single_note(controller_selected, index_selected);
        start_single_note_delete = false;
        return;
    }

    if (start_format_controller_pak) {
        rdpq_detach_show();
        format_controller_pak();
        start_format_controller_pak = false;
        return;
    }
    
    rdpq_detach_show();
}

void view_controller_pakfs_init (menu_t *menu) {
    ctr_p_data_loop = false;
    controller_selected = 0;
    index_selected = 0;
    free_controller_pak_name_notes();
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
}

void view_controller_pakfs_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}
