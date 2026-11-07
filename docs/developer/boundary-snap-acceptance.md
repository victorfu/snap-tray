---
last_modified_at: 2026-11-07
layout: docs
title: Boundary Snap Acceptance Matrix
permalink: /developer/boundary-snap-acceptance/
lang: en
route_key: developer_boundary_snap_acceptance
docs_copy_key: developer
description: Executable boundary-snap acceptance criteria, verification commands, CI artifacts, and remaining physical-device checks.
nav_data: developer_nav
---

# Automated acceptance for issue #280

This maps the [issue's acceptance criteria](https://github.com/victorfu/snap-tray/issues/280) to executable checks. **Synthetic fixtures and injected Qt events are not physical-device or real-image acceptance.** The implementation and measurement contract are documented in [Selection Boundary Snap](boundary-snap.md).

## Run and retain evidence

Build through the repository's normal build/test scripts. After building tests:

```sh
ctest --test-dir build -L boundary-snap-acceptance --output-on-failure
```

On Windows, first prepend `%QT_PATH%\bin` to `PATH` as described in the developer build instructions. `scripts/run-tests.bat` handles Qt DLL setup for the full run.

The label covers seven suites, including `RegionSelector_BoundarySnapMetrics`. The metrics suite runs the Qt offscreen backend and writes `build/boundary-snap-metrics.json`. The existing macOS, Windows and Ubuntu 22.04 CI jobs execute these tests and upload that JSON alongside their JUnit and CTest logs. A workflow definition alone is not evidence of a passing platform run; inspect the artifact for the exact commit.

Metrics include build type, Qt version, CPU model/architecture, OS, backend, source and logical selection dimensions, DPR, p50/p95, hits, fallbacks, incorrect results, source conversion time/storage sharing, controller/source storage sizes, and whole-process peak RSS. `p95_target_met` evaluates the 4 ms target but does **not** determine the test exit code: slow or contended CI hosts still have to return either the correct crop or the unmodified raw rectangle. Each scenario must first produce an exact hit with the deadline disabled (`preflight_correct`). Thus an implementation that always falls back cannot pass. A failed preflight, incorrect crop/fallback behavior, invalid arguments and report-write failures return nonzero. Peak RSS includes Qt and the source image; it is not incremental cache usage or GPU memory. Windows uses the [documented process memory API](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo); Unix hosts use `getrusage`. Missing metadata remains explicitly unavailable.

For Release measurements outside CTest:

```sh
./scripts/build-release.sh
cmake --build release --config Release --target RegionSelector_BoundarySnapBenchmark
ctest --test-dir release -C Release -R '^RegionSelector_BoundarySnapMetrics$' --output-on-failure
```

On Windows use `scripts\build-release.bat` for the first command and prepend `%QT_PATH%\bin` to `PATH` before the CTest command. The app build script prepares `release/` but only builds `SnapTray`, so the explicit benchmark target build is required. CTest locates the executable on every platform and writes `release/boundary-snap-metrics.json`. No threshold settings are persisted. The production query budget stays at 4 ms; a per-query zero budget provides deterministic coverage of timeout fallback and lock clearing without sleeping or relying on scheduler load.

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
| Rounded far screen edge | `roundedScreenEdge` | 400 px at 150%, 1366 px at 125%, 2560 px at 150/175% and 3840 px at 225% retain snapping while the orthogonal span is clamped to the image |
| Nonrepresentable physical boundary | `unrepresentableEdges` | Fractional/Retina edges absent from the integer crop mapping never produce a hit |
| Light/dark card fixtures | `lightAndDarkCards` | Eight synthetic theme/DPR cases land on exact known crop edges |
| Hysteresis, leaving radius, invalidated span | `hysteresisAndSourceReplacement`, `rankingAndJitter` | Lock retention and exit; zero candidate switches for the scripted near-edge jitter trace |
| Source/DPR replacement | `sourceReplacementChangesScaleAndLock` | Old physical lock is not reused with a different capture scale |
| Timeout/resource fallback | `timeoutFallsBackAndDropsLocks`, `invalidSourcesAndBudgetGuard` | Zero budget returns raw, clears lock before next query; invalid DPR and excessive spans are inert |
| Cache agreement | Not applicable | No image-sized cache or alternative cached query path exists |

## Interaction, display and output matrix

| Issue criterion | Automated check | Scope/limit |
|---|---|---|
| Noncentral handle press; first move has no offset jump | `RegionInputHandler::testBoundarySnapToggleDoesNotAccumulate`, existing offset cases | Real mouse event coordinates and original press delta |
| Alt/Option and S, stationary press/release, autorepeat, final release position | `testBoundarySnapInteraction`, `testBoundarySnapToggleDoesNotAccumulate` | 20 on/off cycles with exact geometry/signal counts; final mouse event modifiers honored |
| Initial selection, whole-rectangle movement and excluded modes stay unchanged | `testBoundarySnapLeavesCreationAndMoveAlone`, `testBoundarySnapExcludedModes` | No guides; exact raw geometry; no key-triggered geometry signals in multi/replace/history/locked-ratio modes; Alt-bearing mouse moves match the ordinary resize baseline |
| Outside-click expansion and keyboard precision remain intact | Existing input outside-click cases, selection manager keyboard cases, `HistoryReplay::testDetectedWindowMetadataSurvivesHighlightClear` | Existing ordinary geometry and arrow/Shift-arrow behavior |
| One final geometry commit per update | `handlesAndDpr`, repeated key-toggle test | One `selectionChanged`, never a raw-then-snapped pair |
| Esc restores original resize | `testResizeEscapeDoesNotAcceptQuickPin` | Move first, then restore the pre-drag rectangle in both Esc routes, with/without snapping and Quick Pin; release emits no pin and writes no history |
| Local guides appear and disappear | `RegionPainterChrome::testSnapGuidesAreLocalAndTransient` | Both short edge segments must independently change pixels, with no differences outside them; clearing/finishing returns the original rendered image |
| History entry/return, close, late input | `HistoryReplay::testBoundarySnapHistoryAndCloseLifecycle` | Replay uses a different image and cannot snap; restored live capture must use its own edge; close clears source/guides and late input cannot reuse them |
| Save/copy crop and history source remain clean | `testBoundarySnapCaptureRoundTrip` | Actual shared export pipeline pixels equal original crop while guides are active; history canvas unchanged. OS clipboard transfer itself remains covered by existing platform tests |
| DPR and negative screen origin | `testBoundarySnapCaptureRoundTrip`, `sourceReplacementChangesScaleAndLock` | Actual selector/export context at DPR 1/1.25/1.5/2 with a negative widget origin asserted after each canvas geometry update; simulated capture changes, not physical mixed-monitor routing |
| Old asynchronous work cannot change a new capture | Source/lifecycle tests; no asynchronous worker | There is no worker completion callback to exercise |
| 4K/6K time and memory | `RegionSelector_BoundarySnapMetrics` | 20 warmups + 200 independent two-axis queries per size/DPR, final crop/fallback verified for every sample; timing target and process peak reported |

## What remains outside automated acceptance

- Real screenshots of light/dark applications, code, dialogs/shadows and photos; target coverage and false-snap rates over a representative, labelled corpus. Synthetic contrast fixtures cannot establish these rates.
- Human mouse/trackpad correction counts and perceived jitter. The scripted trace only establishes deterministic lock stability for that trace.
- Physical macOS Retina and Windows 100/125/150/200% mixed-monitor routing, real pointer sampling and native capture/GPU allocation peaks.
- Ubuntu X11 window-manager interception of Alt-drag. Injected Qt events prove the S alternative's application behavior, not the window manager's event delivery.
- Windows/Linux results until the exact follow-up commit's CI artifacts are available. CI virtual/offscreen screens do not replace the physical checks above.

These remain open acceptance items; automated tests do not justify auto-closing #280 or claiming all physical pixel boundaries and objects can snap.

## Reading CI evidence

Select a [CI Build run](https://github.com/victorfu/snap-tray/actions/workflows/ci.yml) whose head SHA matches the PR, then inspect its three `test-evidence-*` artifacts. The JUnit/CTest logs establish which tests ran; `boundary-snap-metrics.json` records host-specific timing and memory. Require `correct` and every scenario's `preflight_correct` before interpreting timing. `p95_target_met` is a separate performance result, not a universal guarantee. Local timing snapshots are not duplicated in these pages.
