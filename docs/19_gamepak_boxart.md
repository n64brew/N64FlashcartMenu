## Metadata and Game Art

### Commercial Game Metadata and Game Art
To use commercial N64 game art images and ROM metadata, place your files within the `sd:/menu/metadata/` folder.

> [!TIP]
> As a starting point, here is a link to the latest, supported menu metadata: [Metadata link](https://github.com/n64-tools/n64-flashcart-menu-metadata/releases), just extract the zip to the `sd:/menu/` folder.


#### Itterating through game art images
When added, extra images will be used if available to itterate:
- `boxart_front.png` (DEFAULT)
- `boxart_back.png`
- `boxart_top.png`
- `boxart_bottom.png`
- `boxart_left.png`
- `boxart_right.png`
- `gamepak_front.png`
- `gamepak_back.png`

#### Supported image sizes
Files must be in `PNG` or `JPEG` format and use the following dimensions:
* American/European N64 boxart sprites: 158x112
* Japanese N64 boxart sprites: 112x158
* 64DD boxart sprites: 129x112
* GamePak front and back sprites: 158x112
* Boxart top/bottom spines: 158x22
* Boxart left/right spines: 112x22

Metadata and Images will be loaded by directories using each character (case-sensitive) of the full 4-character Game Code (as identified in the menu ROM information):  
i.e. for GoldenEye NTSC USA (NGEE), this would be `sd:/menu/metadata/N/G/E/E/boxart_front.png`.  
i.e. for GoldenEye PAL (NGEP), this would be `sd:/menu/metadata/N/G/E/P/boxart_front.png`.

To improve compatibility between regions (as a fallback), you may exclude the region ID (last matched directory) for GamePaks to match with 3-character IDs instead:  
i.e. for GoldenEye, this would be `sd:/menu/metadata/N/G/E/boxart_front.png`.

On the ROM load screen, you can cycle through available images using the **D-pad left**/**C-pad left** and **D-pad right**/**C-pad right** buttons. The menu will display any available images from the list above, skipping images that don't exist.

### External Homebrew ROM Metadata and Game Art
ROMs that use the ["Advanced Homebrew ROM Header"](https://n64brew.dev/wiki/ROM_Header) Game ID (`xEDx`), and DO NOT contain embedded metadata are matched using a folder that uses the "Game Title" as contained within the ROM:
e.g. `sd:/menu/metadata/homebrew/{game title}/boxart_front.png`

### Embedded ROM Metadata
This menu aims to fully support ROM's that contain embedded metadata as specified [in the spec](https://n64brew.dev/wiki/ROM_Metadata)
The Metadata should be shown correctly but included images may not yet work as expected, especially if too large for the available memory.


### Rom information metadata
To show certain (metadata) information on the N64 ROM information screen, add a `.meta` file in the same directory as the game ROM file with the same ROM filename (but excluding the ROM extension i.e. [`.z64`, `.v64`, `.n64`] )
Although the filename is `.meta`, its content is based on an `ini` file.

> [!TIP]
> As a starting point, here is a link to the latest, menu metadata: [Metadata link](https://github.com/n64-tools/n64-flashcart-menu-metadata/releases), which requires regular and independent updates.

> [!TIP]
> A rom called `my N64 game rom.z64` will load Rom information metadata from `my N64 game rom.meta`.

and the following content under the file content header:

> [!WARNING]
> The line endings must be `LF`.

```ini
[meta]
```
Examples of the "key=values" are:

#### ROM minimum age rating
The minimum age rating of the game.  

In future, this can be used for content filtering.  
Any number can be used and the menu will try to align it with the [ESRB](https://www.esrb.org/) ratings, i.e.:

* age-rating=0  :- No age rating defined
* age-rating=1  :- Everyone
* age-rating=10 :- Everyone 10+
* age-rating=13 :- Teen
* age-rating=17 :- Mature
* age-rating=18 :- Adults Only

an example for an age rating of "Everyone 10+" would be:
```ini
[meta]
; Self-certified age rating for the game. This is a number
; identifying the minimum suggested age for a player (0-18).
; This allows readers to provide some kind of content filtering
; capability. You can follow ESBR/PEGI guidelines to get
; some suggestions on this.
age-rating=10
```
an `age-rating=11` or `age-rating=12` would also match the 10+ rating, where as `age-rating=13` would be matched as "Teen".

#### ROM release date
The release date of the game.
This will not yet be shown in the menu.  
```ini
[meta]
; Release date of the game. Use YYYY-MM-DD format.
release-date=2025-11-04
```

#### ROM Short description
A short description with a length of 120 characters.
```ini
[meta]
; Short description of the game, to be displayed in
; menu or ROM managers. Suggested maximum length is
; 120 chars.
short-desc=Shoot your way to the heaven, fighting against birds and aliens
```
