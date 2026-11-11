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

## EverDrive-64 Pro transport qualification

The Pro implementation is **detection/transport only, not a storage backend**.
The pre-integration gate passed on the recorded Pro/X7 configurations below.
The production selector probes after the iQue bypass and before other
cartridge/filesystem initialization. Every recognized-Pro or indeterminate
probe result clears the storage prefix and selects the safe Pro flashcart
adapter without falling back. An identified Pro initializes through the shared
callback: transport failure returns `FLASHCART_ERR_INT`, while READY returns
`FLASHCART_ERR_FUNCTION_NOT_SUPPORTED` because storage is unavailable. An
indeterminate probe returns `FLASHCART_ERR_INT` without transport initialization.
The adapter supplies retained diagnostics through the optional shared error
message callback; other errors retain the generic conversion messages.

All flashcart initialization errors enter the shared, non-returning startup
fault view immediately, before filesystem/assets, controller setup, normal USB
polling, or menu cleanup. The view uses the built-in font and no cartridge I/O.
Other cartridge families and memory/firmware configurations remain unqualified.

The new [driver](../src/flashcart/ed64_pro/ed64_pro.c) performs a passive EDID
read, bounded initial RX cleanup, a framed inherited-status request, mailbox
NOP, one device-stop command, two matching READY samples, post-stop cleanup,
fresh zero stop status, and MCU SD initialization with fresh zero status.
Inherited well-formed nonzero status is retained, not treated as our command's
failure. The same nonzero result after stop or SD initialization fails. No
N64 filesystem is mounted and no ROM, save, file, launch, or USB API is used.

READY is cached internal readiness only. FAILED/POISONED retain the first
phase, error, completed ID, complete valid status frames, and confirmed RX
byte count. An issued DMA that times out or reports a PI error keeps its ticket
and private buffer reserved for the rest of the run, including late completion.
There is no retry, reset, queue cancellation, or warm-restart recovery after
failure: **power cycle**. Calls must be serialized foreground calls with
interrupts enabled, no libdragon kernel, and no competing PI-producing ISR.
Call probe and initialization consecutively; do not reset CP0 COUNT during
the attempt (in particular, call `timer_init` beforehand if it is needed).

### Sources and initial runtime limits

