#include "../cart_load.h"
#include "boot/boot.h"
#include "utils/fs.h"
#include "../sound.h"
#include "views.h"


static const char *emu_nes_rom_extensions[] = { "nes", NULL };
static const char *emu_snes_rom_extensions[] = { "sfc", "smc", NULL };
static const char *emu_gameboy_rom_extensions[] = { "gb", NULL };
static const char *emu_gameboy_color_rom_extensions[] = { "gbc", NULL };
static const char *emu_sega_8bit_rom_extensions[] = { "sms", "gg", "sg", NULL };
static const char *emu_fairchild_channelf_rom_extensions[] = { "chf", NULL };
static const char *emu_sinclair_zxspectrum_rom_extensions[] = { "rzx", NULL };
static const char *emu_microsoft_msx_rom_extensions[] = { "mx1", "mx2", NULL};
static const char *emu_dev_rom_extensions[] = { "emu", NULL};


static cart_load_emu_type_t emu_type;

static char *format_emulator_name (cart_load_emu_type_t emulator_info) {
    switch (emulator_info) {
        case CART_LOAD_EMU_TYPE_NES:
            return "Nintendo Famicom (NES)";
        case CART_LOAD_EMU_TYPE_SNES:
            return "Nintendo Super Famicom (SNES)";
        case CART_LOAD_EMU_TYPE_GAMEBOY:
            return "Nintendo GAMEBOY";
        case CART_LOAD_EMU_TYPE_GAMEBOY_COLOR:
            return "Nintendo GAMEBOY Color";
        case CART_LOAD_EMU_TYPE_SEGA_GENERIC_8BIT:
            return "SEGA 8bit system";
        case CART_LOAD_EMU_TYPE_FAIRCHILD_CHANNELF:
            return "Fairchild Channel F";
        case CART_LOAD_EMU_TYPE_SINCLAIR_ZXSPECTRUM:
            return "Sinclair ZX Spectrum";
        case CART_LOAD_EMU_TYPE_MICROSOFT_MSX:
            return "Microsoft MSX";
        case CART_LOAD_EMU_TYPE_SNK_NEO_GEO:
            return "SNK Neo Geo AES / MVS";
        case CART_LOAD_EMU_TYPE_DEV:
            return "Development Emulator";
        default:
            return "Unknown";
    }
}


static void process (menu_t *menu) {
    if (menu->actions.enter) {
        menu->load_pending.emulator_file = true;
    } else if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER;
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    if (menu->load_pending.emulator_file) {
        ui_components_loader_draw(0.0f, NULL);
    } else {
        ui_components_layout_draw();

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "Load Emulated ROM\n"
        );

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n"
            "\n"
            "Emulated System: %s\n"
            "Rom Name: %s",
            format_emulator_name(emu_type),
            menu->browser.entry->name
        );

        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Load and run Emulated ROM\n"
            "B: Exit"
        );
    }

    rdpq_detach_show();
}

static void draw_progress (float progress) {
    surface_t *d = (progress >= 1.0f) ? display_get() : display_try_get();

    if (d) {
        rdpq_attach(d, NULL);

        ui_components_background_draw();

        ui_components_loader_draw(progress, "Loading emulated ROM...");

        rdpq_detach_show();
    }
}

static void load (menu_t *menu) {
    cart_load_err_t err = cart_load_emulator(menu, emu_type, draw_progress);

    if (err != CART_LOAD_OK) {
        menu_show_error(menu, cart_load_convert_error_message(err));
        return;
    }

    menu->next_mode = MENU_MODE_BOOT;
    menu->boot_params->device_type = BOOT_DEVICE_TYPE_ROM;
    menu->boot_params->tv_type = BOOT_TV_TYPE_PASSTHROUGH;
    menu->boot_params->detect_cic_seed = true;
    menu->boot_params->cheat_list = NULL;
    menu->boot_params->clear_rdram = false;
}


void view_load_emulator_init (menu_t *menu) {
    menu->load_pending.emulator_file = false;

    path_t *path = path_clone_push(menu->browser.directory, menu->browser.entry->name);

    if (file_has_extensions(path_get(path), emu_nes_rom_extensions)) {
        emu_type = CART_LOAD_EMU_TYPE_NES;
    } else if (file_has_extensions(path_get(path), emu_snes_rom_extensions)) {
        emu_type = CART_LOAD_EMU_TYPE_SNES;
    } else if (file_has_extensions(path_get(path), emu_gameboy_rom_extensions)) {
        emu_type = CART_LOAD_EMU_TYPE_GAMEBOY;
    } else if (file_has_extensions(path_get(path), emu_gameboy_color_rom_extensions)) {
        emu_type = CART_LOAD_EMU_TYPE_GAMEBOY_COLOR;
    } else if (file_has_extensions(path_get(path), emu_sega_8bit_rom_extensions)) {
        emu_type = CART_LOAD_EMU_TYPE_SEGA_GENERIC_8BIT;
    } else if (file_has_extensions(path_get(path), emu_fairchild_channelf_rom_extensions)) {
        emu_type = CART_LOAD_EMU_TYPE_FAIRCHILD_CHANNELF;
    } else if (file_has_extensions(path_get(path), emu_sinclair_zxspectrum_rom_extensions)) {
        emu_type = CART_LOAD_EMU_TYPE_SINCLAIR_ZXSPECTRUM;
    } else if (file_has_extensions(path_get(path), emu_microsoft_msx_rom_extensions)) {
        emu_type = CART_LOAD_EMU_TYPE_MICROSOFT_MSX;
    // } else if (file_has_extensions(path_get(path), emu_snk_neogeo_rom_extensions)) {
    //     emu_type = CART_LOAD_EMU_TYPE_SNK_NEO_GEO;
    } else if (file_has_extensions(path_get(path), emu_dev_rom_extensions)) {
        emu_type = CART_LOAD_EMU_TYPE_DEV;
    } else {
        menu_show_error(menu, "Unsupported ROM");
    }

    path_free(path);
}

void view_load_emulator_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);

    if (menu->load_pending.emulator_file) {
        menu->load_pending.emulator_file = false;
        load(menu);
    }
}
