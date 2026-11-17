# EverDrive-64 Pro navigation qualification

> Historical qualification: the shared CIC, path, artwork, and ticker fixes
> described below have been removed from this PR for separate review.
> Their regression tests are also deferred; previous results describe those builds.

The November 16–17, 2026 re-test of [PR #459](https://github.com/n64brew/N64FlashcartMenu/pull/459)
confirmed two pre-existing UI defects and a Pro-specific RAM-validation defect.
Their fixes pass the checks below. Intermittent corruption, startup exceptions
and freezes remain unresolved; these fixes do **not** establish reliable Pro
4 MiB operation. Use an Expansion Pak (8 MiB) for now.

## Setup and frozen builds

Author-run tests used an original NTSC N64, Pro firmware `26.1014`, SummerCart64
firmware `2.20.2`, Jumper Pak (4 MiB) and Expansion Pak (8 MiB). Both cartridges
used the same long-path fixture, PNG artwork and BGM-enabled controller schedule
where compared. These are bounded results, not independent qualification.

| Build | Source | ROM SHA-256 |
| --- | --- | --- |
| Original branch baseline | `33e599e346bd73a071a08b38ddc2a220b3ddae64` | `4a8e1c0b87cab60c54d51082b39c171766dcdeaf72203e6951b98422ce999a01` |
| Raw develop | `717d52c979fe1e1ed0d9b7331270100a9c0465cb` | `99c9d6635476b5e871ce77ff498788caf0aebe7d93dd9dbab8c3f00eff94675d` |
| Cold branch baseline | Exact `33e599e3`, no fixes/hooks | `625544e4edff051f00f987e93c01fb2fc8d77a41d9dee87c2632aeb41f260fe1` |
| Production candidate | `33e599e3` plus ticker, absent-artwork and installed-RAM fixes | `ee235a3d126da673d267f227b7edfc5eafe622d57c42b06de52711324fd85ead` |
| Develop + path fix | `717d52c9` plus only the two root-rebase lines from `343741bf` | `7caf92ac9b193b359f969d52bac5969522ea6e5e6652880d56cdc38da5d98cda` |

Builds use SDK `39d0d6096130836a65710da6730290701d060272` and pinned library
snapshots/assets. The last row is a **hybrid**, never raw develop. The actual
embedded-ROM audit decoded both baselines' `PT_LOAD` segments and matched each
byte to its host ELF. Across original/cold ROMs, the decoded segments differ
only in four credits timestamp digits; entry/load addresses, BSS zero-fill ranges
and decompressor match. DragonFS assets also match. This does not explain their
intermittent runtime results. [Embedded-program audit](../output/unresolved-qualification/20261116/embedded-program-audit/audit.json).

## Method and evidence accounting

The historical menu-stress recorder retained scripts, commands, timings,
return codes and a PNG after each transition. The schedule enters six nested
directories, opens a Unicode-named ROM, changes red/green artwork both ways and
returns. The final 8 MiB check instead uses the ASCII leaf `mario.z64`.
Separate cases exercise absent artwork, text and long ZIP entry names.
The generic recorder and console diagnostics are deferred to separate PRs.

[Evidence](../output/unresolved-qualification/20261116/) is ignored by Git and
must accompany external reports. Earlier passing controls include screen checks
and visual review. The relocated enabled-probe result below uses programmatic
OCR/screen-identity and artwork-color checks, as does the final 8 MiB check;
neither has an additional visual-review claim. Controller acknowledgments alone
prove no menu result. Repeated captures
of one assertion count as one failed run.

The [`pro4-baseline-both-cold` batch](../output/unresolved-qualification/20261116/pro4-baseline-both-cold/manifest.json)
is **excluded: zero valid navigation checkpoints**. Its launch image already
showed an exception before fixture input; the mistakenly started batch stopped
at its first capture. The first `sc64-short-current` attempt's isolated black
capture is also excluded from completed-run counts: a before-input followup was
healthy, and the warmup-adjusted repeat passed. Persistent freezes listed below
have additional input and continuous-video evidence; they are not capture-only
failures.

## Fixed defects and classification

| Defect | Classification and verification |
| --- | --- |
| Absent artwork reports `Could not swap boxart image` on 4 MiB | **Confirmed pre-existing:** reproduced on branch/Pro and [raw develop/SC64](../output/unresolved-qualification/20261116/sc64-develop-noart-error.png). The shared fallback treats unavailable candidates as a failed restore. Candidate Left/Right and B/A reopen pass on SC64; five alternating direction/reopen pairs pass on Pro. |
| Long ZIP name asserts paragraph width `2052 > 2048` | **Confirmed pre-existing:** [raw develop/SC64 assertion](../output/unresolved-qualification/20261116/sc64-develop-longzip-assert.png), identical shared input. The bounded ticker shows the full Unicode basename, five-space gap, wrap and continued scrolling on SC64; the production-speed cycle takes about 205 seconds. Reset/short selection pass. Pro opens the same row without assertion; no full Pro wrap claim. |
| Root pointer becomes stale after path-buffer realloc | **Pre-existing; already fixed** by `343741bf02fe7ed901566169a336bc047f655d29`, with forced-relocation host coverage. Raw develop's long-path return stalled; the labelled develop + path-fix control permits comparison without that known defect. It does not explain every historical failure. |
| Pro sector-buffer guard assumes 8 MiB with Jumper Pak | **Branch-specific validation defect:** installed-RAM bounds replace the fixed limit. The reproducer failed 21 checks before correction; 316 tests across 20 normal/sanitizer suites pass afterward. No observed out-of-range request connects it to the corruption. [Host results](../output/unresolved-qualification/20261116/host/rdram-bounds-summary.json). |

The new fixes are committed separately: RAM bounds `06099ee9`, ticker
`43f299ce`, and absent artwork `46fc4dfc`. The older RTC qualification-ROM
link failure was a branch-specific build defect, already fixed by `81d79c21`
before this round. Fresh INFO, STORAGE and LOAD builds pass; restoring the old
dependency list reproduces the missing RTC symbols. It is not a runtime-crash
classification. [Link regression check](../output/unresolved-qualification/20261116/host/rtc-n64-link-regression/summary.json).

The historical ticker test used production
layout code, the actual SDK paragraph engine and font metrics: 150 checks in
each of ordinary and ASan/UBSan modes (300 total). The former unbounded SDK call
reproduces its assertion as a positive control. This tests layout, not GPU/N64
memory behavior. [Results](../output/unresolved-qualification/20261116/host/durable-ticker-summary.json).
Candidate [SC64 UI](../output/unresolved-qualification/20261116/sc64-production-ui-results.json)
and [Pro UI](../output/unresolved-qualification/20261116/pro4-production-ui-results.json)
records distinguish reviewed OCR false negatives from genuine failures.

## Unresolved observations

These signatures remain distinct; neither shared source equality nor
intermittent A/B passes establishes an inherited cause or branch regression.

| Observation | Evidence and limit |
| --- | --- |
| PNG artwork cycle faults in JPEG decoding | Pro/4 MiB baseline, cycle 5: PC `8002C024`, bad address `7F1B96A4`, decoder pointer `7F1B95EC`. Matching ROM/ELF and GP verified. [Capture](../output/unresolved-qualification/20261116/baseline4-valid-deep/0005-03-art-next/capture.png). Writer/origin unknown. |
| Fixed colored speckles | Visible from the first Pro/4 MiB screen and with BGM disabled; absent in later clean runs. Reseating, power history and timing confound comparisons. Cause unknown. |
| Original Pro baseline freezes | First long-path `deep-return`, checkpoint 5: garbled glyphs, clock/ticker stopped at `15:47:06`, retained in later continuous video. [Run](../output/unresolved-qualification/20261116/pro4-baseline-long-repeat/manifest.json), [video](../output/unresolved-qualification/20261116/pro-baseline-long-capture-control.mp4). |
| Candidate Pro baseline also freezes | Cycle 3 `deep-return`, checkpoint 15 after 14 matched: Unicode leaf and clock `16:06:50` persist after six extra B presses, held B and continuous video. The three fixes do not cure it. [Video](../output/unresolved-qualification/20261116/pro4-production-long-control.mp4). |
| Current baseline freezes on SC64 | Original `4a8e…`, checkpoint 3 after two matched: red artwork remains, garbled glyphs, no response to Right/B holds; later video unchanged and USB debug `help` write times out. **Not confined to the Pro driver.** [Failure](../output/unresolved-qualification/20261116/sc64-full-current-failure.json). |
| Startup exception after both-power cold cycle | Verified console+USB off for five seconds and healthy stock gate, then original baseline PC/bad address `00000000` before fixture input. Retained USB state is not established as the sole cause. [Result](../output/unresolved-qualification/20261116/pro-baseline-both-cold-result.json). |
| Phase diagnostic v1 corrupt flashcart pointer | After 46 matched checkpoints, checkpoint 47 faults at PC `8002BD48`, bad address `00006DA8`; table `800E6D68` observed as `00006D68`, GP correct. JPEG guard did not trigger. This does not identify RTC as the writer/cause. [Registers and analysis](../output/unresolved-qualification/20261116/jpeg-phase-diagnostic/hardware-failure.json). |
| Watchpoint diagnostic display assertion | First checkpoint asserts `display_show called on non-locked display 0 (mask: 2)`; all 50 captures retain that one failure. Diagnostic layout/hooks prevent a production-fix or writer conclusion. [Capture](../output/unresolved-qualification/20261116/watch4-deep/0001-01-deep-enter/capture.png). |
| Historical ROM-open error | `Couldn't open ROM file` after artwork error/B/reselect/A was not re-established. Later reopening does not prove a fix. [Original session](../build/ed64-pro-evidence/session.json). |
| Historical triangle exception | Deep-navigation `rdpq_triangle_rsp`, PC `800B5108`, bad address `FEFEFEFE`, remains unexplained; the JPEG and newer pointer faults are different signatures. [Original session](../build/ed64-pro-evidence/session.json). |

## Passing controls and practical limits

| Configuration / build | Reviewed result |
| --- | --- |
| SC64/4 MiB raw develop and cold current, short path/BGM on | Ten cycles / 50 checkpoints each pass. [Develop](../output/unresolved-qualification/20261116/sc64-short-develop-independent-review.json), [current](../output/unresolved-qualification/20261116/sc64-short-current-repeat-independent-review.json). |
| SC64/4 MiB develop + path fix, full long path/BGM on | Ten cycles / 50 checkpoints pass. [Hybrid review](../output/unresolved-qualification/20261116/sc64-full-develop-pathfix-independent-review.json). |
| SC64/4 MiB cold current `6255…`, full long path/BGM on | Ten cycles / 50 checkpoints pass, despite original `4a8e…` failing checkpoint 3. No deterministic current-versus-develop regression is established. [Review](../output/unresolved-qualification/20261116/sc64-full-current-clean-repeat-independent-review.json). |
| Pro/4 MiB candidate, short path/BGM on | Ten cycles / 50 checkpoints pass; long-path failure remains. [Review](../output/unresolved-qualification/20261116/pro4-production-short-independent-review.json). |
| Pro/4 MiB diagnostic v2, long path/BGM on | Ten cycles / 50 checkpoints pass. Instrumentation changes layout/timing; no cure or writer claim. [Review](../output/unresolved-qualification/20261116/pro4-jpeg-phase-v2-independent-review.json). |
| Relocated SC64 enabled probe (`5ba5f0e1…`) | Three cycles / 15 programmatic OCR/screen/color checkpoints pass; launch marker verified. The skipped-probe control was **not run**, so no probe/RTC causal conclusion follows. [Launch](../output/unresolved-qualification/20261116/relocated-probe-enabled2-launch.json), [manifest](../output/unresolved-qualification/20261116/relocated-probe-enabled2/manifest.json). |
| Pro controls | Original 8 MiB baseline: five cycles pass. 4 MiB/BGM off: ten cycles without JPEG fault, but speckles remain; this does not establish audio as the cause. |
| Pro/8 MiB production candidate, full long directories/BGM on, ASCII leaf | Three cycles / 15 programmatic checkpoints pass, plus four absent-artwork/reopen checks and one text-open check. Read-only RAM probe confirms 8 MiB. [Final result](../output/unresolved-qualification/20261116/final8-result.json), [batch](../output/unresolved-qualification/20261116/pro8-final2-production-long/manifest.json). |

A CPU-only RAM probe passed 21,233,664 checked words over 3072 KiB, three passes /
nine patterns, zero data/guard failures in 25.7 seconds. The other 1024 KiB and
DMA/audio/timing behavior are untested. [Report](../output/unresolved-qualification/20261116/ram4-complete.png).
Read-only Pro/SC64 entry probes both show `RI_CONFIG=0x20`, `RI_SELECT=0x14`;
the automatic-RAC difference hypothesis is unsupported, and no timing writes
were applied. [Comparison](../output/unresolved-qualification/20261116/ri-config-comparison.json).

After the Pro baseline freeze, a console-off 128 MiB FCI dump matches the host
ROM body from `0x1000` through EOF. Its 2244 differing bytes are confined to the
boot prefix/stock-menu header. This is not upload-corruption proof or evidence
of earlier N64 ROM-window mapping/transient integrity.
[Analysis](../output/unresolved-qualification/20261116/pro-baseline-after-freeze-fci.json).
Native Pro stock boot also reproducibly goes black with its custom 640×480 PNG
background and works without it, outside either menu branch. The user authorized
leaving that preference disabled; memory exhaustion is untraced.

## Final hardware state

- **Final 8 MiB verification:** 20/20 scripted screen checks pass; ASCII leaf `mario.z64`, with the original long directory chain. The first stock-menu gate attempt stopped before upload/input because OCR read `5leekmenu64`; it is retained as a gate mismatch, not a crash or navigation run.
- **Restoration:** config/history are byte-identical to their backups, and the original SD-root menu SHA-256 is unchanged. Short-path fixtures and temporary artwork metadata are removed. Native Pro background remains disabled as authorized; its backup is hash-verified. Expansion Pak and Pro are installed; console outlet OFF, USB ON, controller neutral.
- **Cleanup limit:** `/tests/navigation-20261116` remains as one empty directory. Raw API inspection reports zero entries and normal directory attributes (`0x10`); recursive and direct deletion return MCU status `07`, including after selecting `/`. This is an external USB/filesystem cleanup observation with the console off, outside both menu builds; its cause is unclassified. No additional card swap was requested. [Restoration record](../output/unresolved-qualification/20261116/final-restoration.json).

Retail SRAM/FlashRAM save-and-reload, emulator saves and independent tester
reports remain outside this qualification; see the
[Pro checklist](91_test_plan_matrix.md#everdrive-64-pro-testing).
