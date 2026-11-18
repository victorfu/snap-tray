---
last_modified_at: 2026-11-06
layout: docs
title: Pin Window
seo_title: "SnapTray Pin Window: Keep Image References Always on Top"
description: Keep image references always visible with zoom, opacity, merge/layout controls, and inline annotation tools.
permalink: /docs/pin-window/
lang: en
route_key: docs_pin_window
doc_group: workflow
doc_order: 1
---

## Create a pin window

You can pin from capture result (`Enter`) or from tray menu using "Pin from Image...".

## Core controls

### Hover controls

Rest the pointer on a pin and a small bar appears at its bottom edge:

- **Zoom 150%** and **Opacity 80%** show the current values. Click one for a slider, presets, and **Reset to 100%**.
- **● Live** / **⏸ Paused** appears while Live Update runs; click it to pause or resume.
- **Annotate** opens the annotation toolbar (same as `Space`), and **⋯** opens the full menu.

The bar fades out when the pointer leaves the pin, and stays hidden while you drag, resize, annotate, or use click-through. On a narrow pin it keeps only the two values and **⋯**. Turn it off in **Settings → Pin Windows → Show controls on hover**. Everything on the bar also has a keyboard shortcut (below).

### Mouse actions

- Drag to move
- Mouse wheel to zoom
- `Ctrl` + wheel to adjust opacity
- Drag edge to resize

### Keyboard actions

| Action | Key |
|---|---|
| Rotate clockwise | `1` |
| Rotate counter-clockwise | `2` |
| Flip horizontal | `3` |
| Flip vertical | `4` |
| Zoom in / out | `Ctrl/Cmd` + `=` / `Ctrl/Cmd` + `-` |
| Reset zoom to 100% | `Ctrl/Cmd` + `0` |
| Opacity up / down | `]` / `[` |
| Reset opacity to 100% | `Ctrl/Cmd` + `]` (Bring Forward while an annotation is selected) |
| Start / stop Live Update | `L` |
| Pause / resume Live Update | `P` |
| Pick a color from the pinned image (while annotating) | `I` |
| Show toolbar | `Space` |
| Close | `Esc` |

A short badge names each change, for example "Zoom 150%" or "Opacity 80%".

## Context menu actions

The right-click menu (also **⋯** on the hover bar) is grouped by task:

- Arrange (only while an annotation is selected): Bring Forward, Send Backward, Bring to Front, Send to Back
- View: Show Toolbar, Show Border, Zoom (in/out, presets, Reset to 100%, smoothing), Opacity (up/down, Reset to 100%), Image processing (rotate, flip, crop)
- Output: Copy, Save, Watermark, Info, Open History Folder
- Modes: Click-through, Live Update (start/stop, pause/resume, frame rate), Adjust Region Layout
- Pins: Move to Screen, Merge Pins, Close All Pins
- Close

Live Update requires macOS or Windows 10 version 2004 (build 19041) or later.
Windows 10 22H2 and Windows 11 support it. Earlier Windows 10 versions retain
normal pin windows, but Live Update is disabled because those versions cannot
exclude the pin itself from screen capture. Linux beta does not support Live Update.
Changing the frame rate while Live Update is running also updates the capture engine.

### Click-through

With click-through on, clicks pass through the pin to the window below, and a **Click-through · Turn off ✕** label sits at the pin's top-right corner. Click that label to turn it off, or use **Turn Off Click-through for All Pins** in the tray menu. Click-through is available on macOS and Windows.

### Arrange merged images

**Merge Pins** keeps its existing original-size, wrapped layout. On the merged pin (or a multi-region capture), choose **Adjust Region Layout** to preview original sizes, a horizontal row with equal heights, or a vertical column with equal widths. Change spacing and use **Earlier / Later** to change image order. Numbers identify the same source image throughout the edit.

**Do not enlarge source pixels** is on by default. Equal-height/width modes use the smallest source dimension as the shared limit at the output DPR, preserving aspect ratios within pixel rounding. The dialog shows the saved image's pixel dimensions; screen size and display zoom do not select output quality. Turning the option off uses the largest source's natural logical height/width. Spacing is measured in logical output units.

Choose **Manual** to restore the free layout you had before trying automatic arrangements in this editing session, then drag or resize on the canvas. **Cancel** restores the layout from before entering the editor. **Apply** commits one image undo step; open the annotation toolbar to Undo or Redo it. Source annotations and redactions are included in merged pixels, and merging waits for auto-blur. Annotations added to the merged pin retain the existing layout behavior: their centers follow their source region while their visual size stays fixed.

Layouts support up to 20 images, 10,000 logical units per canvas dimension, and 64 megapixels of output. Invalid or oversized layouts are rejected before composition. Saved images and history continue using the existing output pipeline.

## Annotation in pin window

Press `Space`, or click **Annotate** on the hover bar, to open a lightweight annotation toolbar dedicated to the current pinned image. Beautify is on that toolbar too.

- Drawing tools: Pencil, Marker, Arrow, Shape, Text, Mosaic, Step Badge, Emoji, Eraser
- Utility tools: Crop, Measure
- Action tools: OCR (macOS/Windows only; hidden in Linux beta), QR Code Scan, Save, Copy

### Crop

Choose **Crop** on the annotation toolbar, or **Image processing > Crop** in the context menu, then drag over the part to keep. Releasing the mouse leaves a draft selection: nothing is cropped until you apply it.

- Drag inside the draft to move it, or drag one of its eight handles to resize it. Dragging outside starts a new selection.
- Pick an aspect ratio in the options strip: Free, Original, 1:1, 16:9, 9:16, 4:3 or 3:2. The draft is re-fitted at once and stays inside the image. On a rotated pin the ratio applies to the image as you see it.
- Arrow keys move the draft by one pixel of the image, `Shift` + arrow by ten.
- `Enter`, **Apply**, or a double-click inside the draft crops. `Esc` or **Cancel** discards the draft; a second `Esc` hides the toolbar as before.

The size shown next to the draft and in the options strip is in pixels of the image itself, not of the zoomed pin, so a pin from a 2x display reports twice its on-screen size. Positions move pixel by pixel; sizes are limited to those the pin shows without resampling (any size at 1x, even sizes at 2x, or the full width or height).

Applying adds one undo step, like before. Adjusting the draft adds none. Switching to another tool, hiding the toolbar or undoing to another image discards the draft.

## Next step

Use [Hotkeys](/docs/hotkeys/) to bind pin actions for faster workflows.
