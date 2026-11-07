---
last_modified_at: 2026-11-07
layout: docs
title: Boundary Snap Acceptance Matrix
permalink: /developer/boundary-snap-acceptance/
lang: en
nav_data: developer_nav
---

# Automated acceptance for issue #280

This maps the [issue's acceptance criteria](https://github.com/victorfu/snap-tray/issues/280) to executable checks. **Synthetic fixtures and injected Qt events are not physical-device or real-image acceptance.** The implementation and initial measurements are documented in [Selection Boundary Snap](boundary-snap.md).

## Run and retain evidence

Build through the repository's normal build/test scripts. After building tests:

```sh
ctest --test-dir build -L boundary-snap-acceptance --output-on-failure
```

On Windows, first prepend `%QT_PATH%\bin` to `PATH` as described in the developer build instructions. `scripts/run-tests.bat` handles Qt DLL setup for the full run.

The label covers seven suites, including `RegionSelector_BoundarySnapMetrics`. The metrics suite runs the Qt offscreen backend and writes `build/boundary-snap-metrics.json`. The existing macOS, Windows and Ubuntu 22.04 CI jobs execute these tests and upload that JSON alongside their JUnit and CTest logs. A workflow definition alone is not evidence of a passing platform run; inspect the artifact for the exact commit.

Metrics include build type, Qt version, CPU model/architecture, OS, backend, source and logical selection dimensions, DPR, p50/p95, hits, fallbacks, incorrect results, source conversion time/storage sharing, controller/source storage sizes, and whole-process peak RSS. `p95_target_met` evaluates the 4 ms target but does **not** determine the test exit code: slow or contended CI hosts still have to return either the correct crop or the unmodified raw rectangle. Incorrect crop/fallback behavior, invalid arguments and report-write failures return nonzero. Peak RSS includes Qt and the source image; it is not incremental cache usage or GPU memory. Windows uses the [documented process memory API](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo); Unix hosts use `getrusage`. Missing metadata remains explicitly unavailable.

For Release measurements outside CTest:

```sh
QT_QPA_PLATFORM=offscreen ./build-release/tests/RegionSelector_BoundarySnapBenchmark --report boundary-snap-release.json
```

Use the `.exe` and PowerShell environment syntax on Windows. No threshold settings are persisted. The production query budget stays at 4 ms; a per-query zero budget provides deterministic coverage of timeout fallback and lock clearing without sleeping or relying on scheduler load.

## Core and geometry matrix

| Issue criterion | Automated check | What the assertion establishes |
|---|---|---|
| Horizontal/vertical hard edges; uniform/no-edge areas | `Detection_BoundarySnapIndex::hardEdges` | Exact RGB mean and support, both axes and four source formats; adjacent flat columns rejected |
| Single pixel and short line; span-external contrast | `supportAndSpan` | Sparse support rejected; same line accepted only in its occupied span |
| Threshold boundaries; correct mean-of-distance scoring | `supportThresholdBoundary`, `meanDistanceIsNotMeanSquaredDistance` | 54% rejected, 55% accepted; squared-distance substitution would fail |
| Low contrast, antialiasing, gradient/shadow, text-like strokes, table grid | `contrastAndTexture`, `antialiasedEdgesAndGrid` | Controlled pixel fixtures with known edge positions; not real font/photo recognition |
| Empty/tiny/oversized input, invalid span, alpha and storage ownership | `invalidAndAlpha`, `hardEdges` | Safe rejection, transparent RGB ignored through premultiplication, QImage copy-on-write independence |
| Multiple candidates, distance/strength/support/coordinate ordering | `RegionSelector_BoundarySnap::rankingAndJitter`, `tieAndConstraints` | Nearer edge wins; strength then support breaks ties; exact ties choose lower coordinate |
| Eight handles, fixed opposite sides, four DPRs | `handlesAndDpr` | 32 cases at odd coordinates, each final physical crop equals target, only moving edges report guides |
| Both corner axes use the raw span | `cornerQueriesUseRawSpans` | A fixture that fails if X is committed before evaluating Y |
| Minimum size, screen bounds, aspect ratio | `tieAndConstraints`, existing `RegionSelector_SelectionStateManager` cases | Illegal transforms fall back, fixed sides stay fixed; existing eight-handle bounds and ratio tests stay active |
| Nonrepresentable physical boundary | `unrepresentableEdges` | Fractional/Retina edges absent from the integer crop mapping never produce a hit |
| Light/dark card fixtures | `lightAndDarkCards` | Eight synthetic theme/DPR cases land on exact known crop edges |
| Hysteresis, leaving radius, invalidated span | `hysteresisAndGeneration`, `rankingAndJitter` | Lock retention and exit; zero candidate switches for the scripted near-edge jitter trace |
| Source/DPR replacement | `sourceReplacementChangesScaleAndLock` | Old physical lock is not reused with a different capture scale |
| Timeout/resource fallback | `timeoutFallsBackAndDropsLocks`, `invalidSourcesAndBudgetGuard` | Zero budget returns raw, clears lock before next query; invalid DPR and excessive spans are inert |
| Cache agreement | Not applicable | No image-sized cache or alternative cached query path exists |

## Interaction, display and output matrix

| Issue criterion | Automated check | Scope/limit |
|---|---|---|
| Noncentral handle press; first move has no offset jump | `RegionInputHandler::testBoundarySnapToggleDoesNotAccumulate`, existing offset cases | Real mouse event coordinates and original press delta |
| Alt/Option and S, stationary press/release, autorepeat, final release position | `testBoundarySnapInteraction`, `testBoundarySnapToggleDoesNotAccumulate` | 20 on/off cycles with exact geometry/signal counts; final mouse event modifiers honored |
| Initial selection, whole-rectangle movement and excluded modes stay unchanged | `testBoundarySnapLeavesCreationAndMoveAlone`, `testBoundarySnapExcludedModes` | No guides; exact raw geometry; no key-triggered geometry signals in multi/replace/history/locked-ratio modes |
| Outside-click expansion and keyboard precision remain intact | Existing input outside-click cases, selection manager keyboard cases, `HistoryReplay::testDetectedWindowMetadataSurvivesHighlightClear` | Existing ordinary geometry and arrow/Shift-arrow behavior |
| One final geometry commit per update | `handlesAndDpr`, repeated key-toggle test | One `selectionChanged`, never a raw-then-snapped pair |
| Esc restores original resize | `testBoundarySnapInteraction`, `HistoryReplay::testBoundarySnapCaptureRoundTrip` | Input cancellation and actual selector key/event-filter route |
| Local guides appear and disappear | `RegionPainterChrome::testSnapGuidesAreLocalAndTransient` | Pixel diffs confined to the two short edge segments; clearing/finishing returns the original rendered image |
| History entry/return, close, late input | `HistoryReplay::testBoundarySnapHistoryAndCloseLifecycle` | Replay cannot snap; restored live capture can; close clears source/guides and late input cannot reuse them |
| Save/copy crop and history source remain clean | `testBoundarySnapCaptureRoundTrip` | Actual shared export pipeline pixels equal original crop while guides are active; history canvas unchanged. OS clipboard transfer itself remains covered by existing platform tests |
| DPR and negative screen origin | `testBoundarySnapCaptureRoundTrip`, `sourceReplacementChangesScaleAndLock` | Actual selector/export context at DPR 1/1.25/1.5/2 with negative widget origin; simulated capture changes, not physical mixed-monitor routing |
| Old asynchronous work cannot change a new capture | Source/lifecycle tests; no asynchronous worker | There is no worker completion callback to exercise |
| 4K/6K time and memory | `RegionSelector_BoundarySnapMetrics` | 20 warmups + 200 independent two-axis queries per size/DPR, final crop/fallback verified for every sample; timing target and process peak reported |

## What remains outside automated acceptance

- Real screenshots of light/dark applications, code, dialogs/shadows and photos; target coverage and false-snap rates over a representative, labelled corpus. Synthetic contrast fixtures cannot establish these rates.
- Human mouse/trackpad correction counts and perceived jitter. The scripted trace only establishes deterministic lock stability for that trace.
- Physical macOS Retina and Windows 100/125/150/200% mixed-monitor routing, real pointer sampling and native capture/GPU allocation peaks.
- Ubuntu X11 window-manager interception of Alt-drag. Injected Qt events prove the S alternative's application behavior, not the window manager's event delivery.
- Windows/Linux results until the exact follow-up commit's CI artifacts are available. CI virtual/offscreen screens do not replace the physical checks above.

These remain open acceptance items; automated tests do not justify auto-closing #280 or claiming all physical pixel boundaries and objects can snap.

## Local automated measurement (2026-11-07)

Final follow-up Release probe on Apple M3 Pro / macOS 27.2 / Qt 6.12.0 / offscreen raster, with other build activity still running:

- Eight size/DPR scenarios, 1,600 measured queries: **0 incorrect results**, 1,600 exact two-axis hits and 0 ordinary-drag fallbacks. The zero-budget regression separately forces and validates fallback.
- 4K p95: **0.091–0.096 ms**; 6K p95: **0.227–0.687 ms**. Each scenario's `p95_target_met` was true.
- Whole-process peak RSS: **136,036,352 bytes**; pixel-index storage: **0 bytes**. Source sharing and storage sizes are recorded independently in the JSON.
- Report schema, scenario count, sample accounting, correctness and computed target flags were checked. Unsupported CLI arguments and an unwritable/nonexistent report parent each returned exit code 2.

The canonical app build and full suite passed (236/236); after the final stronger source-reset/validator checks, the seven labelled acceptance suites passed again (7/7).

This is one local synthetic run, not a performance guarantee for other machines. The CI artifacts provide the same measurements for their own hosts and commit.
