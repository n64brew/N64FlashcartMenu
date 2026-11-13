# Test Plan Matrix

To properly test all variants we need to prove working over all console regions, memory types, flashcarts and commercial carts.

## Testing Dimensions

The N64FlashcartMenu requires comprehensive testing across multiple hardware and software configurations:

- **Console Regions**: NTSC (US/JP), PAL (EU), MPAL (BR)
- **Memory Configurations**: Jumper Pak (4MB), Expansion Pak (8MB)
- **Flashcarts**: 64drive, EverDrive64 (V3/X7/Pro), SummerCart64
- **ROM/Content Types**: N64 Games, 64DD Disks, Emulator ROMs (NES/SNES/GB/SMS)
- **Display Mods**: PixelFX, UltraHDMI, Analogue3D

## Test Baseline & Starting Point

Before starting a full test cycle, document the current baseline status to track progress and identify regressions.

### Baseline Entry Template

| Property | Value |
|---|---|
| **Test Date** | YYYY-MM-DD |
| **Menu Version** | (e.g., v2.10.0) |
| **Build Commit** | (git commit hash) |
| **Known Passing** | (List working configurations) |
| **Known Failing** | (List broken configurations) |
| **Regressions Since Last** | (Identify new failures) |
| **Tester Notes** | (Hardware used, emulator version, etc.) |

### Example Baseline

| Property | Value |
|---|---|
| **Test Date** | 2026-05-01 |
| **Menu Version** | v2.10.0 |
| **Build Commit** | abc1234 |
| **Known Passing** | NTSC-US + Jumper Pak on 64drive; NTSC-US + Exp.Pak on SummerCart64 |
| **Known Failing** | PAL60 with HDMI mods (awaiting libdragon fix) |
| **Regressions Since Last** | None identified |
| **Tester Notes** | Ares emulator v149; Real hardware: 64drive v2.0 |

---

## Test Matrix

| Console Region | Jumper Pak (4MB) | Exp.Pak (8MB) | Notes |
|---|---|---|---|
| **NTSC (US)** | ✓ Primary | ✓ Secondary | Baseline testing region |
| **NTSC (JP)** | ✓ Primary | ✓ Secondary | Asian market variant |
| **PAL (EU)** | ✓ Primary | ✓ Secondary | European market testing |
| **MPAL (BR)** | ✓ Primary | ✓ Secondary | South American regional variant |

## Flashcart Compatibility Matrix

| Feature | 64drive | ED64 V3 | ED64 X7 | ED64 Pro | SummerCart64 |
|---|---|---|---|---|---|
| **N64 ROM Loading** | ✓ | ✓ | ✓ | Partial | ✓ |
| **64DD Disk Loading** | ✓ | ⚠ | ⚠ | - | ✓ |
| **Emulator ROM Loading** | ✓ | ✓ | ✓ | ? | ✓ |
| **Save Data Persistence** | ✓ | ⚠ | ⚠ | Partial | ✓ |
| **Cheat Codes** | ✓ | ✓ | ✓ | Partial | ✓ |
| **ROM Patches** | ⚠ | ⚠ | ⚠ | ? | ⚠ |
| **USB Communication** | ✓ | Limited | ✓ | - | ✓ |
| **RTC Support** | ✓ | ✓ | ✓ | - | ✓ |
| **Controller Pak** | ✓ | ✓ | ✓ | ? | ✓ |
| **Auto CIC/Region Detection** | ✓ | Manual | ✓ | Partial | ✓ |
| **Fast Reboot** | ✓ | - | - | - | ✓ |
| **Diagnostics** | - | - | - | - | ✓ |

**Notes:**
- ED64 Pro: `Partial` = tested subsets; `?` = unvalidated; `-` = unavailable or not advertised.
  Automatic CIC and save writeback are enabled. CIC 6101/6102/6103/6105/6106 boot checks passed;
  automatic region detection is not advertised, and other console regions remain unqualified.
- ED64 Pro saves/cheats: SM64 EEPROM persistence passed, including FAT32 Unicode ROM/save
  paths over 300 bytes. Other save families have boot/configuration checks; persistence
  remains unqualified. One SM64 Datel cheat passed; broader compatibility remains unqualified.
  Emulator boot/save testing is deferred to external testers.
- ED64 Pro, 4 MiB: SM64 booted and restored settings; cheat actions were refused.
  Earlier intermittent navigation/artwork/open failures remain unresolved.
- Regression checks: SC64 native boot/browse/load/save and X7 native boot/browse/load
  passed on NTSC with 8 MiB.
- ED64 V3: Limited USB support (older revision)
- ED64 V3: Requires manual CIC/region setting
- SummerCart64: Most feature-complete (includes diagnostics & BIOS updates)

## Display Mod Compatibility Matrix

This matrix verifies that the menu displays correctly at boot and when launching ROM content on different display mods.

| Display Mod | Version | Menu Display | ROM Boot Display | NTSC (60Hz) | PAL (50Hz) | PAL (60Hz) |MPAL (60Hz) | Notes |
|---|---|---|---|---|---|---|---|---|
| **N64Digital** | Vx.x.x | ✓⚠ | ✓⚠ | ✓⚠ | ✓⚠ | ✓⚠ | ? | timing issues due to libdragon changes WiP |
| **UltraHDMI** | Vx.x.x | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | |
| **Analogue3D** | Vx.x.x | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | |

**Test Checklist per Display Mod:**
- [ ] Menu screen renders at correct resolution on boot
- [ ] Menu screen renders at correct resolution when launching ROM
- [ ] No visual artifacts or timing glitches during transitions
- [ ] Game ID data transmitted correctly (PixelFX - check OSD after game boots)

## Menu Feature Testing

| Feature | Test Type | Jumper Pak | Exp. Pak | Notes |
|---|---|---|---|---|
| **N64 ROM Loading** | Boot Commercial ROMs | ✓ | ✓ | Tests ROM loading infrastructure |
| **64DD Disk Loading** | Boot 64DD Disks | - | ✓ | Requires Expansion Pak |
| **Emulator ROM Loading** | Boot NES/SNES/GB/SMS ROMs | ✓ | ✓ | Tests multi-system support |
| **Cheat Codes** | Datel Cheat Application | ✓ | ✓ | Parser & installation validation |
| **ROM Patches** | Patch Application | ✓ | ✓ | Feature validation |
| **Save Data** | Game Save Persistence | ✓ | ✓ | Critical system feature |

## Test Coverage Checklist

**Core Menu Functionality:**
- [ ] File browser navigation across all flashcarts
- [ ] ROM detection (region, CIC type, save type)
- [ ] Settings persistence across power cycles
- [ ] Audio playback (menu sounds & MP3 player)

**ROM & Content Loading:**
- [ ] N64 ROM loading and execution on all regions
- [ ] 64DD disk loading (Expansion Pak only)
- [ ] Emulator ROM loading (NES, SNES, GB/GBC, SMS/GG, Fairchild Channel F)
- [ ] Save data persistence for various ROM types

**Advanced Features:**
- [ ] Datel cheat code application and validation
- [ ] ROM patch application
- [ ] Controller Pak operations
- [ ] RTC functionality where supported
- [ ] Display Mod compatibility (PixelFX HDMI Game ID, UltraHDMI, Analogue3D)
- [ ] USB communication (where applicable)
- [ ] Background image customization

**Memory & Performance:**
- [ ] Memory pressure testing with Jumper Pak (4MB)
- [ ] Extended features with Expansion Pak (8MB)
- [ ] Metadata
