# scroll-probe

Developer tool for [#265](https://github.com/victorfu/snap-tray/issues/265). It shows the scroll area that the
system accessibility API reports under the cursor (macOS AX, Windows UI Automation) and measures
programmatic scrolling against lossless captures. It is not part of the product: it only drives the
`scrollcapture` modules (`ScrollAreaLocator`, `ScrollInjector`, `FrameSettleDetector`,
`RowHashShiftEstimator`) that a future long-screenshot feature would use. The product feature stays
off (`PlatformCapabilities::supportsScrollCapture`).

## Build

The probe is a separate target, defined when `BUILD_TESTING` is on (the default). The build scripts
only build the `SnapTray` target, so configure with them once and then build the probe:

```bash
./scripts/build.sh
cmake --build build --target SnapTrayScrollProbe
```

On Windows, run `scripts\build.bat` first, then the same `cmake --build` line.

The binary is `build/bin/SnapTrayScrollProbe` (`build\bin\SnapTrayScrollProbe.exe` on Windows), next to
`SnapTray-Debug`. If `cmake --build` reports an unknown target, reconfigure with
`cmake -S . -B build -DBUILD_TESTING=ON`.

Permissions:

- macOS: the terminal that runs the probe needs Accessibility (reading AX and posting scroll events)
  and Screen Recording (captures). The `environment` field of each record shows
  `accessibilityTrusted`, `postEventAccess` and `screenCaptureAccess`.
- Windows: none. Windows of elevated applications report `blocked` unless the probe is elevated too.
  `wheel-delta` needs "Scroll inactive windows when I hover over them" (the default) or the target
  focused; otherwise the repeat fails with that reason instead of scrolling the focused terminal.
- Linux: not implemented; every query reports `unsupported`.

## Inspect

```bash
build/bin/SnapTrayScrollProbe inspect [--level N] [--ax-boost manual|enhanced|both]
```

Draws a border around the scroll area under the cursor with its role, nesting level, and position. The
overlay label follows position changes; the terminal gets a line only when the area itself (process,
role, level or bounds) changes, so inspecting the terminal that runs the probe does not loop. `--level 1`
selects the next scroll area outward.

### Chromium and Electron (macOS)

Chrome and Electron apps build the accessibility tree of their web content only for assistive clients,
so by default they may report just an outer `AXScrollArea` with no position. `--ax-boost` asks the
target to build it, once per process, the way macshot's element snapping does:

| Mode | Sets |
|---|---|
| `manual` | `AXManualAccessibility` (Electron's switch) |
| `enhanced` | `AXEnhancedUserInterface` (what VoiceOver sets) |
| `both` | both |
| `all` (measure only) | each of the above in turn, restoring in between |

The previous values are restored when the probe exits, including on Ctrl+C. If `AXEnhancedUserInterface`
was already on (VoiceOver), it is left alone. Chromium can return an error and still build the tree, so
the AXError codes are only logged; whether a mode works is decided by locating again. `--ax-wait`
(default 1000 ms) is the pause after each request. `AXEnhancedUserInterface` can make some apps animate
window changes oddly while it is on.

Without the probe, `chrome://accessibility` ("Native accessibility API support") or launching Chrome with
`--force-renderer-accessibility` gives the same comparison.

## Measure

```bash
# Phase 1: detection and position only
build/bin/SnapTrayScrollProbe measure --app-label "Safari article" --output results.jsonl

# Phase 2: also scroll 10 times by 120 px and measure each scroll
build/bin/SnapTrayScrollProbe measure --scroll wheel-pixels --amount 120 --repeats 10 \
    --app-label "Safari article" --output results.jsonl
```

After `--delay` seconds (default 3), the probe measures the scroll area under the cursor and appends one
JSON line to `--output` (and stdout). Keep the mouse still during a measurement; wheel methods move the
cursor into the area and put it back afterwards. They use the centre, or the first of a few other points
inside the area where the accessibility API reports the selected area as the innermost scroll area, so
`--level 1` does not scroll a nested pane that covers the centre. When no point qualifies, the repeat
records `inject.ok = false` with the reason.

Scroll methods:

| Method | Platform | `--amount` |
|---|---|---|
| `wheel-pixels` | macOS | pixels (points) per scroll |
| `wheel-pixels-continuous` | macOS | the same, flagged continuous (handled like a trackpad) |
| `wheel-delta` | Windows | wheel units, 120 = one notch; try 40 as well |
| `pattern-small-increment` | Windows | ScrollPattern small increments |
| `pattern-set-percent` | Windows | uses `--percent` instead |

Other options: `--level`, `--attempts` (accessibility queries before giving up, default 3),
`--ax-boost` (see above; `--enable-ax` is `--ax-boost all`), `--ax-wait`, `--post-to-process` (macOS: `CGEventPostToPid` without moving the cursor), `--timeout`
(settle timeout, default 1500 ms), `--strict-pairs`, `--search-radius`, `--interval`, `--note`.

Use `--frames-dir DIRECTORY` to save the original lossless before/after PNG pairs
for diagnosis. Saving happens after settle timing; paths are included in each
repeat's `frames` field (or `frameSaveError` on failure). These captures may include
application content, so review them before sharing.

Row matching excludes a narrow horizontal perimeter of 2 logical pixels on each
side (rounded up to physical pixels, recorded as `hashHorizontalInsetPx`). UIA
bounds and fractional DPI rounding can include a fixed window-frame gradient;
hashing it as scrolling content otherwise invalidates full-row equality. Full
captures, reported bounds and settle detection remain unchanged.

Schema 2 measures the **dominant displacement** in eight equal-width vertical
bands, using the existing exact-row estimator in each band. At least five of all
eight bands must independently accept the same shift; blank or failing bands do
not reduce that denominator. Equal competing panes are ambiguous, and stationary
bands cannot turn moving frames into a zero-shift success. Per-band match and
ambiguity thresholds are unchanged. This does not identify a stitchable content
rectangle or remove a sticky header/sidebar. `nonUniformMotion` and the individual
band results must be reviewed before treating the entire viewport as uniform.

After visual settling, an independent unconstrained band estimate checks whether
the platform prior's existing search window includes the visual displacement.
If it does not, the probe polls the platform position every 50 ms, servicing the
application event queue, for up to `--timeout` additional milliseconds. Every
retry rechecks the full captured frame and bounds. A changed frame, missing
position, or expired wait rejects the measurement. The final constrained search
still uses the actual platform offset and the requested radius, never a prior
copied from pixels. This bounded check is not a general freshness guarantee for
all accessibility providers. Approximate priors already inside the search window
remain valid, even when they are not pixel-exact.

Schema 2 adds `shiftMethod: "vertical_band_majority"`, `bandMatching` (all eight
estimates, supporting band count, and non-uniform motion flag), and `fullRowShift`
(the legacy full-width estimate for direct comparison). `shift` and
`shiftWithoutPrior` now describe band consensus; their row counts come from one
supporting band, not the whole image. `afterAtVisualSettle` preserves the initial
position query, while `after` is the final query. `positionSync` records its
outcome, samples, wait duration, search tolerance, and `confirmedMs` from dispatch.
The latter includes matching and position synchronization; it is separate from
visual `strictSettleMs`, so UIA latency is not hidden in visual-settle statistics.

Schema 5 adds an opt-in lossless content export:

```text
SnapTrayScrollProbe measure --scroll wheel-delta --amount 120 --repeats 10 --content-output edge-content.png --frames-dir frames --output results.jsonl
```

The scroll viewport and position still come from accessibility. The product core
`assembleScrollContent` finds one common physical-pixel rectangle whose overlaps
are **exactly equal across every pair**, using the prior-validated dominant
displacements. It rejects crops smaller than a majority of either dimension,
blank/repeated evidence, changing capture geometry, missing/unsynchronized priors,
failed injection/settle, reverse/zero motion, and discontinuous frame sequences.
No scale, interpolation, seam blending, guessed displacement, or per-frame crop
is used. Captured pairs and output each have a 64-megapixel budget.

The first frame prefix retains full width, preserving titles spanning above a
sidebar. Only newly revealed content strips are appended afterward; excluded
columns are transparent, so the sidebar appears once. `contentExport.viewportPx`
describes the appended content rectangle, and its statistics are **separate**
from the whole-viewport statistics, which can still report a sidebar conflict.
Check `contentExport.status == "exported"`; rejected runs include a reason and
do not publish a new image. Writes are atomic. This is an explicit probe export,
not an enabled product UI/capability. The region is geometric, not semantic:
inspect the PNG before claiming all desired document content is included. It
exports the captured segment only, not necessarily the entire document.

Schema 4 adds `bandMatching.observedBands`: the eight unconstrained estimates
actually used for conflict detection, alongside the prior-constrained `bands`.
Confirmed different displacements now produce `shift.status: "non_uniform_motion"`
instead of `ambiguous`. `shiftPx` on a non-found result remains diagnostic, never
an accepted alignment. This distinguishes a real sticky-sidebar conflict from
uncertain matching; it does not remove the sidebar or make the frame stitchable.
Only `found` results contribute to displacement statistics.

Schema 3 separates `dominantShift` (the schema-2 majority result) from `shift`
(the conservative alignment result), with `shiftMethod: "consistent_vertical_bands"`.
An independently measured conflicting band makes alignment `ambiguous`; an
unresolved band makes it `not_found`. Conflict detection also searches without
the prior, so a narrow prior cannot hide different sidebar motion. Unchanged,
low-detail blank bands do not count as a textured fixed sidebar. Zero-based
`conflictingBands` and `unresolvedBands` identify the evidence. Acceptance counts
use `shift`, never `dominantShift`. This remains a displacement check, not a
promise that accessibility bounds exclude all headers/sidebar content.

## Record fields

- `environment`: settings that change scrolling — macOS `appleShowScrollBars`, `preferredScrollerStyle`,
  `naturalScrolling`; Windows `wheelScrollLines`, `mouseWheelRouting`, integrity level.
- `screen.devicePixelRatio`: scaling of the screen under the cursor (record 100% and 150% on Windows,
  Retina and non-Retina on macOS).
- `locate`: `status`, `attempts` (how many queries it took), and `area` with `bounds`, `role`,
  `level`/`availableLevels`, `appName`/`appVersion`, `position` (`fraction`, `viewportRatio`,
  `contentHeight`, `offset`, `atTop`, `atBottom`, and the `sources` they came from) and raw
  `diagnostics` (AX role path, scroll bar value, AXContents frame; UIA class name, framework ID,
  scroll percent and view size).
- `accessibilityBoost.attempts[]`: per `--ax-boost` mode, what was set (`AXManualAccessibility` /
  `AXEnhancedUserInterface` with their AXError, or `already_on`, and `enhancedUserInterfaceWasOn`) and
  the `locate` result after it. With `--scroll`, `scrollAccessibilityBoost` is the mode kept on while
  scrolling (`both` for `all`).
- `scroll.repeats[]`: for each scroll, `inject` (`ok`, `detail`, and `preparationMs`, the time spent
  finding a target point before dispatch; settle times count from dispatch), the position before and after, `apiShiftPx` (the shift the
  platform reports, in device pixels), `settle` (`strictSettleMs`, `relaxedSettleMs`,
  `motionObserved`, frames and capture time), `shift` (the measured shift around `apiShiftPx`) and
  `shiftWithoutPrior` (the same frames searched without it).
  Each shift result also has the searched range (`searchLowPx`..`searchHighPx`) and the runner-up shift
  with its votes. `expectedShiftSource` is `platform_offset` or `none` (with `expectedShiftMissing`);
  the prior never comes from earlier repeats. A timed-out settle has a `timeoutReason`:
  `no_scroll_motion_minor_changes_only`, `minor_changes_persisted_after_motion` or
  `content_kept_changing`. An unchanged area alone does not mean the end was reached; read it together
  with `inject` and the `atBottom` positions.
- `scroll.stats`: mean and standard deviation of the measured shift, p50/p95 strict settle time.
  No confidence percentage is reported: the estimator has no calibrated one.

Shift statuses: `found`, `ambiguous` (two shifts equally likely, e.g. two panes moved),
`insufficient_detail` (blank or repeating content), `not_found` (no exact row match, e.g. fractional
scaling re-rasterised the text), `invalid_input`.

Collect the lines from every application in the #265 matrix and judge each one against the
thresholds in the issue.
