## Preparations for OTHER WiP flashcarts
- It is recommended to format the SD Card to FAT32 with the default cluster size.
- Download the latest [menu](https://github.com/Polprzewodnikowy/N64FlashcartMenu/releases/) file specific for your flashcart and place it in the expected location.
- Remove the Micro SD Card from your computer and insert it into the flashcart.

### EverDrive-64 - WiP - UNTESTED AND UNSUPPORTED - USE AT OWN RISK
Currently not supported, but work is in progress (See [PRs](https://github.com/Polprzewodnikowy/N64FlashcartMenu/pulls)).
> [!CAUTION]
> The menu may be able to load ROMs but cannot guarantee save functionality. Existing saves may be corrupted.

#### EverDrive-64 (V series)
The aim is to reach feature parity with [ED64-UnofficialOS](https://github.com/n64-tools/ED64-UnofficialOS-binaries) / [ED64-OfficialOS](https://krikzz.com/pub/support/everdrive-64/v2x-v3x/os-bin/).

* Download the `OS64.v64` ROM from the latest [action run - assets] and place it in the `/ED64` folder.

#### EverDrive-64 (X series)
The aim is to reach feature parity with [OS](https://krikzz.com/pub/support/everdrive-64/x-series/OS/) for now.

* Download the `OS64.v64` ROM from the latest [PR action run - assets] and place it in the `/ED64` folder.

#### ED64P (clones)
The aim is to reach feature parity with [Altra64](https://github.com/networkfusion/altra64)

* Download the `OS64P.v64` ROM from the latest [PR action run - assets] and place it in the `/ED64P` folder.

### EverDrive-64 Pro: detection/transport only

The Pro is identified by its family and device byte, then checked using bounded
PI transfers, initial FIFO cleanup/status, device stop and stable mailbox READY,
post-stop cleanup/fresh status, and MCU SD initialization/fresh status. This
check does **not** mount an N64 filesystem or provide a storage backend.

The Pro uses the shared flashcart interface and startup fault handling. Its
adapter initializes the transport and returns the existing function-not-supported
error when transport is ready, or an internal error when transport fails. The
retained diagnostic message explains the missing storage backend or the failed
phase and transport/original MCU status. Inherited well-formed nonzero status is
distinguished from errors returned by our stop/SD commands.

A failed identity read also selects the safe Pro adapter to prevent fallback,
but returns an internal error without invoking transport initialization. Every
such startup result selects `MENU_MODE_FAULT` and returns from initialization
before filesystem, asset/font/background loading, browser setup, or USB polling.
The existing fault view uses the built-in font and `menu->flashcart_err`. The
adapter advertises no menu features; its mandatory storage callbacks return
function-not-supported without I/O. Firmware version retrieval is not implemented.

Power-cycle to return to stock firmware after this screen, especially after
transport failure. There are no automatic retries, FIFO resynchronization
attempts, shared-DMA resets, or cancellation claims. A timed-out DMA retains
its private buffer/ticket for the rest of the run. Console-to-debug forwarding
is disabled; the terminal screen uses RAM/framebuffer/VI and the built-in font
rather than further PI or filesystem work.

For testing, launch an ordinary ROM from stock firmware using an approved
non-destructive copy/upload procedure. Do not replace the stock OS, update
MCU/FPGA firmware, or use SC64 deployment scripts on a Pro. The existing
`menu.n64` packaging rule is only a byte-identical copy, not a claim of usable
Pro OS support. See [test commands and qualification limits](91_test_plan_matrix.md#everdrive-64-pro-transport-qualification).

The selected protocol subset is adapted from
[krikzz/ed64-pro-pub at `43014e290f85140d4c5ca84fbf5ae48ac755a23d`](https://github.com/krikzz/ed64-pro-pub/tree/43014e290f85140d4c5ca84fbf5ae48ac755a23d).
Its complete copyright 2026 krikzz MIT notice and adaptation provenance are
in the [developer notes](../src/flashcart/ed64_pro/README.md). This project
remains AGPL. No upstream filesystem/launch/bulk ACK implementation is imported.

Filesystem/file APIs, browsing/loading/saves, cartridge-memory bulk transfers,
launch/device start, feature/firmware reporting, RTC/64DD/USB, recovery/reset,
kernel-thread/interrupt-context transport, and firmware updates remain deferred.
