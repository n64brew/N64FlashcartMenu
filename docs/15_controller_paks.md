## Controller Paks
This feature still relies on a controller PAK being "plugged in" to a physical controller (port). The N64 hardware **CANNOT** emulate it from a flashcart.

The N64FlashcartMenu has a Controller Pak Manager accessed from the `Start` button within the main file browser.

> [!CAUTION]
> Mileage may vary when hot swapping paks without exiting and re-entering the screen (and may contain incorrect content), and/or re-powering the console.

Features:
- Full pak backup and restore (saved to `SD:/cpak_saves/`).
- Partial pak ('note') backup and restore (saved to `SD:/cpak_saves/notes/`).

> [!NOTE]
> Note backup filenames may contain `%XX` sequences (e.g. `%2A` for `*`) when the original note name includes characters that are invalid in FAT filenames.
> These sequences are always decoded on restore so the note is written back to the Controller Pak with its original name.

### Controller Pak Manager
Use the Controller Pak Manager (opened from the browser's `Start` menu) to manage the Controller Pak in the selected controller port.

- Use `C or D-Pad Left` and `C or D-Pad Right` to switch controller tabs.
- Use `C or D-Pad Up` and `C or D-Pad Down` to select a note.
- Press `A` to back up the selected note.
- Press `L` or `Z` to back up the whole Controller Pak.
- Press `R` to open options for deleting notes (entry specific), formatting, and restoring.

#### Restoring saves
To restore full backups or individual notes, browse to the saved file (usually contained within `SD:/cpak_saves/`) and follow the menu instructions.
