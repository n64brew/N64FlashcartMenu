#include <stdio.h>
#include <stdlib.h>

#include "ui_components.h"

typedef struct {
    const char *path;
    sprite_t *sprite;
} sprite_entry_t;

static sprite_entry_t sprite_entries[SPRITE_LIST_END] = {
    [SPRITE_FILETYPE_FOLDER]            = { "rom:/menu/sprites/filetype_folder.sprite" },
    [SPRITE_FILETYPE_COMPRESSED]        = { "rom:/menu/sprites/filetype_compressed.sprite" },
    [SPRITE_FILETYPE_N64ROM]            = { "rom:/menu/sprites/filetype_n64cart.sprite" },
    [SPRITE_FILETYPE_N64DISK]           = { "rom:/menu/sprites/filetype_n64disk.sprite" },
    [SPRITE_FILETYPE_MUSIC]             = { "rom:/menu/sprites/filetype_music.sprite" },
    [SPRITE_FILETYPE_TEXT]              = { "rom:/menu/sprites/filetype_text.sprite" },
    [SPRITE_FILETYPE_IMAGE]             = { "rom:/menu/sprites/filetype_image.sprite" },
    [SPRITE_FILETYPE_SAVE]              = { "rom:/menu/sprites/filetype_save.sprite" },
    [SPRITE_FILETYPE_UNKNOWN]           = { "rom:/menu/sprites/filetype_unknown.sprite" },
};

void ui_components_sprites_init(void) {
    for (int i = 0; i < SPRITE_LIST_END; i++) {
        sprite_entries[i].sprite = sprite_load(sprite_entries[i].path);
    }
}

void ui_components_sprite_draw (sprite_type_t sprite, float pos_x, float pos_y) {
    if (sprite >= SPRITE_LIST_END) {
        return;
    }

    sprite_t *current_sprite = sprite_entries[sprite].sprite;
    if (current_sprite == NULL) {
        return;
    }

    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_sprite_blit(current_sprite, pos_x, pos_y, &(rdpq_blitparms_t){
                    .scale_x = 1, .scale_y = 1,
                });
}
