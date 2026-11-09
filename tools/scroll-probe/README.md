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
