## EverDrive-64 Pro developer notes

### Official documentation

https://github.com/krikzz/ed64-pro-pub/blob/main/MANUAL.md

### Protocol source and adaptations

- Source: https://github.com/krikzz/ed64-pro-pub
- Revision: 43014e290f85140d4c5ca84fbf5ae48ac755a23d
- Paths: edio/everdrive.c, edio/everdrive.h

The cartridge-local driver adapts register and FIFO access, bounded command framing, STATUS/NRESP, firmware information, MCU filesystem initialization, raw-sector I/O, SD-to-FCI loading, DEV stop/flags/paths/configuration/start, and diagnostic RTC GET.

Register accesses use libdragon `io_read()`/`io_write()`; FIFO transfers use synchronous `dma_read()`/`dma_write()` with exclusive, aligned cache-line staging and explicit cache maintenance. Each FIFO byte occupies exactly one 32-bit bus word. These addresses are already in the SDK wrappers' ROM range, and aligned reads take the exact-length path without extra FIFO reads.

Libdragon serializes PI access and waits for DMA completion. A mailbox `io_write()` is followed by `dma_wait()` because register writes can return before the bus write completes. Transaction end needs no further wait. FIFO availability, MCU replies, mailbox readiness, STATUS, SD initialization, and cleanup retain protocol deadlines and limits checked between completed SDK operations; these cannot interrupt a synchronous SDK hardware wait. The first protocol fault and confirmed progress remain latched, and ambiguous writes are never retried.