| Source | Inspected revision / use |
|---|---|
| N64FlashcartMenu baseline | `30113a9afab309fefa5384480e2176a1c796331e` |
| Actual libdragon SDK | `39d0d6096130836a65710da6730290701d060272`; raw DMA tickets, queue-idle check, cache APIs |
| [Official Krikzz API](https://github.com/krikzz/ed64-pro-pub/tree/43014e290f85140d4c5ca84fbf5ae48ac755a23d) | `43014e290f85140d4c5ca84fbf5ae48ac755a23d`; selected register/framing/stop/SD routines only |

Krikzz's complete MIT notice and adaptation provenance are retained in the
[developer notes](../src/flashcart/ed64_pro/README.md).
The combined project retains its existing AGPL license.

| Operation | Runtime failure limit |
|---|---|
| One PI operation, including queue acquisition/completion | 10 ms |
| Each of the two prescribed FIFO drains | 100 ms and at most 2,048 logical bytes total |
| Initial or ordinary status transaction | 1,000 ms |
| Device stop / mailbox | 5,000 ms |
| MCU SD initialization, including its status | 5,000 ms |
| Entire probe / initialization attempt | 15,000 ms |

Nested operations use the earlier phase/whole-attempt deadline. Partial
progress never restarts it, and expiry at the deadline is failure. These are
unqualified runtime failure limits, **not upstream timing guarantees**.
The 2,048-byte RX ceiling is conservative; per-direction FIFO capacity, TX
credits, and status bytes 1/2 are not documented by the inspected API.
Upper FIFOSTAT bits are not interpreted as credits. Status bytes 1/2 are
retained as opaque bytes, not an invented protocol-version check.

### Automated procedures

From the project root, run variants sequentially:

```sh
make -f tests/Makefile test-ed64-pro test-ed64-pro-sanitize

# Linux host suites use a separate build directory from the macOS binaries.
docker exec --workdir /work n64-libdragon-prebuilt \
  make -f tests/Makefile test-ed64-pro test-ed64-pro-sanitize \
  CC=gcc HOST_BUILD_DIR=/tmp/n64flashcartmenu-host-tests

docker exec --workdir /work n64-libdragon-prebuilt \
  make -j2 output/N64FlashcartMenu.n64

docker exec --workdir /work n64-libdragon-prebuilt \
  make -f tests/Makefile.n64 -j2

# Separate objects/outputs for the software-only timeout-injection ROM.
docker exec --workdir /work n64-libdragon-prebuilt \
  make -f tests/Makefile.n64 -j2 \
  BUILD_DIR=/work/build/ed64-pro-timeout OUTPUT_DIR=/work/output/ed64-pro-timeout \
  TEST_FLAGS=-DED64_PRO_TEST_FORCE_TIMEOUT

docker exec --workdir /work n64-libdragon-prebuilt \
  make -j2 BUILD_DIR=build/ed64-pro-release OUTPUT_DIR=output/ed64-pro-release \
  FLAGS="-DNDEBUG -DFEATURE_AUTOLOAD_ROM_ENABLED" \
  output/ed64-pro-release/N64FlashcartMenu.n64

docker exec --workdir /work n64-libdragon-prebuilt make ed64-pro
cmp output/N64FlashcartMenu.n64 output/menu.n64

# Instrument the actual menu, not a duplicate startup implementation.
docker exec --workdir /work n64-libdragon-prebuilt \
  make -f tests/Makefile.startup -j2

docker exec --workdir /work n64-libdragon-prebuilt \
  make -f tests/Makefile.startup -j2 \
  BUILD_DIR=build/ed64-pro-startup-timeout OUTPUT_DIR=output/ed64-pro-startup-timeout \
  FLAGS="-DFEATURE_AUTOLOAD_ROM_ENABLED -DED64_PRO_TEST_FORCE_TIMEOUT"

# Non-Pro browser regression only: prevent automatic ROM/save activity.
docker exec --workdir /work n64-libdragon-prebuilt \
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

The [host suite](../tests/ed64_pro_transport_test.c) compiles the production
units against an SDK-boundary model. Fixture reset represents a new ROM
launch; no reset hook is compiled into the driver. Coverage includes queue
ownership races, exact four-byte register reads, whole private cache lines
with distinct CPU/RDRAM contents, late DMA completion, literal command words,
caller offsets 0-31, lengths 0/1/2/3/4/5/15/16/17/127/128/129/2047/2048 and
rejected over-limit/overflow lengths, bounded refill, opaque status bytes,
partial/malformed/trailing replies, wraparound, phase/whole deadlines, and
failure at all 27 DMA boundaries of the golden initialization transcript.
Cold-start BUSY/CONTEXT rejections retain their terminal diagnostics, remain
retryable, and do not replace an active initialization's sticky failure.
The SDK stub intentionally exposes no blocking/fixup DMA or CPU-PI API.
Host models do not establish physical bus or R4300 cache/interrupt behavior.

The [lifecycle suite](../tests/ed64_pro_lifecycle_test.c) compiles the real
flashcart facade against spies in debug and release modes. It checks every
recognized-Pro outcome and indeterminate probe failure for the existing error
category, cleared prefix, safe Pro adapter selection, and zero legacy selection,
filesystem, or USB initialization. Recognized devices invoke the adapter's
initialization callback; indeterminate probes do not. It also checks iQue,
libcart families, ROM fallback, DaisyDrive dispatch, non-Pro SD/init errors,
mandatory/optional callbacks, optional retained-state error messages, generic
message fallback, and stable public error values. The transport suite exercises
the production adapter's initialization mapping, unsupported storage callbacks,
false feature reporting, and zero firmware placeholder without extra I/O. Legacy facade
warnings are scoped out only around its inclusion in the host test; production
code and default build flags are not weakened.

[Makefile.startup](../tests/Makefile.startup) builds the actual menu with
[link-time guards](../tests/ed64_pro_startup_guard.c). Only the menu, startup
fault view, facade, low-level transport, and guard translation units disable LTO, preserving the
external boundaries to wrap. Other sources retain their normal flags. The
wrapper arms after any non-OK flashcart startup result; further PI DMA/CPU I/O, filesystem
initialization/file access, debug/USB initialization/traffic, or controller
setup displays `STARTUP GUARD FAILED` instead of performing that operation.
The normal terminal renderer is unchanged. Check the ELF call sites to
confirm wrapping, then observe both the healthy and timeout-injected variants
on hardware; a successful build alone is not a hardware result.

### Hardware procedure (approval and physical operator required)

1. Obtain explicit approval for a non-destructive SD copy and ordinary-ROM
   launch from stock firmware. Do not overwrite the stock OS/menu, update
   MCU/FPGA firmware, or use the SC64 deployment script on a Pro. MCU-managed
   edlink copies are permitted with the stock menu idle; power off before
   removing SD cards or swapping cartridges/capture connections.
2. Record the on-screen build ID (including any `-dirty` suffix), actual EDID,
   cart revision, stock firmware, SD make/format, console region, memory size,
   and cold/warm-launch history.
3. Launch the normal test ROM on available non-Pro carts. Require exactly the
   passive read and an explicit non-Pro result; then confirm their usual menu
   and SD startup remain healthy after power cycling. iQue is bypassed.
4. On a Pro, require successful handoff/SD readiness and the exact retained
   status/drain observations. Repeat healthy cold and warm launches and record
   counts and maximum timings. A fresh SD error must remain failure; test a
   genuine failure only with a safe boot arrangement, never by hot-removing SD.
5. A ready Pro automatically runs 100,000 non-mutating operations (50,000 status
   requests and 50,000 EDID reads). A 50-us timer ISR loads and dirties the caller
   cache lines but never submits PI work. Require zero corruption,
   desynchronization, or transport/MCU errors and a **nonzero observed IRQ/PI-DMA
   overlap count**. A run without overlap explicitly reports UNQUALIFIED.
   Record displayed phase, PI, status/register maxima, RX peak, and IRQ counts.
6. Repeat representative runs with both Jumper Pak and Expansion Pak. Launch
   the separate timeout-injection ROM; its built-in-font terminal report must
   remain visible after the timeout without filesystem or debug forwarding.
   The harness stops its stress timer on failure and never retries Pro I/O.
7. Stop on contradictory ID/count/timing/handshake evidence. Adjust a bound or
   sequence only with recorded evidence and updated tests, not FIFO padding,
   extra status exceptions, speculative resets, or blind retries.

### Qualification record

The hardware and host results below retain their original dates and source
revisions. They do not qualify the 2026-11-11 interface and startup-view changes
on hardware.

#### Review changes verified on 2026-11-11

The Pro adapter and retained-state error callback use the existing flashcart
interface/error categories. All flashcart initialization failures now stop in
the shared startup fault view before normal services. Transport protocol and
runtime deadlines are unchanged.

| Gate / case | Result |
|---|---|
| 47 transport + 11 facade cases, normal and AddressSanitizer/UndefinedBehaviorSanitizer | PASS on macOS; facade runs in both debug and release modes |
| Linux host suites | PASS with GCC 11.4.0: the same 47 transport + 11 facade cases, normal and sanitizer builds, facade debug/release |
| Target N64 menu and startup-guard builds | PASS: production menu, guarded menu, and separate guarded software-timeout variant |
| Hardware acceptance of the shared startup fault view | PASS on the connected N64/ED64 Pro: guarded healthy transport and software-injected PI completion timeout both rendered the expected retained-state error, with no startup guard failure |

The guarded ROMs were launched from the idle stock firmware using
`edlink run --file`, without replacing the stock OS or updating firmware.
The healthy result reported the unimplemented storage backend; the timeout
result reported unknown cartridge identity, PI DMA completion timeout, and
zero confirmed RX bytes. HDMI captures were inspected directly. The timeout
variant uses `FLAGS=-DED64_PRO_TEST_FORCE_TIMEOUT`; it is software fault
injection, not evidence of a physical PI fault. Both console and USB were
power-cycled between variants and after the final timeout test. These checks
do not repeat the earlier 4/8 MiB, other-cartridge, controller-input, or
incoming-USB-payload qualification.

Local captures are in the ignored `output/pr459-review-2026-11-11/` directory.
The ROM SHA-256 values were:

| ROM | SHA-256 |
|---|---|
| `output/ed64-pro-startup-guard/N64FlashcartMenu.n64` | `4d8425ed8831e3ab557752d8b261cce9cb1d5fccf5cc368a14f590629210ad27` |
| `output/ed64-pro-startup-timeout/N64FlashcartMenu.n64` | `22b0476bd18be3b0de5bc669ce22698adc2e8db59c7a2eaecf127afb154b4018` |

#### Earlier qualification: 2026-11-04 hardware and 2026-11-06 hosts

Hardware results below are observed from user-supplied HDMI screenshots, not
inferred from upload-tool exit codes.

| Gate / case | Result |
|---|---|
| 45 transport + 10 facade cases, normal and AddressSanitizer/UndefinedBehaviorSanitizer | PASS on 2026-11-06 with macOS/Apple Clang 21.0.0 and container Linux/GCC 11.4.0; facade runs in both debug and release modes, including all listed parameterized cases |
| Targeted N64 debug menu build | PASS with the inspected installed SDK, including terminal startup integration |
| Hardware test ROM and timeout-injection ROM builds | PASS; separate output/object directories, production driver linked, no asset filesystem |
| N64 ELF buffer ownership | PASS: 512-byte staging aligned to 512, metadata outside its lines, two 16-byte aligned shared caller-line fixtures |
| Separate release menu and byte-identical Pro packaging | PASS; `cmp` confirms the Pro copy is byte-identical |
| Passive reads and normal boot: 64drive, ED64 V/X, SC64, other/unknown carts | X7 passive read PASS at 8 MiB; normal SD browser/controller regression PASS at 4 MiB with autoload disabled. Other families unqualified |
| Pro identity variants, stock handoff, repeated healthy launches, FIFO-count assumptions | PASS on one Pro/stock 26.1014: A3D 8 MiB, original N64 8 MiB cold/warm and 4 MiB cold; EDID `ED640027`, inherited `5A 07 27 B0`, drains 0 + 0, RX peak 4. Other ID/firmware/SD variants and full FIFO capacity unqualified |
| Real 100,000-operation cache/IRQ stress with actual DMA overlap | PASS on all four recorded Pro launches: 400,000 operations total, nonzero overlap in every run, zero canary errors |
| Runtime timing margins across firmware / SD combinations | All four recorded Pro runs below limits; no cross-configuration timing guarantee or bound changes |
| Physical SD-readiness failure | NOT RUN; safe boot arrangement required |
| RAM-only error rendering after injected timeout, 4 MiB and 8 MiB | PASS on original N64 at both sizes using the guarded actual menu; explicit identity-unknown PI completion error and power-cycle guidance remain visible |
| Physical PI failure | NOT RUN; software injection is not physical-failure evidence |
| Actual menu terminal screen and USB/control exclusion | PASS production menu at 4/8 MiB: incomplete-backend screen, no browser after A/B/Start/D-pad with USB connected. Controller verified working in stock menu |
| Actual facade callback/filesystem/USB spies | PASS in debug/release and sanitizer variants |
| Guarded actual-menu startup and timeout variants | PASS healthy at 8 MiB and timeout at 4/8 MiB; no forbidden-call screen. ELF confirms arming wrapper and guarded PI call sites |

#### Observed hardware runs

| Property | Original N64 / X7 | Analogue 3D / Pro |
|---|---|---|
| Screenshot received | 2026-11-04 09:01 CST | 2026-11-04 09:05 CST |
| Build ID | `e88a74af` | `e88a74af` |
| Console / memory | Original NTSC N64, Expansion Pak (8 MiB), UltraHDMI HW2 | Analogue 3D v1.5.1, NTSC, 8 MiB |
| Cartridge firmware | Latest stock per operator; exact version not recorded | Stock 26.1014 (edlink query), bootloader 26.0613 |
| EDID | `00140014` (non-Pro) | `ED640027` |
| Inherited status | Not requested | `5A 07 27 B0` |
| FIFO drains / RX peak | 0 + 0 / 0 | 0 + 0 / 4 |
| Stop / SD / whole attempt | Not executed | 2 / 22 / 25 ms |
| Maximum PI time | 43 us | 104 us |
| Stress operations | Not executed | 100,000 / 100,000 |
| Timer IRQs / IRQ-DMA overlaps | Not executed | 231,719 / 35 |
| Canary errors | Not applicable | 0 |
| Maximum status / register time | Not applicable | 275 / 128 us |
| Result | Expected passive non-Pro result | Stress PASS for this launch only |

| Property | Original N64 / Pro cold launch | Original N64 / Pro healthy warm relaunch |
|---|---|---|
| Screenshot received | 2026-11-04 09:09 CST | 2026-11-04 09:12 CST |
| Build / console | `e88a74af`, original NTSC N64, Expansion Pak (8 MiB), UltraHDMI HW2 | Same, returned to stock menu using RESET without power cycling |
| Firmware / EDID | Stock 26.1014 / `ED640027` | Same |
| Inherited status | `5A 07 27 B0` | `5A 07 27 B0` |
| FIFO drains / RX peak | 0 + 0 / 4 | 0 + 0 / 4 |
| Stop / SD / whole attempt | 2 / 21 / 24 ms | 2 / 21 / 24 ms |
| Maximum PI time | 104 us | 106 us |
| Stress operations | 100,000 / 100,000 | 100,000 / 100,000 |
| Timer IRQs / IRQ-DMA overlaps | 223,695 / 153 | 223,732 / 148 |
| Canary errors | 0 | 0 |
| Maximum status / register time | 253 / 117 us | 254 / 136 us |
| Result | Native R4300 stress PASS | Healthy warm-handoff + native R4300 stress PASS |

| Property | Original N64 / Pro Jumper Pak cold launch |
|---|---|
| Screenshot received | 2026-11-04 10:26 CST |
| Build / console | `e88a74af`, original NTSC N64, Jumper Pak (**4 MiB confirmed on screen**), UltraHDMI HW2 |
| Firmware / EDID | Stock 26.1014 / `ED640027` |
| Inherited status | `5A 07 27 B0` |
| FIFO drains / RX peak | 0 + 0 / 4 |
| Stop / SD / whole attempt | 2 / 21 / 25 ms |
| Maximum PI time | 100 us |
| Stress operations | 100,000 / 100,000 |
| Timer IRQs / IRQ-DMA overlaps | 223,820 / 151 |
| Canary errors | 0 |
| Maximum status / register time | 253 / 122 us |
| Result | 4 MiB native R4300 transport/cache/IRQ stress PASS |

#### Actual-menu acceptance after integration

All rows use sources committed as `a0a0a448`, on the original NTSC N64 with
UltraHDMI HW2. The Pro is the same stock-26.1014 cartridge as above. Memory was
changed only with console power off; the controller was left connected for
later launches after verifying it could navigate the stock menu.

| Screenshot received (CST) | Memory / cart | Build variant | Observed result |
|---|---|---|---|
| 09:44 | 8 MiB / Pro | Production debug menu | Explicit incomplete-backend terminal screen. A/B/Start/D-pad with USB connected did not enter the browser; the initially late-connected controller was subsequently verified in the stock menu |
| 10:01 | 8 MiB / Pro | Actual menu with startup guards | Same correct terminal screen with working controller connected before launch; controls left it unchanged, no `STARTUP GUARD FAILED` |
| 10:12 | 8 MiB / Pro | Guarded actual menu, injected PI completion timeout | Visible `Cartridge identity unknown`, `Pro probe: PI DMA completion timed out`, confirmed RX 0, power-cycle guidance; no guard failure |
| 10:43 | 4 MiB / Pro | Production debug menu | Same incomplete-backend screen; controls with USB connected left it unchanged |
| 10:45 | 4 MiB / Pro | Guarded actual menu, injected PI completion timeout | Same explicit timeout report and visible power-cycle guidance; no guard failure |
| 10:50, 10:52 | 4 MiB / X7 | Normal menu with ROM autoload disabled for safety (`FLAGS=`) | First-run credits, then normal SD browser listing and operator-confirmed D-pad navigation; no game/save operations |

The X7 regression used the operator-approved normal `/menu` settings/cache
writes. Disabling ROM autoload was limited to that test build, not a change to
the production defaults. This verifies normal non-Pro detection, SD startup,
asset/UI rendering, and browsing, not ROM loading, saves, or autoload itself.
No incoming USB payload was deliberately injected into the Pro's terminal
state; USB remained connected, callback/USB initialization exclusion was checked
with facade spies, and the actual-menu link-time guards were armed to reject
further PI/filesystem/USB API calls after a Pro startup result. No such
forbidden-call failure was observed.

The transport and integrated-menu ROMs were copied to uniquely named ordinary-ROM SD paths and
read back byte-identically before launching; stock OS, firmware, and saves
were not replaced. An initial edlink launch reported `mcmd: no response from
menu` despite a zero process exit code. After the operator power-cycled the
Analogue 3D to its stock menu, the next launch succeeded and produced the
recorded screenshot. Do not equate edlink's process status with command success.
The X7 was RAM-loaded with `echo | UNFLoader -b -f 3 -r <test-ROM>`; the display
confirms execution despite UNFLoader returning exit code 1 after reporting a
successful upload. No debug listener or USB command traffic was run during the
Pro stress test.

The release build reports the existing unused `start_us` variable warning in
[grid.c](../src/menu/views/grid.c); that unrelated code was not changed.

**Pre-integration hardware gate: PASS for the recorded configurations.**
The available X7 passive-read check, Pro handoff/repeated healthy launches,
observed FIFO counts, and real native cache/IRQ stress have recorded results.
This permits terminal startup integration, not a claim of general Pro storage
support.

**Post-integration acceptance: PASS for the recorded 4/8 MiB Pro and X7 checks.**
Production Pro terminal behavior, controller exclusion, guarded RAM-only
timeout rendering at both memory sizes, and the X7 SD browser regression have
recorded results. The runtime bounds and protocol sequence were not changed.
Other supported cart families/regions, Pro revisions/firmware/SD combinations,
full FIFO-capacity behavior, real SD-readiness/physical PI failures, and the
post-integration menu on Analogue 3D remain explicitly unqualified. Software
fault injection is not physical-failure evidence, and these results do not
establish universal hardware qualification or usable Pro storage support.

### Implementation file roles

| Files | Purpose |
|---|---|
| [ed64_pro_ll.c](../src/flashcart/ed64_pro/ed64_pro_ll.c), [ed64_pro_ll.h](../src/flashcart/ed64_pro/ed64_pro_ll.h) | Cartridge-local bounded PI/FIFO protocol, cache ownership, deadlines, quarantine, and typed results |
| [ed64_pro.c](../src/flashcart/ed64_pro/ed64_pro.c), [ed64_pro.h](../src/flashcart/ed64_pro/ed64_pro.h) | One-shot probe/init lifecycle, retained diagnostics, and safe flashcart adapter with unsupported storage callbacks |
| [flashcart.c](../src/flashcart/flashcart.c), [flashcart.h](../src/flashcart/flashcart.h) | Select the Pro adapter without fallback, reuse existing errors, and obtain optional retained-state error details; preserve non-Pro dispatch |
| [menu.c](../src/menu/menu.c), [fault.c](../src/menu/views/fault.c), [views.h](../src/menu/views/views.h) | Route every flashcart startup error into the shared, non-returning, PI-independent fault view before normal services |
| [README.md](../README.md), [Pro developer notes](../src/flashcart/ed64_pro/README.md) | Upstream source/license credit in the main README; full MIT notice, pinned revision, and adaptation provenance in the developer notes |
| [Makefile](../Makefile), [build.yml](../.github/workflows/build.yml) | Compile the low-level driver and run focused host tests in CI before the existing ROM build |
| [.gitignore](../.gitignore), [tests/Makefile](../tests/Makefile) | Track the otherwise ROM-glob-ignored hardware Makefile; segregate normal/release/sanitizer host binaries |
| [ed64_pro_transport_test.c](../tests/ed64_pro_transport_test.c), [libdragon.h](../tests/stubs/libdragon.h) | Test production ownership/protocol/lifecycle code against the SDK-boundary cache/interrupt/DMA model |
| [ed64_pro_lifecycle_test.c](../tests/ed64_pro_lifecycle_test.c), [cart.h](../tests/stubs/libcart/cart.h), [ff.h](../tests/stubs/fatfs/ff.h), [usb.h](../tests/stubs/usb.h) | Compile the real facade against minimal boundary declarations and spies, including non-Pro regressions |
| [ed64_pro_hardware.c](../tests/ed64_pro_hardware.c), [Makefile.n64](../tests/Makefile.n64) | Isolated ordinary-ROM qualification harness using the production drivers and installed SDK rules |
| [ed64_pro_startup_guard.c](../tests/ed64_pro_startup_guard.c), [Makefile.startup](../tests/Makefile.startup) | Instrument the actual menu to reject post-error PI/filesystem/USB work without weakening production flags |
| [README.md](../README.md), [98_flashcart_wip.md](98_flashcart_wip.md), [91_test_plan_matrix.md](91_test_plan_matrix.md) | State limited support, provenance, procedures, measured results, and remaining qualification gaps |
