## EverDrive-64 Pro developer notes

### Official documentation

https://github.com/krikzz/ed64-pro-pub/blob/main/MANUAL.md

### Protocol source and adaptations

- Source: https://github.com/krikzz/ed64-pro-pub
- Revision: 43014e290f85140d4c5ca84fbf5ae48ac755a23d
- Paths: edio/everdrive.c, edio/everdrive.h

The cartridge-local driver adapts register and FIFO access, bounded command framing, STATUS/NRESP, firmware information, MCU filesystem initialization, raw-sector I/O, SD-to-FCI loading, DEV stop/flags/paths/configuration/start, and RTC GET/SET. It uses fixed-width types, bounded libdragon ticket submission/completion, exclusive cache-line staging, and sticky failures instead of upstream's unbounded PI and protocol waits.

N64FlashcartMenu retains its AGPL license; the upstream copyright and MIT license notice appear [below](#upstream-license).

### Storage ownership

The driver requires firmware 26.1014 or newer. It stops MCU services and mounts volume 0 as `sd:/` through libdragon FatFs. MCU handoff requires stopping consumers, syncing writes, unmounting FatFs, and running FS INIT. I/O or release failures are terminal; never retry an ambiguous write. Power-cycle the console and USB after a fault, then check the files on a computer.

Only use `edlink` or other active USB tools with the console powered off or in the stock Pro menu. This menu has no USB transfer mode.

WRE transmits `DE AD BE EF`, matching edlink rather than the upstream N64 sample's read operation. Transfers calculate their own LBA stepping rather than copying upstream's byte-based address increment.

ROM and emulator loads use FatFs with capacity checks and staged paths. Save configuration remains local until the storage handoff; START loads the BRM file and enables MCU game services. The existing boot and cheat engine then runs.

Automatic CIC, automatic region detection, and MCU save writeback are enabled. CIC detection uses the shared IPL3 checksum path. ROM TV type comes from the shared destination-code metadata mapping; manual CIC/TV overrides take priority, and an unknown destination passes through the console TV type.

64DD, fast reboot, and USB debugging are not yet implemented.

FatFs paths remain UTF-8. Only staged GPAK/BRM paths use FatFs's CP437 table and actual 8.3 aliases for unrepresentable components. Missing or unusable aliases, including on exFAT, are rejected before storage handoff without lossy replacement. The input path without `sd:` and its MCU encoding are each limited to 1023 bytes.

`.v64` loading and a game launch are verified. On FAT32, a game booted and its fresh save was written under the correct Unicode name on paths over 300 bytes. Autoload and history paths retain the shared INI parser's 255-byte value limit.

Idle RESET returned to the stock menu on the tested setup. SM64 EEPROM settings persisted after immediate, 3-second and 10-second RESET trials, console-only and console-plus-USB power cycles, and intervening games.

Local synthetic tests passed EEPROM (4/16 Kbit) and SRAM (32/96 KiB) persistence under orderly MCU-stop power cycles. The 128 KiB SRAM, FlashRAM, and PKST2 fixtures had mismatches confined to the final four bytes. Retail SRAM/FlashRAM save-and-reload, emulator saves, and uncontrolled power-loss timing still need qualification.

With Jumper Pak (4 MiB), SM64 booted and restored saved settings, and both cheat actions were correctly refused. Re-testing fixed the shared missing-artwork error and long-name ticker assertion, but intermittent memory corruption, startup exceptions, navigation freezes, and historical file-open failures remain unresolved or unclassified; use an Expansion Pak (8 MiB) for now. See the [navigation qualification report](../../../docs/ed64_pro_navigation_qualification.md) for develop comparisons, passing controls, and remaining limits. SC64 boot/save and X7 boot regression checks passed on the NTSC console with 8 MiB. Emulator boot/save checks remain deferred to external testers.

Independent tester reports are still required before merge. See the [Pro test checklist](../../../docs/91_test_plan_matrix.md#everdrive-64-pro-testing). Gameplay screenshots establish booting, not save persistence.

### Real-time clock

Settings → Time reads and sets the Pro clock for 2000–2099. The menu, `time()`, `gettimeofday()`, `settimeofday()`, and file timestamps share that clock. Startup and Time-pane entry resync from hardware; ordinary reads advance a validated cache without MCU traffic.

Invalid hardware dates and failed reads or write confirmation report errors instead of using the software fallback. The clock remains editable to repair an invalid date. RTC capability is independent of clock availability. Game launch keeps its existing read-only RTC mode.

Verified on an original N64 with firmware `26.1014`: advancing time, Time-pane editing/readback/reentry, console restart, five seconds with console and USB power off, persisted FAT timestamps, and a Super Mario 64 launch. The original clock was restored with elapsed time, and menu settings were restored. RTC behavior on other hardware has not been tested. Host and sanitizer tests cover conversion, range limits, caching, lifecycle, and failures.

### Upstream license

MIT License

Copyright (c) 2026 krikzz

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
