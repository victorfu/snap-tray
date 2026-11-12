# scroll-probe

Developer tool for [#265](https://github.com/victorfu/snap-tray/issues/265). It shows the scroll area that the
system accessibility API reports under the cursor (macOS AX, Windows UI Automation) and measures
programmatic scrolling against lossless captures. It is not part of the product: it only drives the
`scrollcapture` modules (`ScrollAreaLocator`, `ScrollInjector`, `FrameSettleDetector`,
`RowHashShiftEstimator`, `ScrollContentAssembler`) that a future long-screenshot feature would use. The product feature stays
off (`PlatformCapabilities::supportsScrollCapture`).

## Build

The probe is a separate target, defined when `BUILD_TESTING` is on (the default). The build scripts
build all enabled targets. The probe can also be built explicitly:

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

Inspect registers session-only global shortcuts through `HotkeyManager`:

- `Ctrl+Alt+Shift+Up` / `Down`: cycle outward / inward, wrapping at the end.
- `Ctrl+Alt+Shift+M`: measure once at the cursor after releasing all modifiers.
  It uses the selected level and CLI measurement options, with zero countdown
  and one repeat. Pass `--scroll wheel-delta --amount 120` to include scrolling;
  without `--scroll`, it records detection/position only. `--output` appends JSON.
- `Ctrl+Alt+Shift+Q`: stop (including a pending or active measurement).

Keep the cursor over the intended area until the measurement starts. A held
Ctrl/Alt/Shift/Meta modifier cancels the request after three seconds. On Windows,
the guard uses physical `GetAsyncKeyState` state even while the probe is in the
background. Keypad/layout flags are excluded. A timeout logs the limit and
reported modifiers; an explicit stop has its own cancellation message.
While a request is pending or running, additional measure/level shortcuts are
ignored with a reason on stderr. Non-scrollable gaps preserve the selected level;
finding an area with fewer nested levels resets selection to level 0.
Unavailable Up/Down/M shortcuts are reported and disabled individually. Failure
to register the mandatory Q stop shortcut exits. Registration errors do not
assume another application caused a conflict.
These shortcuts do not initialize or persist the product's hotkey settings.

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

### Band matching

Displacement is measured in eight equal-width vertical bands, using the exact-row
estimator with unchanged thresholds in each band. Each repeat reports:

- `dominantShift`: the displacement that at least five of all eight bands
  independently accept. Blank or failing bands do not reduce that denominator,
  and stationary bands cannot turn moving frames into a zero-shift success. Its
  row counts come from one supporting band, not the whole image. This does not
  identify a stitchable content rectangle or remove a sticky header/sidebar.
- `shift`: the conservative whole-viewport alignment result derived from
  `dominantShift`. It is `non_uniform_motion` when another textured band
  measurably moved differently (for example a sticky sidebar) and `not_found`
  when a textured band could not be resolved. Conflict detection searches
  without the prior, so a narrow prior cannot hide different sidebar motion.
  Unchanged, low-detail blank bands (margins) count neither as a conflict nor
  as a displacement. `shiftPx` on a non-found result is diagnostic, never an
  accepted alignment.
- Without a five-band majority, both are `ambiguous` when textured bands
  measured different displacements, `insufficient_detail` when no band could
  measure anything and none mismatched, and `not_found` otherwise.
- `shift.rejectedBy` and `shift.measuredStatus`: present when the frames were
  measured but the repeat did not settle or synchronize (`settle_<outcome>` or
  `position_sync_<outcome>`). `shift.status` is then `not_found`, and
  `measuredStatus` keeps what the frames themselves showed.
- `bandMatching`: `bands` (the eight prior-constrained estimates),
  `observedBands` (the eight unconstrained estimates used for conflict
  detection), `supportingBands`, zero-based `conflictingBands` and
  `unresolvedBands`, and `nonUniformMotion` (textured bands measured more than
  one displacement, with or without a majority).
- `shiftMethod` is `consistent_vertical_bands`. `fullRowShift` is the full-width
  estimate for direct comparison, and `shiftWithoutPrior` is the band-majority
  result searched without the platform prior.

`scroll.stats.shiftsFound` and the displacement statistics count only repeats
whose `shift.status` is `found`, never `dominantShift`. This is a displacement
check, not a promise that accessibility bounds exclude all header/sidebar content.

### Position synchronization

After visual settling, an independent unconstrained band estimate checks whether
the platform prior's search window includes the visual displacement. If it does
not, the probe polls the platform position every 50 ms, servicing the
application event queue, for up to `--timeout` additional milliseconds. Every
retry rechecks the full captured frame and bounds. `positionSync.outcome` is:

- `within_search_window`: the platform offset came within `--search-radius` of
  the visual displacement. Approximate priors inside the window are valid even
  when they are not pixel-exact.
- `timed_out`, `frame_changed`, `position_missing`: the wait expired (or was
  interrupted), the frame or bounds changed, or the offset disappeared. The
  measurement is rejected.
- `not_available`: no check ran, because the repeat did not settle, the
  platform reported no offset, or no dominant displacement was measured. The
  measurement is not rejected on this ground, but it is not synchronized either.

Content export still requires synchronization. Schema 8 records
`positionSync.unavailableReason` at the synchronization gate: missing offsets
take precedence (`position_missing_offset_before/after`), followed by
`visual_shift_<status>` from the estimate computed without a platform prior.
Export preserves this cause rather than inferring it from the later constrained
estimate. Acceptance thresholds are unchanged.

The constrained search always uses the actual platform offset and the requested
radius, never a prior copied from pixels. This bounded check is not a general
freshness guarantee for all accessibility providers. `afterAtVisualSettle` is
the initial position query and `after` the final one. `positionSync` also
records its samples, wait duration, search tolerance, and `confirmedMs` from
dispatch (null unless `within_search_window`). The latter includes matching and
position synchronization; it is separate from visual `strictSettleMs`, so
accessibility latency is not hidden in visual-settle statistics.

### Content export

`--content-output` is an opt-in lossless content export:

```text
SnapTrayScrollProbe measure --scroll wheel-delta --amount 120 --repeats 10 --content-output content.png --frames-dir frames --output results.jsonl
```

The scroll viewport and position come from accessibility. The product core
`assembleScrollContent` finds one common physical-pixel rectangle whose overlaps
are **exactly equal across every pair**, using the prior-validated dominant
displacements. It rejects crops smaller than a majority of either dimension,
blank/repeated evidence, changing capture geometry, missing/unsynchronized priors,
failed injection/settle, reverse/zero motion, a displacement of half the viewport
or more, and discontinuous frame sequences. No scale, interpolation, seam
blending, guessed displacement, or per-frame crop is used. Retained frames
(before and after of every repeat) and the output image are each limited to
64 × 1024 × 1024 pixels; lower `--repeats` on large viewports.

The first frame prefix retains full width, preserving titles spanning above a
sidebar. Only newly revealed content strips are appended afterward; excluded
columns are transparent, so the sidebar appears once. `contentExport.viewportPx`
is the common content rectangle in physical pixels relative to the captured
viewport; its x/width are the columns appended after the first frame.
`contentExport.stats` uses the dominant displacements of the exported pairs, so
it can report success while the whole-viewport statistics report a sidebar
conflict.

A repeat that scrolls nothing ends the exported segment only after successful
settling, unchanged viewport geometry and successful position synchronization.
Its settled frame must be pixel-identical to the start frame; a timed-out repeat
never counts as an endpoint. This is needed because accessibility does not always report
`atBottom`; `contentExport.endedAtNoMotionRepeat` records its index. If a later
repeat moves again, the stall was a lost input and the export is rejected with
`motion_after_no_motion_repeat`.

Check `contentExport.status == "exported"`. A rejected run has a `reason`,
writes no image, prints the reason to stderr and exits with code 3 after writing
its record. The PNG is written atomically, so a rejected or failed run leaves
any file already at that path untouched; `staleFileAtPath` reports whether one
exists. `--frames-dir` and `--output` writes are not atomic. This is an explicit
probe export, not an enabled product UI/capability. The region is geometric, not
semantic: inspect the PNG before claiming all desired document content is
included. It exports the captured segment only, not necessarily the entire
document.

The common rectangle validates each pair over its own overlap length. A short
last scroll therefore preserves the same content height as earlier larger
scrolls, including when a sticky footer bounds the rectangle. Every output row
still comes directly from a captured image.

A non-positive `--amount` with content export is rejected before capture (exit
2); `pattern-set-percent` uses `--percent` instead and its actual displacement is
validated afterward. The destination must have an existing writable parent and
must not be a directory. A temporary sibling checks writability without
modifying an existing image. Invalid destinations also exit 2 before scrolling.

Schema 7 rejection diagnostics:

| `reason` | Meaning |
| --- | --- |
| `capture_failed`, `capture_failed_while_settling`, `injection_failed` | Capture or input failed. |
| `settle_<outcome>` | The detector did not settle, including `settle_timed_out`. |
| `geometry_changed` | The viewport bounds or image dimensions changed. |
| `position_sync_<outcome>` | Synchronization failed or was unavailable. |
| `dominant_shift_<status>` | No usable dominant displacement was found. |
| `non_positive_shift`, `shift_too_large` | Reverse/zero motion, or motion of at least half the viewport height. |
| `no_scroll_motion`, `motion_after_no_motion_repeat` | No initial motion, or motion resumed after an endpoint. |
| `capture_budget_exceeded`, `pixel_budget_exceeded` | Retained frames or assembly exceed the pixel limit. |
| `invalid_input`, `invalid_geometry`, `invalid_content_repeat` | Missing input, incompatible frame geometry, or an incomplete repeat. |
| `no_common_content_region`, `insufficient_content_evidence`, `discontinuous_capture` | No majority rectangle, weak row evidence, or a gap between pairs. |
| `allocation_failed` | Image conversion, copying, output or scratch allocation failed. |
| `save_failed` | PNG writing failed; `stage` is `open`, `encode` or `commit`, and `detail` contains the underlying error. |
| `no_scroll_area`, `interrupted` | No target was found, or the run was interrupted. |

Repeat-level failures carry the zero-based `repeatIndex` of the first failing
repeat. Assembly-wide and saving failures do not attribute a failure to an
arbitrary repeat. Rejection still reports `path` and `staleFileAtPath`.

### Schema history

| Schema | Change |
| --- | --- |
| 2 | Band-majority displacement, `bandMatching`, `fullRowShift`, `afterAtVisualSettle`, `positionSync`. |
| 3 | `dominantShift` separated from the conservative `shift`; `conflictingBands` and `unresolvedBands`. |
| 4 | `bandMatching.observedBands`; conflicting displacements report `non_uniform_motion`. |
| 5 | `--content-output` and `scroll.contentExport`. |
| 6 | `shift.rejectedBy` and `shift.measuredStatus`; blank bands no longer count as a displacement without a majority; `insufficient_detail` for band results; a rejected export exits with code 3 and reports `path`, `staleFileAtPath`; `endedAtNoMotionRepeat`. |
| 7 | Specific rejection reasons and `repeatIndex`; save `stage`/`detail`; validated no-motion endpoints; full overlap validation for variable shifts. |
| 8 | `positionSync.unavailableReason` records the actual gate's cause; export preserves missing-offset or unconstrained `visual_shift_<status>` reasons instead of inferring them from the constrained dominant estimate. Acceptance gates unchanged. |

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
  `motionObserved`, frame count and capture time), `positionSync`, and the shift results described
  under [Band matching](#band-matching): `shift` (the conservative alignment result, searched around
  `apiShiftPx`), `dominantShift`, `shiftWithoutPrior`, `fullRowShift` and `bandMatching`.
  `hashHorizontalInsetPx` is the perimeter excluded from row matching, and `frames` holds the saved
  PNG paths when `--frames-dir` is set.
  Each shift result also has the searched range (`searchLowPx`..`searchHighPx`) and the runner-up shift
  with its votes. `expectedShiftSource` is `platform_offset` or `none` (with `expectedShiftMissing`);
  the prior never comes from earlier repeats. A timed-out settle has a `timeoutReason`:
  `no_scroll_motion_minor_changes_only`, `minor_changes_persisted_after_motion` or
  `content_kept_changing`. An unchanged area alone does not mean the end was reached; read it together
  with `inject` and the `atBottom` positions.
- `scroll.stats`: mean and standard deviation of the measured shift, p50/p95 strict settle time.
  No confidence percentage is reported: the estimator has no calibrated one.
- `scroll.contentExport`: present with `--content-output`; see [Content export](#content-export).

Shift statuses: `found`, `ambiguous` (two shifts tie within a band, or textured bands disagree and
none has a five-band majority), `non_uniform_motion` (a majority displacement exists but another
textured band measurably moved differently, e.g. a sticky sidebar), `insufficient_detail` (blank or
repeating content), `not_found` (no exact row match, e.g. fractional scaling re-rasterised the text;
no band majority; or the repeat was rejected, see `shift.rejectedBy`), `invalid_input`.

On macOS, an `AXWebArea` frame is not treated as document geometry. Chrome can report the
viewport frame even for a long page. Without independent scroll-bar data, its position and
endpoint fields stay unknown; `diagnostics.contentsGeometryIgnored` records why. The probe
still runs the requested repeats and measures image displacement without a platform prior.
This does not establish reliable system position or end detection for the application's
acceptance criteria.

Collect the lines from every application in the #265 matrix and judge each one against the
thresholds in the issue.
