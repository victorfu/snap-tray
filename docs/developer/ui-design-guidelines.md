---
last_modified_at: 2026-11-06
layout: docs
title: UI Design Guidelines
description: Shared visual direction and interaction rules for SnapTray capture tools, editing windows, and libraries.
permalink: /developer/ui-design-guidelines/
lang: en
route_key: developer_ui_design
nav_data: developer_nav
docs_copy_key: developer
---

## Direction and status

The agreed direction is **lightweight capture, focused editing, and one visual language**. SnapTray should feel quiet, precise, and fast. Floating desktop tools remain compact; editing and management windows use stable layouts and predominantly solid surfaces.

This document records the design direction accepted on November 4, 2026. It is a target for incremental implementation, not a description of features already shipped. The generated concept board is illustrative: its labels, sample content, decorative details, and controls do not define backend capabilities. Existing behavior remains authoritative until a scoped change is implemented and verified.

The recording editor rollout is tracked in [#214](https://github.com/victorfu/snap-tray/issues/214). Library consolidation and other product-wide improvements require their own implementation scope.

## Surface families

| Surface | Treatment | Layout priority |
| --- | --- | --- |
| Capture and pin toolbars | Restrained translucent surface through shared glass helpers, with reliable contrast over arbitrary content | Stay near the task without covering the active edit; keep common actions reachable |
| Editor, library, and settings | Solid neutral surfaces, subtle separators, limited elevation | Stable navigation, large content area, predictable settings location |
| Menus and dialogs | Shared controls and platform-appropriate window behavior | Clear purpose, concise choices, predictable dismissal and focus restoration |

Light and dark appearances should express the same hierarchy. Do not make editing windows translucent merely to match floating tools. Respect platform keyboard and window conventions while preserving shared product terminology and components.

## Visual language

- Use neutral backgrounds and strong text contrast. Reserve the existing violet accent for the main action, selection, and focus; avoid filling every active control with violet.
- Use spacing, typography, and grouping before adding boxes around controls. Keep borders and shadows subtle, and corner radii consistent with shared tokens.
- Establish clear title, section, label, value, and supporting-text levels. Supporting text must remain readable in both appearances.
- Keep icon size, stroke weight, and alignment consistent with the existing icon system. Provide visible labels for important completion actions; tooltips supplement labels and expose shortcuts.
- Floating tools may be denser than settings windows. Verify their actual hit areas, spacing, overflow, and high-DPI behavior rather than shrinking controls to fit more tools.
- Use short, purposeful motion for state changes. Panel switching must not reset content, steal focus, or introduce unnecessary layout movement.
- Implement through `SemanticTokens`, `ComponentTokens`, existing shared controls, and `GlassRenderer` / toolbar style helpers. Change shared definitions deliberately instead of introducing local visual systems.

## Capture and annotation

Separate drawing tools, contextual style controls, and completion actions. Keep Copy, Save, and Pin in a predictable action group; do not give every action primary emphasis. Less-used tools may use a labelled group or overflow menu.

Show tool-specific options in a stable location. Make it clear whether a change applies to selected annotations or to the next annotation. Preserve selection, input, and undo behavior when switching tools. Selecting an existing object should make its relevant editing controls discoverable.

Toolbars must avoid covering the active region or selected object where practical, remain inside the available screen, and provide usable overflow. Pin controls should expose zoom, opacity, annotation, and additional actions when needed while leaving the image prominent at rest. Special modes such as click-through and Live Update need recognizable status and a discoverable exit.

## Library direction

Use a common browsing language for screenshots and recordings: navigation, search, sorting, thumbnails, selection, preview, and contextual actions. A proposed shared entry point can distinguish All captures, Screenshots, and Recordings without requiring their storage models to merge.

Distinguish editable projects from exported media with understandable labels. Clarify what opening, saving, or deleting an item affects. Avoid placing a full row of action buttons on every thumbnail. Search, favorites, renaming, and batch operations are candidate work, not assumed existing capabilities.

## Recording editor

Use a top bar for project identity and the primary Export action, a large central preview, a stable right inspector, playback controls below the preview, and a bottom timeline. Increase preview space before adding decorative whitespace.

- Without an object selection, expose project-wide canvas, pointer, audio, and key-hint settings.
- Selecting a clip or effect shows its type, time range, and relevant properties in the inspector. Provide an explicit route back to project settings.
- Keep export format, dimensions, frame rate, quality, codec, and estimated size in the export panel.
- Separate preview playback speed from exported clip speed in naming and placement.
- Keep split, delete, and effect creation near the timeline. Distinguish video and effect lanes without relying on color alone.
- At narrow widths, use a collapsible inspector rather than wrapping a long mixed toolbar into the preview area. Determine breakpoints through real-window testing.

Implementation ownership:

| Issue | Responsibility |
| --- | --- |
| [#215](https://github.com/victorfu/snap-tray/issues/215) | Window structure, responsive behavior, and component boundaries |
| [#216](https://github.com/victorfu/snap-tray/issues/216) | Export panel and protected export entry points |
| [#217](https://github.com/victorfu/snap-tray/issues/217) | Project-wide settings and appropriate controls |
| [#218](https://github.com/victorfu/snap-tray/issues/218) | Selection lifecycle and contextual inspector |
| [#219](https://github.com/victorfu/snap-tray/issues/219) | Timeline lanes and direct segment manipulation |
| [#220](https://github.com/victorfu/snap-tray/issues/220) | Integrated visual, keyboard, and platform verification |

## Controls and feedback

| Intent | Preferred control or behavior |
| --- | --- |
| Enable a setting | Labelled switch with an unambiguous state |
| Choose among a few values | Segmented selector with visible alternatives |
| Choose among many values | Labelled dropdown |
| Adjust a numeric value | Numeric input and, where appropriate, a slider using the supported range and steps |
| Choose a visual style | Preview swatches or thumbnails with accessible names |
| Complete an operation | Clear action label and feedback identifying the result |

Avoid cycling through hidden choices by repeatedly clicking a button. Do not imply continuous precision where the backend supports only discrete presets. Explain unavailable capabilities at the point of use, distinguishing missing source data from platform or format limitations.

Copy, Save, Export, Apply, and Close must describe distinct outcomes. Successful file operations should make the destination discoverable. Errors should retain work and offer a practical next step. Destructive actions should state what is removed; use undo where it is actually supported, and confirmation where needed.

## Interaction quality and verification

Every component must implement appropriate hover, pressed, selected, keyboard-focus, disabled, busy, and error states. Focus must be visible, move predictably, and return sensibly after a menu or dialog closes. Text editing must not trigger playback or drawing shortcuts.

Before completing a UI change:

- Verify ordinary and minimum supported window sizes, long translated text, light/dark appearance, and high-DPI layouts.
- Exercise relevant multi-screen placement and screen-edge behavior for floating surfaces.
- Verify keyboard-only paths, accessible names, and state cues beyond color.
- Preserve pending-input validation, undo/redo, project reopening, cancellation, and close guards where applicable.
- Run the repository build/test scripts for substantive implementation changes, with focused regression coverage for changed behavior.
- Attach actual screenshots or interaction recordings. Identify the platform and any untested capabilities; a concept image is not validation evidence.

Keep the user documentation aligned with actual platform capabilities and the delivered workflow.

## Rollout boundaries

The first implementation introduces neutral editor surface tokens, a stable editor shell, a scrollable inspector, a separate export popup, explicit choices, mutually exclusive effect selection, named effect lanes, and release-to-commit range dragging. Shared dialog buttons gain keyboard activation and visible focus. This editor change does not claim to redesign other surfaces. The capture/annotation toolbars follow [Capture toolbar](#capture-toolbar) (#224), pins [Pin window](#pin-window) (#225), the Library [Library](#library) (#226), and settings and Beautify [Settings and Beautify](#settings-and-beautify) (#227).

The remaining product work is tracked by [#221](https://github.com/victorfu/snap-tray/issues/221): capture/annotation action grouping and overflow; pin mode visibility and reset controls; library browsing and project/output distinction; settings information architecture; and shared Beautify presets. Follow-up implementation is tracked in [#224](https://github.com/victorfu/snap-tray/issues/224) (capture/annotation), [#225](https://github.com/victorfu/snap-tray/issues/225) (pin windows), [#226](https://github.com/victorfu/snap-tray/issues/226) (library), and [#227](https://github.com/victorfu/snap-tray/issues/227) (settings/Beautify). Search, favorites, renaming, and batch actions remain candidates.

### Editor interaction contract

- The same player survives category changes and opening export. At widths below 980 logical pixels, the inspector is a sheet over the preview only, opened from the top bar and closed with ✕ or a click outside; playback controls and the timeline remain uncovered.
- The timeline keeps a fixed height (a ruler, the video lane and a fixed number of visible effect lanes); adding effects never resizes the preview, and further lanes scroll. A selected effect is scrolled into view.
- Two to four alternatives use a full-width segmented selector; longer lists use a drop-down with a single chevron (#230).
- Effect selection is exclusive, and the picture, the timeline and the inspector show the same object: clicking a text, image or mask on the picture selects it like its timeline block (#218). Switching commits valid time input; invalid input keeps its object selected and exposes correction or Escape-to-revert. Escape clears the selection or closes the open category and returns to Project settings; in a narrow window it also hides the inspector before asking to close the recording. Rebuilding segments (Auto Zoom) never leaves an index on a different object.
- A category that the format or the recording cannot use stays listed with its real reason (for example "GIF and WebP have no sound" for Audio). The reason shows only for that cause; a whole editor disabled during an export keeps the summaries.
- Effects use source milliseconds, not concatenated output time. Clip cuts and speeds retain the existing backend mapping. Drag drafts mutate the backend only on release, as one undo step; Escape drops an effect, split or trim-handle draft, and a lost grab restores the trim. Split markers are keyboard reachable (arrows move, Delete joins). Once the edit trims, cuts or changes speed, the playback bar also shows the exported time and length, or "Not exported" in a cut part; an effect lying only in cut parts is crossed out. Exact range fields remain available for short segments. Clicking or dragging the video track only seeks and preserves the active effect/crop edit; double-clicking explicitly selects a clip. The video track stays fixed while effect rows scroll.
- Export entry points open the same panel. The final Export action validates pending ranges, custom width, trim, and crop drafts before calling the existing guarded backend. Export cancellation, errors, project retention, and fast-path encoding remain backend responsibilities.

### Acceptance matrix

Record actual environment, results, and evidence for each implementation; a blank or pending cell is not a pass.

| Check | macOS | Windows | Ubuntu 22.04 X11 beta |
| --- | --- | --- | --- |
| Build and repository tests | `scripts/run-tests.sh`, all suites (#214 follow-up PR) | CI build and tests only | CI build and tests only |
| Wide/narrow, light/dark, selection/export | QML screenshot fixtures (below) | Pending manual pass | Pending manual pass |
| Pending input, drag/undo, close/cancel | Automated preview suites; operation videos; exporting, close-while-exporting and cancelled screenshots (below) | CI tests only | CI tests only |
| New recording end-to-end (trim → split/speed → canvas → text/mask/zoom → export) | Scripted real-input session, `editor-220-flow` | Pending manual pass | Pending manual pass |
| Reopened project (reopen → change → undo/redo → save → reopen) | Scripted real-input session, `editor-220-reopen`; `tst_RecordingPreviewExport::projectsKeepTheRecordingAndItsEdits`, `tst_RecordingEditor::redoAndReopenKeepSelectionHonest` | CI tests only | CI tests only |
| Long translations | de, fi, ru, el at 1200 × 800 and 640 × 480 (below) | Pending | Pending |
| Multiple DPI scales | 1×, 1.5× and 2× renderings (`QT_SCALE_FACTOR` on a Retina display, below); no physical 1× display | Pending (100/150/200 %) | Pending |
| Screen reader names | AX audit passed after #238 fixes ([audit](#macos-accessibility-audit)); no VoiceOver speech session; `inspectorControlsAreNamed`, `choicesReadTheirStateToScreenReaders` | Pending (Narrator) | Pending (Orca) |
| Floating tools at screen edges/multiple displays | Outside this editor change | Pending follow-up | Pending follow-up |

#220 was closed on the macOS evidence above. The pending Windows, Ubuntu 22.04 X11 beta, physical-DPI and screen reader passes are tracked in [#238](https://github.com/victorfu/snap-tray/issues/238); update this matrix with their results.

### macOS accessibility audit

The macOS screen reader check for #238 read the accessibility (AX) tree of the running debug app — the names, roles and states VoiceOver announces — with a small AXUIElement dump tool, for Settings (every page), the Library, the recording editor (inspector, timeline, export panel, Canvas), the capture toolbar and More menu, and pin windows (hover bar, zoom panel, click-through pill). VoiceOver itself was not run, so speech order and verbosity are unverified.

Fixed (each with a regression test that fails before the fix):

- Capture toolbar buttons and More rows had no accessible information at all. Buttons are named by their tooltip; the active drawing tool reads as selected; More rows are named, pressable menu items (`tst_ToolbarOverflow::buttonsAndMoreRowsAreNamedForScreenReaders`).
- The Settings sidebar was a list of menu items without a selected state, and VoiceOver's press did nothing. It is a tab list now (`sidebarPagesArePageTabsForScreenReaders`).
- Drop-downs (Settings, Library sort and smart folders, editor Preview speed and Aspect ratio) read only their label; they now read the label and the current choice.
- Repeated buttons could not be told apart: two "Open Settings" (now with the permission and its status), two "Browse..." (now with the folder and path), and Edit/Clear/Reset on every hotkey row (now with the action; Edit also reads the shortcut and its status). The filename template field is named (`settingsControlsHaveDistinctNames`).
- Exclusive editor choices built from buttons (codec, Blurred Video/Image background, zoom scale, crop aspect) are radio buttons with a selected state (`choicesReadTheirStateToScreenReaders`).
- Pin windows were unnamed dialogs; they are named "Pinned image".

The gaps that audit left were fixed in [#248](https://github.com/victorfu/snap-tray/issues/248), again checked in the AX tree of the running app:

- The style strip below the capture toolbar: sizes and colors are named choices (the custom color button reads the current color); the stroke width is a slider with increase and decrease, and Mosaic's widths are choices; the arrow and line style buttons read the current style; shapes are choices and Filled is a checkbox; Bold, Italic and Underline are checkboxes; font size and font read their value; Auto Blur is named by its hint (`Qml_ToolOptionsAccessibility`). The arrow and line style names are now translated, in the menus too, where they were English-only.
- A toolbar button drawn as disabled (Undo with nothing to undo) is reported as unavailable; a hover layer keeps its tooltip (`tst_ToolbarOverflow::disabledButtonsReadAsUnavailableButKeepTooltips`).
- Library results read as one-of-many choices with a selected state, sections and smart folders as tabs, and Grid/List as a choice (`tst_LibraryWindow::itemsAndSectionsReadTheirSelection`).
- The macOS tray icon has an accessibility label: "SnapTray - Screenshot Utility", followed by the tray's recording status (state, elapsed time, mode, audio and warnings) from the start of a recording until it is finished (`Platform_MacTrayAccessibility`).
- While the modal export panel is open, the editor behind it is disabled, so screen readers cannot operate it there either; it is usable again when the panel closes (`tst_RecordingEditor::exportPanelKeepsTheEditorOutOfReach`). A follow-up also removes the editor subtree from accessibility traversal for the entire visible lifetime of the panel, restoring it on close. `tst_RecordingEditor::exportIsolatesAccessibility` verifies exclusion, accessible panel actions, and focus restoration across two open/close cycles; this follow-up is checked through Qt accessibility interfaces, not a new native AX audit.
- Sliders read their value with its unit, for example "Default opacity, 100%" (`tst_SettingsCombo::slidersReadTheirUnit`, `Qml_PinHoverBarQml`).

### Actual editor evidence

Captured from the QML integration fixture on macOS 27.0.1 / Qt 6.12.0 at 2× device pixel ratio. The solid-colour video is generated test media, not a design mockup. Wide windows are 1200 × 800 logical pixels; the narrow window is 640 × 480. These images verify this editor implementation only.

![Wide editor, light appearance](/assets/images/developer/editor-wide-light.png)

![Selected mask, dark appearance](/assets/images/developer/editor-selected-dark.png)

![Export panel, dark appearance](/assets/images/developer/editor-export-dark.png)

![Narrow editor, dark appearance](/assets/images/developer/editor-narrow-dark.png)

### Operation videos

Recorded on macOS 27.0.1 / Qt 6.12.0 from a scripted session that sends real mouse and key events to the editor; the pointer and the caption are drawn into each frame. The solid-colour video is generated test media. MP4 versions sit next to each GIF.

- Selection (#218): timeline, picture and inspector stay on one object; Delete, Undo, typing in a time field, Escape.

  ![Selection](/assets/images/developer/editor/videos/editor-218-selection.gif)

- Timeline (#219): move, resize, one undo step per drag, Escape cancels a drag, exact times, cuts and speed with the exported time.

  ![Timeline](/assets/images/developer/editor/videos/editor-219-timeline.gif)

- New recording end to end (#220): trim, split, 2× speed, canvas, text, mask, zoom, export.

  ![New recording](/assets/images/developer/editor/videos/editor-220-flow.gif)

- Kept project (#220): export with "Keep editable projects" on, reopen from the Library's Recordings, delete a mask, undo, redo, export again, reopen again.

  ![Reopened project](/assets/images/developer/editor/videos/editor-220-reopen.gif)

### Export and project states

The export status sits on a solid panel, so it reads the same over any frame. Closing the window during an export asks to keep or cancel it; cancelling keeps every edit. A reopened project says "Project" in the top bar (a new recording that will be kept says "Editable project").

![Exporting](/assets/images/developer/editor/15-exporting-light.png)

![Closing during an export](/assets/images/developer/editor/16-close-while-exporting-dark.png)

![Export cancelled, edits kept](/assets/images/developer/editor/17-export-cancelled-light.png)

![Reopened project](/assets/images/developer/editor/18-reopened-project-dark.png)

### Display scales

Rendered at 1× and 1.5× with `QT_SCALE_FACTOR=0.5` and `0.75` on a 2× display, at the same logical sizes as above. Text, icons and hit targets keep their layout; the narrow inspector scrolls.

![1×, wide](/assets/images/developer/editor/19-scale-100-wide-light.png)

![1×, narrow](/assets/images/developer/editor/20-scale-100-narrow-light.png)

![1.5×, wide](/assets/images/developer/editor/21-scale-150-wide-light.png)

![1.5×, narrow](/assets/images/developer/editor/22-scale-150-narrow-light.png)

### Long translations

German, Finnish, Russian and Greek are the longest editor translations. Wide (German) and narrow (all four) windows keep every control reachable; the file name, status chip and an inline label elide by design and keep their full text in the tooltip and accessible name.

![German, wide](/assets/images/developer/editor/13-long-translation-wide-de.png)

![German, Greek, Finnish and Russian, narrow](/assets/images/developer/editor/14-long-translation-narrow.png)

### Regenerating the evidence

All of these come from `tests/Qml/tst_RecordingEditor.cpp` and run only when their variable is set:

```bash
cd build
# Screenshots of every editor state (light and dark); add the concept board with SNAPTRAY_EDITOR_CONCEPT=<png>.
SNAPTRAY_EDITOR_DESIGN_DIR=/tmp/editor ./bin/Qml_RecordingEditor designEvidence designEvidenceError
# The same in another language.
SNAPTRAY_EDITOR_DESIGN_DIR=/tmp/editor SNAPTRAY_EDITOR_TRANSLATION=$PWD/snaptray_de.qm ./bin/Qml_RecordingEditor designEvidence
# Export and project states (15-18).
SNAPTRAY_EDITOR_DESIGN_DIR=/tmp/editor ./bin/Qml_RecordingEditor designEvidenceStates
# Other display scales (19-22 come from 01, 02 and 10 of these runs).
QT_SCALE_FACTOR=0.5 SNAPTRAY_EDITOR_DESIGN_DIR=/tmp/editor-1x ./bin/Qml_RecordingEditor designEvidence:light
QT_SCALE_FACTOR=0.75 SNAPTRAY_EDITOR_DESIGN_DIR=/tmp/editor-1.5x ./bin/Qml_RecordingEditor designEvidence:light
# Operation video frames, then a video per scenario.
SNAPTRAY_EDITOR_VIDEO_DIR=/tmp/videos ./bin/Qml_RecordingEditor operationVideo operationVideoReopen
ffmpeg -framerate 10 -i /tmp/videos/218-selection/frame-%05d.png -c:v libx264 -pix_fmt yuv420p editor-218-selection.mp4
```

## Capture toolbar

Applies to the Region Capture toolbar, the Pin Window annotation toolbar, and the Screen Canvas toolbar (#224). Design: `docs/superpowers/specs/2026-11-05-capture-toolbar-design.md`.

### Contract

- Buttons sit in three groups, defined once in `include/tools/ToolbarTraits.h`: **draw** (shape, arrow, pencil, marker, text, mosaic, eraser, step badge, emoji), **assist** (undo, redo, OCR, QR Code, multi-region, and pin's Beautify/crop/measure), and **finish** (pin, save, copy). A group boundary adds a separator plus a 4 px gap; the finish group sits on a faint accent band.
- When the toolbar does not fit the screen (capture: the selection's viewport; pin: the pin's screen; canvas: the activation screen), buttons collapse into **More** by priority: QR Code, OCR, multi-region, emoji, step badge, eraser, mosaic, then other drawing tools from the end of the row. Copy, save, pin, done, cancel, exit, undo, redo, and the active tool never collapse. Hit targets stay 28×24.
- **More** names each item, works with Up/Down/Enter/Esc, closes on a click outside or a re-layout, and returns focus to the More button.
- With a text, arrow, line, or shape annotation selected, the style strip edits **that annotation**, says `Selected: <type>`, and each change is one undo step (dragging the width slider or clicking through colours coalesces until 500 ms idle). With nothing selected it says `New object` and sets the defaults for the next annotation. Editing a selection never changes the saved defaults. A selected shape keeps its type; emoji, pencil, marker, mosaic, and step badges are not restyled.
- Selection follows the annotation, not its position in the list: erasing other annotations keeps it.

### Acceptance

| Check | macOS | Windows | Ubuntu 22.04 X11 |
| --- | --- | --- | --- |
| Groups, finish band, normal width (light/dark) | Screenshots below | Pending manual pass | Pending manual pass |
| Collapse into More on a narrow screen, menu by keyboard | Screenshots below + `tst_ToolbarOverflow` | Pending manual pass | Pending manual pass |
| Selection placement at screen edges and on a narrow side monitor | `tst_ToolbarOverflow::selectionPlacementStaysOnScreen` | Pending manual pass | Pending manual pass |
| Restyle a selected annotation, undo/redo, defaults untouched | Video below + `*_StyleSync::selectedArrowStyleIsUndoable` (region, pin, canvas) | Pending manual pass | Pending manual pass |
| Long translation (German) | Screenshots below | Pending manual pass | Pending manual pass |
| High DPI | 2× (Retina) screenshots below | 150 % pending | Pending manual pass |
| Screen readers (toolbar, More menu, strip label) | Toolbar, More and the style strip: AX audit passed after #238 and #248 fixes ([audit](#macos-accessibility-audit)); no VoiceOver speech session | Pending Narrator pass | Pending Orca pass |

Known limit: on very narrow screens (about 400 px) the style strip itself does not collapse, so its last control can run past the edge (see the narrow screenshots). The toolbar above it always fits.

### Evidence

Rendered on macOS 27.0.1 / Qt 6.12.0 at 2× from the real toolbar, More menu, and style strip QML over a sample screenshot; style edits go through `AnnotationStyleEditor` and `AnnotationLayer`.

![Normal width, light](/assets/images/developer/toolbar/01-normal-light.png)

![Arrow selected, light](/assets/images/developer/toolbar/02-selected-light.png)

![Arrow selected, dark](/assets/images/developer/toolbar/02-selected-dark.png)

![Narrow screen with More open, dark](/assets/images/developer/toolbar/03-narrow-more-dark.png)

![Narrow screen, arrow selected, German](/assets/images/developer/toolbar/04-narrow-selected-light-de.png)

Operation video: select an arrow, change its colour and width, undo, redo, deselect. MP4 next to the GIF.

![Selected-object styling](/assets/images/developer/toolbar/videos/toolbar-selected-style.gif)

Geometry undo (#237): drag a rectangle, rotate it, change its colour, undo three times (colour, rotation, position come back in turn), redo, then press Esc during another drag. The drags go through the same `ShapeAnnotationEditor` all three surfaces use. Move, resize, rotate, arrow endpoint/curve and polyline vertex drags, and a text re-edit are each one undo step; style and geometry steps undo independently.

![Geometry undo](/assets/images/developer/toolbar/videos/annotation-geometry-undo.gif)

### Regenerating the toolbar evidence

```bash
cd build
SNAPTRAY_TOOLBAR_DESIGN_DIR=../docs/assets/images/developer/toolbar ./bin/Qml_ToolbarEvidence toolbarStates
SNAPTRAY_TOOLBAR_DESIGN_DIR=../docs/assets/images/developer/toolbar SNAPTRAY_TOOLBAR_TRANSLATION=$PWD/snaptray_de.qm ./bin/Qml_ToolbarEvidence toolbarStates:light
SNAPTRAY_TOOLBAR_VIDEO_DIR=/tmp/toolbar-video ./bin/Qml_ToolbarEvidence operationVideo
ffmpeg -framerate 10 -i /tmp/toolbar-video/frame-%05d.png -pix_fmt yuv420p toolbar-selected-style.mp4
SNAPTRAY_TOOLBAR_VIDEO_DIR=/tmp/toolbar-video ./bin/Qml_ToolbarEvidence geometryUndoVideo
ffmpeg -framerate 10 -i /tmp/toolbar-video/geometry-undo/frame-%05d.png -pix_fmt yuv420p annotation-geometry-undo.mp4
```

## Pin window

Applies to pin windows (#225). Design: `docs/superpowers/specs/2026-11-05-pin-window-controls-design.md`.

### Contract

- A pin is the image. Resting the pointer on it for 300 ms shows a hover bar inside its bottom edge (`PinHoverBar`, a non-activating QML tool window): **Zoom N%**, **Opacity N%**, a Live/Paused chip while Live Update runs, **Annotate** and **⋯**. It fades 500 ms after the pointer leaves the pin, the bar and its panel, and never shows during a drag or edge resize, with the annotation toolbar open, in region-layout editing, in click-through, or with "Show controls on hover" off.
- The bar keeps an 8 px inset on every side. When the full bar does not fit, it shows the two values and **⋯**; when even that does not fit, it is not shown. Both widths are measured (labels differ by language).
- A value opens a panel above its button: slider, presets and **Reset to 100%**; Esc, another key or a click outside closes it.
- Keys on a focused pin: `Ctrl/Cmd` + `=` / `-` / `0` (zoom in, out, reset), `[` / `]` (opacity), `Ctrl/Cmd` + `]` (reset opacity), `Space` (annotate), `L` / `P` (Live). The bar and panel are pointer-only (non-activating windows); every action on them has one of these keys, and any key other than Esc closes an open panel before the pin handles it.
- Badges name the value ("Zoom 150%", "Opacity 80%") for every change; the zoom badge moves to the top-right while Live Update holds the top-left.
- Click-through shows "Click-through · Turn off ✕" on the shared glass at the pin's top-right; the tray adds "Turn Off Click-through for All Pins" while any pin uses it.
- The context menu is grouped View | Output | Modes | Pins | Close, with shortcut hints.

### Acceptance matrix

| Check | macOS | Windows | Ubuntu 22.04 X11 beta |
| --- | --- | --- | --- |
| Build and repository tests | `scripts/run-tests.sh`, all suites | CI build and tests only | CI build and tests only |
| Hover bar, panels, compact bar, light/dark/German | Evidence below (`PinWindow_Evidence`) | Pending manual pass (#238) | Pending manual pass (#238) |
| Bar follows pin drag/zoom; hides in excluded states | `PinWindow_PinHoverBar`, `PinWindow_PinHoverIntegration` | CI tests only | CI tests only |
| Click-through pill and tray exit | `PinWindow_Transform`, `App_MainApplicationTrayMenu`; screenshot below | Pending manual pass (#238) | Not supported (no click-through) |
| Physical DPI scales | 2× only | Pending (#238) | Pending (#238) |
| Screen reader names | AX audit passed after #238 fixes ([audit](#macos-accessibility-audit)); no VoiceOver speech session; `Qml_PinHoverBarQml`, `PinWindow_Transform` | Pending (Narrator) | Pending (Orca) |

### Evidence

macOS 27.0.1 / Qt 6.12.0, 2×. The pin and its floating windows are grabbed and composited at their real positions over a generated backdrop.

![Hover bar](/assets/images/developer/pin/01-hover-bar-light.png)

![Zoom panel](/assets/images/developer/pin/02-zoom-panel-light.png)

![Hover bar, dark, Live](/assets/images/developer/pin/05-live-chip-dark.png)

![Hover bar, German](/assets/images/developer/pin/01-hover-bar-light-de.png)

![Click-through](/assets/images/developer/pin/06-click-through-dark.png)

Operation video: hover, zoom panel, reset, opacity panel, leave.

![Pin hover controls](/assets/images/developer/pin/videos/pin-hover-controls.gif)

### Regenerating the pin evidence

```bash
cd build
SNAPTRAY_PIN_DESIGN_DIR=../docs/assets/images/developer/pin ./bin/PinWindow_Evidence screenshots
SNAPTRAY_PIN_DESIGN_DIR=../docs/assets/images/developer/pin SNAPTRAY_PIN_TRANSLATION=$PWD/snaptray_de.qm ./bin/PinWindow_Evidence screenshots:light
SNAPTRAY_PIN_VIDEO_DIR=/tmp/pin-video ./bin/PinWindow_Evidence operationVideo
ffmpeg -framerate 10 -i /tmp/pin-video/frame-%05d.png -pix_fmt yuv420p -vf "pad=ceil(iw/2)*2:ceil(ih/2)*2" pin-hover-controls.mp4
```

## Library

Applies to the Library window (#226): the former History window, now holding screenshots and editable recording projects. Design: `docs/superpowers/specs/2026-11-05-capture-library-design.md`.

### Contract

- One window, opened by the tray's **Library** (hotkey "Library") and, on its **Recordings** section, by **Recording Library...**. The storage models stay separate: `HistoryStore` for screenshots, `RecordingProjects` for projects. `LibraryModel` maps both into one list, and every row keeps its source row so actions reach the right backend.
- The sidebar has **All Captures**, **Screenshots** and **Recordings**, each with a count. **Smart Folders** filter screenshots. "All Captures" merges both kinds in the window's sort order: newest or oldest by capture or recording time, or largest first.
- No row has its own buttons. One command bar and one context menu act on the selection, with actions for its kind:
  - Screenshot: Edit, Pin, Copy, Save As..., Delete...
  - Project: Open in Editor, Show Saved File (only when that file exists), Show Project Folder, Delete Project...

  The caption says what is selected. `Return` opens, `Delete` asks.
- Each card or row names its kind ("Screenshot", "Editable project"). A project's detail line says whether it was saved and under which name, or that the saved file was moved, or that it is open in the recording preview. A project open in the preview cannot be opened again or deleted.
- Delete always asks, in a strip above the results that names what goes and what stays. Esc, Cancel or a new selection cancels it, and Esc closes the window only when nothing is pending.
- Where projects are shown, a note says that projects keep the full recording, how many days unchanged projects are kept, and whether keeping is off.
- Below 820 px the sidebar becomes a row of section buttons and the smart folders move into a menu next to the sort (Screenshots only); below 720 px the header stacks. The minimum window width is 640 px.

### Acceptance matrix

| Check | macOS | Windows | Ubuntu 22.04 X11 beta |
| --- | --- | --- | --- |
| Build and repository tests | `scripts/run-tests.sh` | CI build and tests only | CI build and tests only |
| Sections, counts, merge order, roles | `Qml_LibraryModel`, `Qml_LibraryWindow` | CI tests only | CI tests only |
| Actions per kind, delete confirmation, busy project, privacy note | `Qml_LibraryWindow` | CI tests only | CI tests only |
| Wide and narrow, light/dark/German | Evidence below (`Qml_LibraryEvidence`) | Pending manual pass (#238) | Pending manual pass (#238) |
| Physical DPI scales | 2× only | Pending (#238) | Pending (#238) |
| Screen reader names | AX audit passed after #238 and #248 fixes ([audit](#macos-accessibility-audit)); no VoiceOver speech session | Pending (Narrator) | Pending (Orca) |

### Evidence

macOS 27.0.1 / Qt 6.12.0, 2×, generated demo captures.

![All Captures, a project selected](/assets/images/developer/library/01-library-all-light.png)

![Recordings, list view, dark](/assets/images/developer/library/02-recordings-list-dark.png)

![Delete Project confirmation](/assets/images/developer/library/03-delete-project-light.png)

![Narrow window, Screenshots](/assets/images/developer/library/04-narrow-screenshots-dark.png)

![All Captures, German](/assets/images/developer/library/01-library-all-light-de.png)

### Regenerating the Library evidence

```bash
cd build
SNAPTRAY_LIBRARY_DESIGN_DIR=../docs/assets/images/developer/library ./bin/Qml_LibraryEvidence screenshots
SNAPTRAY_LIBRARY_DESIGN_DIR=../docs/assets/images/developer/library SNAPTRAY_LIBRARY_TRANSLATION=$PWD/snaptray_de.qm ./bin/Qml_LibraryEvidence screenshots:light
```

## Settings and Beautify

Applies to the Settings window and the Beautify panel (#227). Design: `docs/superpowers/specs/2026-11-06-settings-beautify-design.md`.

### Contract

- **Pages by task:** General, Capture, Pin Windows, Recording, Saving, Text Recognition, Hotkeys. Updates and About come after a divider.
  - Recording is shown only when `recordingSupported`, and Text Recognition only when `ocrSettingsVisible`.
  - Pages load on first selection.
  - The keys are `general`, `capture`, `pins`, `recording`, `saving`, `ocr`, `hotkeys`, `updates`, `about`. `SettingsWindow.selectPage(key)` opens one.
- **What moved:**
  - Advanced is split. Capture gets After Capture, Region Selector and Auto Blur. Pin Windows gets Appearance and Steps.
  - Files and Watermark become Saving.
  - "Max history entries" becomes **Saving > Library > Screenshots kept in Library**.
  - Setting keys, managers and stored values are unchanged.
- **Page header:** every page except About starts with `SettingsPageHeader`, a title and one sentence. The header can hold a page-wide action, such as Hotkeys' **Restore All Defaults**.
- **Search:** a "Search settings" field tops the sidebar (`Ctrl+F`/`Cmd+F`). It finds pages by name, settings by label, section or page (translated and English), and hotkeys by action name. Opening a result goes to the page, scrolls to the row and highlights it briefly. `Esc` clears the search.
- **Defaults and reset:**
  - Settings save at once.
  - A row with a `resetKey` shows a reset button in its left gutter while its value differs from the manager default; `SettingsBackend.resetSetting` writes the default through the property. Hotkeys keep their per-shortcut Reset and Restore All Defaults.
- **Label column:** 140 px, widened to fit the longest single word of a label (up to 45 % of the row) so long compounds do not break mid-word.
- **Beautify, results first:**
  - The panel shows the preview, then **Style**, **Background** and a collapsed **Customize**.
  - The four styles are Compact, Standard (the defaults), Spacious and Flat. A style sets padding, corners and shadow, and leaves the background and ratio alone. The card matching the current values is chosen; otherwise the caption reads **Custom**.
  - The background swatches are the eight named gradients, the same as the recording Canvas.
  - Customize lists Background type, Colors, Padding, Corners, Shadow (the shared `SettingsToggle`), Shadow blur and Aspect ratio, with no colons. It stays open or closed while the panel is reused. When opening it grows the panel, the panel stays on the screen.
  - **Save style…** saves the whole look under a name (inline field; `Esc` cancels the name, not the panel). Up to 8 saved styles sit below the built-in ones, each with a ✎ to rename (or `F2`) and a × to delete (or `Delete`). Two saved styles cannot share a name; a taken name is shown in red and Rename stays disabled. Saving under an existing name updates it.
- **Keyboard and names:**
  - Style cards, swatches, color chips and Customize are Tab stops. Space and Return activate them.
  - Each has a button role and a name ("Style: Standard", "Background: Ocean"). Cards and swatches also report checked.
  - A chosen item has a thicker ring as well as the accent colour.

### Acceptance matrix

| Check | macOS | Windows | Ubuntu 22.04 X11 beta |
| --- | --- | --- | --- |
| Build and repository tests | `scripts/run-tests.sh` | CI build and tests only | CI build and tests only |
| Page order, groups, gating, removed pages | `Qml_SettingsWindowFeatureGating` | CI tests only | CI tests only |
| Former settings reachable on their new page | `Qml_SettingsWindowFeatureGating` | CI tests only | CI tests only |
| Style and background presets, chosen and Custom states | `Qml_BeautifyPanelBackend`, `Qml_BeautifyPanelQml` | CI tests only | CI tests only |
| Per-setting reset, search and reveal, label column | `Qml_SettingsWindowFeatureGating` | CI tests only | CI tests only |
| Saved styles | `Qml_BeautifyPanelBackend`, `Qml_BeautifyPanelQml` | CI tests only | CI tests only |
| Moved strings keep their translations | `Settings_QmlTranslations` | CI tests only | CI tests only |
| Ordinary and minimum window, light/dark/German | Evidence below (`Qml_SettingsEvidence`) | Pending manual pass (#238) | Pending manual pass (#238) |
| Physical DPI scales | 2× only | Pending (#238) | Pending (#238) |
| Screen reader names | AX audit passed after #238 fixes ([audit](#macos-accessibility-audit)); no VoiceOver speech session; `Qml_SettingsWindowFeatureGating`, `Qml_SettingsCombo` | Pending (Narrator) | Pending (Orca) |

### Evidence

macOS 27.0.1 / Qt 6.12.0, 2×.

![Settings, Capture page](/assets/images/developer/settings/01-settings-capture-light.png)

![Settings, Saving page scrolled to Watermark and Library, dark](/assets/images/developer/settings/02-settings-saving-dark.png)

![Settings at the minimum window size, Pin Windows](/assets/images/developer/settings/03-settings-pins-minimum-light.png)

![Beautify, styles and backgrounds](/assets/images/developer/settings/04-beautify-styles-light.png)

![Beautify, Spacious on Lavender with Customize open, dark](/assets/images/developer/settings/05-beautify-customize-dark.png)

![Settings search for "watermark", Saving opened at the result, a changed setting with its reset button](/assets/images/developer/settings/06-settings-search-light.png)

![Beautify with saved styles, dark](/assets/images/developer/settings/07-beautify-saved-dark.png)

![Settings, Capture page, German](/assets/images/developer/settings/01-settings-capture-light-de.png)

![Beautify, German](/assets/images/developer/settings/04-beautify-styles-light-de.png)

### Regenerating the Settings evidence

```bash
cd build
SNAPTRAY_SETTINGS_DESIGN_DIR=../docs/assets/images/developer/settings ./bin/Qml_SettingsEvidence screenshots
SNAPTRAY_SETTINGS_DESIGN_DIR=../docs/assets/images/developer/settings SNAPTRAY_SETTINGS_TRANSLATION=$PWD/snaptray_de.qm ./bin/Qml_SettingsEvidence screenshots:light
```
