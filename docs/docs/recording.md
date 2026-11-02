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

While recording, the tray menu also offers Pause/Resume Recording and, on macOS and Windows, Show Recording Controls. Both can be given hotkeys in Settings > Hotkeys.

## Recording lifecycle

1. Choose the screen to record when prompted on multi-display setups.
2. Recording starts immediately on the selected screen.
3. Use the floating control bar to monitor duration, pause or resume, and stop recording. While paused, the bar shows "Paused" and the time stops. On Linux, use the tray menu to pause, resume, or stop; floating controls are hidden while recording.
4. Click Stop to open the preview (or to save directly when preview is off).

In the preview, the trash button, **Esc**, and the window's close button ask before deleting the recording. **Esc** or **Enter** on that question keeps the recording; press **Tab** to move to Discard. While you are drawing a crop, **Esc** only cancels the crop.

While a recording is in progress, the tray or menu bar icon shows its state with a corner badge: a ring while preparing or counting down, a red dot while recording, pause bars while paused, and three dots while the video is finishing. Hover the icon to see the state, the recorded time (pauses excluded), and the screen being recorded. If the tray sits on the recorded screen, the icon can appear in the video.

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

### Check audio before you record

Turn on **Show setup before recording** in Settings > Recording to see a short setup each time you record. On setups with more than one display it appears anyway when you pick the screen. It shows the screen, the microphone and its input device, system audio, and a summary of the output format, preview, and countdown. Changes are saved, so later quick recordings use them.

Click **Test Microphone** to see a live input level. The microphone is only opened while the test runs and is released when you stop the test, start recording, or cancel. Nothing is played back. Silence shows as an empty meter and does not stop the recording.

If a saved microphone is disconnected, it is listed as "Not connected" and recording falls back to the default microphone with a notice.

### Audio status while recording

The tray or menu bar tooltip lists the microphone and system audio separately for the whole recording: Recording, Starting, Off, Permission denied, Device not found, Unavailable, or Not supported by GIF/WebP. If audio falls back during a recording (for example to system audio only), the tooltip keeps showing it after the notification is gone.

## Quality tuning

Open Settings > Recording and adjust frame rate, quality, countdown, and preview behavior.

The quality slider sets the quality of the saved video. When preview is on, the recording itself is captured at high quality so trims and crops do not lose detail; saving converts it to the selected quality, or keeps the file as is when it is already small enough. If the drive holding temporary recordings has less than about ten minutes of high-quality space free, SnapTray records at the selected quality instead and shows a warning. With preview off, recordings are written directly at the selected quality.
