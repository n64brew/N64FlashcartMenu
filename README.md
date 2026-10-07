![Build](https://github.com/polprzewodnikowy/N64FlashcartMenu/actions/workflows/build.yml/badge.svg)
![GitHub Org's stars](https://img.shields.io/github/stars/Polprzewodnikowy/N64FlashcartMenu)
[![Average time to resolve an issue](http://isitmaintained.com/badge/resolution/Polprzewodnikowy/N64FlashcartMenu.svg)](http://isitmaintained.com/project/Polprzewodnikowy/N64FlashcartMenu "Average time to resolve an issue")
[![Percentage of issues still open](http://isitmaintained.com/badge/open/Polprzewodnikowy/N64FlashcartMenu.svg)](http://isitmaintained.com/project/Polprzewodnikowy/N64FlashcartMenu "Percentage of issues still open")
[![#yourfirstpr](https://img.shields.io/badge/first--timers--only-friendly-blue.svg)](https://github.com/Polprzewodnikowy/N64FlashcartMenu/blob/main/CONTRIBUTING.md)

# N64FlashcartMenu
An open source menu for N64 flashcarts that aims to support as many as possible.  
This menu is not affiliated with any particular flashcart and does not necessarily expose all possible firmware features.

> [!TIP]
> Help sponsor development [NetworkFusion on Ko-Fi](https://ko-fi.com/networkfusion). Or submit your Pull Request.

> [!TIP]
> New users are invited to read the latest [Documentation / User Guide](./docs/00_index.md).

## Flashcart Support
This menu aims to support as many N64 flashcarts as possible.  
The current state of support is:

### Supported
* SummerCart64
* 64Drive

### Work in Progress
* EverDrive-64 (X and V series)
* ED64P (clones)

### Not yet planned
* Doctor V64
* PicoCart
* DaisyDrive
* EverDrive-64 Pro series


## Current (notable) menu features
* Fully Open Source.
* Loads all known N64 games, even if they are byteswapped.
* Fully emulates the 64DD and loads 64DD disks (SummerCart64 only).
* Emulator support (NES, SNES, GB, GBC, SMS, GG, CHF) ROMs.
* N64 ROM box art image support.
* Background image (PNG & JPEG) support.
* Comprehensive ROM save database (including homebrew headers).
* Comprehensive ROM information display.
* Real Time Clock support.
* Music playback (MP3 and FLAC) with id3 support.
* Menu background music and sound effects.
* N64 ROM autoload option (on reset).
* ROM history and favorites.  
* ROM Datel code editor.
* Zip archive browsing and file extraction.
* Controller Pak backup and restore (including individual notes).
* Game art image switching.
* Menu themes.


## Aims
* Support as many N64 Flashcarts as possible.
* Be open source, using permissively licensed third-party libraries.
* Be testable in an emulated environment (Ares).
* Encourage active development from community members and N64 FlashCart owners.
* Support as many common mods and features as possible (flashcart dependent).


## Flashcart specific information

### SummerCart64
Download the latest `sc64menu.n64` file from the [releases](https://github.com/Polprzewodnikowy/N64FlashcartMenu/releases/) page, then put it in the root directory of your SD card.  
  
> [!TIP]
> A quick video tutorial can be found here:
>
> [![Video tutorial](https://img.youtube.com/vi/IGX0XXf0wgo/default.jpg)](https://www.youtube.com/shorts/IGX0XXf0wgo)


### 64drive
* Ensure the cart has the latest [firmware](https://64drive.retroactive.be/support.php) installed.
* Download the latest `menu.bin` file from the [releases](https://github.com/Polprzewodnikowy/N64FlashcartMenu/releases/) page, then put it in the root directory of your SD card.


# Contributors
The features in this project were made possible by the [contributors](https://github.com/Polprzewodnikowy/N64FlashcartMenu/graphs/contributors).

# License
This project is released under the [GNU AFFERO GENERAL PUBLIC LICENSE](LICENSE.md) as compatible with all other dependent project licenses.  
Other license options may be available upon request with permissions of the original `N64FlashcartMenu` project authors / maintainers.  
* [Mateusz Faderewski / Polprzewodnikowy](https://github.com/Polprzewodnikowy)
* [Robin Jones / NetworkFusion](https://github.com/networkfusion)

# Open source software and licenses used
## Libraries
* [libdragon](https://github.com/DragonMinded/libdragon/tree/preview) - [UNLICENSE License](https://github.com/DragonMinded/libdragon/blob/preview/LICENSE.md)
* [libspng](https://github.com/randy408/libspng) - [BSD 2-Clause License](https://github.com/randy408/libspng/blob/master/LICENSE)
* [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo) - [BSD 3-Clause License](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md)
* [minimp3](https://github.com/lieff/minimp3) - [CC0 1.0 Universal](https://github.com/lieff/minimp3/blob/master/LICENSE)
* [dr_flac](https://github.com/mackron/dr_libs) - [MIT License](https://github.com/mackron/dr_libs/blob/master/LICENSE)
* [miniz](https://github.com/richgel999/miniz) - [MIT License](https://github.com/richgel999/miniz/blob/master/LICENSE)


## Sounds
See [License](https://pixabay.com/en/service/license-summary/) for the following sounds:
* [Cursor sound](https://pixabay.com/en/sound-effects/click-buttons-ui-menu-sounds-effects-button-7-203601/) by Skyscraper_seven (Free to use)
* [Actions (Enter, Back) sound](https://pixabay.com/en/sound-effects/menu-button-user-interface-pack-190041/) by Liecio (Free to use)
* [Error sound](https://pixabay.com/en/sound-effects/error-call-to-attention-129258/) by Universfield (Free to use)

See [License](https://creativecommons.org/licenses/by/4.0/) for the following sounds:
* [Background Music](https://www.playonloop.com/2017-music-loops/flying-dreams/) POL-flying-dreams-short
* [Background Music alternative](https://www.playonloop.com/2011-music-loops/warm-waves/) POL-warm-waves-short


## Emulators
* [neon64v2](https://github.com/hcs64/neon64v2) by *hcs64* - [ISC License](https://github.com/hcs64/neon64v2/blob/master/LICENSE.txt)
* [sodium64](https://github.com/Hydr8gon/sodium64) by *Hydr8gon* - [GPL-3.0 License](https://github.com/Hydr8gon/sodium64/blob/master/LICENSE)
* [gb64](https://github.com/lambertjamesd/gb64) by *lambertjamesd* - [MIT License](https://github.com/lambertjamesd/gb64/blob/master/LICENSE)
* [smsPlus64](https://github.com/fhoedemakers/smsplus64) by *fhoedmakers* - [GPL-3.0 License](https://github.com/fhoedemakers/smsplus64/blob/main/LICENSE)
* [Press-F-Ultra](https://github.com/celerizer/Press-F-Ultra) by *celerizer* - [MIT License](https://github.com/celerizer/Press-F-Ultra/blob/master/LICENSE)

## Fonts
* [Firple](https://github.com/negset/Firple) by *negset* - (SIL Open Font License 1.1)
