# Longshot independent sections and image edits

Implemented scope: the two requested follow-ups, retaining every reliable section and repairing the generated image. Baseline began at `0665e278`; the concurrent C++17 capture compatibility commit `9b080883` was preserved.

## Behavior

- Solver keeps the existing largest-island position projection for closure searching, and separately preserves every component supported by at least two connected frames. Components use independent origins and are ordered by their first recorded frame. Singletons remain reported as unjoined footage.
- Session renders each reliable section separately. Section identity and height splitting are different metadata; the preview labels both, and PNG names use `-s01` / `-s01-p001`. No join between sections is guessed. The existing 512 MiB output-pixel budget is shared across sections, and cancellation never presents a partial render as a successful run.
- Each selected image part supports keeping a horizontal range or deleting a horizontal band, with 32 undo/redo states and an always-available original for Reset. History stores row intervals rather than copies of every intermediate image. Removing the entire image is rejected.
- Warnings are mapped through retained row intervals, including when the original start of a warning was deleted. Source timestamps are taken from surviving observations. Other parts remain unchanged.
- PNG / Copy / Pin use edited pixels. Saving a changed part publishes a new unique file; prior exports remain intact, and an unchanged retry does not duplicate history. Annotate in Pin opens the edited part with the existing toolbar visible.
- Back plus View Result retains image edits. Source crop/trim changes or regeneration reset image edits; the UI explains this boundary.
- All 24 translation catalogs and both English/Traditional Chinese recording guides were updated.

## Verification

- Canonical macOS build passed (Qt 6.11.2).
- Final focused run: all six suites passed: Longshot_ImageEdit, Longshot_Sections, Qml_LongshotController, Qml_RecordingCropOverlay, Cursor_CursorQmlGuard and App_MainApplicationTrayMenu.
- Native QML tests exercised drag selection, keep/delete, undo, edited-pixel annotation requests, Back/View Result retention and minimum 640×480 layout. The rendered result was visually inspected.
- QML lint exited 0 with warnings; `python3 scripts/check-longshot-translations.py` passed all 24 catalogs.
- Full canonical test run executed 185 suites. Three initially failed: two native pauses cases below and the new raw crosshair cursor binding. The cursor binding was changed to CursorTokens.captureSelection and its suite passed in the final focused run. The two pauses failures remain; the full run is not claimed green.

## Existing native pause-matching gap

Longshot_Pipeline trajectories(pauses) places 45/64 frames; Longshot_Renderer endToEnd(pauses) produces 1785 rows instead of 2235. The same pipeline failure reproduced using the pre-change PositionSolver implementation substituted into the current test binary. The analyzer and pipeline matching logic were not changed by this work, and no acceptance thresholds were relaxed. The solver comparison preserves the current interface layout but removes this change's section-retention logic; it is a behavioral isolation check, not a claim that every baseline target was rebuilt.

Evidence logs for this session: `/tmp/sections-edits-final-tests.log`, `/tmp/sections-pauses-baseline.log`, `/tmp/sections-edits-verified.log`, `/tmp/sections-edits-ui-final.log`, `/tmp/sections-edits-last-lint.log`. The native pause issue is left separate from these two requested feature additions.

Engine milestone: `88e329a1`. The final image-selection regression also verifies that clicks in the blank area below a shortened image do not select or delete its last row.
