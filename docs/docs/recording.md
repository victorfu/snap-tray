---
last_modified_at: 2026-10-04
layout: docs
title: Recording
seo_title: "SnapTray Screen Recording: MP4, GIF and WebP Capture for macOS, Windows and Linux X11"
description: "macOS, Windows and Linux X11: record full screen sources with MP4, GIF, and WebP outputs."
permalink: /docs/recording/
lang: en
route_key: docs_recording
doc_group: workflow
doc_order: 2
---

Recording is available on macOS, Windows, and Linux X11 builds with recording
enabled. Linux uses the system FFmpeg libraries and PulseAudio (or PipeWire's
PulseAudio compatibility service).

## Recording entry points

- Tray menu: Record Screen
- Recording hotkey: configure Record Screen in Settings > Hotkeys

## Recording lifecycle

1. Choose the screen to record when prompted on multi-display setups.
2. Recording starts immediately on the selected screen.
3. Use the floating control bar to monitor duration and stop recording. On Linux, use the tray menu to pause, resume, or stop; floating controls are hidden while recording.
4. Click Stop to export.

## Crop a recording

Recording always captures the full screen. To keep only part of it, open the preview after you stop:

1. Click **Crop** in the preview toolbar.
2. Drag on the video to draw the area, then drag inside it to move or drag a handle to resize. Edges snap to the video edges and centre lines. Hover over a window and it lights up; click it to crop to that window as it was at the current moment of the video.
3. Press **Enter** to apply or **Esc** to cancel. The applied size appears at the top-left of the video; click ✕ to clear it.
4. Save as MP4, GIF, or WebP. MP4 exports keep their audio.

## Output formats

| Format | Typical use |
|---|---|
| MP4 (H.264) | Tutorials, long recordings |
| GIF | Short looping demos |
| WebP | Lightweight animated snippets |

## Audio options

Audio capture is available for MP4 recordings when supported by the current platform and source:

- Microphone
- System audio
- Mixed capture

GIF and WebP exports are silent.

## Quality tuning

Open Settings > Recording and adjust frame rate, quality, countdown, and preview behavior.

The quality slider sets the quality of the saved video. When preview is on, the recording itself is captured at high quality so trims and crops do not lose detail; saving converts it to the selected quality, or keeps the file as is when it is already small enough. If the drive holding temporary recordings has less than about ten minutes of high-quality space free, SnapTray records at the selected quality instead and shows a warning. With preview off, recordings are written directly at the selected quality.

## Long screenshots from recordings

Record while manually scrolling one vertical content area. In Recording Preview, select **Create Long Screenshot**. Analysis starts automatically using the current time range and crop, or the full recording if neither was changed. Playback pauses; cancelling keeps the recording.

The recommendation shows the source's start and end previews, time range, and number of images. Select **Generate Long Screenshot** to create only this range. When the entire recording cannot be joined, the page explicitly recommends partial content. **Other available ranges** is optional: choose one to replace the recommendation. Disconnected content is never forced together or filled in.

Use **Adjust Analysis Range** to change the existing crop and trim controls. **Analyze Again** confirms the changes; **Cancel** restores the previous settings and recommendation. Dragging a control does not start another analysis.

- **Save PNG** saves the selected range. Content taller than 30,000 pixels is split into numbered images without reducing resolution; the recommendation announces the count beforehand. Existing files are not overwritten, and retries to the same destination skip successfully saved images.
- **Copy** copies the current image at its original resolution.
- **More…** contains **Pin** and **Annotate in Pin** for the current image. Save annotations from the pin window.
- **Choose Content** returns to the recommendation; **Recording Preview** returns to the video. The original recording remains available until the preview is closed.

The result fits the viewport width without enlarging small images. Scroll to read it, use Ctrl+wheel to zoom and Ctrl+0 to reset. If seams need review, one summary offers **Review** to visit the marked areas. Marks appear only in the preview, never in saved or copied images. Fixed headers and static sidebars are handled automatically; there is no separate image-row editor.

If no usable range is found, adjust to one scrolling content area or record again while scrolling slowly. Stationary recordings are not presented as successful long screenshots. Large analysis ranges or outputs may require a smaller crop or shorter range. This feature handles vertical scrolling, and availability depends on the platform's offline video reader. 5K/6K recording has not yet been validated.
