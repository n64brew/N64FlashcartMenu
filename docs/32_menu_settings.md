## Menu Settings

N64FlashcartMenu automatically creates a `config.ini` file in `sd:/menu/`, which contains various settings that can be set within the menu's Settings editor.
If required, you can manually adjust the file (required for some advanced settings) on the SD card using your computer.

### Using the Settings Editor

Press `START` in the File Browser to open the settings menu, then select `Menu settings` to open the Settings Editor.

- Use `C or D-Pad Left` and `C or D-Pad Right` to switch between the General, Browser, Audio, and System tabs. Builds with beta settings enabled also have a Beta tab for beta-only options.
- Use `C or D-Pad Up` and `C or D-Pad Down` to select a setting.
- Press `A` to review and confirm a setting change; press `B` to cancel.
- Press `B` to return to the File Browser.
- Press `R` to open Settings Options, where you can reset settings or remove the background.

### Show Hidden Files

Shows any N64FlashcartMenu system-related and hidden files/folders. This setting is OFF by default.

Besides system-related files, the File Browser respects FAT hidden attributes, allowing you to hide files and folders from the file list. This is useful for organizing your SD card and keeping system-related, homebrew-related, or temporary files out of sight.

#### Setting Hidden Attribute on Windows

1. Right-click the file or folder you want to hide.
2. Select **Properties**.
3. In the **General** tab, check the **Hidden** checkbox.
4. Click **Apply** and then **OK**.

#### Setting Hidden Attribute on Linux

Use the `fatattr` command-line tool to manage FAT attributes:

```bash
# Set a file or folder as hidden
fatattr +h /path/to/file

# Remove the hidden attribute
fatattr -h /path/to/file

# View current attributes
fatattr /path/to/file
```

#### Setting Hidden Attribute on macOS

Use the `chflags` command-line tool to manage FAT attributes:

```bash
# Set a file or folder as hidden
chflags hidden /path/to/file

# Remove the hidden attribute
chflags nohidden /path/to/file

# View current attributes
ls -lO /path/to/file
```

### Use Save Folders

Controls whether N64FlashcartMenu should use `/saves` folders to store ROM save data. This setting is ON by default.
ON: ROM saves are saved in separate subfolders (called `\saves`, will create one `\saves` subfolder per folder).
OFF: ROM saves are saved alongside the ROM file.

### Sound Effects

The menu has default sound effects to improve the user experience. See the [sound documentation](./40_sound.md) for details. This setting is OFF by default.

### Background Music

Background Music turns menu music on or off and is OFF by default. Background Music Track selects between Flying Dreams, Warm Waves, Blue Horizon, and White Stones; Flying Dreams is selected by default.

### Fast ROM reboots

Certain flashcarts support the ability to use the N64 `RESET` button for re-loading the last game, rather than returning to the menu. When enabled (and if supported by your flashcart), the power switch must be toggled to return to the menu.

> [!TIP]
> if a USB cable is connected to the flashcart, the last game will continue to be re-loaded. If debugging the menu, make sure this option is off!

Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is implemented.
This setting is OFF by default.
