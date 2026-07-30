# Design: mosaic brush size discoverability

- **Date:** 2026-07-30
- **Status:** Approved for implementation
- **Surface:** Mosaic options in Region Capture and Pin Window
- **Issues:** [#95](https://github.com/victorfu/snap-tray/issues/95), split from [#94](https://github.com/victorfu/snap-tray/issues/94)
- **Root feedback:** [issue comment 5127690269](https://github.com/victorfu/snap-tray/issues/94#issuecomment-5127690269)

## Problem

The existing interaction already satisfies the reporter's core need: the mouse
wheel changes the Mosaic brush footprint. The reporter explicitly confirmed
that this was the desired behavior and that the remaining problem was the lack
of a hint.

There is also a separate functional defect. Mosaic currently shares the same
stored width as pencil, arrow, shape, and polyline. The shared default is 3,
while `MosaicStroke` renders a footprint twice its stored width. A fresh Mosaic
session therefore starts with a 6 px footprint even though the handler's
intended brush default is 18 (36 px footprint).

These are two independent product decisions:

1. Teach the existing wheel gesture without making the toolbar permanently
   larger or more complex.
2. Give Mosaic an independent, persisted brush size with a useful default.

## Design principles

- **Teach the existing gesture.** Do not introduce a second primary adjustment
  model when the reporter has already accepted the wheel interaction.
- **Progressive disclosure.** Instruction appears when it is useful, then gets
  out of the way after the user demonstrates the gesture.
- **Visual feedback over internal numbers.** The compact preview and Mosaic
  cursor show the footprint. Do not expose the internal value `18` as though it
  were the rendered 36 px diameter.
- **Use the cross-surface common path.** Region Capture also accepts wheel input
  over the canvas, but Pin Window uses canvas wheel input for zoom. The hint
  therefore teaches scrolling on the size control, which works everywhere.
- **Keep scope Mosaic-specific.** This request does not justify redesigning the
  width UI for pencil, arrow, shape, or polyline.

## User experience

### Compact size preview

`WidthSection` remains a 28 x 28 item containing the existing 22 x 22 blue
preview tile and scaled white dot. It does not gain a number, chevrons, tiny
buttons, auto-repeat, a slider, or presets.

While the Mosaic hint is active, the preview tile receives a subtle border and
slight emphasis so the anchored message has an obvious target. Mosaic hover
uses the same emphasis; other tools retain their existing preview appearance.
The preview remains non-clickable; wheel events continue to be handled by the
existing strip-wide and canvas paths.

### First-use coachmark

When Mosaic becomes active and the user has not yet demonstrated the size
gesture, an input-transparent tooltip is anchored to the compact size preview:

> Scroll here to adjust the mosaic brush size

Behavior:

- Show only when entering Mosaic, not every time the sub-toolbar refreshes.
- Display for 3 seconds and then fade/hide.
- If the user switches tools, closes the overlay, or hides the sub-toolbar,
  hide it immediately.
- A successful wheel gesture while Mosaic is active dismisses it immediately
  and persists that the gesture has been learned.
- Merely waiting for the coachmark to time out does not mark it learned. If the
  user leaves Mosaic without trying the gesture, show it again on the next
  Mosaic activation.
- The tooltip never accepts input or focus.

### Reminder tooltip

Once the automatic coachmark is no longer visible, hovering the Mosaic size
preview shows the same short text. This remains available after the gesture is
learned, while a user who let the coachmark time out can still rediscover it.
It disappears on hover exit. Other tools do not receive a new tooltip in this
change.

### Adjustment feedback

Wheel behavior is unchanged:

- Region Capture: wheel over the sub-toolbar or eligible canvas adjusts width.
- Pin Window: wheel over the sub-toolbar adjusts width; canvas wheel behavior
  remains zoom-related.
- Screen Canvas keeps its existing stroke-width behavior and has no Mosaic tool.

The compact dot preview and Mosaic cursor update immediately. The Mosaic cursor
outline remains the source of truth for the actual painted footprint.

## Functional brush-size behavior

- Mosaic has an independent width slot with stored default 18.
- Stroke tools continue sharing the existing `annotationWidth` setting and
  default 3.
- Mosaic uses a new `mosaicBrushSize` setting, clamped to the supported 1-30
  internal range when loaded.
- `MosaicToolHandler` reads the Mosaic slot rather than the shared stroke slot.
- Region Capture and Pin Window load the active tool's slot when switching tools
  and save changes back to that slot.
- Screen Canvas remains on the stroke slot.
- Cursor preview and painting both use the same Mosaic width.

The learned state is stored through `AnnotationSettingsManager` under a separate
boolean setting. It records that the user performed the wheel gesture, not just
that the coachmark was displayed.

## Architecture

### Width routing

Add a small data-driven `WidthSlot` lookup:

```cpp
enum class WidthSlot { Stroke, MosaicBrush };
WidthSlot widthSlotForTool(ToolId toolId);
```

Mosaic maps to `MosaicBrush`; all other tools default to `Stroke`.
`AnnotationSettingsManager` and `ToolManager` use the same lookup so persistence
and runtime behavior cannot disagree. Existing `loadWidth()` / `saveWidth()`
remain compatibility wrappers for the stroke slot.

### Hint semantics

`PinToolOptionsViewModel` tracks the currently displayed tool and provides the
translated Mosaic hint text. When `handleWidthWheelDelta()` is invoked while
Mosaic is active, it emits a semantic `mosaicBrushAdjustmentLearned()` signal.
Programmatic width synchronization does not emit that signal.

### Anchored tooltip

`QmlFloatingSubToolbar` owns a reusable anchored tooltip controller based on the
existing `RecordingTooltip.qml` glass surface. It supports an above/below
preference, screen-edge flipping, input transparency, transient parenting, and
macOS window-level policy.

This change does not migrate the existing main-toolbar tooltip implementations;
that refactor is independent of the Mosaic UX and can be reviewed separately.

`QmlFloatingSubToolbar` owns the coachmark timer and activation state. It marks
the hint learned through `AnnotationSettingsManager` after receiving the
ViewModel's semantic signal.

## Out of scope

- Permanent numeric width readout.
- Inline up/down stepper or press-and-hold auto-repeat.
- Small/medium/large presets or a click-to-open size popover.
- Redesigning width affordances for non-Mosaic annotation tools.
- Increasing the internal maximum above 30.
- Mosaic block granularity (#55).
- Rectangle-selection Mosaic.
- The unrelated CJK text blur bug from #94.
- Refactoring all existing toolbar tooltip call sites.

## Acceptance criteria

- The Mosaic size preview remains 28 x 28 and does not permanently widen the
  sub-toolbar.
- Entering Mosaic before the gesture is learned shows the coachmark without
  requiring hover.
- The coachmark is anchored to the size preview, stays on-screen, does not cover
  the main toolbar when a below placement is available, and never steals input.
- A Mosaic wheel gesture dismisses the coachmark and prevents it from appearing
  on later activations and application restarts.
- Timing out without a gesture does not mark it learned.
- Hovering the Mosaic preview still reveals the instruction after learning.
- Pencil, arrow, shape, and polyline receive no new permanent controls.
- Mosaic defaults to 18 independently of stroke width, persists independently,
  and restores both slots correctly when switching tools.
- Cursor outline and actual Mosaic footprint remain consistent at 1x, 1.5x,
  and 2x scale factors.
- Pin Window canvas-wheel zoom behavior is unchanged.

## Verification

Automated tests cover width-slot mapping, settings defaults/clamping/persistence,
ToolManager routing, `MosaicStroke` width consumption, ViewModel learning-signal
semantics, compact QML dimensions, hover signaling, and translations.

Manual checks cover first activation, timeout without learning, dismissal after
wheel input, restart persistence, screen-edge placement, Region Capture and Pin
Window, mouse wheel and macOS trackpad, and 1x/2x display scale.
