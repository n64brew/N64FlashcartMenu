## Experimental Features (Subject to change)

### Controller Paks
See: [Controller Paks](15_controller_paks.md)

### Cheats
See: [Cheats (Gameshark, etc.)](./13_datel_cheats.md)

### Customizing the font
The N64FlashcartMenu allows the ability to test new fonts or adding regional characters without recompiling the menu. However the font is explicitly linked to the currently used version of the libdragon SDK.  
Add a `font64` file to the `sd:/menu/` directory called `custom.font64`.

You can build a font64 file with `Mkfont`, one of `libdragon`'s tools. At the time of writing, you will need to obtain `libdragon`'s [preview branch artifacts](https://github.com/n64brew/libdragon/actions/workflows/build-tool-windows.yml) to find out a copy of the prebuilt Windows executable. [Read its related Wiki page](https://github.com/n64brew/libdragon/wiki/Mkfont) for usage information.

### Customizing the background music
The N64FlashcartMenu allows the ability to test new background music without recompiling the menu. However the music is explicitly linked to the currently used version of the libdragon SDK.
It is recommended to convert a wav to wav64 with compression level 1 and save to `sd:/menu/custom_bgm.wav64`

You can build a wav64 file with `Audioconv64`, one of `libdragon`'s tools. At the time of writing, you will need to obtain `libdragon`'s [preview branch artifacts](https://github.com/n64brew/libdragon/actions/workflows/build-tool-windows.yml) to find out a copy of the prebuilt Windows executable. [Read its related Wiki page](https://github.com/n64brew/libdragon/wiki/Audioconv64) for usage information.

### Fast ROM reboots
It can be currently be enabled by removing the flag `FLAGS ?= -DFEATURE_AUTOLOAD_ROM_ENABLED` from the makefile.
Certain flashcarts support the ability to use the N64 `RESET` button for re-loading the last game, rather than returning to the menu. When enabled (and if supported by your flashcart), the power switch must be toggled to return to the menu.

> [!TIP]
> if a USB cable is connected to the flashcart, the last game will continue to be re-loaded. If debugging the menu, make sure this option is off!

Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is implemented.
This setting is OFF by default.
