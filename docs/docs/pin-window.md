---
last_modified_at: 2026-02-26
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

The bar fades out when the pointer leaves the pin, and stays hidden while you drag, resize, annotate, or use click-through. On a narrow pin it keeps only the two values and **⋯**. Turn it off in **Settings → Advanced → Pin Window → Show controls on hover**. Press `Tab` on a focused pin to reach it with the keyboard.

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
| Reset opacity to 100% | `Ctrl/Cmd` + `]` |
| Start / stop Live Update | `L` |
| Pause / resume Live Update | `P` |
| Show toolbar | `Space` |
| Close | `Esc` |

A short badge names each change, for example "Zoom 150%" or "Opacity 80%".

## Context menu actions

The right-click menu (also **⋯** on the hover bar) is grouped by task:

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

## Annotation in pin window

Press `Space`, or click **Annotate** on the hover bar, to open a lightweight annotation toolbar dedicated to the current pinned image. Beautify is on that toolbar too.

- Drawing tools: Pencil, Marker, Arrow, Shape, Text, Mosaic, Step Badge, Emoji, Eraser
- Utility tools: Crop, Measure
- Action tools: OCR (macOS/Windows only; hidden in Linux beta), QR Code Scan, Save, Copy

## Next step

Use [Hotkeys](/docs/hotkeys/) to bind pin actions for faster workflows.
