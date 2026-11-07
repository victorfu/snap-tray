---
last_modified_at: 2026-11-07
layout: docs
title: Selection Boundary Snap
permalink: /developer/boundary-snap/
lang: en
nav_data: developer_nav
---

# Selection boundary snapping

Issue [#280](https://github.com/victorfu/snap-tray/issues/280) defines the first version: hold Alt/Option, or hold S after starting a handle drag, to align a live single selection with horizontal/vertical contrast boundaries. Ordinary selection, movement, multi-region/replace, history editing and aspect-ratio-locked resizing retain their existing geometry. The source is the unannotated capture, not an overlay or a detected OS window. See the [user instructions](../docs/region-capture.md).

## Data and geometry

- `BoundarySnapIndex` holds an immutable, implicitly shared QImage. RGB32 and premultiplied ARGB32 are reused; other formats convert once to premultiplied ARGB32. Transparency is effectively composited over black, without scoring alpha separately. Null, smaller-than-2-pixel and over-40MP sources are disabled.
- Queries evaluate adjacent RGB Euclidean distances along a half-open physical span. The mean must reach 28 and at least 55% of samples must reach 28. There is no full-screen float map, downsampling, OCR or object recognition.
- `BoundarySnapController` searches representable logical edges within 6 logical pixels. An existing lock is retained within 9 pixels only while its current raw span still qualifies. Otherwise rank by logical distance, mean strength, support, then ascending coordinate. Corner queries both use the same raw rectangle.
- Leading crop edges use floor(logical × DPR); trailing edges use ceil((origin + size) × DPR). Candidates are evaluated at these actual physical boundaries, so unreachable physical edges are skipped. The resulting crop is checked again with `CoordinateHelper::toPhysicalCoveringRect`.
- `SelectionStateManager::updateResize` optionally transforms its original-pointer-delta preview, checks minimum size, bounds and fixed sides, then emits one final geometry update. Invalid transforms fall back to raw geometry. Crossing an opposite side retains the previous resize normalization and disables snapping for that update.
- Capture-context replacement increments a generation and resets locks. Queries are synchronous, so no worker can publish an old result. History entry/exit, release, cancellation and close clear interaction state. Source coordinates are local to the current capture; global screen origins never enter the edge query.
- Short guide segments use the shared `RegionPainter` selection accent in both the host and detached Windows chrome. The export and history pipelines do not render these guides.

## Work and memory limits

The controller queries at most 13 candidates per axis, plus an existing lock, and rejects spans over 16,384 physical pixels. It checks a 4 ms elapsed-time budget between candidates and after both axes. A timeout discards both results and clears locks. The current candidate is completed before the next check; the budget is not a hard real-time deadline, and OS scheduling may also exceed it.

There is no persistent image-sized edge index or cache (0 bytes of pixel-index storage). The controller also owns a small fixed-size pair of optional locks and QImage metadata. Source pixel storage is separate: RGBA-sized 4K is 33,177,600 bytes and 6016×3384 is 81,432,576 bytes. `QPixmap::toImage` may share or allocate depending on the backend; a format conversion may allocate another source-sized buffer while the input lives. Conservatively allow two additional source-sized buffers at conversion peak, separate from the original screenshot. A failed conversion disables snapping. Actual source-sharing, conversion time and whole-process memory measurements must identify the platform/backend rather than assuming zero-copy everywhere.

## Verification and measurement

- `Detection_BoundarySnapIndex`: formats/alpha, hard edges, contrast, sparse texture, gradients, span/support rejection and invalid input.
- `RegionSelector_BoundarySnap`: eight handles × DPR 1/1.25/1.5/2, final crop edges, press offsets, one geometry signal, constraints, hysteresis and generation replacement.
- `RegionSelector_RegionInputHandler`: stationary key transitions, S fallback, release position, cancellation, source changes and excluded modes.
- `RegionSelector_HistoryReplay::testBoundarySnapCaptureRoundTrip`: real selector/capture/export/history route at four DPRs, with a negative widget origin; exported pixels and history source remain unmodified by guides.
- Existing selection, history, export, painter and multi-region tests cover shared callers.

For reproducible synthetic timing, build `RegionSelector_BoundarySnapBenchmark` with CMake in **Release** mode and run it directly (it is intentionally not a CTest timing assertion). It records two-axis, full-height/full-width near-edge queries, resetting locks each time, 20 warm-ups and 200 samples, 4K/6K at four DPRs. It reports p50/p95, successful two-axis hits, source conversion, source sharing and storage size. Use the OS memory profiler or `/usr/bin/time -l` on macOS to record whole-process peak memory separately. Debug timing is not release timing.

The automated synthetic checks do not establish real-world target coverage, false-snap rate, jitter or mouse/trackpad ergonomics. Manual validation remains necessary on macOS Retina, Windows 100/125/150/200% and Ubuntu 22.04 X11, including Alt interception and the S alternative, light/dark cards, tables, text, shadows and photos. These pending checks must not be described as completed or as a guarantee that every pixel boundary can snap.

### Local measurement, 2026-11-07

Apple M3 Pro, macOS 27.2, Qt 6.12.0, AppleClang 21, Release (`-O3`), Qt offscreen raster backend. The probe was compiled from the production index/controller/coordinate-helper sources using a standalone CMake target; unrelated desktop/build activity was not suspended. These are synthetic query measurements, not end-to-end pointer latency or real-image accuracy.

| Source | DPR | Raw logical selection | p50 ms | p95 ms | Two-axis hits / 200 |
|---|---:|---|---:|---:|---:|
| 3840×2160 | 1 | 3727×2047 | 0.093 | 0.304 | 200 |
| 3840×2160 | 1.25 | 2979×1635 | 0.096 | 0.317 | 200 |
| 3840×2160 | 1.5 | 2480×1360 | 0.098 | 0.324 | 200 |
| 3840×2160 | 2 | 1857×1017 | 0.098 | 0.314 | 200 |
| 6016×3384 | 1 | 5903×3271 | 0.243 | 1.379 | 197 |
| 6016×3384 | 1.25 | 4719×2614 | 0.536 | 3.092 | 194 |
| 6016×3384 | 1.5 | 3931×2176 | 0.547 | 3.787 | 191 |
| 6016×3384 | 2 | 2945×1629 | 0.249 | 1.312 | 196 |

All reported p95s met the initial 4 ms target. The missing 6K hits were budget fallbacks; the raw drag remained available. Source conversion/setup took 0.0007–0.0013 ms and shared pixel storage with the input on this raster backend. The controller object was 120 bytes, with no pixel index/cache. `/usr/bin/time -l` reported a 136,265,728-byte whole-process maximum RSS and 118,833,824-byte peak memory footprint for all eight scenarios. These figures include Qt and the source images; they do not measure a native capture backend's GPU allocations or prove zero-copy behavior on other platforms.
