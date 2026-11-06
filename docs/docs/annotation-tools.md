---
last_modified_at: 2026-11-06
layout: docs
title: Annotation Tools
seo_title: "SnapTray Annotation Tools: Draw, Text, Mosaic and Steps"
description: Use drawing, text, mosaic, and step tools to communicate clearly.
permalink: /docs/annotation-tools/
lang: en
route_key: docs_annotation_tools
doc_group: capture
doc_order: 2
---

## Tool categories

### Drawing

- Pencil
- Marker
- Arrow (single-line and polyline modes)
- Shape (rectangle/ellipse)

### Markup and guidance

- Text
- Step Badge
- Emoji Sticker

### Privacy and correction

- Mosaic
- Eraser

## Straight pencil and marker strokes

Hold `Shift` before pressing the left mouse button, then drag with Pencil or Marker to draw a straight stroke from the starting point to the cursor. The line can follow any angle and keeps the tool’s normal style, including the marker’s transparency. Each stroke is one Undo/Redo step.

The Shift state at the start determines the mode for the entire stroke. Pressing or releasing Shift during a drag does not switch modes. Start without Shift for normal freehand drawing. This works in Region Capture, Pin Windows, and Screen Canvas.

## Changing an annotation you already drew

Select a text, arrow, line, or shape annotation. The style bar then says **Selected: Arrow** (or the type you picked), and color, width, and style changes apply to that annotation. Press `Ctrl/Cmd + Z` to undo a change. Dragging the width slider or clicking through colors counts as one change. A selected shape keeps its type; pick the Shape tool to draw a different one.

With nothing selected, the style bar says **New object** and changes apply to the next annotation you draw. Changing a selected annotation never changes those settings.

Moving, rotating, or resizing an annotation, dragging an arrow's ends or curve, dragging a line's points, and editing a text again are each one undo step. Press `Esc` during a drag to put the annotation back where it was.

## Picking a color from the screen

Click the eyedropper next to the custom color swatch, or press `I`, to take a color from the image you are annotating, for example to match a brand color or a button. A magnifier shows the pixels under the pointer and the color code.

- Click to use the color. You can also hold the button, move to fine-tune, and release.
- The arrow keys move one physical pixel at a time, even on a 2x display. Press `Enter` to use the color.
- `Esc`, a right click, or a click outside the image cancels and keeps the current color.

The color comes from the image itself, never from annotations you drew or from SnapTray's toolbars:

| Where | Picks from |
|---|---|
| Region Capture | The frozen screenshot |
| Pin Window | The pinned image, as shown (zoom, rotation, flip) |
| Screen Canvas | A snapshot of the screen under the pointer, taken with the canvas hidden when you start picking and discarded afterwards. A whiteboard or blackboard gives its own color. |

With an annotation selected, the color applies to it and `Ctrl/Cmd + Z` undoes it. With nothing selected, the color becomes the current color and is remembered for next time, like any color you choose. Screen readers announce the result, for example "Color set to #1A73E8".

`I` works whenever the color palette is shown, except while you type in a text box. Before you select a region, `C` still copies the color code under the magnifier.

## Putting one annotation above another

Select an annotation, then change which annotations it covers:

| Command | macOS | Windows / Linux |
|---|---|---|
| Bring Forward | `⌘]` | `Ctrl+]` |
| Send Backward | `⌘[` | `Ctrl+[` |
| Bring to Front | `⌥⌘]` | `Ctrl+Shift+]` |
| Send to Back | `⌥⌘[` | `Ctrl+Shift+[` |

The same four commands are buttons at the end of the style bar for a selected text, arrow, line, or shape, and an **Arrange** submenu in a pin's right-click menu. Bring Forward and Send Backward go past the next annotation that overlaps the selected one, so each press makes a visible change; with nothing overlapping in that direction they go all the way. Pressing the same command several times in a row is one undo step. Step numbers never change when you rearrange: they follow the order you placed the badges in. Copy, Save, and Pin use the order you see.

## When the toolbar does not fit

On a narrow screen, less-used buttons move into **More** (⋯), starting with QR Code and OCR. Copy, Save, Pin, Undo, Redo, and the tool you are using always stay on the toolbar. Use the arrow keys and `Enter` to pick from **More**, and `Esc` to close it.

## Editing workflow

1. Draw rough annotations first.
2. Adjust color and width globally.
3. Add text and step badges for sequence.
4. Apply mosaic to sensitive areas.
5. Use Undo/Redo before export.

## Best practices

- Keep one accent color for key highlights.
- Use marker for emphasis, not for dense text.
- Prefer step badges for procedural screenshots.

## Beautify

**Beautify** on the pin toolbar puts the screenshot on a background, ready to share.

- **Style** sets padding, corners and shadow together: **Compact**, **Standard** (the default), **Spacious** or **Flat** (no shadow, square corners). The chosen style is outlined; after your own changes it reads **Custom**.
- **Background** offers eight named gradients. A style keeps the background, and a background keeps the style.
- **Customize** opens the details: background type (solid, linear or radial gradient), colors, padding, corners, shadow, shadow blur and aspect ratio.
- **Save style…** keeps the whole current look (background, padding, corners, shadow and aspect ratio) under a name you choose. Saved styles appear below the built-in ones; click one to use it, rename it with its **✎** (or `F2` when it has keyboard focus), or delete it with its **×** (or `Delete`). Two saved styles cannot share a name. Saving under an existing name updates that style. You can keep up to 8.
- **Copy** and **Save** remember the look for next time.

The recording editor's **Canvas** uses the same gradients and the same names (Background, Padding, Corners, Shadow, Aspect ratio).

## Export actions

- `Enter` to pin result
- `Ctrl/Cmd + C` to copy
- `Ctrl/Cmd + S` to save

## Next step

If you want annotations directly on the desktop, open [Screen Canvas](/docs/screen-canvas/).
