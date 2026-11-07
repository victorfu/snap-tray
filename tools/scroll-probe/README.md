# scroll-probe

Developer tool for [#265](https://github.com/victorfu/snap-tray/issues/265). It shows the scroll area that the
system accessibility API reports under the cursor (macOS AX, Windows UI Automation) and measures
programmatic scrolling against lossless captures. It is not part of the product: it only drives the
`scrollcapture` modules (`ScrollAreaLocator`, `ScrollInjector`, `FrameSettleDetector`,
`RowHashShiftEstimator`) that a future long-screenshot feature would use. The product feature stays
off (`PlatformCapabilities::supportsScrollCapture`).

## Build

The probe is built with the tests (`BUILD_TESTING`, on by default):

```bash
./scripts/build.sh
```

The binary is `build/SnapTrayScrollProbe` (`build\SnapTrayScrollProbe.exe` on Windows).

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
build/SnapTrayScrollProbe inspect [--level N]
```

Draws a border around the scroll area under the cursor with its role, nesting level, and position, and
prints a line whenever it changes. `--level 1` selects the next scroll area outward.

## Measure

```bash
# Phase 1: detection and position only
build/SnapTrayScrollProbe measure --app-label "Safari article" --output results.jsonl

# Phase 2: also scroll 10 times by 120 px and measure each scroll
build/SnapTrayScrollProbe measure --scroll wheel-pixels --amount 120 --repeats 10 \
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
`--enable-ax` (macOS: set `AXManualAccessibility` on the target and locate again, for Chromium and
Electron), `--post-to-process` (macOS: `CGEventPostToPid` without moving the cursor), `--timeout`
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
- `enhancedAccessibility`: the same after `--enable-ax`.
- `scroll.repeats[]`: for each scroll, `inject` (`ok`, `detail`, and `preparationMs`, the time spent
  finding a target point before dispatch; settle times count from dispatch), the position before and after, `apiShiftPx` (the shift the
  platform reports, in device pixels), `settle` (`strictSettleMs`, `relaxedSettleMs`,
  `motionObserved`, frames and capture time), `shift` (the measured shift around `apiShiftPx`) and
  `shiftWithoutPrior` (the same frames searched without it).
- `scroll.stats`: mean and standard deviation of the measured shift, p50/p95 strict settle time.

Shift statuses: `found`, `ambiguous` (two shifts equally likely, e.g. two panes moved),
`insufficient_detail` (blank or repeating content), `not_found` (no exact row match, e.g. fractional
scaling re-rasterised the text), `invalid_input`.

Collect the lines from every application in the #265 matrix and judge each one against the
thresholds in the issue.
