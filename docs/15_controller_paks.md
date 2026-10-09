## Controller Paks
This feature still relies on a controller PAK being "plugged in" to a physical controller (port). The N64 hardware **CANNOT** emulate it from a flashcart.

The Controller Pak manager is in the Controller pane of the Settings tab.
For direct access, press `Z` on an entry in the file browser and choose
"Controller Pak manager". This opens the same pane, already focused.

> [!CAUTION]
> Mileage may vary when hot swapping paks without exiting and re-entering the screen (and may contain incorrect content), and/or re-powering the console.

Features:
- Full pak backup and restore (saved to `SD:/cpak_saves/`).
- Partial pak ('note') backup and restore (saved to `SD:/cpak_saves/notes/`).

> [!NOTE]
> Note backup filenames may contain `%XX` sequences (e.g. `%2A` for `*`) when the original note name includes characters that are invalid in FAT filenames.
> These sequences are always decoded on restore so the note is written back to the Controller Pak with its original name.

### Controller Pak Manager
Use the Controller pane to back up and manage the Pak, including full backups or
individual notes. Left and Right select a controller; `Start` opens its options.
Press `B` to return to Settings categories, or use `L` and `R` to switch tabs
when no modal operation is active.
![Backup Controller Pak](./images/cpak-manager.png "Backup Controller Pak confirmation") 

#### Restoring saves
To restore full backups or individual notes, browse to the saved file (usually contained within `SD:/cpak_saves/`) and follow the menu instructions.
