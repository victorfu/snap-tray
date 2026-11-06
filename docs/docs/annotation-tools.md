---
last_modified_at: 2026-11-05
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
- **Copy** and **Save** remember the look for next time.

The recording editor's **Canvas** uses the same gradients and the same names (Background, Padding, Corners, Shadow, Aspect ratio).

## Export actions

- `Enter` to pin result
- `Ctrl/Cmd + C` to copy
- `Ctrl/Cmd + S` to save

## Next step

If you want annotations directly on the desktop, open [Screen Canvas](/docs/screen-canvas/).
