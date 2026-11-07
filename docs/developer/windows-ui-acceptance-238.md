# Windows UI acceptance — issue #238

Tracking: [#238](https://github.com/victorfu/snap-tray/issues/238).
This is the single-monitor Windows 11 run, separate from the Windows 10 /
two-monitor results in `ui-design-guidelines.md`. QTest-driven operations,
render fixtures, native desktop input and Narrator speech are distinct evidence.

## Current revision and environment

- Rebased on the latest fetched main: `b155fdd5`; fetched again after testing,
  with no newer main commit.
- Full build/test revision: `608c5ee1`; the subsequent test-only selector
  lifetime guard is `d582f7b0`. The overlay-selection fix is `ef8989ea`,
  rebuilt with `scripts/build.bat` and validated with focused suites.
- Session date: November 7, 2026, Asia/Taipei; earlier preparation November 6.
- Windows 11 build `26300.9711` / `26H2`; one 1920 x 1200 display, observed at
  125%. Mixed-monitor DPI is unavailable on this host.
- Qt 6.12.0, `C:\Qt\6.12.0\msvc2022_64`; MSVC 14.50.35717.
  Deployed Qt Core, Qt Test and offscreen plugin all report 6.12.0.0.
- `QT_PATH` selects 6.12.0. Old 6.10.1/6.11.2 SDK directories were removed
  at the user's request during preparation.

**Desktop input is blocked:** an actual screenshot showed the Windows lock
screen at 18:02, 18:34 and 18:50. Activating Paint for the neutral recording
background failed. No native mouse/keyboard pass is claimed. The user was
asked to unlock Windows; there was no unlocked desktop available during this
run. The previous QA app was stopped and temporary settings restored.

## Build and regression results

`scripts\run-tests.bat` rebuilt the latest main successfully, deployed matching
Qt DLLs/plugins, and ran all **244 suites**. **239 passed**, in 1096.44 seconds:

```text
The following tests FAILED:
 33 - RecordingManager_RecordingSetup (Failed)
 54 - RecordingManager_RecordingAppearance (Failed)
 75 - App_MainApplicationTrayMenu (Exit code 0xc0000409)
 80 - PinWindow_Transform (Failed)
130 - RegionSelector_StyleSync (Failed)
```

| Suite | Observed failure and follow-up |
| --- | --- |
| RecordingSetup | `quickSaveActionsDoWhatTheySay`: clipboard text empty instead of saved MP4 path; `OpenClipboard Failed` / `0x800401d0` |
| RecordingAppearance | 4K blurred background 268.3 ms/frame against 250 ms; isolated recheck **passed**, 194.2 ms/frame (gradient 61.1), suite 3.74 s. Threshold unchanged |
| MainApplicationTrayMenu | Selector disappeared during `qWait(50)`; `QTest::keyClick(nullptr)` aborted the suite. Added a post-wait precondition. Focused rerun now reports an ordinary selector-lifetime failure and continues; subsequent clipboard cases fail and the suite times out at 60 s |
| PinWindow_Transform | `testInfoCopyTracksCurrentTransform`: clipboard text empty instead of dimensions; clipboard access unavailable |
| RegionSelector_StyleSync | Disabled-magnifier and Beaver-style clipboard-isolation checks read empty text instead of `keep-me`; clipboard access unavailable |

The isolated performance pass is **not** a clean full-suite pass. Four suites
still require an unlocked-desktop rerun. The guarded test continues reporting
a failure; it does not skip or pass the missing-window condition. No assertions
or performance limits were relaxed.

The preview export, editor, crop, geometry/AnnotationLayer, pin hover integration,
Media Foundation and other completed suites passed. Logs are retained locally:
`build/qa238/latest-main-tests.log`, `latest-appearance-recheck.log`,
`latest-guard-build.log`, and `latest-tray-recheck.log`.

## Repairs included in this PR

1. **Deploy the offscreen plugin.** windeployqt installed only the application's
   windows platform plugin. Tests selecting `QT_QPA_PLATFORM=offscreen` failed
   to start even though `Qt6Cored.dll` and `Qt6Testd.dll` were present. The helper
   now copies the selected SDK's matching offscreen plugin alongside Qt Test,
   even when no full runtime deployment is needed. Regression coverage replaces
   an old plugin and restores a missing one; packaging tests pass.
2. **Avoid a fatal test assertion after event processing.** Recheck the selector
   QPointer after the intentional event wait, before sending the Return key.
   A disappearing selector is now a diagnostic failure instead of aborting all
   remaining tests in that process.

3. **Reveal text/image overlays after timeline selection.** On a 5 fps recording,
   an overlay beginning at 600 ms has a 300 ms fade-in. The old timeline click
   sought 700 ms, which displayed the 600 ms frame: the selection rectangle
   appeared but the overlay had zero opacity. Timeline activation now uses the
   existing `showOverlay` helper, also used when adding text, to seek past the
   fade-in. The regression `timelineSelectionRevealsFadedOverlay` checks opacity
   at the actual displayed frame. It failed before the change (0 instead of 1)
   and passed afterward (shown frame at 1000 ms, full opacity).

After this product fix, the standard build succeeded and the complete affected
suites passed **4/4** in 139.00 seconds: RecordingEditor, RecordingPreviewExport,
RecordingPreviewTimeline and RecordingPreviewMasks. The independent new
regression passed with 3 total passes (setup/test/cleanup), no skips. This
focused validation is separate from the earlier 239/244 full-suite baseline;
no second full run is claimed. Logs: `overlay-before.txt`, `overlay-after.txt`,
`overlay-fix-build.log`, `overlay-focused-tests.log` under `build/qa238/`.

An earlier HEVC experiment was discarded: current main intentionally disables
Windows HEVC under #210 and exports H.264. This PR preserves that policy and
contains no Media Foundation encoder change.

## Current rendered and scripted evidence

The committed screenshots were regenerated from the new build after rebase.
The existing editor `designEvidence` fixtures passed in dark/light at effective
100/150/200% and in German, Finnish, Russian and Greek at native 125%.
Toolbar and pin screenshot fixtures passed. The screenshot-only toolbar run
skips its two opt-in operation cases; those are invoked separately below.

The display stayed at 125%; `QT_SCALE_FACTOR=0.8/1.2/1.6` exercised effective
100/150/200% rendering, not physical display changes. Narrow captures are
640 x 480, 960 x 720 and 1280 x 960 physical pixels. Requested wide 1200 x 800
logical windows are clamped at higher scales: captures are 1200 x 800,
1800 x 1175 and 1924 x 1176 pixels. Do not claim 1200 x 800 logical at every scale.

Representative primary controls remain legible, including long translations.
Narrow inspector content needs scrolling; screenshots alone do not establish
native reachability. The narrow style strip extends past the fixture edge,
matching the documented shared limitation in `ui-design-guidelines.md`.

| Fixture | Committed evidence |
| --- | --- |
| Editor 100%, light, wide/narrow | [Wide](../assets/images/developer/windows-238/editor-100-wide-light.png), [narrow](../assets/images/developer/windows-238/editor-100-narrow-light.png) |
| Editor 150%, dark export | [Export](../assets/images/developer/windows-238/editor-150-export-dark.png) |
| Editor 200%, dark, narrow | [Inspector](../assets/images/developer/windows-238/editor-200-narrow-dark.png) |
| Native 125%, German/Finnish/Russian/Greek; 800 x 600 pixels | [de](../assets/images/developer/windows-238/editor-de-narrow.png), [fi](../assets/images/developer/windows-238/editor-fi-narrow.png), [ru](../assets/images/developer/windows-238/editor-ru-narrow.png), [el](../assets/images/developer/windows-238/editor-el-narrow.png) |
| Shared toolbar, selected arrow / narrow More | [Selected](../assets/images/developer/windows-238/toolbar-selected-dark.png), [More](../assets/images/developer/windows-238/toolbar-more-light.png) |
| Pin zoom panel / compact bar | [Zoom](../assets/images/developer/windows-238/pin-zoom-dark.png), [compact](../assets/images/developer/windows-238/pin-compact-light.png) |

### Scripted operation results

All opt-in operation runs completed without failures or skips:

| Run | Result |
| --- | --- |
| Editor `operationVideo` (selection, timeline, flow) + `operationVideoReopen` | 6 passes including setup/cleanup; 153.378 s, rerun after overlay fix |
| Editor `designEvidenceStates` (dark/light export and project states) | 4 passes including setup/cleanup; 15.609 s |
| Toolbar `operationVideo` + `geometryUndoVideo` | 4 passes including setup/cleanup; 38.309 s |
| Pin `operationVideo` | 3 passes including setup/cleanup; 20.204 s |

These use generated native-encoded test media and QTest events, not a fresh
DXGI desktop recording or OS-level keyboard/mouse automation. The editor flow
performed trim/split/speed, canvas, text/mask/zoom and actual GIF/MP4 exports.
The reopen flow exported, reopened the project, deleted a mask, undid/redid,
exported again and verified the next reopen. File-dialog completion/project
opening are controlled by the fixture, not native dialog/Library clicks.

- [Completed flow](../assets/images/developer/windows-238/editor-flow-complete.png)
- [Completed reopen](../assets/images/developer/windows-238/editor-reopen-complete.png)
- Actual exports: [GIF](../assets/images/developer/windows-238/editor-flow-export.gif), [MP4](../assets/images/developer/windows-238/editor-flow-export.mp4)

Full frame sequences and logs remain in `build/qa238/latest-fixtures/`; the
runner summary is `build/qa238/latest-evidence.log`. The final editor rerun
after the overlay fix is in `build/qa238/overlay-fixed-operations/` and
`overlay-fixed-operations.txt`. The committed flow/reopen images and GIF/MP4
exports come from that final rerun. Visual inspection confirms the selected
text is now visible in the reopened project.

## Manual checklist

Each row requires its own result, actual window size and scale, and screenshot
or video reference. “Not tested” is not a pass. Mixed-monitor checks require a
second display; a process scale override is not physical mixed-DPI evidence.

| Area | Check | Result / evidence |
| --- | --- | --- |
| Editor | New recording → trim → split/2× → canvas → text/mask/zoom → GIF and MP4 | QTest flow and real exports passed on generated media; fresh desktop recording/native handoff not tested |
| Editor | Retain project → export → library reopen → delete mask → undo/redo → export → reopen | QTest project reopen flow passed; native Library navigation not tested |
| Editor | Close during export: continue, cancel, edits retained | Automated preview suite and state fixtures passed; native close interaction not tested |
| Editor | 1200 × 800 and 640 × 480; reachable controls, toggleable inspector | Render fixtures passed (actual sizes above); native reachability not tested |
| Editor | Light and dark | Render fixtures visually checked; native input not tested |
| Editor | German, Finnish, Russian, Greek | Render fixtures visually checked; native input not tested |
| Editor | 100%, 150%, 200%, mixed-DPI monitor move | Effective-scale render fixtures passed; physical display stayed at 125%; mixed-monitor not tested (one monitor) |
| Editor | Tab/focus; Space, S, Delete, Ctrl+Z, Ctrl+Shift+Z, Escape; text-input isolation | Automated editor suite/operation scenarios passed; physical Tab/focus walkthrough not tested |
| Toolbars | Region, pin and canvas: three groups and finish background | Shared toolbar render checked; native three-surface pass not tested |
| Toolbars | Narrow overflow; Up/Down/Enter/Escape; focus returns to More | Not tested (desktop access blocked) |
| Toolbars | Selected arrow/text/shape: label, restyle, style-only undo/redo | Scripted toolbar restyle/Undo flow passed; all three native surfaces not tested |
| Toolbars | Tool/background clears selection and restores New object | Not tested (desktop access blocked) |
| Toolbars | Canvas moved to narrower monitor; record actual overflow behavior | Not tested (desktop access blocked) |
| Geometry, all three surfaces | Move/resize/rotate; one undo per drag; redo | AnnotationLayer and scripted geometry Undo passed; native three-surface walkthrough not tested |
| Geometry, all three surfaces | Escape during drag restores object, release does not move it, no undo entry | Not tested (desktop access blocked) |
| Geometry, all three surfaces | Text re-edit is one undo; Escape restores original text | Not tested (desktop access blocked) |
| Geometry, all three surfaces | Style changed during drag: one undo restores geometry and style | Not tested (desktop access blocked) |
| Geometry, all three surfaces | Crop then undo/redo preserves annotation positions | Not tested (desktop access blocked) |
| Geometry, all three surfaces | 150%, 200%, mixed-DPI drag/undo | Not tested (desktop access blocked) |
| Pins | Hover show/hide delay; entering controls keeps them visible | Not tested (desktop access blocked) |
| Pins | Zoom slider, stable panel, 33/50/100/200 presets, reset; opacity 10–100 and reset | Scripted pin operation passed; native slider/preset walkthrough not tested |
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

## Remaining work

Unlock Windows before the clipboard and native-input rechecks. Use this
rebased build for the remaining checklist. Physical mixed-monitor checks need
a second display; Narrator speech requires a listening session. Neither is
replaced by rendering overrides or accessibility assertions.
