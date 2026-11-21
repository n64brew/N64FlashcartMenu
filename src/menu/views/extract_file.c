#include <miniz.h>
#include <miniz_zip.h>
#include <sys/utime.h>
#include "../sound.h"

#include "utils/fs.h"
#include "views.h"

static mz_zip_archive_file_stat st;

static size_t mz_zip_file_write_callback(void *pOpaque, mz_uint64 ofs, const void *pBuf, size_t n) {
    float progress = ofs / (float)st.m_uncomp_size;
    surface_t *d = (progress >= 1.0f) ? display_get() : display_try_get();
    n = fwrite(pBuf, 1, n, (FILE *)pOpaque);

    if (d) {
        rdpq_attach(d, NULL);

        ui_components_background_draw();

        ui_components_loader_draw(progress, "Extracting file...");  

        rdpq_detach_show();
    }

    return n;
}

static void extract(menu_t *menu) {
    path_t *path = path_clone(menu->browser.directory);
    path_pop(path);
    path_push(path, st.m_filename);

    path_t *dir = path_clone(path);
    path_pop(dir);

    if (file_exists(path_get(path))) {
        menu->browser.select_file = path_clone(path);
        menu_show_error(menu, "File already exists");
    } else if (directory_create(path_get(dir))) {
        menu_show_error(menu, "Failed to create directory");
    } else {
        FILE *file = fopen(path_get(path), "wb");
        if (file) {
            struct utimbuf mtime = { st.m_time, st.m_time };
            if (!mz_zip_reader_extract_to_callback(
                &menu->browser.zip,
                st.m_file_index,
                mz_zip_file_write_callback,
                file, 0)) {
                menu_show_error(menu, "Failed to extract file");
            }
            fclose(file);
            utime(path_get(path), &mtime);
            view_grid_library_changed();
            menu->browser.select_file = path_clone(path);
            menu->next_mode = MENU_MODE_BROWSER;
        } else {
            menu_show_error(menu, "Failed to create file");
        }
    }

    path_free(path);
    path_free(dir);
}

static void process (menu_t *menu) {
    if (menu->actions.enter && !st.m_is_directory && st.m_is_supported) {
        sound_play_effect(SFX_ENTER);
        menu->load_pending.extract_file = true;
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    if (menu->load_pending.extract_file) {
        ui_components_loader_draw(0.0f, NULL);
    } else {
        ui_components_layout_draw();

        zip_file_attributes_t zip_info = {
            true,
            st.m_is_encrypted,
            .compressed_size = st.m_comp_size,
            .crc32 = st.m_crc32
        };

        file_info_t info = {
            st.m_is_directory,
            st.m_time,
            st.m_uncomp_size,
            {0},
            zip_info,
            {0}
        };
        ui_components_file_info_draw(st.m_filename, &info);

        if (st.m_comment_size > 0) {
            ui_components_main_text_draw(
                STL_DEFAULT,
                ALIGN_LEFT, VALIGN_BOTTOM,
                " Comment:\n"
                "\n"
                "%s",
                st.m_comment
            );
        }

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            st.m_is_supported ? "A: Extract\nB: Exit" : "\nB: Exit"
        );
    }

    rdpq_detach_show();
}


void view_extract_file_init (menu_t *menu) {
    if (!mz_zip_reader_file_stat(&menu->browser.zip, menu->browser.entry->index, &st)) {
        menu_show_error(menu, "Couldn't obtain archived file information");
    }
}

void view_extract_file_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);

    if (menu->load_pending.extract_file) {
        menu->load_pending.extract_file = false;
        extract(menu);
    }
}
