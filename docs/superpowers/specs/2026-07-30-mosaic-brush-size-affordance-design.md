# Design: brush size affordance + independent mosaic brush size

- **Date:** 2026-07-30
- **Status:** Approved (design), pending implementation plan
- **Surface:** annotation sub-toolbar (`ToolOptionsStrip`), tool width plumbing, QML overlay tooltips
- **Issues:** [#95](https://github.com/victorfu/snap-tray/issues/95) (split from [#94](https://github.com/victorfu/snap-tray/issues/94))

## Goal

Make annotation brush/line size **visibly adjustable**, and give the mosaic tool
its own size that is not hijacked by the pencil line width.

The reporter on #94 raised two things that look like one: "馬賽克範圍太小" and
"提示不好". They have separate causes and both need fixing.

1. **Discoverability.** Size is adjustable today — the mouse wheel works both
   over the sub-toolbar ([`ToolOptionsStrip.qml:67`](../../../src/qml/toolbar/ToolOptionsStrip.qml)) and
   over the canvas ([`RegionSelector.cpp:4346`](../../../src/RegionSelector.cpp)). Nothing on screen
   says so. [`WidthSection.qml`](../../../src/qml/toolbar/WidthSection.qml) is a 28×28 blue square with a
   white dot and no interaction of its own.
2. **Wrong default.** `ToolContext::width` is a single shared value across
   pencil / arrow / shape / polyline / mosaic ([`ToolContext.h:39`](../../../include/tools/ToolContext.h)),
   persisted under one `annotationWidth` key with default 3
   ([`AnnotationSettingsManager.h:58`](../../../include/settings/AnnotationSettingsManager.h)). Mosaic reads it directly
   ([`MosaicToolHandler.cpp:16`](../../../src/tools/handlers/MosaicToolHandler.cpp)) and `MosaicStroke` doubles it
   ([`MosaicStroke.cpp:413`](../../../src/annotations/MosaicStroke.cpp)), so the out-of-box mosaic brush is
   6 px instead of the intended 36 px. `MosaicToolHandler::kDefaultBrushWidth = 18`
   is dead code — the `ctx->width > 0` fallback never fires.

## Scope

**In scope:**

- Rework `WidthSection` into a control that reads as adjustable: size preview +
  numeric value + up/down stepper. Applies to all five tools that show a width
  section (pencil, arrow, shape, polyline, mosaic).
- Extract the duplicated QML overlay tooltip machinery into a shared controller
  and wire the sub-toolbar to it; show a tooltip on `WidthSection` hover.
- Give mosaic its own persisted brush size, default 18.

**Out of scope:**

- Three-size presets (小/中/大) for mosaic. Reconsider after the default is
  fixed; the stepper can grow a preset popover later without rework.
- Raising `maxWidth` above 30. Brush cursors have no upper clamp
  ([`CursorStyleCatalog.cpp:186`](../../../src/cursor/CursorStyleCatalog.cpp)) and a much larger cursor will
  hit OS limits — that needs an on-canvas brush ring instead, and its own design.
- Mosaic pixel-block granularity (issue #55).
- The #94 CJK text blur bug. Same issue thread, unrelated code.
- Tooltips for the other sub-toolbar sections. The shared controller makes them
  cheap later; not needed for this change.
- Renaming `RecordingTooltip.qml`, which is already the shared tooltip UI
  despite its name.

## Decisions

1. **Keep the wheel exactly as-is.** No change to `handleWidthWheelDelta`, the
   `ToolOptionsStrip` wheel `MouseArea`, or `RegionSelector::wheelEvent`. The
   stepper is an addition, and its main job is to advertise that the wheel works.
2. **Value readout is always visible**, not hover-only. It is the only size
   feedback available while the pointer is over the sub-toolbar rather than the
   canvas (where the mosaic cursor already previews actual coverage).
3. **Chevrons are always visible** at reduced opacity, full opacity on hover.
   Hover-only affordances do not solve discoverability.
4. **Extract the tooltip controller rather than copy it a third time.** The
   machinery already exists twice, near-identically, in
   [`QmlFloatingToolbar.mm:322`](../../../src/qml/QmlFloatingToolbar.mm) and
   [`QmlWindowedToolbar.mm:147`](../../../src/qml/QmlWindowedToolbar.mm).
5. **Mosaic size ships with this change**, not separately. Both touch the same
   files (`ToolContext`, `ToolManager`, `PinToolOptionsViewModel`,
   `WidthSection.qml`), and the affordance alone still leaves every mosaic
   session starting at 6 px.

## Architecture

### A. `WidthSection` becomes a stepper

Layout, left to right, inside the existing sub-toolbar row:

| Part | Size | Notes |
|------|------|-------|
| Dot preview | 22×22 | Unchanged: blue rounded square, white dot scaled 4–20 px |
| Value label | ~15 px wide | 12 px text, `SemanticTokens` foreground, always visible |
| Stepper | 8×18 | Two `ToolbarChevron` stacked; down chevron rotated 180° for up |

`implicitWidth` goes 28 → 48. No downstream size constants need updating:
`ToolOptionsStrip.contentWidth` sums visible children's `implicitWidth`
([`ToolOptionsStrip.qml:25`](../../../src/qml/toolbar/ToolOptionsStrip.qml)), and `QmlFloatingSubToolbar`
resizes its `QQuickView` from the root item's `implicitWidth`
([`QmlFloatingSubToolbar.mm:38`](../../../src/qml/QmlFloatingSubToolbar.mm)).

Interaction:

- Click a chevron: ±1, clamped by the existing `qBound(minWidth(), width, maxWidth())`
  in [`PinToolOptionsViewModel.cpp:120`](../../../src/qml/PinToolOptionsViewModel.cpp) (1–30).
- Press and hold: auto-repeat after 400 ms at 60 ms intervals, driven by a QML
  `Timer`. Stops on release, on pointer leave, and when the bound is reached.
- At a bound, the unavailable chevron dims to 0.25 opacity and stops responding.
  It is not removed — a disappearing control reads as a bug.
- The chevrons sit inside the existing strip-wide wheel `MouseArea`, so wheeling
  over them keeps working.

`ToolbarChevron` is reused as-is; it already renders a filled 8×6 triangle and
takes `strokeColor` from `ComponentTokens.toolbarIcon`.

### B. Shared tooltip controller

**New:** `include/qml/ToolbarTooltipController.h` + `src/qml/ToolbarTooltipController.mm`,
added to `SNAPTRAY_QML_NATIVE_SOURCES` in [`CMakeLists.txt:998`](../../../CMakeLists.txt). The
existing `if(NOT APPLE)` / `LANGUAGE CXX` rule at line 1011 already covers new
entries in that list, so no build-system change beyond the one line.

Responsibilities, lifted verbatim from the two current copies:

- Lazily create the tooltip `QQuickView` via
  `QmlOverlayManager::createParentOverlay("components/RecordingTooltip.qml")`,
  with `WindowDoesNotAcceptFocus`, `WindowTransparentForInput`, and
  `SizeRootObjectToView`.
- Own the transient-parent sync and the macOS window-level / `hidesOnDeactivate`
  / `sharingType` policy currently in `applyTooltipWindowFlags`.
- Measure `implicitWidth`/`implicitHeight` after a `QTimer::singleShot(0, …)`
  polish, clamp x to the screen's available geometry, and show.
- Carry the existing request-id guard so a stale async placement cannot
  reposition a newer tooltip.

Public API:

```cpp
enum class TooltipPlacement { Above, Below };

void showFor(const QString& text,
             const QRect& anchorGlobalRect,
             QQuickView* owner,
             TooltipPlacement preferred);
void hide();
QWindow* window() const;
```

`preferred` is new and is the reason a straight copy would not have worked: the
sub-toolbar sits **below** the main toolbar, so its tooltip must open downward or
it covers the toolbar the user is aiming at. Both existing call sites pass
`Above` and keep their current behaviour, including the existing off-screen
fallback that flips to the other side.

Migration: `QmlFloatingToolbar` and `QmlWindowedToolbar` delegate to the
controller and drop their `m_tooltipView` / `m_tooltipRootItem` members,
`ensureTooltipView`, `applyTooltipWindowFlags`, `syncTooltipTransientParent`,
`showTooltip`, `hideTooltip`, and the destructor teardown. Net effect is roughly
200 lines removed, one implementation added.

Sub-toolbar wiring:

- `ToolOptionsStrip.qml` gains `signal sectionHovered(string key, real x, real y, real w, real h)`
  and `signal sectionUnhovered()`. `WidthSection` gets a `hoverEnabled` MouseArea
  with `acceptedButtons: Qt.NoButton` so clicks still reach the chevrons.
- `QmlFloatingSubToolbar` connects those signals, maps the key to text, and calls
  `showFor(..., TooltipPlacement::Below)`.

### C. Per-tool width storage

Mosaic gets its own slot rather than a second UI control, so the sub-toolbar
still shows one size widget.

- `AnnotationSettingsManager`: add `loadMosaicBrushSize()` / `saveMosaicBrushSize(int)`,
  key `mosaicBrushSize`, `kDefaultMosaicBrushSize = 18`, clamped to 1–30 on load
  so a hand-edited value cannot escape the UI range.
- `ToolContext`: add `int mosaicWidth = 18;` alongside the existing `width`.
- `MosaicToolHandler::onMousePress` reads `ctx->mosaicWidth`. The dead
  `kDefaultBrushWidth` fallback is removed; the default now lives in the settings
  manager, one place.
- `ToolManager::setWidth` / `width()` route through a lookup, not a conditional:

  ```cpp
  enum class WidthSlot { Stroke, MosaicBrush };
  WidthSlot widthSlotForTool(ToolId);   // table lookup, default Stroke
  ```

  Mosaic maps to `MosaicBrush`; every other tool maps to `Stroke`. Adding a
  future tool with its own size is a table row.
- `CursorManager` needs no change: [`CursorManager.cpp:291`](../../../src/cursor/CursorManager.cpp) already calls
  `mosaicHandler->setWidth(toolManager->width())`, which now returns the mosaic
  slot when mosaic is active.
- The hosts (`RegionSelector`, `ScreenCanvasSession`, `PinWindow`) already push a
  width into the ViewModel on tool change; they load and save via the slot for
  the newly-selected tool. Switching pencil → mosaic → pencil restores each
  tool's own value.

This follows the precedent set by `StepBadgeToolHandler`, which already keeps its
size out of `ctx->width` ([`StepBadgeToolHandler.cpp:21`](../../../src/tools/handlers/StepBadgeToolHandler.cpp)).

`ScreenCanvas` has no mosaic tool ([`ToolRegistry.cpp:434`](../../../src/tools/ToolRegistry.cpp)), so the
mosaic slot is only reachable from Region Capture and Pin Window. The stepper and
tooltip still apply to all three surfaces via the other width tools.

### D. Tooltip copy

Text carries the current value so one hover teaches both what the control is and
how to drive it:

- Mosaic: `馬賽克大小：18（滾輪可調整）`
- Other width tools: `線寬：3（滾輪可調整）`

Wrapped in `tr()` and added to all 24 `TS_FILES` in [`CMakeLists.txt:1284`](../../../CMakeLists.txt).
While the tooltip is open, wheeling updates the value in place rather
than dismissing it — seeing the number move under the hint is the moment the
gesture is learned.

## Edge cases

- **Bounds.** Chevron at 1 (down) or 30 (up) dims and no-ops. Wheel past a bound
  is already a no-op via `qBound`.
- **Auto-repeat leak.** The hold timer must stop on release, on pointer exit, and
  on sub-toolbar hide — otherwise it keeps firing into a hidden ViewModel.
- **Tooltip lifetime.** Hide on sub-toolbar hide, tool switch, and overlay close.
  The existing toolbars already hide on those transitions; the controller must
  not outlive its owner view.
- **Input transparency.** The tooltip window keeps `WindowTransparentForInput`;
  it must never sit between the pointer and the stepper.
- **Marker** has no width section and is unaffected. **StepBadge** uses
  `SizeSection`, not `WidthSection`, and is unaffected.
- **Migration.** Existing users have `annotationWidth` set. On first run after
  the update `mosaicBrushSize` is absent and defaults to 18 — an intentional
  jump from whatever the shared width was.

## Testing

Automated:

- `tests/Settings` — mosaic brush size round-trips; absent key yields 18;
  out-of-range stored values clamp to 1–30.
- `tests/Tools` — `widthSlotForTool` mapping; `ToolManager::setWidth`/`width()`
  route to the right slot; switching tools preserves both values independently;
  `MosaicStroke` receives `mosaicWidth`, not `width`.
- `tests/Qml` — `WidthSection` reports the expected `implicitWidth`; chevron
  clicks step the ViewModel by ±1 and clamp at both bounds; the wheel path still
  reaches `handleWidthWheelDelta` unchanged.

Manual:

- Stepper + tooltip on all three surfaces: Region Capture, Screen Canvas, Pin
  Window; at 1x and 2x DPI.
- Mosaic size independence: pencil 3 → mosaic 18 → pencil back at 3; persists
  across restart.
- **Tooltip regression check on the two migrated call sites** — main capture
  toolbar and Pin Window toolbar tooltips must still appear above, position
  correctly near screen edges, and not steal input. This is the main risk in the
  change.
- Verify build on all three platforms; the tooltip controller is Objective-C++
  on macOS and plain C++ elsewhere.

Commands: `./scripts/build.sh` then `./scripts/run-tests.sh` (macOS/Linux),
`scripts\build.bat` / `scripts\run-tests.bat` (Windows).

## Related

- Issue #95 — this design
- Issue #94 — parent thread; the CJK text blur bug there is separate work
- Issue #55 — mosaic pixel-block granularity, unrelated to brush footprint
