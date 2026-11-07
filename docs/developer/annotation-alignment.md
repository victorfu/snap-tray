# Annotation alignment assistance

The General settings page contains **Annotation alignment assistance** (on by
default). While dragging the body of an unrotated rectangle, ellipse, text box,
or emoji, SnapTray can align matching edges or centers, or create equal
edge-to-edge gaps with two nearby annotations. Pink lines identify alignment;
paired lines with end ticks identify equal gaps. This is single-object placement,
not multi-selection or a batch distribution command.

Hold Alt/Option to temporarily use the raw pointer displacement. If the Linux
window manager intercepts Alt-drag, disable the preference instead. Escape
restores the original geometry; releasing commits one Undo command. The guides
are transient overlays and are not part of save, copy, or history rendering.

The feature is enabled in Region Selector and PinWindow. ScreenCanvas, drawing,
text editing, resizing, rotation, endpoints, arrows, polylines, freehand strokes,
mosaic, and step badges do not participate. Background pixels and screenshot
selection edges are not targets: capture boundary snapping (#280) is a separate
interaction, active only during selection resize.

## Geometry and coordinates

`AnnotationSnapController::layoutBounds` uses shape design geometry and the
editable text box, including scale but excluding hit-test and repaint padding.
Emoji uses its transformed design glyph rectangle, without inspecting pixels.
Rotated, hidden, invalid, and unsupported annotations are excluded.

`AnnotationSnap::Engine` is a pure geometry component. Input rectangles use
continuous QRectF min/max semantics. Enter/exit distances are 5/8 view DIPs;
targets must intersect a 300-DIP neighborhood. Spacing requires at least 50%
overlap of the smaller orthogonal extent and adjacent objects, nonnegative gaps,
and no intervening objects or collision. Matching anchors only are considered.
Equal scores prefer alignment, then stable annotation IDs. Existing valid locks
are retained within the exit radius. Both axes are revalidated together.

The shared controller stores the initial pointer and rectangle, and applies the
difference between the new total displacement and the previously applied total.
It compares only target IDs and layout bounds before rebuilding the index;
the moving annotation's layer revision does not invalidate the target index.
The index stores sorted per-axis targets; query examines adjacent pairs rather
than all pairs. Editors keep ownership of gesture commit and cancellation.

PinWindow's current annotation coordinates are already display logical units;
its coordinate mapper applies rotation/reflection, not screenshot zoom. The host
therefore uses scale 1, avoiding a second zoom conversion. The engine accepts a
scale for hosts whose canvas units differ from view DIPs. Neither host currently
clamps body drags; callers that introduce clamping must use the engine's allowed
rectangle and validate the actual applied geometry.

Emoji now retains fractional position internally. Its existing QPoint placement
accessor remains available. History geometry and JSON position arrays preserve
fractional numbers; existing integer positions remain readable. Older readers
may round fractional emoji positions.

## Verification

`Tools_AnnotationSnapEngine` covers matching anchors, horizontal/vertical gap
insertion and continuation, fractional gaps, exclusions, stable ties, hysteresis,
drag history, disable/invalidated targets, and query/controller timing with 100/500 targets.
Host tests exercise Emoji release and PinWindow zoom/source-pixmap-DPR combinations; serializer
coverage checks fractional Emoji Undo/Redo and round trips.

Native modifier handling and visual overlays on Windows/Linux still require
manual verification on those systems. Automated geometry tests do not establish
window-manager shortcut behavior.

### Local results (2026-11-07)

macOS / Qt 6.12.0 Debug: `scripts/build.sh` passed. `scripts/run-tests.sh`
built all targets and passed 239/240 CTest suites (306.74 s). The unrelated
`Qml_RecordingEditor::stableLayoutAndExport(dark)` popup-close assertion failed
once, then passed without a code change on an isolated rerun. The final rebuilt
`Tools_AnnotationSnapEngine` and `Qml_RecordingEditor` rerun passed 2/2 suites;
the snap suite passed all 24 cases. Recording-editor screenshot/video and real
mixed-monitor evidence cases remain opt-in and were skipped.

| Targets | Engine index build | Query p95 | 1,000 queries | Controller begin | 1,000 moves |
| --- | --- | --- | --- | --- | --- |
| 100 | 0.040 ms | 0.032 ms | 24.97 ms | 0.063 ms | 55.47 ms |
| 500 | 0.218 ms | 0.070 ms | 56.89 ms | 0.323 ms | 182.70 ms |

These are deterministic shape fixtures, not a claim about every real document.
The host matrix uses source pixmap DPR 1/1.25/1.5/2 and zoom 0.5/1/2;
it does not emulate physical monitors at those scale factors. Windows/Linux
native runs and hands-on three-platform drag/visual acceptance remain pending.
New settings strings currently use English fallback where untranslated.
