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

The complete upstream copyright and MIT license notice is in the
[main README](../../../README.md#everdrive-64-pro-api-mit-license).

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
