# macOS scroll-probe validation — 2026-11-10

Issue: [#265](https://github.com/victorfu/snap-tray/issues/265).
Revision: `2e9dcca6b9638db7ea4ac65b671df7d82707b8b6`.

## Retest after the local fix

The local working-tree fix on the revision above excludes `AXWebArea` frames
from document geometry inference. Native document frames and independent AX
scrollbar values still participate. An ignored web frame is retained in raw
diagnostics with `contentsGeometryIgnored=web_area_frame_is_not_document_geometry`.

On the same Chrome issue page and host, all five retest records identify Google
Chrome 156.0.8078.81 and report `position: {"sources": []}`. The endpoint fields
are now unknown, not true. All requested repeats execute; there is no premature
`stoppedAtEnd`. Raw data: [chrome-fixed.jsonl](chrome-fixed.jsonl), with
[computed summary](chrome-fixed-summary.json).

| Method, 120 points per request | Repeats | Injection accepted / motion observed | Displacement result | Strict settle p95 / confirmation p95 |
| --- | --- | --- | --- | --- |
| Pixel wheel | 10 | 10 / 10 | 10 `not_found`; no accepted shifts | 239 / 329 ms |
| Continuous pixel wheel | 10 | 10 / 10 | 10 `not_found`; no accepted shifts | 136 / 220 ms |
| Pixel wheel posted to PID | 10 | 10 / 0 | 10 `found` at 0px, unchanged frames; **not successful scrolling** | 0 / 376 ms; no motion, not a scrolling-latency result |
| Ordinary pixel wheel after PID experiment (control) | 1 | 1 / 1 | `not_found`; candidate 240px | 132 / 222 ms (single observation) |

All moving repeats used lossless 2940 × 1672 captures at DPR 2 and
`expectedShiftSource=none`. Their best candidate was 240 physical pixels, but
the estimator correctly rejected it: matched/overlapping moving-row ratios
were 20.2–31.5% for pixel wheel and 20.4–29.1% for continuous wheel, below the
existing 50% threshold. These rejected candidates must not be reported as a
verified mean or standard deviation. The page includes fixed content; its
causal contribution to matching failures was not isolated in this experiment.

The PID experiment's zero shift/zero standard deviation only describes unchanged
frames. A subsequent ordinary wheel event still moved the page, which rules out
an already-reached document end as the explanation for that sequence. The exact
reason Chrome did not respond to this PID delivery mode remains undetermined.

The false-endpoint bug is fixed and the wheel measurement path now runs. **Chrome
still does not meet #265's complete acceptance criteria:** system position/end
detection is unavailable in this configuration, and this sticky GitHub page did
not yield an accepted nonzero image displacement. Keep the product capability
disabled. Do not lower estimator thresholds based only on this experiment.
The three experiments used successive sections of the same page, not identical
starting positions; they are observations, not a controlled ranking of methods.
Full tests were paused during these measurements to avoid competing test activity.

Post-fix validation: `./scripts/build.sh` and the probe/test target builds passed;
the four focused CTest suites passed. The new MacScrollPosition test has eight
data cases (10 passes including setup/cleanup). `./scripts/run-tests.sh` then
passed **247/247** tests in 284.84 seconds outside the sandbox. The sandbox run
was stopped after GUI tests reported unavailable screens/pasteboard services;
those failures did not recur in the host-desktop run. `git diff --check` passed.

## Original result before the fix

**Chrome fails the position/end-point acceptance criterion.** This is a partial
validation, not a completed application matrix or a weighted coverage estimate.
Keep scroll capture disabled. Correct the position inference before relying on
the probe's end-of-scroll guard for subsequent scrolling measurements.

| Item | Observation |
| --- | --- |
| Host | macOS 27.0.1 (26A434), arm64, built-in Retina display, DPR 2, logical 1470 × 956 |
| Scroll bars | Automatic (default), overlay style |
| Browser | Google Chrome 156.0.8078.81 |
| Page | GitHub issue #265, long article with sticky issue header |
| Permissions | Accessibility, event posting and screen capture all true after user-authorized macOS authentication |
| Area | `AXScrollArea`, bounds `(0,120,1470,836)`, one level, first query succeeds |
| Position | `offset=0`, `contentHeight=836`, `viewportRatio=1`, both `atTop` and `atBottom` true |
| Raw source | No vertical AX scroll bar; `AXContents` is an `AXWebArea` with the same frame as the viewport |
| Manual accessibility boost | `AXManualAccessibility=-25205`; position remains unchanged. Enhanced UI was already on; this probe did not enable it. |
| Requested wheel measurement | 120 points × 10; **zero events executed**, `stoppedAtEnd=0`, empty repeats |
| Independent check | A computer-use scroll visibly moved the page from its introduction to the module table/platform section. A subsequent probe still reports offset zero and both endpoints true. |

Raw records: [chrome.jsonl](chrome.jsonl). The three records cover the initial
verified Chrome detection (including manual boost), the aborted wheel measurement,
and detection after visible scrolling. No displacement standard deviation or settle
p95 can be inferred from zero repeats. Bounds were visually consistent with the
browser content viewport, but were not certified to the issue's 2pt tolerance;
sticky content remains inside them.

## Cause identified in current code

`MacScrollArea::readPosition` in `src/scrollcapture/ScrollCapturePlatform_mac.mm`
applies NSScrollView document-frame semantics to any `AXContents`. Here the
`AXWebArea` frame describes the viewport, not the entire document. The inferred
scroll range becomes zero, setting both endpoint flags true. In
`tools/scroll-probe/main.cpp`, the `atEnd` guard then stops before injection.
This is an incorrect position result, not evidence that Chrome cannot scroll.
Unknown document geometry must not be treated as a confirmed zero scroll range.

## Build and tests

- `./scripts/build.sh`: passed.
- `cmake --build build --target SnapTrayScrollProbe ScrollCapture_FrameSettleDetector ScrollCapture_RowHashShiftEstimator`: passed.
- `ctest --test-dir build -R 'ScrollCapture_|Platform_Capabilities' --output-on-failure`: 3/3 passed.
- Direct Qt Test results: FrameSettleDetector 13 passed; RowHashShiftEstimator 15 passed (including setup/cleanup).
- Linker warnings: project target macOS 14.0 links Homebrew Qt frameworks built for 14.4; this host is 27.0.1.

## Measurement limitations and remaining work

Computer-use app operations initially did not place Chrome under the host cursor.
Those attempts hit ChatGPT and were excluded from this Chrome evidence. One
countdown attempt hit `SkyComputerUseService` and returned AXError -25208;
that is a test-harness obstruction, not a Chrome result. After the user brought
Chrome forward, a retry without countdown correctly identified Chrome by app
name, process ID and window owner.

Other applications, nested scrolling, an ordinary non-sticky page, always-visible
scroll bars, non-Retina displays and Electron restart behavior remain unvalidated.
The original run made no product changes; the retest above includes the local
position-inference fix. Neither run establishes the issue's global continue/stop
coverage decision.
