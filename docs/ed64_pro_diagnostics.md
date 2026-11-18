# EverDrive-64 Pro diagnostics

Open **Settings > Flashcart**, then press **Z** to show the Diagnostics overlay.
Press **Z** or **B** to close it.

The overlay shows:

- The physical cartridge button's Pressed/Released state.
- The cartridge's 3.3 V supply in millivolts, refreshed once per second.
- `Unsupported` for temperature, which the Pro does not report.

An unsuccessful voltage read displays an error. Startup also uses this voltage
reading for the existing low-voltage warning below 3000 mV.

## Testing

No USB tools or capture hardware are required. Build and install the menu using
the [normal development workflow](99_developer_guide.md), then:

1. Open Diagnostics and check that voltage is displayed in millivolts.
2. Press and release the cartridge's physical button. Check that the displayed
   state follows it.
3. Leave the overlay open for several seconds to check that readings refresh.
   The value can stay the same when the supply is stable.
4. Close with Z, reopen with Z, and close with B.
5. Return to the file browser and launch a ROM.

The [host tests](../tests/README.md) cover voltage decoding, refresh timing,
failed reads, button state, the overlay, and low-voltage warning boundaries.
