## Menu Settings

N64FlashcartMenu automatically creates a `config.ini` file in `sd:/menu/`, which
contains the options shown in the Menu pane of the rightmost Settings tab.
If required, you can manually adjust the file (required for some advanced settings) on the SD card using your computer.

Use `L` and `R` to switch main tabs. In Settings, use Up and Down to choose Menu,
Theme, Controller, Time, Information, Flashcart, or N64, then press `A` to enter the pane.
Press `B` to return to categories. Tab switching is blocked while a pane has a
modal confirmation or operation open.

In the Menu pane, use Up and Down to select a row and `A` or Left/Right to toggle
a boolean setting. Actions such as Reset to Defaults, clearing Autoload ROM, and
clearing Grid Cache require `A`. Default Directory and Grid Library are
read-only status rows.

### Autoload ROM

To configure autoload, open a ROM's details from the file browser, press `R`, and
choose "Set ROM to autoload". The menu will load that ROM on subsequent boots.
Hold `Start` while the menu boots to disable autoload and clear the saved target.
The Autoload ROM row in the Menu pane shows the active ROM's path, truncated with
an ellipsis to fit, or "None" when disabled. Select it and press `A` to disable
autoload and clear the saved target immediately. Left and Right do not change it;
use the ROM options to choose a new target.

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

### Global Saves Folder

Controls whether N64FlashcartMenu keeps every save in one global `/saves` folder at the root of the SD card. This setting is OFF by default.
ON: saves are stored as `/saves/<ROM file name>.sav`, whichever folder the ROM is launched from. Copies of the same ROM file in different folders (for example a favourites folder and an A-Z folder) therefore share one save.
OFF: saves are stored per folder, as described in Use Save Folders, so each copy of a ROM keeps its own save.

### Migrate Old Saves

Only used when Global Saves Folder is ON. This setting is OFF by default.
ON: when a ROM has no global save yet but still has one in its own location (see Use Save Folders), that save is moved into the global folder the first time the ROM is launched, so existing progress carries over.
OFF: saves in the per-folder locations are left where they are, and a ROM without a global save starts a new one.
Saves that were moved to the global folder are not moved back when either setting is turned OFF.

### Sound Effects

The menu has default sound effects to improve the user experience. See the [sound documentation](./40_sound.md) for details. This setting is OFF by default.

### Themes

The Theme pane contains Simple and Hardware presets plus one editable Custom
palette. Presets use static four-corner gradients or a solid background.
Use Up/Down to choose a family, then A or Right to open it. Up/Down previews its
presets; A selects one and returns to categories. B or Left returns to families
and restores the previous theme. Press Start on a preset to copy its palette
into Custom. Leaving the Theme pane saves the current selection and Custom palette.

In Custom, choose Hue Shift and use Left/Right to adjust all colors by five
degrees. The remaining rows edit border, panel, text, selection, and the four
gradient corners. Press A to edit a color, Left/Right to choose R, G, or B, and
Up/Down to change the value. Panel also offers A (alpha): 0 is transparent and
255 is opaque. A or B finishes editing. A black-and-white outline marks the
focused row. Z hides the menu to preview the background; B or Z restores it.

To use an image, open a PNG or JPEG in Files and press A twice to confirm it as
the background. This copies the current palette into Custom and selects the
image background. Custom hue changes also affect the image. Menu → Remove
Background removes the saved image and restores Custom's static gradient while
keeping its colors.

The selected theme is stored as `theme` in `[menu]`. `[custom_theme]` stores the
preset copied into Custom (`base`), its exact RGBA `palette`, `hue_shift`, and
whether the saved background image is enabled (`background_image`). Settings
files from before themes existed keep showing a previously set background image.

### Grid

These settings are hidden when the SD card has no `menu/metadata` folder, since
Grid is too.

**Boot Into** in Settings > Menu selects **Grid** or **Files**. It defaults to
Files and does not override ROM autoload. The corresponding `[menu]` INI key is
`boot_into_grid` (`false` for Files, `true` for Grid).

**Grid Library** shows the Grid library directory, stored as `grid_directory`
independently of `default_directory`. When absent, it inherits
`default_directory`. To change it, open the directory in Files, press `Z`, and
select **Set current directory as Grid library**. **Grid Cache** shows how many
ROMs Grid has indexed; press `A` to delete the index, including the tile arrangement and view choices. Grid
rebuilds it the next time it opens.
See [Grid](34_grid.md) for controls, artwork, and indexing.
