# Grid

Grid is an artwork view of the N64 ROMs in one library directory. It is the
first of the main tabs: Grid, Files, History, Favorites, and Settings.

Grid needs the `menu/metadata` folder described in
[Game Pak and box art](19_gamepak_boxart.md). Without it, the Grid tab, its
settings, and **Set current directory as Grid library** are hidden, and the
menu boots into Files.

**Boot Into** in Settings > Menu opens Grid or Files at startup. Files is the
default. An enabled ROM autoload still takes precedence.

## Library

Grid indexes the `.z64`, `.n64`, `.v64`, and `.rom` files directly inside its
library directory, up to 512 ROMs. Subdirectories and `sc64menu` are not
included. To choose the library, open the directory in Files (press `A` on it
so its contents are listed), press `Z`, and select **Set current directory as
Grid library**. The option always uses the directory being shown, not the
highlighted entry, so pressing it with a subdirectory highlighted sets the
parent. Configurations without a library directory use the Files start
directory.

The index, artwork choice, sorting, grouping, and manual order are stored in
`menu/cache/grid.index`. When Grid first opens after startup, and after
deleting or extracting files in Files, it picks up added ROMs and drops removed
ones, showing a progress bar if reading new ROMs takes a while. Details of ROMs
already in the index are cached; after changing their metadata, select Settings >
Menu > **Grid Cache** and press A to delete the index. This also resets the tile
arrangement and view choices. If the library can't be read, Grid keeps its index
and the Grid Cache row shows **Scan failed**.

Titles, authors, release dates, and artwork come from the `menu/metadata`
layout described in [Game Pak and box art](19_gamepak_boxart.md), or from a
ROM's `.meta` companion or embedded metadata, in the same order ROM details uses.
Without metadata, Grid uses the ROM header title or filename and a placeholder.

On first opening, Grid selects the most recently played ROM.

## Controls

- D-pad or stick: move between tiles and pages.
- A: open ROM details, or expand an author group.
- Hold A and move: rearrange tiles in the ungrouped title order; release to save.
- Start: launch the selected ROM immediately.
- C-Up: title order, which keeps your arrangement. Press again to re-sort by title.
- C-Left: sort by release date, with unknown dates last.
- C-Right: group by the first listed author.
- Z: switch between box and cartridge artwork.
- B: leave an author group, or go to Files.
- L/R: switch tabs.

Inside an author group, C-Up and C-Left sort that group only.
