## EverDrive-64 Pro developer notes

### Official documentation

https://github.com/krikzz/ed64-pro-pub/blob/main/MANUAL.md

### Protocol source and adaptations

Source: https://github.com/krikzz/ed64-pro-pub
Revision: 43014e290f85140d4c5ca84fbf5ae48ac755a23d
Paths: edio/everdrive.c, edio/everdrive.h

The cartridge-local driver adapts register and FIFO access, bounded command
framing, STATUS/NRESP, device stop, firmware information, MCU filesystem
initialization, and raw-sector reads/writes. It uses fixed-width types,
bounded libdragon ticket submission/completion, exclusive cache-line staging,
and sticky failures instead of upstream's unbounded PI and protocol waits.

The combined N64FlashcartMenu project retains its existing AGPL license.
The upstream copyright and MIT license notice is retained [below](#upstream-license).

### Storage ownership

The driver requires firmware 26.1014 or newer. It stops MCU services and mounts
volume 0 as `sd:/` through libdragon FatFs. Before returning storage to the MCU,
consumers stop, writes must be synchronized, FatFs unmounts, and FS INIT runs.
A failed I/O or release is terminal; never retry an ambiguous write. Power-cycle
the console and USB after a fault, then check the files on a computer.

Use edlink or other active USB tools only with the console off or while the
stock Pro menu runs. Keep them idle while this menu or a launched game runs.
There is no in-menu USB transfer mode.

WRE transmits `DE AD BE EF`, matching edlink rather than the upstream N64
sample's read operation. Transfers calculate their own LBA stepping rather
than copying upstream's byte-based address increment.

Game loading and launch remain unavailable until their integration steps pass.
Optional features are not advertised.

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
