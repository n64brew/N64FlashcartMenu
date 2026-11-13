## EverDrive-64 Pro developer notes

### Official documentation

https://github.com/krikzz/ed64-pro-pub/blob/main/MANUAL.md

### Protocol source and adaptations

Source: https://github.com/krikzz/ed64-pro-pub
Revision: 43014e290f85140d4c5ca84fbf5ae48ac755a23d
Paths: edio/everdrive.c, edio/everdrive.h

The cartridge-local driver adapts register and FIFO access, bounded command
framing, STATUS/NRESP, firmware information, MCU filesystem initialization,
raw-sector I/O, SD-to-FCI loading, and DEV stop/flags/paths/configuration/start.
It uses fixed-width types, bounded libdragon ticket submission/completion,
exclusive cache-line staging, and sticky failures instead of upstream's
unbounded PI and protocol waits.

N64FlashcartMenu retains its AGPL license; the upstream copyright and MIT
license notice appear [below](#upstream-license).

### Storage ownership

The driver requires firmware 26.1014 or newer. It stops MCU services and mounts
volume 0 as `sd:/` through libdragon FatFs. MCU handoff requires stopping consumers,
syncing writes, unmounting FatFs, and running FS INIT. I/O or release failures are
terminal; never retry an ambiguous write. Power-cycle the console and USB after
a fault, then check the files on a computer.

Only use `edlink` or other active USB tools with the console powered off or in
the stock Pro menu. This menu has no USB transfer mode.

WRE transmits `DE AD BE EF`, matching edlink rather than the upstream N64
sample's read operation. Transfers calculate their own LBA stepping rather
than copying upstream's byte-based address increment.

ROM and emulator loads use FatFs with capacity checks and staged paths. Save
configuration remains local until the storage handoff; START loads the BRM file
and enables MCU game services. The existing boot and cheat engine then runs.

Automatic CIC and MCU save writeback are enabled. 64DD, fast reboot, USB
debugging, menu RTC and automatic region detection are not yet implemented.

FatFs paths remain UTF-8. Only staged GPAK/BRM paths use FatFs's CP437 table
and actual 8.3 aliases for unrepresentable components. Missing or unusable aliases,
including on exFAT, are rejected before storage handoff without lossy replacement.
The input path without `sd:` and its MCU encoding are each limited to 1023 bytes.

`.v64` loading and a game launch are verified. On FAT32, a game booted and its
fresh save was written under the correct Unicode name on paths over 300 bytes.
Autoload and history paths retain the shared INI parser's 255-byte value limit.

Idle RESET returned to the stock menu on the tested setup. SM64 EEPROM settings
persisted after immediate, 3-second and 10-second RESET trials, console-only
and console-plus-USB power cycles, and intervening games. Other save families
have boot/configuration checks; their persistence and direct power-loss timing
remain unqualified.

With Jumper Pak (4 MiB), SM64 booted and restored saved settings, and both cheat
actions were correctly refused. Earlier intermittent navigation/artwork/open
failures remain unresolved. SC64 boot/save and X7 boot regression checks passed
on the NTSC console with 8 MiB. Emulator boot/save checks are user-deferred to
external testers.

### Upstream license

MIT License

Copyright (c) 2026 krikzz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
