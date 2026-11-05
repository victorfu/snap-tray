# Design: Capture toolbar grouping, priority overflow, and selected-object styles

- **Date:** 2026-11-05
- **Status:** Approved (design), pending implementation plan
- **Issue:** [#224](https://github.com/victorfu/snap-tray/issues/224) (tracked by #221)
- **Surfaces:** Region Selector capture toolbar, Pin Window annotation toolbar, Screen Canvas toolbar

## Goal

Apply the #221 "light floating tool" direction to the annotation toolbars without
building a new toolbar system:

1. Make the three responsibilities of the toolbar visible: drawing tools, editing
   aids, and finishing actions.
2. Keep the toolbar usable on small selections, near screen edges, and across
   monitors by collapsing low-frequency buttons into **More** instead of shrinking
   hit targets.
3. Make the style strip honest about what it changes: with an annotation selected
   it edits **that annotation** (undoable); otherwise it sets up the **next new
   object**, as today.

## Current state (2026-11-05, `eb5d9113`)

- The Region Selector toolbar is one row of 17 buttons
  (`ToolRegistry::getToolsForToolbar(RegionSelector)`). Grouping is only a few
  `separatorBefore` flags.
- Overflow exists only for the Pin Window toolbar (`QmlWindowedToolbar` →
  `computeToolbarOverflow`), which removes buttons from the tail and pins
  Save/Copy/Done. The Region Selector toolbar (`QmlFloatingToolbar`) has none.
- Style changes (`onColorSelected`, `onLineWidthChanged`, …) in
  `RegionSelector`, `PinWindow`, and `ScreenCanvasSession` update only the tool
  context and saved defaults. A selected annotation is unchanged, except for text
  while it is being typed.
- `AnnotationLayer` history has two command kinds, `Add` and `Remove`. Edits of
  existing annotations (move, resize, rotate, text re-edit) are not undoable.
- Selectable annotations: text, emoji sticker, shape, arrow, polyline
  (`hitTest*`). Pencil, marker, mosaic, and step badge cannot be selected.

## Part 1 — Toolbar grouping and priority overflow

### Groups

Every toolbar button carries a `group`, defined once in `ToolRegistry` (lookup
table, no `switch`):

| Group | Buttons (Region Selector) |
| --- | --- |
| `draw` | Shape, Arrow, Pencil, Marker, Text, Mosaic, Eraser, Step Badge, Emoji |
| `assist` | Undo, Redo, OCR, QR Code, Multi-Region |
| `finish` | Pin, Save, Copy |

Pin Window and Screen Canvas use the same groups for the tools they show; their
mode-specific buttons (for example Pin's Done, Canvas whiteboard/close) are
assigned a group in the same table.

- `ToolbarViewModelBase` adds `group` to each button entry. `separatorBefore`
  is derived from a group change and is no longer set by hand per view model.
- `FloatingToolbar.qml` draws a separator plus a wider gap between groups. The
  `finish` group gets a subtle accent-tinted background from shared tokens; its
  buttons keep the existing action colors.
- Button hit targets stay 28×24; nothing is shrunk to fit.

### Priority overflow

`computeToolbarOverflow` removes buttons by **collapse priority** instead of from
the tail:

- Never collapsed: Copy, Save, Pin (and Pin Window's Done), Undo, Redo, and the
  active tool.
- Collapse order (first to go): QR Code, OCR, Multi-Region, Emoji, Step Badge,
  Eraser, Mosaic, then the remaining drawing tools from the end of the group.
- The priority lives in the same `ToolRegistry` table as the group.
- If even the pinned set does not fit, the existing "everything in More" fallback
  remains.

The Region Selector toolbar (`QmlFloatingToolbar`) uses the same function and the
same **More** menu (`ToolbarOverflowMenu.qml`). Its available width is the
available geometry of the screen that contains the selection.

### More menu

- Collapsed items show their name next to the icon.
- The menu is keyboard operable (arrows, Enter, Esc). Closing it returns focus to
  the **More** button.

### Placement

`QmlFloatingToolbar::resolveTopLeftForSelection` already tries outside-below,
then the other candidates, and clamps to the inset viewport. That logic is kept.
What changes is the width it receives: today a 17-button row can be wider than
the viewport, so clamping cannot keep it fully visible. With priority overflow
the width never exceeds the viewport. Tests cover a selection on a secondary
monitor and at each screen edge.

## Part 2 — Styles apply to the selected annotation

### Target

- **Annotation selected:** the style strip edits that annotation.
- **Nothing selected:** the style strip sets defaults for the next new object
  (unchanged behaviour, still saved through `AnnotationSettingsManager`).

### Strip behaviour

- On selection the strip shows the sections for the selected type, filled with
  the annotation's current values.
- A short label leads the strip: "Selected: Arrow" (per type) or "New object".
  It is also the strip's accessible name; the state is not shown by colour alone.
- Clearing the selection (click empty space, Esc, switch tool) returns the strip
  to the current tool's defaults. Edits made to a selected annotation never change
  the saved defaults.

### Supported types and properties

| Type | Editable |
| --- | --- |
| Text | Colour, font family, font size, bold / italic / underline |
| Arrow, Polyline | Colour, width, arrow (line-end) style, line style |
| Shape | Colour, width, fill mode, line style |
| Emoji sticker | No style sections; existing scale/rotate handles stay |

Changing a shape's type (rectangle → ellipse) is out of scope.

### Undo: `Modify` history command

- `AnnotationLayer::HistoryCommandKind` gains `Modify`. The command stores the
  annotation id plus `before` and `after` clones (`AnnotationItem::clone()`).
  Undo/redo swaps the scene item in place, keeping its id and z-order, and
  invalidates its render cache.
- New API: `AnnotationLayer::beginModify(index)` → token, `commitModify(token)`,
  `cancelModify(token)`. A modify session is coalesced: repeated changes to the
  same annotation and property (dragging the width slider, clicking through the
  palette) become one command, committed on release or after 500 ms idle, on
  selection change, or before any other history operation.
- Commands count toward the existing limits (`kMaxHistoryCommands`,
  `kMaxHistoryOwnedBytes`) and clear the redo stack like other commands.
- A modify is refused while an erase transaction holds the history lock.
- After undo/redo the annotation stays selected and the strip refreshes.

### Structure

- New `AnnotationStyleEditor` (one small class, used by all three surfaces):
  - `readStyle(const AnnotationItem&)` → the values for the strip.
  - `applyStyle(AnnotationItem&, property, value)` → bool (unsupported
    properties are ignored).
  - Owns the coalescing timer and the begin/commit calls on the layer.
- `AnnotationLayer` emits `selectionChanged()` from `setSelectedIndex` /
  `clearSelection`, so the strip follows selection without patching each of the
  ~15 call sites.
- `ShapeAnnotation` and `ArrowAnnotation` / `PolylineAnnotation` gain the colour,
  width, and line-style setters they lack.
- `PinToolOptionsViewModel` gains `targetLabel` and a "selected type" mode that
  picks sections from the selected annotation instead of the active tool.
- `RegionSelector`, `PinWindow`, and `ScreenCanvasSession` style handlers route
  to `AnnotationStyleEditor` when a selection exists, otherwise to the current
  default path.

## Part 3 — Tests and evidence

### Unit and integration tests

| Test | Covers |
| --- | --- |
| `tst_ToolbarOverflow` | Priority order (QR first), pinned set kept, group gaps in width, all-in-More fallback |
| `tst_RegionToolbarViewModel`, `tst_PinToolbarViewModel`, `tst_CanvasToolbarViewModel` | Each button has the right group; groups consistent across surfaces |
| `tst_AnnotationLayer` | `Modify` undo/redo swap, id/order kept, redo cleared by new action, history limits, refused during erase transaction |
| `tst_AnnotationStyleEditor` (new) | Read/apply per type, coalescing into one command, unsupported properties ignored |
| `tst_PinToolOptionsViewModel` | Sections and values follow selection, `targetLabel`, defaults restored after deselect |
| Region Selector, Pin Window, Screen Canvas integration | Select arrow → change colour → item changes → undo restores → redo reapplies; saved default untouched |

### Evidence

Env-gated screenshot tests (skipped unless the variable is set), as in the
recording editor:

- Normal capture toolbar; narrow selection with items in **More** and the menu
  open; arrow selected with "Selected: Arrow"; each in light and dark.
- German long translation; 2× (Retina) scale.
- One operation video: select → change style → undo → redo.
- Images under `docs/assets/images/developer/toolbar/`; a "Capture toolbar"
  acceptance section in `docs/developer/ui-design-guidelines.md` with macOS,
  Windows, and Ubuntu results recorded separately. Untested platforms are marked
  "pending manual pass".

### Docs and translations

- User docs (`docs/docs/` and `docs/zh-tw/docs/`): editing a selected
  annotation's style, the **More** menu.
- New strings ("Selected: %1", "New object", type names, group names) are run
  through lupdate and translated in all 24 languages.

## Out of scope

- Undo for move, resize, rotate, and text re-edit of existing annotations
  (separate issue, will reuse `Modify`).
- New annotation tools; making pencil/marker/mosaic/step badge selectable;
  changing shape type.
- Splitting the toolbar into separate floating pieces.

## Delivery

Branch `feature/issue-224-toolbar` (worktree `/private/tmp/snaptray-issue-224`),
one PR with `Closes #224`.
