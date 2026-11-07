---
last_modified_at: 2026-11-07
layout: docs
title: Region Capture
seo_title: "SnapTray Region Capture: Precise Screenshots and Magnifier"
description: Precision region selection with magnifier, window detection, multi-region support, and quick export actions.
permalink: /docs/region-capture/
lang: en
route_key: docs_region_capture
doc_group: capture
doc_order: 1
---

## Start capture

Press `F2` (default) to enter Region Capture. The cursor switches to crosshair, and the cursor companion appears according to the `Cursor companion` setting in Settings.

## Full screen capture

Use `Full Screen Capture` from the tray menu, or bind it in Settings > Hotkeys, to capture the screen under the cursor without selecting a region. The output keeps the screen's native resolution on every monitor, including fractional scaling such as 125% or 150%. SnapTray's own windows, such as pins and notifications, are left out of the capture.

Choose what happens next in Settings > Capture > `After full screen capture`:

- `Copy to clipboard` (default)
- `Save to file`, using your Saving settings
- `Open editor`, which opens the capture with the whole screen selected so you can annotate before copying or saving

Captures are added to the [Library](#library).

## Delayed capture

Use `Delayed Capture` from the tray menu and pick `3 seconds`, `5 seconds`, or `10 seconds` to capture UI that disappears as soon as you press a key, such as hover states, dropdown menus, context menus, and tooltips. A small countdown pill appears at the top of the screen. It is click-through, so you can open the menu or hover the control you want while it counts down.

When the countdown ends, SnapTray hides the pill, freezes the screen under the cursor, and opens Region Capture on that frozen frame. From there the flow is the same as a normal region capture, and the countdown pill never appears in the result.

- The tray remembers the last delay you picked. Bind `Delayed Capture` in Settings > Hotkeys to start a countdown with that delay from the keyboard.
- To cancel, choose `Cancel Delayed Capture` from the tray menu or press the `Delayed Capture` hotkey again. Starting another capture during the countdown is ignored.

## Selection behavior

### Precision targeting

- Magnifier mode provides pixel-level feedback
- Width and height are shown near the selection
- Hold `Shift` to constrain aspect ratio

### Snap a resized edge to image content

After making a single selection, drag an edge or corner handle and hold `Alt` (`Option` on macOS) to align it with a nearby horizontal or vertical image boundary. You can instead hold plain `S` (without Ctrl/Cmd/Shift) **after starting the handle drag**, including when a Linux window manager reserves Alt-drag. A short accent line marks each successful snap. Releasing the key restores the raw drag position immediately, even without moving the pointer; releasing the mouse commits the current position. `Esc` during a live single-selection resize restores the selection from before the drag.

Snapping looks for sustained contrast along the selected side. It does not recognize whole objects, and short lines, low-contrast borders or photo textures may not give the intended result. It is disabled for initial selection, whole-selection moves, multi-region/replacement, history editing and locked aspect ratios. Arrow-key adjustments keep their existing behavior.

Only boundaries that the final screenshot crop can represent are eligible. At fractional scaling or Retina resolution, some physical pixel boundaries cannot be represented by the integer selection rectangle and are skipped. If analysis exceeds its budget, that update uses ordinary dragging. Guides are never included in saved, copied or history images.

### Window-aware selection

When window detection is available, SnapTray can detect the window under the cursor so you can select it with one click.

### Cancel and retry

Right-click cancels the current selection. During a live single-selection handle drag, `Esc` cancels only the resize and restores the pre-drag rectangle, even without Alt/Option or S held. In Quick Pin, releasing the mouse after this cancellation does not create a pin. Outside this gesture, including history replay, Esc retains the normal capture/replace cancellation behavior.

## Multi-region workflow

Press `M` to toggle multi-region capture mode. You can collect multiple regions, then export combined or separate outputs.

## Post-selection actions

After the selection is complete, the toolbar supports:

- Annotation + edit tools (Arrow/Pencil/Marker/Shape/Text/Mosaic/Step Badge/Emoji/Eraser)
- `OCR` (macOS/Windows only; hidden on Linux beta) and `QR Code Scan`
- `Auto Blur` (faces + credentials)
- `Pin`, `Save`, and `Copy`

## Useful shortcuts

| Action | Shortcut |
|---|---|
| Start Region Capture | `F2` |
| Copy color code under cursor (when magnifier mode is active) | `C` |
| Constrain ratio | Hold `Shift` |
| Toggle multi-region | `M` |
| Pick a color from the screenshot (after selecting a region) | `I` |
| Restore pre-drag selection during a live single-selection resize; otherwise normal cancellation | `Esc` |

## Library

**Library** in the tray menu opens one window for what SnapTray keeps: the screenshots in its history and the recordings kept as editable projects.

- The sidebar has **All Captures**, **Screenshots** and **Recordings**, each with a count. **Smart Folders** (Last 7 Days, Large Files, Replayable) filter the screenshots.
- Select a capture, then use the bar at the top, the right-click menu or the keys. `Return` opens it: **Edit** for a screenshot, **Open in Editor** for a project. `Delete` asks to delete it.
- Each card says what it is: **Screenshot** or **Editable project**. A project also says whether it has been saved, and under which name.
- Deleting always asks first and says what stays. Deleting a screenshot removes SnapTray's copy; files you saved from it stay. Deleting a project removes its full recording and edits; the file you saved from it stays.
- In a narrow window the sections become a row of buttons above the actions.

## Next step

Go to [Annotation Tools](/docs/annotation-tools/) to edit captures before save/copy.
For full-screen recording on macOS or Windows, use [Recording](/docs/recording/).
