# Menu navigation and artwork hardware tests

`menu_stress.py` drives an ordinary menu build through NICE64 and records a PNG
following every controller script. It records scripts and hashes, command
arguments, exit codes, timings and logs. It stops on tool failures. A completed
batch requires screen checks; controller acknowledgments do not prove that the
menu performed the intended action. Supply `--check-command` for automated OCR,
pixel/color checks or another deterministic image checker. The recorder appends
each capture path to that command and stops if it returns nonzero or prints an
`ERROR` line. Its verdict covers only the supplied check criteria.

## Preparation

Use a verified NICE64 Controller port explicitly. Run
`~/bin/detect-hdmi-capture-device` and keep its printed command prefix unchanged.
Power control and ROM deployment are separate from this recorder. With ED64 Pro,
use edlink only with the console off or the stock menu active. Preserve the firmware and any custom
`/ed64/menu.n64` override, and back up configuration/history before changing test settings.
Verify the stock screen, then the launched menu, before starting a batch.

Use separate evidence directories for the original and fixed ROMs and for each
memory Pak. Record the menu commit/hash, firmware, console, RAM size, settings,
fixture hashes and initial selection. Keep interrupted or invalid attempts, but
exclude them from qualification counts.

## Record a batch

Write absolute-path `.n64script` files for each transition. For example, when a
six-directory chain is selected in Files:

```text
# deep-enter.n64script: finish at the leaf directory
press A pause 1s *6
wait 500ms
```

```text
# deep-info.n64script: finish at ROM details
press A
wait 1s
```

```text
# deep-return.n64script: ROM details -> six parents
press B pause 1s *7
wait 500ms
```

Run the cases in order; each repetition continues from the previous case's
ending selection. Add separate Left/Right cases before returning if testing
artwork, so every changed image has its own captured screen.

```sh
python3 tests/menu_stress.py \
  --port "$NICE64_PORT" \
  --output "$EVIDENCE_DIRECTORY" \
  --capture-prefix "$CAPTURE_PREFIX" \
  --check-command 'python3 /absolute/check_screen.py' \
  --repeat 10 \
  --case deep-enter=/absolute/deep-enter.n64script \
  --case deep-info=/absolute/deep-info.n64script \
  --case deep-return=/absolute/deep-return.n64script
```

For a single checkpoint, use `--once LABEL --inputs 'press A; wait 1s'` instead
of `--case`. The output directory must be new or empty. Review `manifest.json`
and the checker results, including expected errors and recovery screens. Without
a checker, review every decisive capture. For
ffmpeg devices that occasionally return a black first frame, append `-ss 1` to
the detected capture prefix to read a second of video before taking the still.
Keep a failed capture and a follow-up image separate; a single black frame does
not establish a menu crash.

## Regression scenarios

- **Deep navigation and file opening:** use six directories named
  `segment-N-` followed by 43 `x` characters and a leaf ROM named
  `日本語-é.z64`. Use the normal N64 theme and enable BGM to retain the original
  reported settings. Repeat entering the leaf, opening details, changing artwork
  and returning. Also open a text fixture and an ordinary shallow-path ROM.
  Stage Unicode names through a UTF-8-aware filesystem path; an edlink roundtrip
  with a mangled displayed name is not a valid Unicode fixture.
- **Missing artwork:** on Jumper Pak, open a ROM with no embedded or SD artwork
  and press Left/Right. It should stay on ROM details. Before the guard fix this
  produced `Could not swap boxart image`. Capture B followed by a neutral A as
  well, to check reopening after the error. Expansion Pak is the control case.
- **Present artwork:** provide distinguishable PNGs as `boxart_front.png`,
  `boxart_back.png` and `gamepak_front.png` under the ROM's normal metadata
  directory. Check both directions, fast input while decoding, repeated opens
  and the full-width/height image geometry.
- **Long archive names:** make a ZIP whose entry name includes the entire deep
  relative path above. Browsing that entry previously asserted that paragraph
  width exceeded 2048 pixels. Verify the ticker remains visible and scrolling
  through the complete path and wraparound; check ordinary short names and
  selection changes too. Browsing and extracting are separate checks.

A passing traversal does not establish the cause of the historical intermittent
ROM-open error or the separate `rdpq_triangle_rsp` exception. Keep those results
bounded to the tested builds and schedules.

## Ticker host regression

Run `make -f tests/Makefile test-file-list-ticker test-file-list-ticker-sanitize`.
The test compiles the current production ticker helpers with the current SDK
paragraph engine and existing built-in Monogram font metrics. It checks the
former overflow, long names, UTF-8 boundaries, whitespace, maximum glyph advance,
baseline positioning and cleanup. Generated sources stay under `build/tests`;
no implementation or font data is copied into the tests. GPU rendering remains
a hardware check.
