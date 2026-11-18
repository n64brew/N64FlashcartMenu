# Flashcart voltage diagnostics

The Pro exposes supply voltage but no temperature sensor. Its voltage-only
callback lets the existing startup warning and Settings diagnostics use that
reading while continuing to report temperature as `Unsupported`. Implementing
the combined callback would falsely advertise a temperature reading; hiding
voltage would omit the existing low-voltage warning for this cartridge.

This small capability extension stays with Pro support so those existing views
can use its diagnostics through the flashcart interface, without cartridge
checks in menu code. The combined callback and SummerCart64 backend are unchanged:
`flashcart_get_voltage()` falls back to the combined callback, while Settings
continues to request the combined sample directly when temperature is supported.
No additional diagnostic view or setting is introduced.

## Automated regression checks

From the repository root:

```sh
make -f tests/Makefile test-flashcart-diagnostics
make -f tests/Makefile test-flashcart-diagnostics-sanitize
```

Both commands passed on macOS on 2026-11-18: seven cases per mode. They execute
the production facade, startup decisions, Settings formatting and modal controls
with mocked backends. Coverage includes combined-callback fallback, error
propagation, unsupported temperature, signed temperature formatting, and the
3000 mV warning boundary. These checks do not establish physical SC64 operation.

## SummerCart64 hardware regression

Record the menu ROM SHA-256, cart firmware and console/memory configuration for
each run. Using the same menu build as the Pro regression:

| Check | Expected result |
| --- | --- |
| Boot at the cart's normal supply voltage | No spurious low-voltage warning. |
| Settings, flashcart pane, Z: Diagnostics | Numeric voltage and signed temperature; neither is marked unsupported. |
| Close with B, reopen with Z, close with Z | Existing modal controls work and return to Settings. |
| Browse and launch a known working ROM | Ordinary operation still works after diagnostics. |

Physical regression results are recorded in the [Pro validation summary](ed64_pro/VALIDATION.md).
