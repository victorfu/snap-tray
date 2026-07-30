# Mosaic brush size discoverability - implementation plan

**Design:** [mosaic-brush-size-discoverability-design.md](../specs/2026-07-30-mosaic-brush-size-discoverability-design.md)

## Constraints

- Preserve the existing wheel paths and Pin Window canvas zoom behavior.
- Keep `WidthSection` at 28 x 28; no numeric readout or stepper.
- Use `AnnotationSettingsManager` for all persistence.
- Keep Mosaic's internal range at 1-30 and default at 18.
- User-visible text is translated in every file listed by `TS_FILES`.
- Do not migrate unrelated toolbar tooltip implementations in this change.

## Task 1: Width slots and settings

- Add `WidthSlot` and `widthSlotForTool(ToolId)` with a table override for
  Mosaic and a Stroke default.
- Add active-tool width loading/saving to `AnnotationSettingsManager` while
  retaining stroke compatibility wrappers.
- Add the Mosaic size default, range clamping, and learned-hint boolean.
- Add unit tests for mapping, defaults, clamping, round trips, and learned state.
- Register the new source and tests in CMake.

## Task 2: Runtime width routing

- Add an independent `mosaicWidth` to `ToolContext`.
- Route `ToolManager::setWidth()` and `width()` through `WidthSlot`.
- Make `MosaicToolHandler` consume `mosaicWidth` and remove its unreachable
  fallback default.
- Add tests proving tool switching preserves both slots and a rendered Mosaic
  stroke receives the Mosaic slot.

## Task 3: Host integration

- Region Capture loads the active width slot before synchronizing the
  sub-toolbar and saves changes to that slot.
- Ensure same-tool Mosaic toggle keeps `RegionToolbarHandler` and ToolManager in
  sync before sub-toolbar callbacks run.
- Pin Window loads the selected tool's slot before showing its sub-toolbar and
  saves changes to the active slot.
- Screen Canvas remains on the Stroke slot through the compatibility API.
- Add or extend regression tests for tool switching and Mosaic toggle behavior.

## Task 4: Hint semantics and compact QML

- Track the active tool in `PinToolOptionsViewModel`.
- Provide translated `mosaicBrushHintText()`.
- Emit `mosaicBrushAdjustmentLearned()` only for wheel gestures while Mosaic is
  active; programmatic width updates must not emit it.
- Restore `WidthSection` to its compact preview layout.
- Add hover signals using `HoverHandler` so wheel delivery is not intercepted.
- Add a temporary `hintActive` visual emphasis with no click affordance.
- Extend QML and ViewModel tests.

## Task 5: Anchored coachmark and hover reminder

- Add a reusable anchored tooltip controller using `RecordingTooltip.qml`.
- Integrate it only into `QmlFloatingSubToolbar`.
- Show a pending coachmark after `positionBelow()` has placed the sub-toolbar.
- Keep automatic activation separate from repeated same-tool refreshes.
- Hide on tool switch, overlay hide/close, and successful Mosaic wheel input.
- Persist learned state only after the semantic learning signal.
- After the 3-second coachmark expires, retain hover-only reminder behavior.
- Associate the tooltip with the parent widget safely using `QPointer`.

## Task 6: Localization, changelog, and verification

- Add `Scroll here to adjust the mosaic brush size` to all translations with
  context-appropriate translations.
- Extend translation integrity tests.
- Update `CHANGELOG.md` under Unreleased.
- Run `git diff --check`, the Windows build script, targeted tests, and the full
  canonical test suite.
- Perform final read-only UX and correctness reviews.

## Manual verification checklist

- Fresh learned-state: entering Mosaic shows the coachmark once per activation.
- Let it time out without scrolling, switch away and back: it appears again.
- Scroll while active: size changes, preview/cursor update, hint disappears.
- Restart and select Mosaic: learned hint no longer appears; hover reminder does.
- Region Capture canvas and sub-toolbar wheel paths still work.
- Pin Window sub-toolbar wheel changes Mosaic size; canvas wheel still zooms.
- Screen-edge and multi-monitor placement remain on-screen and input-transparent.
- Validate mouse wheel on Windows/Linux and trackpad on macOS.
