---
last_modified_at: 2026-11-04
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

The first implementation introduces neutral editor surface tokens, a stable editor shell, a scrollable inspector, a separate export popup, explicit choices, mutually exclusive effect selection, named effect lanes, and release-to-commit range dragging. Shared dialog buttons gain keyboard activation and visible focus. Capture, pin, library, settings navigation, and Beautify are still governed by the specification above; this editor change does not claim to redesign those surfaces.

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
| Pending input, drag/undo, close/cancel | Automated preview suites; operation videos (below) | CI tests only | CI tests only |
| New recording end-to-end (trim → split/speed → canvas → text/mask/zoom → export) | Scripted real-input session, `editor-220-flow` | Pending manual pass | Pending manual pass |
| Reopened project (reopen → change → undo/redo → save → reopen) | `tst_RecordingPreviewExport::projectsKeepTheRecordingAndItsEdits`, `tst_RecordingEditor::redoAndReopenKeepSelectionHonest` | CI tests only | CI tests only |
| Long translations | de, fi, ru, el at 1200 × 800 and 640 × 480 (below) | Pending | Pending |
| Multiple DPI scales | 2× only (Retina) | Pending (100/150/200 %) | Pending |
| Screen reader names | Inspector sweep `inspectorControlsAreNamed`; no VoiceOver session yet | Pending (Narrator) | Pending (Orca) |
| Floating tools at screen edges/multiple displays | Outside this editor change | Pending follow-up | Pending follow-up |

Keep #220 and #221 open until their remaining acceptance and follow-up tracking are complete.

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
# Operation video frames, then a video per scenario.
SNAPTRAY_EDITOR_VIDEO_DIR=/tmp/videos ./bin/Qml_RecordingEditor operationVideo
ffmpeg -framerate 10 -i /tmp/videos/218-selection/frame-%05d.png -c:v libx264 -pix_fmt yuv420p editor-218-selection.mp4
```
