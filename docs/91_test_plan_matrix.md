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

## EverDrive-64 Pro transport qualification (inert foundation)

The Pro implementation is **detection/transport only, not a storage backend**.
The production selector still uses the false-returning legacy detector, so the
normal menu does not execute the new probe or transport. Do not enable that
route until the pre-integration hardware gate below has recorded passing
results. The eventual Pro-only terminal startup screen and facade integration
are deliberately gated, not implemented as an unqualified browser/backend.

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
| Sleekmenu64 reference | `1229b87f011626e8c3ed96b4d6acd219da9d7470`; cache-line ownership diagnosis and historical stock-handoff observation |
| Sleekmenu64 vendored API | `5d7e96905331a841f97f8c51e0b0cba878e72fe3`; older reference, not the selected upstream revision |

Krikzz's complete MIT notice and adaptation provenance are retained in
[LICENSE.krikzz](../src/flashcart/ed64_pro/LICENSE.krikzz). The combined project
retains its existing AGPL license. No Sleekmenu64 implementation or tests were
copied; its ownership diagnosis informed the private-buffer design.

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
The SDK stub intentionally exposes no blocking/fixup DMA or CPU-PI API.
Host models do not establish physical bus or R4300 cache/interrupt behavior.

### Hardware procedure (approval and physical operator required)

1. Obtain explicit approval for a non-destructive SD copy and ordinary-ROM
   launch from stock firmware. Do not overwrite the stock OS/menu, update
   MCU/FPGA firmware, or use the SC64 deployment script on a Pro. Power off
   before host-side SD changes.
2. Record the on-screen build ID (including any `-dirty` suffix), actual EDID,
   cart revision, stock firmware, SD make/format, console region, memory size,
   and cold/warm-launch history. Stock firmware `26.1014` returning inherited
   `0xB0` is a historical Sleekmenu64 observation, not a new qualification result.
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

Software record on 2026-11-04; hardware coverage is not implied by these tests.

| Gate / case | Result |
|---|---|
| 39 host cases, normal and AddressSanitizer/UndefinedBehaviorSanitizer | PASS on macOS/Apple Clang and container Linux/GCC; includes all listed parameterized cases |
| Targeted N64 debug menu build | PASS with the inspected installed SDK; production Pro selection remains inactive |
| Hardware test ROM and timeout-injection ROM builds | PASS; separate output/object directories, production driver linked, no asset filesystem |
| N64 ELF buffer ownership | PASS: 512-byte staging aligned to 512, metadata outside its lines, two 16-byte aligned shared caller-line fixtures |
| Separate release menu and byte-identical Pro packaging | PASS; `cmp` confirms the Pro copy is byte-identical |
| Passive reads and normal boot: 64drive, ED64 V/X, SC64, other/unknown carts | NOT RUN; each unavailable family remains unqualified |
| Pro identity variants, stock handoff, repeated healthy launches, FIFO-count assumptions | NOT RUN |
| Real 100,000-operation cache/IRQ stress with actual DMA overlap | NOT RUN |
| Runtime timing margins across firmware / SD combinations | NOT RUN |
| Physical SD-readiness failure | NOT RUN; safe boot arrangement required |
| RAM-only error rendering after injected timeout, 4 MiB and 8 MiB | NOT RUN |
| Physical PI failure | NOT RUN; software injection is not physical-failure evidence |
| Actual menu terminal screen, USB/control exclusion, facade callback spies | DEFERRED to the hardware-gated integration stage |

The release build reports the existing unused `start_us` variable warning in
[grid.c](../src/menu/views/grid.c); that unrelated code was not changed.

**Pre-integration hardware gate: BLOCKED / unqualified.** Keep startup probing
inactive until the passive non-Pro reads, healthy Pro handoff/repeated starts,
FIFO assumptions, and real cache/IRQ stress have recorded results. Then add
the Pro facade error, immediate PI-independent menu terminal branch, facade
spy tests, and support documentation, and run the actual-menu/non-Pro checks.
Passing software tests alone does not complete the hardware-dependent milestone.
