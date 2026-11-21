## File Browser

<!-- Could use a beauty screenshot here -->

The File Browser allows you to navigate and manage files on your N64 flashcart. Below are the key features and instructions on how to use the File Browser effectively.

### Features

- **File and folder navigation**: Browse through directories and files on your flashcart.
- **File operations**: Perform operations such as delete and show properties.
- **File information**: View detailed information about each file, including size and date modified.
- **Load files**: Load files from the file system.
- **Extract files**: Extract files from ZIP archives.
- **Switching tabs**: Switch between the file browser, favorites and history tabs.
- **Hide (system) files and folders**: Hides specific files/folders from view. See [menu settings documentation](./32_menu_settings.md#show-hidden-files).

### Usage Instructions

<!-- Maybe all the Control pages could be merged into this section? -->

1. **Navigating Files**:
   - Use the directional `Up` and `Down` buttons (`C-Up` and `C-Down` for fast scrolling) to move through the list of files and directories.
   - Press the `A` Button to open a directory or load a supported file.

2. **Performing File Operations**:
   - Highlight the file or directory you want to operate on.
   - Press the `Z` Button to open the operations menu.
   - Select the desired operation and follow the on-screen prompts:
     - **Show entry properties**: Shows file information for the highlighted entry; inside a ZIP archive it opens the extract screen instead.
     - **Delete selected entry**: Deletes the highlighted file, or the highlighted directory if it is empty.
     - **Extract selected entry**: Only inside a ZIP archive; extracts the highlighted file.
     - **Controller Pak manager**: Opens the Controller Pak pane of Settings.
     - **Set current directory as Files start**: Files opens in the directory currently being shown (not the highlighted entry) at startup. Stored as `default_directory`.
     - **Set current directory as Grid library**: Grid shows the ROMs directly inside the directory currently being shown (not the highlighted entry). Stored as `grid_directory`, and listed as **Grid Library** in Settings > Menu. Only present when the SD card has a `menu/metadata` folder. See [Grid](./34_grid.md#library).
   - To choose a directory with either "Set current directory" option, press `A` on it first so its contents are listed, then press `Z`.

3. **Viewing Settings menu**:
   - Use `L` and `R` to reach the Settings tab, then select a category and press `A`.

4. **Switching tabs**:
   - Press the `L` and `R` Buttons to switch between Files, History, Favorites, and Settings.

5. **Extract files**:
   - Press the `A` Button on a ZIP file to open the archive.
   - Navigate to the file you want to extract.
   - Press the `A` Button to open the file info and press `A` again to extract the file.
