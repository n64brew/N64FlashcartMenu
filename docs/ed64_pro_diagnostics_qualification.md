# ED64 Pro Diagnostics qualification

Implemented on `acd/ed64-pro/04-diagnostics`, based on `ea6baa49`.

The Pro exposes its physical top button and 3.3 V supply through the existing
Diagnostics overlay. Temperature is explicitly `Unsupported`. Voltage is sampled
on first use and then at most once per second; an unsuccessful refresh reports
an error and does not publish stale data. The startup warning keeps its existing
strictly-below-3000 mV condition, text, and control flow.

## Necessary shared changes

- `flashcart.[ch]`: one optional voltage callback and two wrappers permit voltage
  without inventing temperature. The wrapper prefers that callback, otherwise
  uses the existing combined callback with a local temperature output. The
  combined API and SummerCart64 backend remain unchanged.
- `flashcart_info.c`: recognize voltage-only support and show an unsupported
  temperature while retaining the same rows, overlay, and controls. The existing
  combined display, including negative temperatures, is preserved.
- `startup.c`: use the voltage wrapper so the existing warning also covers Pro.

## Host and build validation

`make -f tests/Makefile test-ed64-pro test-ed64-pro-sanitize` passes all 318 test
executions. Coverage includes dispatch/fallback/invalid arguments, packed BCD and
unchanged failed outputs, exact bounded GET_INF requests, partial/trailing/missing
responses, terminal faults, live 32-bit bit-16 button reads, cache timing and
failed refreshes, startup at 2990/3000/3010 mV and unavailable voltage, and actual
overlay/startup production code. Shared tests remain under `tests/`; Pro tests
extend the existing `tests/ed64_pro/` fixtures.

The production ROM builds through `./acd-build.sh`; a final incremental build
uses the same installed repository SDK and container. No dependencies, firmware,
RTC, storage handoff, USB support, or other backend implementation was changed.

## Hardware evidence

Test date: 2026-11-18. One original N64 with Expansion Pak, ED64 Pro serial
`0005EE06.0000077F`, firmware `26.1014`. Evidence and exact source/ROM hashes are
under the ignored local directory `output/ed64-pro-diagnostics-20261118/`.

- The final production ROM is SHA-256
  `3f1fa8c2b91b81fda564392182d0378f793e641cd233589ef58f7e998b93795a`.
- Edlink reported 3240 mV with the stock menu active, in a separate ownership
  session. The custom menu displayed 3240 mV, with subsequent samples also
  changing. This compares the vendor and menu interpretation, not calibration
  against an independent voltmeter.
- Physical top-button Pressed and Released states were captured in the actual
  Diagnostics overlay. The user independently confirmed that pressing worked.
- Five final-build captures include Pressed, then Released, with live readings
  of 3150/3160 mV. Z and B both exit the overlay, and Z reopens it normally.
- The unchanged Time pane resynchronized from `01:15:51` to `01:16:16` on
  reentry. No date/time fields were changed or saved.
- Existing SD browser/history data and the Super Mario 64 preview loaded;
  launching that existing ROM reached its responsive SELECT FILE screen after
  the normal storage handoff. This is a launch regression check, not a new
  save-persistence or cheat-compatibility qualification.
- After transient USB discovery/power recovery, the installed custom menu also
  booted directly and displayed Released / 3230 mV. Earlier reboot attempts
  returned to stock; those attempts remain in the local evidence.
- Low-voltage conditions and transport failures are host-tested only; the
  console was not physically undervolted or fault-injected.

The known-working button branch was reviewed for its service-start requirement.
This checkout already starts MCU services for native RTC support, so no extra
service lifecycle or storage-ownership changes were necessary.

## Qualification limits

The production ROM was launched through edlink while the stock menu owned the
card. Active host MCU/filesystem commands were not used while the custom menu
owned FatFs. The later direct custom-menu boot used the same verified ROM.
Other consoles, firmware versions, 4 MiB systems, and physical low-voltage
behavior remain untested. SummerCart64 preservation is host-tested; no physical
SummerCart64 was used.

## Preservation

Final readback verified all 16 backed-up files. `/ed64/menu.n64` intentionally
contains the final production ROM; its original binary remains backed up. The
pre-existing `/ed64/menu.n64` and `/N64FlashcartMenu.n64` had different hashes, so
they were backed up separately. `/N64FlashcartMenu.n64`, menu configuration and
history, SM64 settings/cheats/save, and all nine existing Pro sysdata files match
their original bytes. Only sysdata `config.ini` and `fm-state.dat` needed
restoration. The evidence is `preservation.json` and the before/after readbacks.
Final power is N64 off, USB on, matching the starting state.
