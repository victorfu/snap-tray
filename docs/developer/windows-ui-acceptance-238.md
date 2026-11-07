# Windows UI acceptance — issue #238

Tracking: [#238](https://github.com/victorfu/snap-tray/issues/238).
This report covers the **single-monitor Windows 11 session at `1bec6606`**,
based on `3d88dc42`. Automated checks and screenshot fixtures do not establish
a manual pass or a Narrator pass. It is separate from the Windows 10,
two-monitor session already recorded in `ui-design-guidelines.md`.

**Integration note:** after these checks, main advanced to `b155fdd5` and
explicitly disabled Windows HEVC export under #210. The experimental HEVC
fallback below was removed when integrating main; it is not part of the final
PR diff. The product fix retained by this PR is offscreen plugin deployment.
The screenshots and full-suite results below describe the pre-integration
revision, not a fresh acceptance pass of current main. In particular, the HEVC
choice visible in the historical export screenshot is no longer offered.

After integration, `python tests/Packaging/tst_WindowsQtDetection.py` passed
all 8 tests in 11.419 seconds, covering the final deployment change. A new
full application build/test run of the integrated revision remains pending;
the 229/233 result below belongs to the explicitly named earlier revision.

## Environment

- Session dates: 2026-11-06 and 2026-11-07 (Asia/Taipei).
- Source: branch `codex/windows-238-qa`, based on `main` at
  `3d88dc42` (includes the accessibility changes from #247).
  Initial baseline was captured at `747e15df`; the committed screenshot
  fixtures were refreshed on November 7 at `1bec6606`, before main integration.
- Windows build: `26300.9711`, display version `26H2` (registry).
- Display observed in Windows Settings: 1920 × 1200, 125% scale.
- Initial SDKs were 6.10.1 and 6.11.2. During the session the user installed
  Qt 6.12.0. The old SDK directories were removed at the user's request;
  user `QT_PATH` now selects `C:\Qt\6.12.0\msvc2022_64`.
- Selected compiler: MSVC 14.50.35717 (Visual Studio Build Tools 18).
- The user confirmed that only one display is available. Mixed-monitor DPI
  checks and a listening session with Narrator remain outside this run.

The Qt 6.12.0 Debug build succeeded. The deployed `Qt6Cored.dll` and
`Qt6Testd.dll` both report 6.12.0.0. Existing binaries predating this build were
not used as acceptance evidence.

## Test environment repair

The first full-suite attempt could not initialize the `offscreen` platform:
only `windows` was deployed beside the executable. This caused startup
failures, runtime assertion dialogs and a timeout in tests that explicitly
set `QT_QPA_PLATFORM=offscreen`; the Qt Core and Qt Test DLLs were present.

`scripts/deploy-windows-qt.cmake` now copies the selected SDK's offscreen
plugin alongside Qt Test, including when no full runtime deployment is needed.
The deployment regression test covers replacing an older plugin and restoring
a missing one. The five affected suites passed in an isolated recheck:
RedactionTrack, ZoomTrack, CursorEffects, RecordingAppearance and KeyTimeline.
The complete suite was then restarted using `scripts\run-tests.bat`.

## Deployment repair and superseded HEVC investigation

1. **Missing test platform plugin:** see the deployment repair above. This was
   a runtime search/deployment problem, not a missing `Qt6Cored.dll` or
   `Qt6Testd.dll`. Direct test invocations also need the selected Qt `bin` on
   `PATH`, as in `scripts/run-tests.bat`.
2. **Small HEVC export failure (superseded by upstream policy):** `mp4OptionsReachTheOutput` reliably
   failed exporting 160 x 120 to 80 x 60 with HEVC, with
   `Write edited media sample failed (0x80004005)`. Disabling hardware transforms
   made the same output pass. An experimental writer retried software HEVC if hardware
   configuration or the first video write fails, only before any audio/video
   sample was accepted. It kept the requested codec and exact dimensions.
   Hardware remained preferred; failures after accepted samples stayed errors.
   Experimental regression cases covered H.264 and HEVC, each with and without audio, checking
   dimensions, codec, duration, audio presence and rendered mask/cursor position.

The initial complete run passed 231/233 suites. Besides the reproducible HEVC
failure, `RecordingManager_RecordingAppearance::costAt4K` measured 253.7 ms/frame
against its 250 ms bound; an isolated repeat passed at 192.1 ms/frame. The bound
was not changed. The pre-integration full run with the experimental HEVC fix completed in 880.33 seconds:
**229/233 suites passed**. `Qml_RecordingPreviewExport` and
`RecordingManager_RecordingAppearance` both passed. The four failures were:

| Suite | Failure from `ctest --output-on-failure` |
| --- | --- |
| `RecordingManager_RecordingSetup` | `quickSaveActionsDoWhatTheySay`: clipboard text empty, expected saved MP4 path |
| `App_MainApplicationTrayMenu` | 60-second timeout; `cliClipboardPinPosition` created 0 pins, expected 1 |
| `PinWindow_Transform` | `testInfoCopyTracksCurrentTransform`: clipboard text empty, expected transformed dimensions |
| `RegionSelector_StyleSync` | `testDisabledMagnifierIgnoresShiftAndCopyShortcuts` and `testBeaverStyleIgnoresShiftAndCopyShortcuts`: clipboard text empty, expected `keep-me` |

All four emitted `Unable to obtain clipboard` / `OpenClipboard Failed`
(`0x800401d0`), and all four failed again in an 81.78-second isolated rerun.
`LogonUI` was present in the same Windows session, and the requested SnapTray
Settings window was not available in the UI inventory. This is consistent with
an inaccessible/locked desktop; it does not establish a product clipboard bug.
The user was asked to unlock Windows. **A clean full-suite pass is still pending.**
Do not disable these assertions or replace the Windows clipboard with an
in-memory platform to claim acceptance.

An independent pre-integration `mp4OptionsReachTheOutput` run passed all four data rows
(H.264/HEVC, silent/audio): 6 passes including setup/cleanup, no failures/skips,
4.229 seconds. Both HEVC rows logged the hardware failure and successful
software retry. Logs remain in `build/qa238/` (`final-tests.log`,
`failed-recheck.log`, `hevc-regression-final.txt`).

## Screenshot fixtures (not a native input pass)

The existing `Qml_RecordingEditor::designEvidence`, `Qml_ToolbarEvidence`, and
`PinWindow_Evidence::screenshots` fixtures all passed at `1bec6606`, before main integration.
These render the actual components over controlled test content. They do not
prove native focus, screen-reader speech, physical monitor transitions or the
complete interactive scenarios below.

The display remained at 125%. `QT_SCALE_FACTOR=0.8/1.2/1.6` exercised effective
100/150/200% rendering; these are process overrides, not changes to Windows
Display Settings. Narrow editor captures measured 640 x 480, 960 x 720 and
1280 x 960 physical pixels. Requested wide 1200 x 800 logical windows measured
1200 x 800, 1800 x 1175 and 1924 x 1176 physical pixels, respectively: Windows
clamped the higher-scale wide windows to the available display. They are not
proof of a full 1200 x 800 logical window at all scales.

Visual inspection found legible primary controls in the examples below,
including the four long translations. Narrow inspector content requires
scrolling; screenshot inspection alone does not verify reachability. The narrow
toolbar's style strip extends past the fixture edge, matching the documented
shared narrow-strip limitation in `ui-design-guidelines.md`.

| Fixture | Committed evidence |
| --- | --- |
| Editor 100%, light, wide/narrow | [Wide](../assets/images/developer/windows-238/editor-100-wide-light.png), [narrow inspector](../assets/images/developer/windows-238/editor-100-narrow-light.png) |
| Editor 150%, dark export | [Export](../assets/images/developer/windows-238/editor-150-export-dark.png) |
| Editor 200%, dark, narrow | [Inspector](../assets/images/developer/windows-238/editor-200-narrow-dark.png) |
| Native 125%, German/Finnish/Russian/Greek; 800 x 600 pixels | [de](../assets/images/developer/windows-238/editor-de-narrow.png), [fi](../assets/images/developer/windows-238/editor-fi-narrow.png), [ru](../assets/images/developer/windows-238/editor-ru-narrow.png), [el](../assets/images/developer/windows-238/editor-el-narrow.png) |
| Shared toolbar, selected arrow / narrow More | [Selected](../assets/images/developer/windows-238/toolbar-selected-dark.png), [More](../assets/images/developer/windows-238/toolbar-more-light.png) |
| Pin zoom panel / compact bar | [Zoom](../assets/images/developer/windows-238/pin-zoom-dark.png), [compact](../assets/images/developer/windows-238/pin-compact-light.png) |

## Manual checklist

Each row requires its own result, actual window size and scale, and screenshot
or video reference. “Not tested” is not a pass. Mixed-monitor checks require a
second display; a process scale override is not physical mixed-DPI evidence.

| Area | Check | Result / evidence |
| --- | --- | --- |
| Editor | New recording → trim → split/2× → canvas → text/mask/zoom → GIF and MP4 | Not tested (desktop access blocked) |
| Editor | Retain project → export → library reopen → delete mask → undo/redo → export → reopen | Not tested (desktop access blocked) |
| Editor | Close during export: continue, cancel, edits retained | Not tested (desktop access blocked) |
| Editor | 1200 × 800 and 640 × 480; reachable controls, toggleable inspector | Render fixtures passed (actual sizes above); native reachability not tested |
| Editor | Light and dark | Render fixtures visually checked; native input not tested |
| Editor | German, Finnish, Russian, Greek | Render fixtures visually checked; native input not tested |
| Editor | 100%, 150%, 200%, mixed-DPI monitor move | Effective-scale render fixtures passed; physical display stayed at 125%; mixed-monitor not tested (one monitor) |
| Editor | Tab/focus; Space, S, Delete, Ctrl+Z, Ctrl+Shift+Z, Escape; text-input isolation | Not tested (desktop access blocked) |
| Toolbars | Region, pin and canvas: three groups and finish background | Shared toolbar render checked; native three-surface pass not tested |
| Toolbars | Narrow overflow; Up/Down/Enter/Escape; focus returns to More | Not tested (desktop access blocked) |
| Toolbars | Selected arrow/text/shape: label, restyle, style-only undo/redo | Not tested (desktop access blocked) |
| Toolbars | Tool/background clears selection and restores New object | Not tested (desktop access blocked) |
| Toolbars | Canvas moved to narrower monitor; record actual overflow behavior | Not tested (desktop access blocked) |
| Geometry, all three surfaces | Move/resize/rotate; one undo per drag; redo | Not tested (desktop access blocked) |
| Geometry, all three surfaces | Escape during drag restores object, release does not move it, no undo entry | Not tested (desktop access blocked) |
| Geometry, all three surfaces | Text re-edit is one undo; Escape restores original text | Not tested (desktop access blocked) |
| Geometry, all three surfaces | Style changed during drag: one undo restores geometry and style | Not tested (desktop access blocked) |
| Geometry, all three surfaces | Crop then undo/redo preserves annotation positions | Not tested (desktop access blocked) |
| Geometry, all three surfaces | 150%, 200%, mixed-DPI drag/undo | Not tested (desktop access blocked) |
| Pins | Hover show/hide delay; entering controls keeps them visible | Not tested (desktop access blocked) |
| Pins | Zoom slider, stable panel, 33/50/100/200 presets, reset; opacity 10–100 and reset | Not tested (desktop access blocked) |
| Pins | Escape/other key/outside closes panel; no shortcut leaks to image | Not tested (desktop access blocked) |
| Pins | Ctrl+=, Ctrl+-, Ctrl+0, [, ], Ctrl+] | Not tested (desktop access blocked) |
| Pins | Zoom/opacity badge for wheel/edge/menu/panel/shortcut; live badge location | Not tested (desktop access blocked) |
| Pins | Compact/tiny/short-wide sizes, rotation and flip | Not tested (desktop access blocked) |
| Pins | Suppression while dragging/resizing/annotating/cropping/layout/click-through; hover setting off | Not tested (desktop access blocked) |
| Pins | View/Output/Modes/Pins/Close menu groups; opacity submenu/reset | Partial native check at 125%: opened imported-image context menu and closed the test pin; full grouping/reset flow not tested |
| Pins | Click-through exit pill and tray exit-all action | Not tested (desktop access blocked) |
| Pins | Live Update excludes overlapping controls/panels/tooltips; red-dot tooltip; pause/resume | Not tested (desktop access blocked) |
| Pins | Controls and panels remain with pin after switching applications | Not tested (desktop access blocked) |
| Pins | Annotate and More hover tooltips | Not tested (desktop access blocked) |
| Pins | Light/dark/German; 100%, 150%, 200% | Not tested (desktop access blocked) |
| Narrator | Editor inspector/timeline/export, toolbars/More, pin controls/click-through names and states | Not tested; accessibility-tree checks alone are insufficient |

## Resume

Unlock the interactive Windows session first. Paint access approval also timed
out during preparation of a neutral recording background; no Paint input or
new recording was completed. Temporary recording-hotkey and keep-projects
settings were restored to their pre-test defaults; the QA app was stopped.
The native editor, toolbar and pin flows remain
open acceptance work, not failures inferred from render fixtures.


1. Select a Qt 6.12.0 MSVC SDK with `QT_PATH`. Rebuild the integrated main
   revision; the pre-integration binaries and screenshots above are historical.
2. Run `scripts\build.bat`, then `scripts\run-tests.bat` sequentially.
3. Launch the newly built debug app and execute the rows above, using the
   reference videos linked in `ui-design-guidelines.md` and #238.
4. Record failures with reproduction steps and evidence, link follow-up issues,
   and update only the corresponding Windows acceptance cells.
