# Test Plan Matrix

To properly test all variants we need to prove working over all console regions, memory types, flashcarts and commercial carts.

## Testing Dimensions

The N64FlashcartMenu requires comprehensive testing across multiple hardware and software configurations:

- **Console Regions**: NTSC (US/JP), PAL (EU), MPAL (BR)
- **Memory Configurations**: Jumper Pak (4MB), Expansion Pak (8MB)
- **Flashcarts**: 64drive, EverDrive64 (V3/X7), SummerCart64
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

| Feature | 64drive | ED64 V3 | ED64 X7 | SummerCart64 |
|---|---|---|---|---|
| **N64 ROM Loading** | ✓ | ✓ | ✓ | ✓ |
| **64DD Disk Loading** | ✓ | ⚠ | ⚠ | ✓ |
| **Emulator ROM Loading** | ✓ | ✓ | ✓ | ✓ |
| **Save Data Persistence** | ✓ | ⚠ | ⚠ | ✓ |
| **Cheat Codes** | ✓ | ✓ | ✓ | ✓ |
| **ROM Patches** | ⚠ | ⚠ | ⚠ | ⚠ |
| **USB Communication** | ✓ | Limited | ✓ | ✓ |
| **RTC Support** | ✓ | ✓ | ✓ | ✓ |
| **Controller Pak** | ✓ | ✓ | ✓ | ✓ |
| **Auto CIC/Region Detection** | ✓ | Manual | ✓ | ✓ |
| **Fast Reboot** | ✓ | - | - | ✓ |
| **Diagnostics** | - | - | - | ✓ |

**Notes:**
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

## EverDrive-64 Pro

The Pro implementation is **detection/transport only, not a storage backend**.

### Automated procedures

First, run a pre-built libdragon Docker container:

```sh
docker pull --platform linux/amd64 ghcr.io/n64brew/libdragon:preview

container_name=n64-libdragon-preview

# Keep the container so its installed libdragon survives between builds.
if ! docker container inspect "$container_name" >/dev/null 2>&1; then
  docker create -i --platform linux/amd64 \
    --name "$container_name" \
    --mount type=bind,source="$PWD",target=/work \
    --workdir /work \
    ghcr.io/n64brew/libdragon:preview bash
fi

if [[ "$(docker container inspect --format '{{.State.Running}}' "$container_name")" != true ]]; then
  docker start "$container_name"
fi

# Incrementally build and install this checkout's libdragon, then build the menu.
docker exec --workdir /work/libdragon "$container_name" make -j4 libdragon tools
docker exec --workdir /work/libdragon "$container_name" make -j4 install tools-install
docker exec --workdir /work "$container_name" make -j4
```

From the project root, run variants sequentially:

```sh
# macOS host suites
make -f tests/Makefile test-ed64-pro test-ed64-pro-sanitize

# Linux host suites use a separate build directory from macOS binaries.
docker exec --workdir /work n64-libdragon-preview \
  make -f tests/Makefile test-ed64-pro test-ed64-pro-sanitize \
  CC=gcc HOST_BUILD_DIR=/tmp/n64flashcartmenu-host-tests

docker exec --workdir /work n64-libdragon-preview \
  make -j2 output/N64FlashcartMenu.n64

docker exec --workdir /work n64-libdragon-preview \
  make -f tests/Makefile.n64 -j2

# Separate objects/outputs for the software-only timeout-injection ROM.
docker exec --workdir /work n64-libdragon-preview \
  make -f tests/Makefile.n64 -j2 \
  BUILD_DIR=/work/build/ed64-pro-timeout OUTPUT_DIR=/work/output/ed64-pro-timeout \
  TEST_FLAGS=-DED64_PRO_TEST_FORCE_TIMEOUT

docker exec --workdir /work n64-libdragon-preview \
  make -j2 BUILD_DIR=build/ed64-pro-release OUTPUT_DIR=output/ed64-pro-release \
  FLAGS="-DNDEBUG -DFEATURE_AUTOLOAD_ROM_ENABLED" \
  output/ed64-pro-release/N64FlashcartMenu.n64

docker exec --workdir /work n64-libdragon-preview make ed64-pro
cmp output/N64FlashcartMenu.n64 output/menu.n64

# Instrument the actual menu, not a duplicate startup implementation.
docker exec --workdir /work n64-libdragon-preview \
  make -f tests/Makefile.startup -j2

docker exec --workdir /work n64-libdragon-preview \
  make -f tests/Makefile.startup -j2 \
  BUILD_DIR=build/ed64-pro-startup-timeout OUTPUT_DIR=output/ed64-pro-startup-timeout \
  FLAGS="-DFEATURE_AUTOLOAD_ROM_ENABLED -DED64_PRO_TEST_FORCE_TIMEOUT"

# Non-Pro browser regression only: prevent automatic ROM/save activity.
docker exec --workdir /work n64-libdragon-preview \
  make -j2 BUILD_DIR=build/ed64-pro-x7-regression OUTPUT_DIR=output/ed64-pro-x7-regression \
  FLAGS= output/ed64-pro-x7-regression/N64FlashcartMenu.n64
```

Reconfirm the existing container, `/work` mount, `N64_INST=/n64_toolchain`, and
installed SDK before use. Code-only changes do not require rebuilding the SDK.
The hardware Makefile delegates to the installed `n64.mk`; the normal test ROM
is `output/ed64-pro-test/ed64_pro_hardware.z64`, with its ELF under
`build/ed64-pro-test/`. It does not replace the production ROM or load assets.
The timeout variant has its own output/object directories and forces the
production completion loop to expire after an actual submitted read. It does
not electrically stall PI, cancel the transfer, reset the cart, or prove a
physical PI-failure case.
