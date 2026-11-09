# Incremental Mosaic preview

Pixelate keeps source-global averaged block colors separate from the stroke's
binary square coverage. Colors are local to a stroke: the source identity and
DPR are reset by `setSourcePixmap`; block size and blur type are immutable.
Translation discards coverage but can reuse source block colors. No global
source image cache is introduced.

Coverage is stored as sparse 128 x 128 physical-pixel tiles. An append stamps
only the previous endpoint, the new segment's interpolated squares and the new
endpoint. The first repeated point is retained because the legacy renderer
requires two points to draw a dot; subsequent repeated endpoints do no work.
Bounds are maintained incrementally. Block averages, truncation, source-edge
clipping, square interpolation and the legacy replacement of source alpha by
mask alpha are preserved, including opaque black under the mask outside source.

## Compatibility and limits

- Fractional source DPR: changing bounds can change old pixels because the old
  renderer rounds relative to those bounds. An origin change rebuilds coverage;
  a size-only change reuses coverage but can change the final resampling phase.
  Both invalidate the full preview. Keep cached source block colors. This intentionally
  prioritizes pixel parity over speed in these frames (issue #292 option 1).
- Mixed DPR, zoom, rotation, smoothing or non-SourceOver composition: assemble
  tiles into a temporary image and use the legacy single-pixmap draw operation.
  This preserves the resampling phase and cross-tile filtering and requests
  full preview damage for subsequent additions. Native, integral
  DPR SourceOver painting draws the tiles directly. Fractional/mixed-DPR cached
  draws still pay temporary assembly cost; this is not a universal speedup.
- Per stroke: 16 MiB tile budget and 256 KiB LRU block-color budget, including a
  conservative 128-byte metadata charge per entry. Source pixmaps are shared and
  excluded. `estimatedRetainedBytes()` includes these caches for history limits.
  On tile overflow, discard tiles and use the legacy renderer without retaining
  its full bounding image. Temporary rendering allocations are not covered by
  the cache budget. Large over-budget strokes can be slower than the old cache.
- Cancelling or switching tools destroys the active stroke and its caches.
  Committing transfers it to the annotation/history layer under existing limits.
  Escape retains host behavior (e.g. closing capture/canvas); it is not given a
  new tool-specific meaning. Gaussian uses the existing clipped-patch path.

`previewAffectedBoundingRect()` covers every append since the last draw, so
coalesced mouse events do not lose damage. `MosaicToolHandler::previewBounds()`
feeds the existing ToolManager signal and RegionSelector, PinWindow and
ScreenCanvasSession repaint planners. Source changes, translation, fractional
bounds changes and fallback paths request conservative damage. Deactivation
requests a full preview clear using the existing empty-bounds convention.

## Reproduce correctness and timing

Run the canonical build and test scripts first. The annotation test freezes the
renderer from `bc6f6818333a58d0e30a6139416d7495a2e547ea` in `tests/Annotations/reference`.
It compares every frame across all 16 source/target DPR combinations
(1, 1.25, 1.5, 2), including reversals, self-crossings, negative coordinates,
source edges, alpha, replacement, translation, cloning and transformed painting.
The tool test reconstructs the preview using only requested damage and compares
it to full painting, then checks commit, undo/redo, serialization and cancellation.
These are automated renderer/ToolManager checks, not manual OS-window validation.

The optional benchmark uses the same executable and reference implementation:

```sh
# After scripts/run-tests.sh has built the test target:
QT_QPA_PLATFORM=offscreen SNAPTRAY_MOSAIC_BENCHMARK=1 \
  ./build/bin/Annotations_MosaicStroke benchmark
```

For optimized results configure a separate Release build with the same Qt SDK,
build `Annotations_MosaicStroke`, and substitute that build's binary above. On
Windows set the corresponding environment variables and add the matching Qt
DLL directory to PATH, as in `scripts/run-tests.bat`.

Input: 2600 x 1600 physical source at DPR 2; brush width 24, block size 12;
25/100/200 points `(40 + 5*i, 40 + 3*i)`, followed by a new `(1,1)` delta.
Each implementation primes its cache before timing the append's draw. One warmup
is excluded, followed by 20 samples; report upper median and nearest-rank p95.
Painting is offscreen and unclipped, so the timing includes drawing all retained
tiles. The report also includes newly sampled source blocks, stamps and tile
writes. Compare both renderers in the same binary/build; this microbenchmark
does not measure overall application, compositor or input-event latency.
