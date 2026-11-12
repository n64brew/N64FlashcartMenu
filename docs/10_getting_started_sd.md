## Initial Setup of an SD Card

### First steps
Insert the (Micro) SD card to your PC and ensure it is properly formatted to be compatible with your flashcart.

#### Preparations for SC64

> [!CAUTION]
> Install the battery correctly, most issues encountered are due to the positive battery terminals being bent due to forced insertion.
> A quick video tutorial on correct insertion can be found here:  
> [![Video tutorial](https://img.youtube.com/vi/LV5Aj_P3U_E/default.jpg)](https://www.youtube.com/shorts/LV5Aj_P3U_E)

> [!TIP]
> Most (Micro) SD cards are factory formatted to FAT32 or EXFAT with MBR partition and don't require re-formatting.

- Download the latest `sc64menu.n64` file from the [releases](https://github.com/Polprzewodnikowy/N64FlashcartMenu/releases/) page, then place it in the root directory of your Micro SD card.
- Place your ROM files on the Micro SD card, **in any folder except the root directory `menu` folder**.
- Remove the Micro SD Card from your computer and insert it into the SC64. 

#### Preparations for EverDrive-64 Pro

- Use firmware 26.1014 or newer.
- Copy the `menu.n64` artifact to the SD card, for example as `/N64FlashcartMenu.n64`.
- Launch it from the stock Pro menu. Keep the stock `/ed64/menu.n64`; this menu is launched as an ordinary ROM.
- Keep edlink and other active USB tools idle while this menu or a launched game runs.
  Use them only with the console off or in the stock Pro menu.
- After an SD or transport fault, power-cycle both the console and USB before continuing,
  then check the files on a computer.

Automatic CIC and MCU save writeback are enabled. RESET returns to the stock
menu on the tested firmware. 64DD, fast reboot, USB debugging and menu RTC are
unavailable on the Pro. Autoload and history paths inherit the shared 255-byte
INI value limit. Emulator boot/save tests are deferred to external testers.

ROM and save paths use the cartridge's CP437 encoding, with existing 8.3 aliases
for characters it cannot represent. A path without a usable alias is rejected
before game startup. FAT32 Unicode game launch and save writeback are verified
on paths over 300 bytes.

With Jumper Pak (4 MiB), a ROM booted and restored saved settings; cheat actions
were correctly refused. Earlier intermittent navigation, boxart and ROM-open
failures remain unresolved.

### Advanced Steps
Advanced steps are optional, which adds additional capabilities.

> [!TIP]
> Files and folders with the "hidden" property, will also be hidden in the menu by default.

#### Add Boxart support
See the [Boxart](./19_gamepak_boxart.md) page.

#### Add Emulator support
See the [Emulators](./18_emulators.md) page.

#### Add 64DD Disk support
See the [64DD](./17_64dd.md) page.

### So what could the layout of the SD card look like?
```plaintext
SD:\
│
├── sc64menu.n64
│
│
├── menu\
│   │
│   ├── metadata\
│   │    ├── N\G\E\E\boxart_front.png
│   │    ├── N\G\E\E\boxart_back.png
│   │    ├── N\G\E\E\metadata.ini
│   │    ├── N\G\E\description.txt
│   │    └── homebrew\myromname\boxart_front.png
│   │
│   ├── 64ddipl\
│   │    ├── NDDE0.n64
│   │    ├── NDDJ2.n64
│   │    └── NDXJ0.n64
│   │    
│   └── emulators\
│        ├── neon64bu.rom
│        ├── sodium64.z64
│        ├── gb.v64
│        ├── gbc.v64
│        ├── smsPlus64.z64
│        └── Press-F.z64
│
├── (a rom).z64
├── (a rom).n64
├── (some folder with ROMs)\
        │   └── (some folder with ROMs)\
        |       └── (some supported ROM files)
        │
        ├── (some supported ROM files)
        |
        └── (some folder with 64DD disk images)\
            └── (some 64DD disk images)
```


## Save Files
All save files (whether `FlashRam`, `SRAM` or `EEPROM`) will be read from a `/saves` folder located **in the same directory as the ROM** 
and they must share the same file name, but use the `.sav` extension. `.sav` files will be created and modified whenever a ROM file saves to 
the "cartridge save memory".

```plaintext
├── (some folder with ROMs)\
    ├── a_rom.z64
    ├── b_rom_whatever.n64
    └── saves\
        ├── a_rom.sav
        └── b_rom_whatever.sav
```

### Transferring saves from an ED64 or emulator
If you are transferring a file from a different flashcart, such as the ED64, or an emulator you must change the file extension to `sav`. 
For example, a save file called `Glover (USA).eep` should have its extension changed to `Glover (USA).sav` to work with N64FlashcartMenu.

> [!CAUTION]
> Saves created with certain emulator versions, or created for a different ROM version (as in revisions of the same game), or region (as in between NTSC and PAL versions of a same game) may be incompatible.

Dependent on the original save, you may need to correct the byte order and/or pad/trim the files to their expected size:

> [!TIP]
> Use a tool such as [Ninjiteu's N64SaveConverter](https://github.com/Ninjiteu/N64SaveConverter).
> Certain files may need parsing multiple times.

### Recommended Action
- Make sure you regularly back up important files from the SD Card to your computer to avoid accidental loss.
