# ED64 Pro hardware tests

Use the project's normal libdragon build environment, with `N64_INST` set, and
run commands from the repository root. Hardware tests need an EverDrive-64 Pro
with firmware **26.1014 or newer**, an N64, and an SD card. Results appear on the
console screen; no USB, capture, or controller automation tools are required.

Build each mode in its own build and output directories. Copy the resulting
ROM to the SD card and launch it from the stock Pro menu. Power off before
changing files on the card. Leave USB tools idle while a test is running.
Tests halt after reporting their result; power-cycle the console after a failure.

## Transport and device information

```sh
make -f tests/ed64_pro/Makefile.n64
make -f tests/ed64_pro/Makefile.n64 TEST_FLAGS=-DED64_PRO_TEST_INFO \
    BUILD_DIR=build/ed64-pro-info OUTPUT_DIR=output/ed64-pro-info
```

The default build produces `output/ed64-pro-test/ed64_pro_hardware.z64`. It runs
100,000 status/register operations under timer interrupts and checks for data
corruption. A pass requires interrupts to overlap DMA. The information build
shows cartridge identity, firmware, and initialization results.

## Storage and loading

Create the test files using Python 3, already included in the build environment:

```sh
python3 tests/ed64_pro/prepare_fixtures.py output/ed64pro-test
```

Copy the generated `ed64pro-test` directory to the SD card root. The script
recreates its named fixtures, including resetting `scratch.bin` to its initial
pattern. The generated ROM files are test data; launch only the hardware test
ROM, which loads and compares them without executing them.

```sh
make -f tests/ed64_pro/Makefile.n64 TEST_FLAGS=-DED64_PRO_TEST_STORAGE \
    BUILD_DIR=build/ed64-pro-storage OUTPUT_DIR=output/ed64-pro-storage
make -f tests/ed64_pro/Makefile.n64 TEST_FLAGS=-DED64_PRO_TEST_LOAD \
    BUILD_DIR=build/ed64-pro-load OUTPUT_DIR=output/ed64-pro-load
```

Storage mode reads the 1 MiB `read.bin` and checks CRC32 `1821D2CD`, then releases
storage. Loading mode compares an 8 MiB ROM, an auxiliary file loaded at an
offset, and a byte-swapped `.v64` against their cartridge memory contents.

Additional modes use the same Makefile and separate build/output directories:

| Check | `TEST_FLAGS` |
| --- | --- |
| Raw-sector writes to `scratch.bin` | `-DED64_PRO_TEST_STORAGE -DED64_PRO_TEST_RAW_WRITE` |
| Injected write failure and refused handoff | `-DED64_PRO_TEST_STORAGE -DED64_PRO_TEST_STORAGE_FAIL_WRITE -DED64_PRO_TEST_FAIL_WRITE_N=1` |
| Oversized ROM rejection | `-DED64_PRO_TEST_LOAD -DED64_PRO_TEST_OVERSIZE` |
| 8/32/64 MiB load timing | `-DED64_PRO_TEST_LOAD -DED64_PRO_TEST_LOAD_TIMING` |
| Injected PI timeout | `-DED64_PRO_TEST_FORCE_TIMEOUT` |

Use `--extended` when preparing fixtures for the oversize or timing modes.
Timing mode reports durations without comparing contents. Write modes modify
only the scratch fixture; recreate it before each run. After a raw-write pass,
power off, mount the SD card on your computer, and verify the result with:

```sh
python3 tests/ed64_pro/prepare_fixtures.py /path/to/sd/ed64pro-test --check-scratch
```

## Startup and region handling

```sh
make -f tests/ed64_pro/Makefile.startup
make -f tests/ed64_pro/Makefile.startup TEST_FLAGS=-DED64_PRO_TEST_FAIL_MOUNT \
    BUILD_DIR=build/ed64-pro-mount-failure OUTPUT_DIR=output/ed64-pro-mount-failure
make -f tests/ed64_pro/Makefile.region-probe
make -f tests/ed64_pro/Makefile.region
```

The startup builds produce `N64FlashcartMenu.n64` in their output directories.
The normal build opens the browser and stops with an error if a legacy
cartridge or USB probe is called after Pro detection. The mount-failure build
should show the normal startup error screen.

The region probe displays the TV type received at boot. Copy
`output/ed64-pro-region/probe/ed64_pro_region_probe.z64` to an SD directory as
`region.z64`. In the normal menu, set that directory as the default, use list
view, disable ROM autoload, and finish first-run setup. Launch
`output/ed64-pro-region/input/N64FlashcartMenu.n64`: it selects `region.z64`,
holds the ROM details and CIC screens for 15 seconds each, then launches it.
Controller input cancels the sequence. Automatic region selection should
preserve the console's TV type for this unknown-destination ROM; a manual TV
override should be reflected by the probe.

## Menu checks

With a normal menu build, browse files, launch a game, save, restart, and check
that the save is restored. Check cheats, Controller Pak backup/restore, and
emulator loading with your own content. In **Settings > Time**, save a changed
date/time, reopen the page, and check it after a power cycle; restore the
correct time when finished. See the [diagnostics guide](../../docs/ed64_pro_diagnostics.md)
and [64DD guide](../../docs/17_64dd.md) for their manual checks.
