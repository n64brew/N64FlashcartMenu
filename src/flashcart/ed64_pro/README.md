## EverDrive-64 Pro developer notes

### Official documentation

- [Manual](https://github.com/krikzz/ed64-pro-pub/blob/main/MANUAL.md)
- [Protocol source](https://github.com/krikzz/ed64-pro-pub), revision `43014e290f85140d4c5ca84fbf5ae48ac755a23d`, `edio/everdrive.c` and `edio/everdrive.h`

Firmware **26.1014 or newer** is required. The backend supports N64 and emulator
ROMs, save persistence, cheats, Controller Pak operations, automatic CIC/region
detection, native 64DD, RTC reads/writes, and diagnostics. Fast reboot and USB
debugging are not implemented.

N64FlashcartMenu retains its AGPL license; the adapted driver retains the
upstream copyright and MIT notice [below](#upstream-license).

### Support limits

The tested configuration is an original NTSC-U console with an 8 MiB Expansion
Pak. Intermittent menu preview/startup faults with a 4 MiB Jumper Pak remain
unresolved. Use an Expansion Pak; native DD requires one independently of this
menu limitation.

### Transport and storage ownership

The cartridge-local driver handles register/FIFO access, command framing,
firmware information, raw-sector I/O, ROM loading, launch configuration, and
storage handoff. Register accesses use libdragon `io_read()`/`io_write()`;
FIFO transfers use synchronous DMA with aligned cache-line staging and explicit
cache maintenance. Each FIFO byte occupies one 32-bit bus word.

Libdragon serializes PI access and waits for DMA completion. Mailbox writes
also call `dma_wait()`. Protocol deadlines bound polling between SDK operations;
they cannot interrupt a synchronous SDK hardware wait. The first fault and
confirmed progress are retained, and ambiguous writes are never retried.

The N64 owns volume 0 as `sd:/` through FatFs while the menu runs. Before launch,
consumers stop, writes synchronize, FatFs unmounts, and FS INIT returns storage
to the MCU. START loads the save file and enables game services. A failed I/O or
handoff requires a console/cartridge power cycle.

Only use active USB tools with the console powered off or in the stock Pro menu.
This menu has no USB transfer mode.

FatFs paths remain UTF-8. Paths passed to the MCU use CP437, with actual FatFs
8.3 aliases for characters outside that encoding. Missing or unusable aliases
are rejected before handoff, including on exFAT. Both the input path without
`sd:` and its MCU encoding are limited to 1023 bytes. Autoload and history
retain the shared INI parser's 255-byte value limit. Manual CIC/TV overrides
take priority over automatic detection.

EEPROM (4/16 Kbit) and SRAM (32/96 KiB) saves are supported. Firmware `26.1014`
corrupts the last four bytes of 128 KiB SRAM and FlashRAM saves, including
PKST2. This does not affect known retail games; homebrew using those bytes needs
a firmware fix.

### Diagnostics

Settings > Flashcart > Z shows the live cartridge button and 3.3 V supply in
millivolts. Temperature is unsupported. Voltage uses a validated GET_INF request
and a one-second cache; a failed refresh returns an error. Startup uses the same
reading for its low-voltage warning below 3000 mV.

See [diagnostics](../../../docs/ed64_pro_diagnostics.md) for controls and manual
checks, and the [shared voltage rationale and regression checks](../../../tests/flashcart_diagnostics.md).

### Native 64DD

Hardware tests demonstrate disk boot and swapping. Disk-save persistence has
not yet been qualified with a disk edit, restart, and reload/readback test.

An Expansion Pak is required; disconnect a physical 64DD. Use writable,
native-endian 64,931,840-byte `.ndd` images and the matching native-endian 4 MiB
IPL in `/menu/64ddipl/`:

- `NDDJ2.n64`: Japan retail.
- `NDDE0.n64`: USA prototype.
- `NDXJ0.n64`: Development.

Preview a disk and press A for standalone startup. For a cartridge/disk combo,
preview the cartridge, return with B, then preview the disk and press Z. Combo
ROMs are limited to 120 MiB to leave room for the IPL; oversized combos are
rejected before the IPL is written.

The backend validates IPL identity, prepares the disk and cartridge save,
hands storage to the MCU, and checks the mapped IPL before boot. Failed ROM or
save preparation cannot leave a partial cartridge configured for a DD launch.
Standalone startup enters the DD IPL; combo startup uses the prepared cartridge.

Firmware discovers disk-swap groups in the same directory. Press the physical
cartridge button to advance disks. The menu's three-slot preview does not limit
the firmware's swap group. DD launches use STD RTC; ordinary ROM launches use
RDO. Set the clock in Settings > Time before launching a disk: firmware
`26.1014` does not apply native DD RTC SET commands.

### Real-time clock

After mounting FatFs, the backend starts MCU services with ROM_OFF, zero ROM
size, STD RTC, and cartridge/save/DD services disabled. Libdragon detects the
Joybus RTC and provides its normal POSIX clock integration and cache.

The Pro path sets libcart's `cart_type` to `CART_MAX` to suppress overlapping
legacy-cart probes during SDK RTC detection. This sentinel is outside libcart's
valid driver indices: Pro operations stay on the Pro backend and must never
pass it to `cart_init()`, `cart_exit()`, or `cart_card_*()`.

**Settings > Time** reads and saves the clock; leaving and reentering refreshes it. Game
launch closes the RTC and stops menu services before storage handoff.

The hardware calendar supports 2000–2099; the shared Time editor offers years
through 2095. An invalid hardware calendar reports a clock error rather than a
software fallback date. MCU RTC GET can trail a Joybus write by about one
second. Native DD RTC SET is affected by the firmware limitation above; writing
through Settings > Time works.

Run the [host tests](../../../tests/README.md) for transport, lifecycle,
adapter, diagnostics, RTC, and DD loading coverage. See the
[build-specific hardware results](../../../tests/ed64_pro/VALIDATION.md) for
measured RTC/save outcomes and remaining merge evidence.

### Upstream license

MIT License

Copyright (c) 2026 krikzz

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
