# Undoable Geometry and Text Re-edit for Annotations — Design

Issue: [#237](https://github.com/victorfu/snap-tray/issues/237). Builds on #224 (PR #236), which added the style-only `Modify` history command.

## Problem

Annotation history records only Add, Remove, and (since #224) style changes. Moving, rotating, or scaling an existing annotation, dragging an arrow's endpoints or curve control point, dragging a polyline vertex, and re-editing a text's content are not recorded. Ctrl/Cmd+Z skips them and undoes an older add, remove, or style change, so undo looks broken. This happens in Region Capture, Pin Window, and Screen Canvas.

## Decisions

| Question | Decision |
| --- | --- |
| Escape during a drag, rotate, or scale | Restores the annotation to where it was before the gesture, for every type; no history entry. Today Text/Shape commit the half-done state and Emoji/Arrow/Polyline leave stale drag flags. |
| Text re-edit (double-click → finish) | One undo step for the whole edit: content, font/colour changed inside the editor, and position. No change, no entry. |
| Implementation | Snapshot when the gesture starts, record on release (approach A). The three per-surface copies of the Emoji/Arrow/Polyline code are hooked where they are; merging them is a separate refactor. |
| Undo/redo while a gesture or text edit is active | Does nothing; the toolbar's Undo/Redo are disabled meanwhile. |

Out of scope: merging the triplicated Emoji/Arrow/Polyline handling; the unreachable `SelectionToolHandler`; mirroring (no interactive mirror exists — Pin only compensates orientation when an annotation is created).

## Terms

A **gesture** is one complete interaction on an existing annotation: press → drag → release, or for text re-edit, start editing → finish. However many mouse moves it contains, it becomes at most one undo step.

## 1. History model

### `AnnotationGeometry` (new, `include/annotations/AnnotationGeometry.h`, `src/annotations/AnnotationGeometry.cpp`, in `snaptray_core`)

Mirrors `AnnotationStyle`: a lookup table keyed by annotation kind, no type `switch` chains.

- `bool copy(const AnnotationItem& from, AnnotationItem& to)` copies geometry and content only. Returns false if the kinds differ or the kind has no movable geometry.
- `bool equal(const AnnotationItem& a, const AnnotationItem& b)` compares the same fields. Uses the same field table as `copy`.

| Kind | Geometry / content fields |
| --- | --- |
| Shape | rect, rotation, scaleX, scaleY |
| Arrow | start, end, control point |
| Polyline | points |
| Text | text, position, box, wrap width, rotation, scale, mirrorX, mirrorY |
| Emoji | position, scale, rotation, mirrorX, mirrorY |

Step badge, mosaic rect/stroke, pencil, and marker have no interactive geometry: `copy` returns false and no command is recorded.

Text's box is derived from font + text + wrap width, so a text geometry copy restores the box and wrap width last, and a combined style + geometry exchange applies style first.

Visibility is never copied: the text re-edit flow owns it.

### `Modify` parts

`HistoryCommand` gains `ModifyParts parts` (bit flags `Style = 1`, `Geometry = 2`).

- `modifyItem` (the #224 style path) records `Style`, as now.
- Gestures record `Geometry`; text re-edit records `Style | Geometry`.
- Undo and redo exchange only the recorded parts between the stored clone and the scene item. The scene object, its id, and its visibility stay. Style is exchanged before geometry.

Because each command touches only its parts, interleaved style and geometry steps undo independently (style → move → undo reverts only the move; undo again reverts the style), and an undo never brings back a whole-object state — the failure #236's review fixed.

### Gesture API on `AnnotationLayer`

```cpp
bool beginGesture(AnnotationId id);       // snapshot; false if locked or unknown id; commits a stale active gesture first
bool ensureGesture();                     // begins on the selected item unless one is active
bool commitGesture(ModifyParts parts);    // records one Modify if the parts changed; ends the gesture
void cancelGesture();                     // restores the snapshot's parts (Style|Geometry); ends the gesture
bool isGestureActive() const;
```

- `beginGesture` ends #224 style coalescing, and fails while the history is locked by an erase transaction. If a gesture is still active (a release that never arrived), it is committed first so undo cannot stay blocked.
- Hosts begin lazily: `ensureGesture()` runs right before the first mutation in a move handler, so a click without movement records nothing.
- `commitGesture` compares with `AnnotationGeometry::equal` (and `AnnotationStyle` for the style part). If nothing changed it records nothing. Otherwise it pushes through `pushAppliedCommand`, so the history state token advances and Pin's crop ordering (`matchesCropAnnotationBoundary`) keeps working.
- While a gesture is active, `undo()` and `redo()` return false, and `canUndo()`/`canRedo()` return false. Beginning and ending a gesture emit `changed()` so toolbars refresh.
- `clear()`, `replaceItems()`, starting an erase transaction, or removing the gesture's item drops the snapshot without recording.
- The snapshot counts toward `kMaxHistoryOwnedBytes` once recorded. `translateAll`/`translateOwnedItems` already reach command-owned clones, and they also reach the active snapshot, so Pin crop and region-layout moves never leave stale coordinates.
- Undo/redo of a geometry command on the selected id keeps the selection, as #224 does for style.

## 2. Surface wiring

### Shared handlers (all three surfaces)

- `TextAnnotationEditor` and `ShapeAnnotationEditor`: `updateDragging`/`updateTransformation` call `ensureGesture()` before the first mutation; `finishDragging`/`finishTransformation` call `commitGesture(Geometry)`.
- `TextToolHandler::handleEscape` and `ShapeToolHandler::handleEscape`: during a drag or transform, call `cancelGesture()` instead of finishing.

### Per-surface copies (Emoji, Arrow, Polyline)

Region (`RegionInputHandler`), Pin (`PinWindow`), and Canvas (`ScreenCanvasSession`) each call `ensureGesture()` in their move handlers before the first mutation and `commitGesture(Geometry)` at release. Each surface gets one `cancelAnnotationGesture()` helper. Escape calls it first: if a gesture was active it is cancelled, the drag flags are cleared, and that Escape does nothing else; otherwise Escape behaves as before.

Region's right-click during an annotation drag cancels the gesture before any other right-click behaviour.

### Text re-edit

- `TextAnnotationEditor::startReEditing` calls `beginGesture(id)` and stores the item's **id** instead of its index.
- `finishEditing` applies content, font, colour, and position as now, then calls `commitGesture(Style | Geometry)`.
- `cancelEditing` calls `cancelGesture()` and restores visibility as now.
- Pin's direct `startReEditing` calls go through the same editor methods, so they are covered.

## 3. Testing and evidence

### Unit (`tests/Annotations`)

- New `tst_AnnotationGeometry`, for each movable kind:
  - `copy` moves geometry only and leaves style alone;
  - `copy` returns false for mismatched kinds;
  - `equal` matches `copy`;
  - text's box follows the restored font.
- `tst_AnnotationLayer`:
  - commit records one step; undo and redo both work;
  - an unchanged gesture records nothing;
  - cancel restores and records nothing;
  - style → move → undo reverts only the move, and the reverse order behaves the same way;
  - undo/redo are refused while a gesture is active;
  - `beginGesture` fails during an erase transaction;
  - `clear()`, `replaceItems()` and removing the item drop the gesture;
  - after `translateAll`, undo of a geometry step lands at the translated position;
  - a commit advances the state token;
  - a re-edit step restores content, font and position, and leaves the text visible.

### Integration (real mouse/key events)

These go in the `tst_StyleSync` suites of `RegionSelector`, `PinWindow`, and `ScreenCanvas`:

- move a shape → undo → redo;
- drag an arrow endpoint, drag a polyline vertex, and rotate an emoji: one step each;
- Escape mid-drag restores the annotation and `canUndo` is unchanged;
- re-edit a text's content → undo restores the old content, and the text is visible;
- Pin: crop → move → undo reverts the move, undo again reverts the crop; and move → crop in the reverse order.

### Evidence

A gated operation video (environment variable, like `tst_ToolbarEvidence`): move → rotate → colour → undo ×3 → redo, then Escape during a drag. Attached to the PR.

### Verification

- `./scripts/run-tests.sh` on macOS: all pass.
- Windows and Ubuntu 22.04 X11 manual checks are added to #238; they are not marked as passed.

### Docs

- `docs/docs/annotation-tools.md` and `docs/zh-tw/docs/annotation-tools.md` say that moving, rotating, scaling and re-editing are undoable, and that Escape during a drag cancels it.
- No new UI strings.
