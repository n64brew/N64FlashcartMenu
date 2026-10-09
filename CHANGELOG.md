# Release Notes

- For the SummerCart64, use the `sc64menu.n64` file in the root of your SD card.
- For the 64Drive, use the `menu.bin` file in the root of your SD card.
- For the ares emulator, use the `N64FlashcartMenu.n64` file.

## Release Notes 202x-vNext

- **New Features**
	- ~~Browser now allows hiding files and folders with hidden attributes set (thanks [Xeroxxx](https://github.com/Xeroxxx)).~~ Awaiting performance enhancement.
	- JPEG image decoding.
	- Added Custom background music support.
	- File browser filetype images.
	- Menu now dogfoods embedded metadata.
	- Menu now shows embedded metadata box/game/screenshot images in the boxart view.
	- Menu colour themes.


- **Bug Fixes**
	- Some users have reported crashes in Zelda OOT (anti-piracy checks), The boot function now resets the VI (mainly H-Sync) registers to fix the issue.
	- Certain Aleck64 conversions failed to boot.

- **Documentation**
	- Improve documents with new features.

- **Refactor**
	- Settings is now a tab.
	- Expansion Pak requirements now aligns with homebrew spec.
	- Generation of DFS has been reorganised to use folders rather than a flat filesystem.

- **Other**
	- Change the default BGM to a different (more subtle) tune. The original is still included in the ROM image.
	- Menu will now show an error if it detects a low voltage on startup.
	- Fast Reboot support has been disabled in favour of ROM autoload due to too many issues being reported.
	- Added ED64 OS3.06 firmware files.
	- Deprecated functions have been removed.

### Breaking changes
- None.

### Notes
- A new version of the SMSPlus64 emulator is available: https://github.com/fhoedemakers/smsplus64/releases/tag/v0.9

### Current known Issues
- Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu.
- PixelFX HDMI mods may need to be updated to latest FW to support display.

### Deprecation notices
- None.

## Release Notes 2026-09-12 - Tagged 0.3.4

- **New Features**
	- ~~Browser now allows hiding files and folders with hidden attributes set (thanks [Xeroxxx](https://github.com/Xeroxxx)).~~ Awaiting performance enhancement.

- **Bug Fixes**
	- Fix potential crashes in Zelda OOT, The boot function now resets the VI (mainly H-Sync) registers to fix the issue.

- **Documentation**

- **Refactor**

- **Other**

### Breaking changes
- None.

### Notes
- None.

### Current known Issues
- Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is fully implemented.
- PixelFX HDMI mods may need to be updated to latest FW to support display.

### Deprecation notices
- None.

## Release Notes 2026-08-26 - Tagged 0.3.3

- **New Features**
	- Adds Background music.
	- Adds ability to use infinite scroll within the file browser.
	- ROM override submenu now remembers current custom settings.
	- Added extra info to the flashcart information screen to show Button state and voltage on supported carts.
	- Experimental DD swap support.
	- Added console region to the system information screen.
	- Audio (MP3) player now supports FLAC and ID3 metadata (thanks [zelifcam](https://github.com/zelifcam)).
	- Add Emulator ROM override INI and extra emulator extension file type matches.
	- Add progress of save file creation on ROM load.
	- Add ability to override emulator configurations through INI file.
	- Add option to clear RDRAM on a per ROM basis (required for RTYI demo).
	- Add ROM additional metadata (Maximum number of simultaneous players that the game supports).
	- Add warning before loading a ROM requiring an Expansion Pak that isn't present for supported ROMs.
	
- **Bug Fixes**
	- CPak manager: Large Controller Paks with more than 123 pages are now shown properly.
	- CPak notes backup and restore now works with invalid FAT characters.
	- Move CA and KR ROM tv types to NTSC.

- **Documentation**
	- Improved DD section with latest information.
	- Improved MP3 player information.
	- Improved Emulator information with ROM overrides.
	- Minor fixes.

- **Refactor**
	- Optimized PNG handler to work on memory-constrained consoles (without an Expansion Pak).
	- Improved memory usage and OOB fixes across the whole codebase.

- **Other**
	- Added ED64 pseudo state for later consumption.
	

### Breaking changes
- None.

### Notes
- Progress has been made towards disk swapping, but it is still WiP.
- RTYI Demo requires the new "Clear RDRAM on boot" ROM override to work.

### Current known Issues
- Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is fully implemented.
- Some users have reported crashes in Zelda OOT (anti-piracy checks). Menu V0.2.0 works as expected.
- PixelFX HDMI mods may need to be updated to latest FW to support display.

### Deprecation notices
- None.


## Release Notes 2026-05-23 - Tagged 0.3.2

- **New Features**
	- Adds settings to hide cheat and save file types in the browser.
	- Adds ability to display embedded homebrew ROM metadata in ROM info.
	- Adds ability to display Commercial game metadata using ROM DB.
	- Menu settings now know what setting is currently applied.

- **Bug Fixes**
	- Neon64 1Mbit SRAM.
	- Potential buffer overflows.
	- Fixed an issue where large ROMs failed to load in certain circumstances.
	- Fixed a lockup when selecting a game in history when the ROM no longer exists.

- **Documentation**
	- Minor fixes.

- **Refactor**
	- PAL60 (using new libdragon support).
	- ROM view, Age ratings and other metadata now align and support homebrew metadata standard.
	- Menu credits.
	- Disk Drive, disk info view.
	- CPak manager, Added menu option to for notes restore.
	- Replace mini.c INI lib with custom implementation.
	- Browser highlight colour for better display on CRT.

- **Other**
	- Updated libDragon {preview} SDK.
	- Updated miniz lib.
	- Updated minimp3.
	- Add docfx devcontainer.
	- Remove rolling prerelease (all releases to main should be tagged).
	- Added AI instructions to repo.
	- Added an extra build option (run-debug-reboot) that aids debugging remotely without the need for uploading files to the SD card.
	

### Breaking changes
- (as of 2026-03-15) libdragon SDK (and this menu) now requires MI repeat mode support, (supported by latest Ares and Gopher64, A3D also works though needs the latest FW). 

### Notes
- (as of 2026-03-01) libdragon {preview} SDK now compiles ROMs that use EEPROM to conform with OG wait timings by default.
- Progress has been made towards disk swapping, but it is still WiP.

### Current known Issues
- Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
- Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is fully implemented.
- Some users have reported crashes in Zelda OOT (anti-piracy checks). Menu V0.2.0 works as expected.
- A user has reported that the menu fails to load RTYI demo 2. Workaround by not setting a background image.
- PixelFX HDMI mods may need to be updated to latest FW to support display.


### Deprecation notices
- None.


## Release Notes 2025-12-04 - Tagged 0.3.1

- **New Features**
	- Settings contexts now preset to the saved option.
	- Added latest Viewpoint64 final proto ROM to database.
	- Added Rumble PAK and Transfer PAK features to ROM info screen.

- **Bug Fixes**
	- Fixed MP3 Player crashes menu if the MP3 file's sample rate is less than 44100 hz and menu SFX are enabled.
	- Fixed game_code_path size that caused crash when loading homebrew boxart.
	- Fixed boot process which could lead to blank screens or crashes.
	- Fixed a potential issue that could happen when a RTC was not detected.


- **Documentation**
	- Moved ED64 documentation to [98_flashcart_wip.md](./docs/98_flashcart_wip.md)
	- Other minor fixes.

- **Refactor**
	- Output 4MB files as MB, rather than kB.
	- Improved icons for direction.
	- Controller Pak now selects notes using up/down rather than left/right.

- **Other**
	- Updated libDragon SDK.
	- Updated docker container to Trixy

### Breaking changes
- None.


### Current known Issues
- Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
- Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is implemented.
- Some users have reported crashes in Zelda OOT (anti piracy checks). Menu V0.2.0 works as expected.
- A user has reported that the menu crashes with a CPU exception. Menu V0.2.0 works as expected.


### Deprecation notices
- None.


## Release Notes 2025-11-15 - Tagged 0.3.0

- **New Features**
	- Added ability to hide save folders (on by default).
	- Added ability to reset the menu setting to default from the menu UI.
	- Updated the UI font to Firple-Bold which supports more characters.
	- Shows info message within the loading progress bar.
	- Add the ability to display ESRB age ratings (see [documentation](./docs/65_experimental.md)).
	- Add Beta Datel code GUI (see [documentation](./docs/13_datel_cheats.md)).
	- Add ability to load boxart from ROMs that use the homebrew header (see [documentation](./docs/19_gamepak_boxart.md)).
	- Add ability to extract files from ZIP archives (thanks [VicesOfTheMind](https://github.com/VicesOfTheMind)).
	- Add Alpha FEATURE_PATCHER_GUI_ENABLED (build flag to enable it).
	- Add Controller Pak manager (thanks [LuEnCam](https://github.com/LuEnCam))
	- Add Game art image switching (thanks [dpranker](https://github.com/dpranker))

- **Bug Fixes**
	- Fix ability to set the RTC via menu (Hotfixed in last release).
	- Fix Game ID (used by PixelFX HDMI mods) sent over Joybus is not working (Hotfixed in last release).
	- Fix GB / GBC emulator not saving in certain circumstances (Hotfixed in last release).
	- Fix issue with emulation of cold boot, as otherwise the FPU might start in an unexpected state.
	- Fix missing enum case for 1 Mbit SRAM saves (Hotfixed in last release).

- **Documentation**
	- Improved Emulator information for known working NES emulator version.
	- Updated experimental features to reflect feature change.
	- Added sounds documentation.
	- Updated autoload to reflect feature change.

- **Refactor**
	- Improve tab navigation by using any left/right control input and add cursor SFX.
	- Add ability for font style to be used in ui_components_main_text_draw and ui_components_actions_bar_text_draw.

- **Other**
	- Updated libDragon SDK.
	- Updated miniz library.
	- Updated Github templates.

### Breaking changes
* Deprecated "Autoload ROM" function was removed from menu (use `FEATURE_AUTOLOAD_ROM_ENABLED` as a build flag to re-enable it).
* Deprecated Boxart image handler was removed (see [documentation](./docs/19_gamepak_boxart.md) for new boxart link).
* ROM's that used custom CIC, TV and/or Save type set from the menu will need to re-set them, now uses "custom_boot" header within the ini file.


### Current known Issues
* Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
* Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is implemented.
* MP3 Player crashes menu if the MP3 file's sample rate is less than 44100 hz and menu SFX are enabled.
- Some users have reported crashes in Zelda OOT (anti piracy checks). Menu V0.2.0 works as expected.
- A user has reported that the menu crashes with a CPU exception. Menu V0.2.0 works as expected.


### Deprecation notices
* Boxart directory has changed to metadata directory.


## Release Notes 2025-03-31 - Tagged 0.2.0

- **New Features**
	- Introduced tabs in main menu for ROM favorites and recently played ROM history.
	- Introduced first run check to ensure users are aware of latest changes.
	- Introduced ability to turn off GUI loading bar.
	- BETA_FEATURE: Introduces ROM descriptions from files.
	- BETA_FEATURE: Enabled setting for fast ROM reboots on the SC64.
	- Add macOS metadata to hidden files.
	- Added settings schema version for future change versioning.
	- Added setting for PAL60 compatibility mode (see breaking changes).
	- BETA_FEATURE: Added setting for line doublers that need progressive output, enable using "force_progressive_scan" setting in `config.ini`.


- **Bug Fixes**
	- Menu sound FX issues (hissing, popping and white noise).
	- RTC not showing or setting correct date parameters in certain circumstances.
	- ~~GB / GBC emulator not saving in certain circumstances.~~


- **Documentation**
	- Re-orginised and improved user documentation.
	- Added a lot of doxygen compatible code comments.
	- Added project license.


- **Refactor**
	- RTC subsystem (align with libDragon improvements).
	- Boxart images (Deprecates old boxart image folder layout).
	- Settings (PAL60 compatibility, schema version, fast reboot, first run, progress bar).

- **Other**
	- Updated libDragon SDK.
	- Updated miniz library.

### Breaking changes
* ~~GB /GBC emulator changed save type to SRAM (from FRAM) to improve compatibility with Summercart64 (which only uses H/W compatible FRAM), this may break your ability to load existing saves.~~
* For similar PAL60 functionality, you may need to also enable the new "pal60_compatibility_mode" setting in `config.ini`.


### Current known Issues
* The RTC UI requires improvement (awaiting UI developer).
* Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
* Fast Rebooting a 64DD disk once will result in a blank screen. Twice will return to menu. This is expected until disk swapping is implemented.
* MP3 Player crashes menu if the MP3 file's sample rate is less than 44100 hz.


### Deprecation notices
* Autoload ROM's will be deprecated in favor of Fast Reboot in a future menu version.
* Old boxart images using filenames for game ID is deprecated and the compatibility mode will be removed in a future release.


## Release Notes 2025-01-10

- **Bug Fixes**
	- Fixed menu display (PAL60) by reverted libdragon to a known working point and re-applying old hacks.

### Current known Issues
* The RTC UI requires improvement (awaiting UI developer).
* Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
[Pre-release menu]:
* BETA_SETTING: PAL60 when using HDMI mods has regressed (awaiting libdragon fix).
* ALPHA_FEATURE: ED64 X Series detection does not occur properly (however this is not a problem as not tag released asset).
* ALPHA_FEATURE: ED64 V Series only supports loading ROMs (however this is not a problem as not tag released asset).


## Release Notes 2024-12-30

- **New Features**
	- Introduced menu sound effects for enhanced user experience (the default is off).
	- Added N64 ROM autoload functionality, allowing users to set a specific ROM to load automatically.
	- Added menu boot hotkey (hold `start` to return to menu when autoload is enabled).
	- Added context menu and settings management options GUI for managing various settings in `config.ini`.
	- Added functionality for editing the real-time clock (RTC) within the RTC menu view.
	- Improved flashcart info view for showing supported flashcart features and version.
	- Enhanced UI components with new drawing functions and improved organization.
	- Added emulator support for `SMS`, `GG`, and `CHF` ROMs.
	- Enhanced joypad input handling for menu actions, improving responsiveness.
	- Optimized boxart image loading from filesystem.
	- Improved various text to make the functionality more clear.

- **Bug Fixes**
	- Improved error handling in multiple areas, particularly in save loading and ROM management.
	- Enhanced memory management to prevent potential leaks during error conditions.
	- Fixed text flickering in certain circumstances.

- **Documentation**
	- Updated README and various documentation files to reflect new features and usage instructions.
	- Added detailed setup instructions for SD cards and menu customization.
	- Enhanced clarity in documentation for RTC settings and menu customization.
	- Improved organization and clarity of SD card setup instructions for various flashcarts.

- **Refactor**
	- Standardized naming conventions across UI components for better organization.
	- Restructured sound management and input handling for improved responsiveness.
	- Streamlined the loading state management for ROMs and disks within the menu system.
	- Improved clarity and usability of the developer guide and other documentation files.

### Current known Issues
* BETA_SETTING: PAL60 when using HDMI mods has regressed (awaiting libdragon fix).
* The RTC UI requires improvement (awaiting UI developer).
* Menu sound FX may not work properly when a 64 Disk Drive is also attached (work around: turn sound FX off).
* ALPHA_FEATURE: ED64 X Series detection does not occur properly (however this is not a problem as not tag released asset).
* ALPHA_FEATURE: ED64 V Series only supports loading ROMs (however this is not a problem as not tag released asset).

### Breaking changes
* Disk drive expansion ROMs are now loaded with `Z|L` instead of `R` to align with ROM info context menu (and future functionality).
