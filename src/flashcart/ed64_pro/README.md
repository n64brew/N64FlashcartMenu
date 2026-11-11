## EverDrive-64 Pro developer notes

### Official documentation

https://github.com/krikzz/ed64-pro-pub/blob/main/MANUAL.md

### Protocol source and adaptations

Source: https://github.com/krikzz/ed64-pro-pub
Revision: 43014e290f85140d4c5ca84fbf5ae48ac755a23d
Paths: edio/everdrive.c, edio/everdrive.h

The cartridge-local driver adapts only the register layout, FIFO word/byte
encoding, command/status framing, mailbox synchronization, device stop, and
MCU SD initialization. It uses fixed-width types, bounded libdragon ticket
submission/completion, exclusive cache-line staging, and a sticky failure
lifecycle instead of upstream's unbounded PI and protocol waits.

The combined N64FlashcartMenu project retains its existing AGPL license.

The complete upstream copyright and MIT license notice is retained
[below](#upstream-license).

### Menu integration

The Pro retains a `flashcart_t` adapter while storage support is incomplete.
An identified device initializes through its shared callback; transport errors
map to `FLASHCART_ERR_INT`, and transport READY maps to
`FLASHCART_ERR_FUNCTION_NOT_SUPPORTED`. An indeterminate probe selects the same
safe adapter but returns an internal error without initializing the transport
or trying another cartridge. All such paths leave the storage prefix NULL.

Mandatory storage callbacks return function-not-supported without I/O, feature
queries return false, and the firmware version callback returns a zero
placeholder rather than a measured version. Optional services remain absent.
The optional error-message callback formats retained diagnostics without I/O;
the shared conversion function keeps its generic fallback for other errors.
The shared startup fault view stops before filesystem, USB, controller, or
normal menu initialization. Transport READY does not provide usable storage.

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