N64FlashcartMenu retains its AGPL license; the upstream copyright and MIT license notice appear [below](#upstream-license).

### Storage ownership

The driver requires firmware 26.1014 or newer. It stops MCU services and mounts volume 0 as `sd:/` through libdragon FatFs. MCU handoff requires stopping consumers, syncing writes, unmounting FatFs, and running FS INIT. I/O or release failures are terminal; never retry an ambiguous write. Power-cycle the console and USB after a fault, then check the files on a computer.

Only use `edlink` or other active USB tools with the console powered off or in the stock Pro menu. This menu has no USB transfer mode.

WRE transmits `DE AD BE EF`, matching edlink rather than the upstream N64 sample's read operation. Transfers calculate their own LBA stepping rather than copying upstream's byte-based address increment.

ROM and emulator loads use FatFs with capacity checks and staged paths. Save configuration remains local until the storage handoff; START loads the BRM file and enables MCU game services. The existing boot and cheat engine then runs.

Automatic CIC, automatic region detection, and MCU save writeback are enabled. CIC detection uses the shared IPL3 checksum path. ROM TV type comes from the shared destination-code metadata mapping; manual CIC/TV overrides take priority, and an unknown destination passes through the console TV type.

Fast reboot and USB debugging are not implemented.

FatFs paths remain UTF-8. Only staged GPAK/BRM/disk paths use FatFs's CP437 table and actual 8.3 aliases for unrepresentable components. Missing or unusable aliases, including on exFAT, are rejected before storage handoff without lossy replacement. The input path without `sd:` and its MCU encoding are each limited to 1023 bytes.

`.v64` loading and a game launch are verified. On FAT32, a game booted and its fresh save was written under the correct Unicode name on paths over 300 bytes. Autoload and history paths retain the shared INI parser's 255-byte value limit.

Idle RESET returns to the stock menu. SM64 EEPROM settings persist after immediate, 3-second and 10-second RESET trials, console-only and console-plus-USB power cycles, and intervening games.

Local synthetic tests passed EEPROM (4/16 Kbit) and SRAM (32/96 KiB) persistence under orderly MCU-stop power cycles. The 128 KiB SRAM, FlashRAM, and PKST2 fixtures had mismatches confined to the final four bytes. Beyond the F-Zero SRAM result below, other retail SRAM/FlashRAM save-and-reload, emulator saves, and uncontrolled power-loss timing still need qualification.

### Native 64DD

The existing menu selects a matching native-endian 4 MiB IPL: `/menu/64ddipl/NDDJ2.n64` (Japan retail), `NDDE0.n64` (USA prototype), or `NDXJ0.n64` (development). The backend verifies the canonical filename against its header identity/revision and stages it through the bounded FCI window at `0x0F800000`. After START, mapped IPL words must match the selected file before shared boot proceeds. An Expansion Pak is required; disconnect a physical 64DD.

Only writable 64,931,840-byte native `.ndd` images are supported. Preview the disk and press A for standalone IPL startup. For a combo, preview the cartridge, return with B, preview the disk and press Z. The unchanged shared loader stops on ROM/save errors before loading DD. Backend ROM/auxiliary loading and save preparation must both finish before a cartridge is retained; incomplete preparation is discarded when DD begins. IPL and disk readiness are independent, so a missing or invalid IPL/disk does not discard a complete cartridge.

The backend enables DD with GPAK and its cartridge save services when a complete cartridge remains prepared; otherwise it selects IPL with cartridge backup services disabled. The existing shared boot parameters choose the startup aperture: standalone starts at DD IPL even when an earlier complete GPAK/save remains configured. Selecting standalone does not clear every earlier cartridge mapping. No freshness heuristic identifies launch intent.

DD attachment rejects a retained cartridge extent above 120 MiB **before writing any IPL bytes**, then invalidates that cartridge/save preparation. This protects the IPL mirror without reducing ordinary ROM-loading capacity. A freshly prepared oversized combo fails; a first standalone attempt after abandoned oversized preparation can also fail, and the next standalone attempt can proceed after that rejection. There is no silent truncation or fallback that drops an intended combo.

Both existing DD callbacks stage the primary image. The multi-disk callback ignores the menu's sibling list because firmware discovers its own swap group. Keep the group in one directory; the physical cartridge button advances disks. The menu retains its existing three-slot preview, which does not describe the firmware's full group or order. DD launches use STD RTC; ordinary ROM launches retain RDO. Checked storage handoff, readiness, mapping verification, and failure diagnostics use the existing backend facilities. Recoverable menu failures retain the generic shared error text.

Restoring the shared disk view also restores metadata preview before its later Expansion Pak refusal. The revised production build passed both steps on its first cold Jumper Pak boot, followed by ordinary F-Zero ROM startup. Historical intermittent 4 MiB preview stalls and startup RDP faults remain unisolated; this focused passing run does not establish a general fix. Native DD RTC SET remains unqualified and is outside this cleanup. Other console regions and USA/development-disk compatibility remain untested.

Host checks: `make -f tests/Makefile test-ed64-pro test-ed64-pro-sanitize test-cart-load-dd test-cart-load-dd-sanitize`.

### Real-time clock

After mounting FatFs, the Pro backend sends a documented DEV_SET_CFG with ROM_OFF, ROM size zero, STD RTC, and every other field zero, then DEV_START before the first `rtc_init()`. Backup RAM and 64DD services remain disabled while N64 FatFs owns storage. No ROM extent, bundle enumeration, bank fingerprinting, FCI samples, private SDK API, or production build metadata is needed.

ROM_OFF disables ROM mapping according to the vendor documentation; it does not mean "preserve configuration", and size zero is not an "unchanged" convention. On the tested original N64 and Pro firmware 26.1014, this configuration left the default cartridge read aperture usable through both stock-menu SD launch and directly installed physical cold boot. A 3 MiB probe independently verified a known fixture at offset `0x280000` and compared fresh header/fixture bytes before and after START. This is measured behavior on that hardware/firmware, not a claim about every firmware implementation.

Libdragon then detects the genuine Joybus RTC and supplies its ordinary POSIX clock integration and cache. There are no Pro clock overrides. The unchanged Settings > Time pane reads and saves the clock; leaving and reentering refreshes the same native cache. Game launch closes native RTC state and stops the menu services before FatFs unmount/FS INIT. Ordinary ROMs use game-side RDO configuration; DD launches configure STD mode.

Native DD reads passed, but the identical DD SET probe failed to match its target within a two-second window under both final replacement and visually verified stock menus. Both runs verified the original advancing clock and confirmed STOP. DD RTC SET remains unqualified on this hardware/firmware; neither an exclusive backend defect nor a firmware cause is established. This DD command result is separate from the menu's Joybus RTC integration.

Use hardware dates in 2000–2099. The shared year selector remains 1996–2095, so qualification uses the supported intersection; no shared range or invalid-clock editing behavior was changed. If genuine detection succeeds but the calendar is invalid, public source resynchronization preserves the clock error instead of presenting libdragon's software fallback date. Failed service transitions retain their first error and require a console/cartridge power cycle.

OFF/zero probes selected genuine Joybus RTC after START on both launch routes; settled native and physical MCU readings agreed within one second. MCU RTC GET can lag a native Joybus write by approximately one second, so immediate cached success alone is not persistence evidence.

### Upstream license

MIT License

Copyright (c) 2026 krikzz

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
