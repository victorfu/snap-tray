---
last_modified_at: 2026-10-04
layout: docs
title: Recording
seo_title: "SnapTray Screen Recording: MP4, GIF and WebP Capture for macOS and Windows"
description: "macOS/Windows only: record full screen sources with MP4, GIF, and WebP outputs."
permalink: /docs/recording/
lang: en
route_key: docs_recording
doc_group: workflow
doc_order: 2
---

Recording is available on macOS and Windows only. Linux beta does not include
recording, and its recording UI is hidden.

## Recording entry points

- Tray menu: Record Screen
- Recording hotkey: configure Record Screen in Settings > Hotkeys

## Recording lifecycle

1. Choose the screen to record when prompted on multi-display setups.
2. Recording starts immediately on the selected screen.
3. Use the floating control bar to monitor duration and stop recording.
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

On macOS and Windows, record while manually scrolling one vertical content area. In Recording Preview, trim the relevant interval and crop to the scrolling area (window snapping is available). Choose **Long Screenshot**, then use the save/generate button. Playback pauses while the screenshot is analyzed and rendered; **Cancel** keeps the recording.

Review the result before sharing. The result reports automatic side cropping, low-confidence regions and unjoined sections. Click a marker with a known source time to return to that point in the video; a missing-coverage marker without a source cannot seek. Uncertain joins are not invented. An entirely unreliable recording produces an error instead of an image.

- **Save PNG** saves all parts, with numbered filenames when needed. Existing files are not overwritten; collisions receive a unique name. A failed part can be retried without saving successful parts again to the same destination.
- **Copy** and **Pin** use the selected part at its original resolution.
- **Back** returns to the video to adjust the crop or trim, or export a video instead.
- Fixed headers are excluded by default; use **Include Fixed Header** to regenerate with the observed header.

Outputs taller than 30,000 pixels split automatically. All three output actions keep the original recording open. Closing Recording Preview still discards its temporary recording. The preview loads visible image tiles rather than one oversized texture. Extremely large results or ranges are rejected with a message; shorten the range or reduce the crop.

This feature supports vertical scrolling only and is unavailable in Linux beta. Recording on 5K/6K displays has not yet been validated.

### Independent sections and image repair

When reliable portions cannot be joined, each connected section is exported separately, in order of its first appearance in the recording. No relative page position is guessed between sections. The result labels **Section** separately from **Part**: a section may itself split at 30,000 pixels. Isolated frames without a reliable pair remain reported as unjoined footage. Save PNG exports every section; filenames use `-s01` and, when needed, `-s01-p001`.

Choose **Edit Image** and drag vertically over the image to select a horizontal band. **Keep Selection** crops away everything above and below it; **Delete Selection** removes the band and brings the remaining rows together. Edits affect only the selected part. Undo/Redo (Ctrl/Cmd+Z and Ctrl/Cmd+Shift+Z, or Ctrl+Y) retain up to 32 operations per part; **Reset Image** can always restore the generated image. Deleting the entire image is disabled. Warning markers follow the surviving pixels.

**Back** returns to the recording, and **View Result** returns to the existing edited result. Changing the recording crop/trim or generating again resets these image edits. Saving an edited revision creates a new unique file and leaves earlier exports intact. **Annotate in Pin** opens the current edited part with the existing annotation toolbar visible; save annotations from that pin window. Those annotations do not modify other parts or the recording preview's image.
