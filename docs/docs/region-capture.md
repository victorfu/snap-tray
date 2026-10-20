---
last_modified_at: 2026-10-20
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

Choose what happens next in Settings > Advanced > `After full screen capture`:

- `Copy to clipboard` (default)
- `Save to file`, using your Files settings
- `Open editor`, which opens the capture with the whole screen selected so you can annotate before copying or saving

Captures are added to History.

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

### Window-aware selection

When window detection is available, SnapTray can detect the window under the cursor so you can select it with one click.

### Cancel and retry

Right-click or press `Esc` to cancel current selection.

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
| Cancel | `Esc` |

## Next step

Go to [Annotation Tools](/docs/annotation-tools/) to edit captures before save/copy.
For full-screen recording on macOS or Windows, use [Recording](/docs/recording/).
