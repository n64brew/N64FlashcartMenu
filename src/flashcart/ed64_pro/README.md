## EverDrive-64 Pro developer notes

### Official documentation

https://github.com/krikzz/ed64-pro-pub/blob/main/MANUAL.md

### Protocol source and adaptations

- Source: https://github.com/krikzz/ed64-pro-pub
- Revision: 43014e290f85140d4c5ca84fbf5ae48ac755a23d
- Paths: edio/everdrive.c, edio/everdrive.h

The cartridge-local driver adapts register and FIFO access, bounded command framing, STATUS/NRESP, firmware information, MCU filesystem initialization, raw-sector I/O, SD-to-FCI loading, DEV stop/flags/paths/configuration/start, and RTC GET/SET.

Register accesses use libdragon `io_read()`/`io_write()`; FIFO transfers use synchronous `dma_read()`/`dma_write()` with exclusive, aligned cache-line staging and explicit cache maintenance. Each FIFO byte occupies exactly one 32-bit bus word. These addresses are already in the SDK wrappers' ROM range, and aligned reads take the exact-length path without extra FIFO reads.

Libdragon serializes PI access and waits for DMA completion. A mailbox `io_write()` is followed by `dma_wait()` because register writes can return before the bus write completes. Transaction end needs no further wait. FIFO availability, MCU replies, mailbox readiness, STATUS, SD initialization, and cleanup retain protocol deadlines and limits checked between completed SDK operations; these cannot interrupt a synchronous SDK hardware wait. The first protocol fault and confirmed progress remain latched, and ambiguous writes are never retried.

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

Idle RESET returns to the stock menu. SM64 EEPROM settings persist after immediate, 3-second and 10-second RESET trials, console-only and console-plus-USB power cycles, and intervening games.

Local synthetic tests passed EEPROM (4/16 Kbit) and SRAM (32/96 KiB) persistence under orderly MCU-stop power cycles. The 128 KiB SRAM, FlashRAM, and PKST2 fixtures had mismatches confined to the final four bytes. Retail SRAM/FlashRAM save-and-reload, emulator saves, and uncontrolled power-loss timing still need qualification.

### Real-time clock

The Pro backend initializes libdragon time after mounting storage, then installs its clock hooks. The unchanged menu's later `rtc_init()` is idempotent. The menu, `time()`, `gettimeofday()`, and file timestamps use the Pro clock; ordinary reads advance a validated cache without MCU traffic.

Pro-specific clock editing in Settings is deferred to a separate PR. Set the clock in the stock Pro menu or with `edlink` while this menu is not running. Hardware dates must be in 2000–2099. Invalid dates and failed reads report errors instead of using a software fallback. Game launch keeps its existing read-only RTC mode.

### Upstream license

MIT License

Copyright (c) 2026 krikzz

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
