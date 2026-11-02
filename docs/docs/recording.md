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

While cropping, the row under the timeline offers:

- **Aspect presets**: Free, Original, 16:9, 4:3, 1:1, and 9:16. Drawing, resizing, and clicking a window keep the chosen shape.
- **X, Y, W, H**: the crop in video pixels. Typed values are adjusted to even numbers and the minimum size, as the encoder requires, and the size label next to the selection always shows the size you will get.
- **Arrow keys** move the crop by 2 px (hold Shift for 10 px). Keys typed into a field stay in the field; **Esc** there only restores the field.

With a trim range, the start and end times under the timeline can be typed as `m:ss.mmm` (for example `0:07.250`).

Every applied crop and trim change can be undone (**Ctrl+Z**) and redone (**Ctrl+Shift+Z** or **Ctrl+Y**). Use ⟲ to go back to the unedited recording (also undoable), and ◧ or **\\** to compare with the original frame. The line under the timeline shows the original and output pixel sizes, the kept duration, and whether the chosen format has audio.

## Disk space

SnapTray checks the free space where the recording is written before it starts and every few seconds while it runs. The limits follow the recording: room to finish the file, a few seconds of recording, and, with preview on, room for an exported copy.

- **Before starting**: if there is almost no room, the recording does not start and says how much is free.
- **Low space**: one warning with an estimate of the minutes left (or the free space when there is no estimate yet); the tray tooltip says "low disk space". Freeing space clears it.
- **Almost full**: recording stops on its own, the file is finished normally, and everything recorded so far is kept. It is not discarded.
- **Unknown**: if the free space cannot be read, SnapTray says so once and keeps recording; it is not treated as unlimited.

Exports from the preview check for room for the new file first, and saving to another drive checks the destination. If there is not enough room, nothing is deleted: free up space or choose another location and save again. Write errors (for example an unplugged drive) are still reported even when the free space looked fine.

## When a recording is interrupted

If something outside SnapTray ends a recording, it stops the same way as pressing Stop: the file is finished, the preview (or save dialog) opens, and everything recorded so far is kept. A notice says why it stopped:

- the recorded screen was disconnected;
- its resolution, scaling or orientation changed (the output keeps one fixed size, so SnapTray stops instead of distorting the video);
- screen recording was stopped from the system's own controls, the permission was revoked, or capture stopped working;
- the disk is almost full (see above).

If several of these happen at once, the recording still ends only once and the first reason is reported. If the microphone is unplugged while recording, the recording continues without it and SnapTray says so; it never switches to another microphone on its own. If the file itself cannot be finished, SnapTray says whether a playable part was kept (on Linux it can be recovered from the tray menu, see below). A recording is only deleted when you choose Discard.

## The pointer in recordings

The recorded video does not contain the mouse pointer. While a recording that opens in the preview runs, SnapTray keeps a separate record of the pointer next to it: where it was, what it looked like, and when buttons were pressed. Keyboard input is never recorded. This record stays with the temporary recording and is deleted with it when you save or discard; saved videos and exports never include it. It is what later editing features use to show, enlarge or highlight the pointer.

On Linux (X11) it includes the pointer's shape and clicks. On macOS and Windows only the position is kept for now, and the pointer is drawn as a plain arrow. If the record cannot be written, the recording itself is not affected.

### Pointer effects in the preview

When a recording has this record, the preview shows pointer controls next to the format buttons. What you see in the preview is what the export draws, through the same crop and trim:

- **Pointer**: show or hide the mouse pointer in the exported video.
- **Size** (1×, 1.5×, 2×, 3×): draw the pointer larger.
- **Smooth** (Off, Low, Medium, High): smooth out a shaky path. Clicks always stay exactly where and when they happened. Off by default.
- **Clicks**: show a brief ring where each click happened. Needs recorded clicks (Linux).

Your last choices are remembered, and they can be undone like crop and trim (**Reset Crop and Trim** keeps them). Drawing the pointer means the video is re-encoded when saved; with the pointer hidden and no other edits, an unedited recording is still saved without re-encoding. On macOS and Windows, pointer effects are available for GIF and WebP exports; MP4 exports there do not draw them yet, and the preview says so.

### Zoom and pan

Full-screen recordings often make buttons and text small once shared. The preview can zoom in on parts of the recording:

