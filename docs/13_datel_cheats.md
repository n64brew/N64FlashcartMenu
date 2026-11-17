## Cheats (Gameshark, etc.)

> [!WARNING]
> **THIS FEATURE IS EXPERIMENTAL**

The N64FlashcartMenu has underlying support for the cheat code types made popular by the peripherals:
- GameShark
- Action Replay

Another product by Blaze, called the Xploder64/Xplorer64 also existed in some regions, but these codes are unlikely to work (yet).

> [!CAUTION]
> It is not advised to connect a physical cheat cartridge in conjunction with most flashcarts.

> [!WARNING]
> The N64FlashcartMenu can only support cheat codes based on Datel carts when **also** using an Expansion Pak. Codes will not be loaded when the Expansion Pak is not found.

Caveats:
- Requires an Expansion Pak to work.
- Codes that require a button to pressed are not yet supported (TODO: possible with the SC64, requires hook).
- `DE` codes are ignored.
- Games that were developed using later versions of the libDragon SDK are not supported and are currently incompatible due to ipl3 clearing RDRAM.
- The maximum number of cheat codes that will work is dependent on the available space in the N64 RAM (especially with game ROM's that use an Expansion Pak), exceeding them may cause unwanted side effects.
 - Whilst an experimental feature, the UI is currently hardcoded to limit the amount of codes to 64 (keeping it low allows us to collect caveats). This will be increased once enough feedback is collected.
- The cheat engine is subtly different from an actual GameShark and ED64 implementation so working enable and cheat codes may vary.

### Enable Cheats
Ensure you have a cheat file and have accessed the Datel Code Editor screen, then from the ROM info screen, press `R` to set `Use Cheats` to `Enabled`.

![MM ROM info](./images/rom-info-NZSE.png "MM ROM info")  

#### Datel Code Editor
When within the ROM info screen, Press `R` and load the `Datel Code Editor`. Adjust the cheat codes as needed then press `A` to `Apply ROM with these cheats`.

![MM Datel Code Editor](./images/datel-code-editor-NZSE.png "MM Datel Code Editor")  

> [!WARNING]
> Any changes made in the code editor are totally volatile (i.e. not saved when returning to the current screen). Press `L|Z` to save them.


#### Datel Cheat Files

> [!TIP]
> A good reference for cheat codes is: https://gamehacking.org and downloading ED64 text files, though note that usage may vary, as these may have been "corrupted" by the ability to use them in an in-accurate emulator.

Cheat files should be contained in the same directory as the ROM, but with the file extension `.datel` (instead of the ROM extension).
The files use the following content format (as aligned to text files made popular by the ED64):
(one cheat per line).
- An 8 character address, followed by a space, and then a 4 character value.
- An optional description can be added after the value (with a preceding space).
- A line that starts with `#` or `;` or `//` will be ignored as a comment (though will be lost if saved from the editor).
- A line that starts with `:` (followed by a space) and then a valid cheat will be loaded, but disabled.

> [!TIP]
> All codes in the file are active by default. Use the Datel Code Editor to disable individual cheats (or prefix the line with `:` (followed by a space) in the file).

> [!WARNING]
> Incorrectly formatted files may cause unknown side effects.

e.g. For Majoras Mask (USA):
```text
F1096820 2400 Activator
FF000220 0000
811F05AA FFFF Complete Bomber's Notebook
811F05AC FFFF
811F05AE FFFF
811F05B0 FFFF
810F6C3C 2400 Disable 3-day Timer
811EF6B6 FFFF Enable All Owl Statues
810A6A0E 6A40 Get Items From Anywhere
```

e.g. For Super Mario 64:
```text
8033B21D 0064 Super mario 64 infinite lives
80207723 0001 120 stars part 1
8020770B 00C7 120 stars part 2
50001101 0000 120 stars part 3
8020770C 00FF 120 stars part 4
```

e.g. For Paper Mario
```text
F106D650 2400 Enable Code (Must Be On)
8110DD9C 03E7 Infinite Max Coins 
50000601 0000 Infinite Max HP + FP 
8010DD91 0063
8110E01E 0007 Have All Star Spirits 
8010DD99 0063 Max Level 
8010DDA0 0063 Max Star Points 
8010DD98 007F Max Badge Points 
8010DD9F 00A0 Max Star Pieces
8010E0BD 00A0
50000C08 0000 Have All Members in your party
8010DDAC 0001
8010DD91 0002 Have Hammer (Level 3)
8010DD90 0002 Have Jump Boots (Level 3)
8010DDA0 0000 No Star Points (0) (If Games hangs at level up, enable this cheat and don't disable it whatsoever!)
D0071337 0020 Press L Button  For Moon Jump
8010DAF4 0043
8110E020 0707 Infinite Star Power
```

### Current community reported issues
These issues have been reported as differences when comparing a real GS device (or proved working on an ED64):
 - Exceeding 12 cheats in Majoras Mask will cause issues (Requires better Enable Code?!).
 - Exceeding 15 cheats in DK64 will cause issues (Requires better Enable Code?!).
 - F-Zero X when using the DD expansion kit (due to unloading of cheats).
 - Xeno Crisis cheats do not work (this game was based on later versions of libDragon and is incompatible due to ipl3 clearing RDRAM).
 - AeroGauge (EUR) no damage hangs the game on impact.
