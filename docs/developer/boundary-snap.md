---
last_modified_at: 2026-11-07
layout: docs
title: Selection Boundary Snap
permalink: /developer/boundary-snap/
lang: en
route_key: developer_boundary_snap
docs_copy_key: developer
description: Image-boundary scoring, crop geometry, interaction limits, and CI evidence for selection resizing.
nav_data: developer_nav
---

# Selection boundary snapping

Issue [#280](https://github.com/victorfu/snap-tray/issues/280) defines the first version: hold Alt/Option, or hold S after starting a handle drag, to align a live single selection with horizontal/vertical contrast boundaries. Ordinary selection, movement, multi-region/replace, history editing and aspect-ratio-locked resizing retain their existing geometry. The source is the unannotated capture, not an overlay or a detected OS window. See the [user instructions](../docs/region-capture.md).

## Data and geometry

- `BoundarySnapIndex` holds an immutable, implicitly shared QImage. RGB32 and premultiplied ARGB32 are reused; other formats convert once to premultiplied ARGB32. Transparency is effectively composited over black, without scoring alpha separately. Null, smaller-than-2-pixel and over-40MP sources are disabled.
- Queries evaluate adjacent RGB Euclidean distances along a half-open physical span. The mean must reach 28 and at least 55% of samples must reach 28. There is no full-screen float map, downsampling, OCR or object recognition.
- `BoundarySnapController` searches representable logical edges within 6 logical pixels. An existing lock is retained within 9 pixels only while its current raw span still qualifies. Otherwise rank by logical distance, mean strength, support, then ascending coordinate. Corner queries both use the same raw rectangle.
- Leading crop edges use floor(logical × DPR); trailing edges use ceil((origin + size) × DPR). Candidates are evaluated at these actual physical boundaries, so unreachable physical edges are skipped. The rounded logical canvas may overhang the last physical pixel on fractional-DPR screens. Query spans and final crops are intersected with the image bounds, matching export; genuinely out-of-canvas rectangles remain ineligible.
- `SelectionStateManager::updateResize` optionally transforms its original-pointer-delta preview, checks minimum size, bounds and fixed sides, then emits one final geometry update. Invalid transforms fall back to raw geometry. Crossing an opposite side retains the previous resize normalization and disables snapping for that update.
- Capture-context replacement swaps the source and resets locks. Queries are synchronous; there is no background worker or generation callback. History entry/exit, release, cancellation and close clear interaction state. Source coordinates are local to the current capture; global screen origins never enter the edge query.
- Short guide segments use the shared `RegionPainter` selection accent in both the host and detached Windows chrome. The export and history pipelines do not render these guides.

## Work and memory limits

The controller queries at most 13 candidates per axis, plus an existing lock, and rejects spans over 16,384 physical pixels. It checks a 4 ms elapsed-time budget between candidates and after both axes. A timeout discards both results and clears locks. The current candidate is completed before the next check; the budget is not a hard real-time deadline, and OS scheduling may also exceed it.

There is no persistent image-sized edge index or cache (0 bytes of pixel-index storage). The controller also owns a small fixed-size pair of optional locks and QImage metadata. Source pixel storage is separate: RGBA-sized 4K is 33,177,600 bytes and 6016×3384 is 81,432,576 bytes. `QPixmap::toImage` may share or allocate depending on the backend; a format conversion may allocate another source-sized buffer while the input lives. Conservatively allow two additional source-sized buffers at conversion peak, separate from the original screenshot. A failed conversion disables snapping. Actual source-sharing, conversion time and whole-process memory measurements must identify the platform/backend rather than assuming zero-copy everywhere.

## Verification and measurement

- `Detection_BoundarySnapIndex`: formats/alpha, hard edges, contrast, sparse texture, gradients, span/support rejection and invalid input.
- `RegionSelector_BoundarySnap`: eight handles × DPR 1/1.25/1.5/2, final crop edges, press offsets, one geometry signal, constraints, hysteresis and source replacement.
- `RegionSelector_RegionInputHandler`: stationary key transitions, S fallback, release position, cancellation, source changes and excluded modes.
- `RegionSelector_HistoryReplay::testBoundarySnapCaptureRoundTrip`: real selector/capture/export/history route at four DPRs, with a negative widget origin; exported pixels and history source remain unmodified by guides.
- Existing selection, history, export, painter and multi-region tests cover shared callers.

For reproducible synthetic timing, build `RegionSelector_BoundarySnapBenchmark` with CMake in **Release** mode and run it directly. CTest also runs it as `RegionSelector_BoundarySnapMetrics`; correctness gates the test while timing targets are reported separately. It records two-axis, full-height/full-width near-edge queries, resetting locks each time, 20 warm-ups and 200 samples, 4K/6K at four DPRs. It reports p50/p95, successful two-axis hits, source conversion, source sharing and storage size. Use `--report file.json` to retain the metrics, including CPU/OS/Qt/build metadata and whole-process peak RSS (peak working set on Windows). An unavailable memory measurement is -1 and an unavailable CPU model is empty. This excludes GPU allocations. Debug timing is not release timing. See the [acceptance matrix](boundary-snap-acceptance.md) for exact test mappings and CI artifacts.

The automated synthetic checks do not establish real-world target coverage, false-snap rate, jitter or mouse/trackpad ergonomics. Manual validation remains necessary on macOS Retina, Windows 100/125/150/200% and Ubuntu 22.04 X11, including Alt interception and the S alternative, light/dark cards, tables, text, shadows and photos. These pending checks must not be described as completed or as a guarantee that every pixel boundary can snap.

## Measurement evidence

Use the JSON in the exact commit's `test-evidence-*` artifacts from [CI Build](https://github.com/victorfu/snap-tray/actions/workflows/ci.yml). Compare like-for-like CPU, build type, backend, source size and DPR. Timing varies with host load; static local timing tables are intentionally not maintained here. The [acceptance matrix](boundary-snap-acceptance.md) contains the reproduction commands and report interpretation.