- **Auto Zoom** zooms in where you clicked or dragged: nearby clicks share one steady zoom, a far-away action pans across, and the view goes back to the whole picture when nothing happens. It needs recorded clicks (Linux); actions spread over most of the screen stay at the full view. Running it again replaces the automatic zooms but keeps the ones you added or changed.
- **Add Zoom** adds a two-second, 2× zoom at the playhead, centred on the pointer when it is known.
- Zooms appear as bars above the timeline. Click one to edit it: choose 1.5×, 2×, 2.5× or 3×, type its start and end, or click the video to move its centre. **Less Motion** makes all zooms smaller and slower without direct pans, **Zoom On/Off** turns them all off without deleting them, and **Delete** removes one.

The preview shows exactly what the export draws, and zooms can be undone like other edits. A zoom never shows anything outside the recording or the crop. Typing and scrolling are not recorded, so they do not trigger automatic zooms. Like pointer effects, zooms are available for MP4 on Linux and for GIF and WebP everywhere.

### Masking sensitive areas

Before sharing, cover account names, chats, notifications or other private content with **Add Mask**. A mask starts over the middle of the picture for the whole recording; drag it to move it and drag its corner to resize it. While it is selected you can choose:

- **Solid**: covers the area with black. This is the default and the only style that removes the content.
- **Pixelate** or **Blur**: weaken the detail. They do not guarantee that text or faces cannot be recognised or recovered; use Solid for anything that must not be seen.
- **Whole Recording**, or a start and end time for the mask.

Masks are burned into the exported MP4, GIF or WebP, and the preview shows them on the real picture, before any crop or zoom, so a zoom never uncovers what a mask hides. Each mask starts a fraction of a second early so no frame shows the content as it begins. Masks can be undone like other edits; **Reset Crop and Trim** keeps them.

**Masks are only in the exported file.** The unmasked recording is a temporary file: it stays available in the preview until you save (also after Cancel or a failed save), and once the masked export is saved, SnapTray deletes it. If you need an unmasked copy too, save once without masks first. SnapTray never uploads recordings. If the selected format cannot draw masks on your system (MP4 on macOS and Windows for now), SnapTray refuses to export and says so, instead of saving a file without the masks. Masks are not tracked automatically: if the content moves, add masks for each time range.

## Recovering unsaved recordings

SnapTray keeps a small record next to each recording until it is saved or discarded. If SnapTray stops before that (a crash, power loss, or quitting with an unsaved preview open), the next start shows a notice, and **Recover Recordings** in the tray menu lists what was left. Each entry shows when it was recorded, how much of it can be played, and whether audio that should be there is missing. **Recover** opens it in the preview so you can save it; **Discard** deletes it; **Later** keeps it for next time. Files waiting here are never removed by SnapTray's routine clean-up of old temporary files.

On Linux, recordings are written in fragments, so an interrupted recording can usually be recovered up to its last fragment (about every two seconds of video). This depends on the disk and when the system last wrote to it, so it is not a guarantee. On macOS and Windows an interrupted recording cannot be played yet and is listed only so you can discard it; a recording that finished normally but was never saved can be recovered on every platform.

## When recording can't keep up

If the computer cannot encode frames as fast as they are captured for several seconds in a row, the tray tooltip says "dropping frames" until it recovers. A short hiccup or an unchanging screen does not count. Lowering the frame rate in Settings > Recording usually helps.

Recording warnings never appear on screen while you record, so they cannot end up in the video. The latest one is shown in the tray tooltip, and all of them appear in one notification when the recording stops.

After a recording, **Copy Recording Diagnostics** in the tray menu copies a short text summary (frames captured, encoded and dropped with the reason, queue and encode times). It contains no images, audio, window titles or file names, and nothing is sent anywhere.

## Saving and cancelling

While MP4, GIF, or WebP is being exported, the preview shows what it is doing: converting (with a percentage), writing the file, or saving. **Cancel** stops any of them between frames. Your recording, its crop, and its trim stay, so you can change them or pick another format and save again.

The recording is only removed once the file is saved. If you cancel the save dialog, or the file cannot be saved, the preview stays open with everything you had; use **Try Again** or **Save** to save again. Closing the preview window during an export asks whether to keep exporting or cancel it.

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
