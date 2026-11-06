---
last_modified_at: 2026-11-06
layout: docs
title: Hotkeys
seo_title: "SnapTray Hotkeys: Capture and Platform-Specific Recording Shortcuts"
description: Configure capture shortcuts and platform-specific recording shortcuts through centralized hotkey management.
permalink: /docs/hotkeys/
lang: en
route_key: docs_hotkeys
doc_group: workflow
doc_order: 3
---

## Default hotkeys

| Action | Default |
|---|---|
| Region Capture | `F2` |
| Screen Canvas | `Ctrl+F2 / Cmd+F2` |
| Paste | `F3` |
| Quick Pin | `Shift+F2` |

## Additional configurable actions (no default)

- Full Screen Capture
- Delayed Capture
- Pin from Image
- Library
- Toggle Hide/Show All Pins
- Record Screen: starts a recording; pressing it again stops (or cancels while preparing)
- Pause/Resume Recording
- Show Recording Controls: brings the floating control bar back onto the recorded screen (macOS/Windows)

Recording actions appear only where recording is available. A recording hotkey pressed again within a fraction of a second, for example through key repeat, counts as one press.

## Keys inside an editor

The hotkeys above work anywhere. Inside Region Capture, a Pin Window, or Screen Canvas, single keys act on that editor and cannot be rebound, for example `I` to pick a color from the screen (see [Annotation Tools](/docs/annotation-tools/#picking-a-color-from-the-screen)). Each page lists its own keys.

## Annotation shortcuts

While you annotate (Region Capture, a pin, or Screen Canvas), these shortcuts change the drawing order of the selected annotation. They are not global and cannot be rebound.

| Action | macOS | Windows / Linux |
|---|---|---|
| Bring Forward | `⌘]` | `Ctrl+]` |
| Send Backward | `⌘[` | `Ctrl+[` |
| Bring to Front | `⌥⌘]` | `Ctrl+Shift+]` |
| Send to Back | `⌥⌘[` | `Ctrl+Shift+[` |

On a pin, `Ctrl/Cmd + ]` resets opacity when no annotation is selected. See [Annotation Tools](/docs/annotation-tools/#putting-one-annotation-above-another).

## Hotkey categories

Hotkeys are managed by category inside Settings:

- Capture
- Canvas
- Clipboard
- Pin
- Recording (shown where recording is available)

## Conflict strategy

If your keybinding conflicts with another app:

1. Rebind action in Settings > Hotkeys.
2. Test trigger immediately.
3. Keep frequently used actions on single key combos.

## Team recommendation

For shared environments, define a standard hotkey profile and document it internally.
