---
last_modified_at: 2026-11-03
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
4. Click Stop to open the preview in edit mode, or to save the file right away in quick mode.

## Choosing an area before recording

Turn on **Choose an area before recording** in Settings > Recording (or **Choose an area first** in the recording setup) to pick what to record before it starts. It is off by default: recording starts on the whole screen, as before.

After you choose the screen, it dims: drag to draw the area, drag inside it to move it, and pick a ratio (Free, 16:9, 4:3, 1:1, 9:16) if you need one. The label shows the size in video pixels; an area is at least 64 pixels on each side and stays on one screen. **Record** (or **Enter**, or a double-click) starts, **Full Screen** records the whole screen, and **Cancel** (or **Esc**) does not record.

- **Edit mode** still records the whole screen. The preview opens cropped to the area, so you can move or resize the crop, undo back to it, or reset it to see everything. A recording recovered after a crash opens with the same crop, and the recovery list says that the file holds the whole screen.
- **Quick mode** records only the area, because there is no preview to crop in afterwards.

While recording, a frame around the area shows what is being recorded, amber while paused. It is left out of the recording on macOS and Windows. On Linux X11 the capture cannot leave it out, so it is shown only in quick mode, just outside the recorded area; in edit mode, where the whole screen is recorded, no frame is shown, so changing the crop later never reveals one. While the area is being chosen, screenshots, Screen Canvas and pins wait until it is done or cancelled.

The temporary recording in edit mode, and a recording waiting for recovery, contain the whole screen, including what is outside the area, until they are saved or discarded.

## Quick mode and edit mode

**After recording** in the recording setup and in Settings > Recording chooses what Stop does. Both places change the same setting.

| | Edit mode (**Edit in the preview**) | Quick mode (**Save right away**) |
| --- | --- | --- |
| After Stop | The preview opens: trim, crop, cut, zoom, mask, add effects, then save | The save dialog opens, or the file is saved automatically when auto-save is on |
| Recorded as | MP4 at high quality, converted to your format and quality when you save | Your output format and quality directly |
| Audio | Recorded, also for GIF and WebP exports (which stay silent) | MP4 only; GIF and WebP have no sound |
| Pointer, window and key tracks | Kept with the temporary recording for editing | Not recorded |
| After saving | The preview closes (or keeps an editable project, if that is on) | Optionally **Copy the file** to paste it into a chat or folder, or **Show in folder** |

Quick mode behaves exactly like the former "Show preview" off. The tray tooltip says which mode the current recording uses. Disk space monitoring, stopping safely when the disk is nearly full, and recovering a recording after a crash work the same in both modes. On a system that records MP4 directly without a preview, recording is always in quick mode.

In the preview, the trash button, **Esc**, and the window's close button ask before deleting the recording. **Esc** or **Enter** on that question keeps the recording; press **Tab** to move to Discard. While you are drawing a crop, **Esc** only cancels the crop.

While a recording is in progress, the tray or menu bar icon shows its state with a corner badge: a ring while preparing or counting down, a red dot while recording, pause bars while paused, and three dots while the video is finishing. Hover the icon to see the state, the recorded time (pauses excluded), and the screen being recorded. If the tray sits on the recorded screen, the icon can appear in the video.

## Editing workspace

The top bar shows the recording's file name and whether it is kept as an editable project, with **Undo**, **Redo**, the primary **Export…** button, and discard. Playback controls sit directly under the preview; the timeline is at the bottom.

The right inspector starts at **Project settings**: **Canvas**, **Pointer**, **Audio**, and **Keys** affect the whole recording, each with a summary of its current state. Unavailable categories stay listed, dimmed, with the reason. Select a clip or effect on the timeline, or click a text, image, or mask on the picture, to edit its own properties; the inspector shows its type and time range, and **Return to Project settings** at the bottom goes back. At narrow window widths the inspector is a sheet over the preview, opened with the settings button in the top bar and closed with ✕ or a click outside it. Escape deselects the current effect or closes the open category and returns to Project settings; at narrow widths it also hides the inspector.

**Add effect** groups Auto Zoom, Add Zoom, Add Mask, Add Text, and Add Image beside the timeline. Effects have named lanes, including separate rows for overlapping masks and overlays. Drag a segment to move it or drag an edge to change its range. Changes apply when you release, as one undo step; Escape during a drag (of an effect, a split, or a trim handle) cancels it and nothing changes. An effect that lies only in a cut part is crossed out: it is kept but not exported. Start/end fields in the inspector remain available for precise edits and very short segments. Invalid time input must be corrected or cancelled with Escape before selecting another object.

Click or drag the video track to seek without leaving the active effect or crop edit. Double-click a clip to select it in the inspector (or focus it with Tab and press Enter). Only the effect lanes scroll; the video track and time labels stay visible.

The timeline and effect ranges use **source recording time**, including cut parts. Exported duration reflects cuts and speed changes; the player skips cut parts. Once something is trimmed, cut, or sped up, the time beside the playback controls also shows where the playhead lands in the export (**Export 0:01.500 / 0:04.000**), or **Not exported** inside a cut part. **Preview speed** (beside the playback controls) changes playback only; a clip's **Export speed** (in the clip inspector) changes it in the saved file.

**Export…**, Ctrl/Cmd+S, and Enter outside text or crop editing open the same export panel. Select MP4, GIF, or WebP there, then use **Export…** to validate pending edits and save. **Back to editing** preserves your selection. Export also updates a kept editable project; there is no separate project-only save command.

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

## Cut parts out and change their speed

The timeline can do more than trim the ends. In the preview:

1. Move the playhead and click **Split** (or press **S**) to divide the recording there. Splits show as small bars on the timeline; drag one to move it, double-click it to join its two parts again. With the keyboard, Tab to a split, then use the arrow keys to move it (Shift: 1 s) and **Delete** to join its parts.
2. **Cut Part** (or **Delete**) leaves a part out of the export: the selected clip if one is selected, otherwise the part at the playhead. The part is crossed out on the timeline. **Keep Part** puts it back.
3. Double-click a part to select it. In the inspector, **Export speed** sets how fast it plays in the export: 0.5×, 1×, 1.5×, 2× or 4×. The speed shows on the part.

The preview plays the edit: cut parts are skipped and each part plays at its own speed. The time under the video shows the exported length. Cuts and speeds can be undone like other edits, and **Reset Crop and Trim** removes them.

The export joins the kept parts in order. Pointer smoothing, motion blur and click highlights never reach across a cut, so a click in a cut part never shows. Audio is kept for parts at normal speed and stays in sync at every seam; **parts at another speed are silent** in this first version. Cutting only the first or last part works like a trim. Cuts and speed changes are available for MP4, GIF and WebP on all supported recording platforms.

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

The recorded video does not contain the mouse pointer. While a recording that opens in the preview runs, SnapTray keeps a separate record of the pointer next to it: where it was, what it looked like, and when buttons were pressed. Keyboard input is only recorded if you separately enable shortcut hints. This record stays with the temporary recording and is deleted with it when you save or discard; saved videos and exports never include it. It is what later editing features use to show, enlarge or highlight the pointer.

On Linux (X11) it includes the pointer's shape and clicks. On macOS and Windows only the position is kept for now, and the pointer is drawn as a plain arrow. If the record cannot be written, the recording itself is not affected.

### Pointer effects in the preview

When a recording has this record, open **Pointer** in the inspector's Project settings. What you see in the preview is what the export draws, through the same crop and trim:

- **Pointer**: show or hide the mouse pointer in the exported video.
- **Size** (1×, 1.5×, 2×, 3×): draw the pointer larger.
- **Smooth** (Off, Low, Medium, High): smooth out a shaky path with a spring that anticipates movement and clicks, with tighter tracking during drags. Clicks stay at their recorded positions and times. Off preserves the recorded path and remains the default. Seeking and exporting use the same precomputed path; smoothing never reaches across a cut. Existing projects with smoothing enabled also use this improved model when reopened and exported, so their pointer paths can differ from earlier exports.
- **Clicks**: show a brief ring where each click happened. Needs recorded clicks (Linux).
- **Pointer** also includes these effects, all off by default:
  - **Hide When Idle** (1 s, 2 s, 5 s): fade the pointer out once it has not moved for that long. It fades back in just before it moves or clicks, so it is always visible at a click, and click rings are never hidden.
  - **Motion Blur** (Low, Medium, High): a short trail behind a fast-moving pointer. A resting pointer is drawn sharp.
  - **Still End** (0.5 s, 1 s, 2 s): hold the pointer still for the last moments of the exported clip, so moving to the stop button is not shown. It follows the trim end.
  - **Hide Pointer Here** hides the pointer (and click rings) for two seconds from the playhead. Hidden ranges are shown on named pointer lanes; click one to type its start and end, or delete it.

Your last choices are remembered (hidden ranges belong to one recording and are not), and they can be undone like crop and trim (**Reset Crop and Trim** keeps them). Drawing the pointer means the video is re-encoded when saved; with the pointer hidden and no other edits, an unedited recording is still saved without re-encoding. Pointer effects are available for MP4, GIF and WebP on all supported recording platforms.

### Shortcut hints

SnapTray can show the shortcuts you pressed while recording, for tutorials ("Ctrl + C", "Ctrl + Shift + T"). Keys are **never recorded unless you switch it on** before the recording: in the setup shown before recording (**Record shortcut keys**), or in Settings > Recording (**Record keys**). While keys are recorded, the tray status says so.

- **Shortcuts only** (the default once switched on) keeps keys held with Ctrl, Alt or the logo key (Win on Windows, Super on Linux), and keys such as Enter, Esc, Tab, the arrows and F-keys. Plain typing, Shift+letter included, is not kept.
- **All keys, including typed text** is a separate choice. Do not use it while entering passwords.

The keys are kept next to the temporary recording, without window titles or application names, and are deleted with it: they are never in the saved file or the History. Keys pressed while the recording is paused are not kept. Keys are read passively on Linux X11, macOS and Windows. macOS asks for Input Monitoring permission only after you opt in; secure input suspends key recording. Windows suppresses keys when the focused control reports a password field, and when that check fails or focus changes during the check; a shortcut that moves the focus itself, such as Alt+Tab or Alt+F4, is still kept when it was pressed outside a password field. With All keys on Windows, keys an input method is composing (Chinese, Japanese, Korean and similar) are not kept where the input method reports its state, since they are not the text typed; shortcuts and keys such as Enter still are. Some input methods do not report it, so keep All keys off when that matters. If the probe cannot start, a warning explains that key hints are unavailable and the recording continues. Applications that do not expose protected fields cannot always be detected; keep All keys off when entering sensitive text.

In the preview, **Keys** in Project settings (enabled when the recording has keys) draws them into the export: **Keys: On/Off**, **Place** (bottom, bottom left, bottom right, top), **Size**, **Dark/Light**, and how long a hint stays (1, 1.5 or 2.5 s). Keys pressed close together show together, and a repeated shortcut shows once with a count ("Ctrl + Z ×3"). Hints follow cuts (#185): keys in a cut part never show. Hints are drawn for MP4, GIF and WebP.

### Zoom and pan

Full-screen recordings often make buttons and text small once shared. The preview can zoom in on parts of the recording:

- **Auto Zoom** zooms in where you clicked or dragged: nearby clicks share one steady zoom, a far-away action pans across, and the view goes back to the whole picture when nothing happens. It needs recorded clicks (Linux); actions spread over most of the screen stay at the full view. Running it again replaces the automatic zooms but keeps the ones you added or changed.
- **Add Zoom** adds a two-second, 2× zoom at the playhead, centred on the pointer when it is known.
- Zooms appear in the Zoom lane. Click one to edit it: choose 1.5×, 2×, 2.5× or 3×, type its start and end, or click the video to move its centre. **Less motion** makes all zooms smaller and slower without direct pans, **Zoom effects** turns them all off without deleting them, and **Delete zoom** removes one.

Zooming in and out changes the size evenly from start to finish, and a pan between zooms far apart pulls back a little on the way so you can see where it goes. The preview shows exactly what the export draws, and zooms can be undone like other edits. A zoom never shows anything outside the recording or the crop. Typing and scrolling are not recorded, so they do not trigger automatic zooms. Zooms are available for MP4, GIF and WebP.

### Masking sensitive areas

Before sharing, cover account names, chats, notifications or other private content with **Add Mask**. A mask starts over the middle of the picture for the whole recording; drag it to move it and drag its corner to resize it. While it is selected you can choose:

- **Solid**: covers the area with black. This is the default and the only style that removes the content.
- **Pixelate** or **Blur**: weaken the detail. They do not guarantee that text or faces cannot be recognised or recovered; use Solid for anything that must not be seen.
- **Whole Recording**, or a start and end time for the mask.

Masks are burned into the exported MP4, GIF or WebP, and the preview shows them on the real picture, before any crop or zoom, so a zoom never uncovers what a mask hides. Each mask starts a fraction of a second early so no frame shows the content as it begins. Masks can be undone like other edits; **Reset Crop and Trim** keeps them.

**Masks are only in the exported file.** The unmasked recording is a temporary file: it stays available in the preview until you save (also after Cancel or a failed save), and once the masked export is saved, SnapTray deletes it. If you need an unmasked copy too, save once without masks first. SnapTray never uploads recordings. If the selected format cannot draw masks on your system, SnapTray refuses to export and says so, instead of saving a file without the masks. Masks are not tracked automatically: if the content moves, add masks for each time range.

### Text and images

**Add Text** puts a title or caption on the video for three seconds from the playhead; **Add Image** adds a picture file (PNG, SVG, JPEG, WebP), such as a logo, for the whole recording. Overlays appear as bars at the bottom of the timeline; click one to edit it:

- Text: type it in the box (several lines are fine), choose a font (Default uses the system font), make it smaller or larger, pick a colour, a dark or light box behind it, and the alignment.
- Image: smaller or larger, and its opacity.
- Both: drag it on the video to move it, drag its corner to resize it, show it for the whole recording or type a start and end time, or delete it. Overlays fade in and out.

Overlays sit on the exported picture: they keep their place through a crop and do not zoom with it, and masks never cover them. An image is read once when you add it, so deleting or moving the file afterwards does not affect the export. Overlays can be undone like other edits; **Reset Crop and Trim** keeps them. They are drawn for MP4, GIF and WebP. The font family is saved with editable projects; if it is unavailable on another computer, Qt uses a fallback font. The watermark from Settings is still added while recording, as before; overlays do not replace it.

### A frame around the video

**Canvas** in the preview puts the exported video on a background, like Beautify does for screenshots. The first time it starts from your screenshot Beautify look (switched off); after that, your last frame for recordings is remembered. It can be undone like other edits, and **Reset Crop and Trim** keeps it.

- **Background**: one of the gradient presets, white, black, **Blurred Video** (the video itself, blurred, behind it) or **Image…** (a picture file).
- **Saved styles**: the styles you saved in screenshot Beautify, and any saved here with **Save style…**, are one list. Click one to use it; **Rename** and **Delete** act on the style the frame matches. A style keeps a **Blurred Video** or **Image…** background, which belong to recordings only. The same style on a screenshot and a recording of the same size looks the same.
- **Padding**, **Corners** (None, Small, Medium, Large), **Shadow** and **Border**. Sizes follow the video, so a frame looks the same on a small or a 4K recording. A saved style with sizes between these shows **Custom**.
- **Ratio**: Auto, or 1:1, 16:9, 4:3, 9:16, 2:1. The background fills the extra space.

The frame makes the exported file larger than the cropped picture by the padding (and the ratio); MP4 export options then scale the framed video. It is drawn last, around a picture that already has its masks, zoom and pointer, and a blurred background is made from the masked picture, so nothing a mask hides shows around it. The preview shows the frame except while you edit the crop or compare with the original. The frame is available for MP4, GIF and WebP.

## Recovering unsaved recordings

SnapTray keeps a small record next to each recording until it is saved or discarded. If SnapTray stops before that (a crash, power loss, or quitting with an unsaved preview open), the next start shows a notice, and **Recover Recordings** in the tray menu lists what was left. Each entry shows when it was recorded, how much of it can be played, and whether audio that should be there is missing. **Recover** opens it in the preview so you can save it; **Discard** deletes it; **Later** keeps it for next time. Files waiting here are never removed by SnapTray's routine clean-up of old temporary files.

On Linux, recordings are written in fragments, so an interrupted recording can usually be recovered up to its last fragment (about every two seconds of video). This depends on the disk and when the system last wrote to it, so it is not a guarantee. On macOS and Windows an interrupted recording cannot be played yet and is listed only so you can discard it; a recording that finished normally but was never saved can be recovered on every platform.

## Editable projects

Normally the recording is deleted once its file is saved. Turn on **Keep editable projects** in Settings > Recording (off by default) to keep it with your edits instead: crop, trim, cut parts and speeds, pointer, zoom, masks, frame, shortcut hints, text and images, the audio mix, and the export format and MP4 options. **Recording Library...** in the tray menu opens the **Recordings** section of the [Library](/docs/region-capture/#library), which lists the kept projects with a thumbnail, length, size, date and the file saved from each. **Open in Editor** brings one back into the preview with every edit as you left it; saving again exports a new file and updates the project. Closing a project without saving keeps it as it was last saved. Projects are kept when you save from the preview, so the setting needs **Show preview**. A background image used by the frame is copied into the project. If a project cannot be kept or updated (for example, the disk is full), the saved file stays and the recording stays open in the preview, so nothing is lost; save again to retry. Saving into the projects' own folder is refused.

A project holds the **full screen recording**, including anything you blurred, masked or cropped out, plus its pointer, keys and separate audio tracks, so it uses disk space and contains what the saved file leaves out. It stays on this computer in SnapTray's app data folder and is never uploaded. **Delete Project...** in the Library asks first, then removes a project (the file you saved from it stays). The Recordings section repeats that projects keep the full recording, and how long unchanged ones are kept, and **Delete projects not changed for** removes old ones automatically after 7, 30 or 90 days. Projects are separate from recordings waiting for recovery; neither one ever removes the other's files.

## When recording can't keep up

If the computer cannot encode frames as fast as they are captured for several seconds in a row, the tray tooltip says "dropping frames" until it recovers. A short hiccup or an unchanging screen does not count. Lowering the frame rate in Settings > Recording usually helps.

Recording warnings never appear on screen while you record, so they cannot end up in the video. The latest one is shown in the tray tooltip, and all of them appear in one notification when the recording stops.

After a recording, **Copy Recording Diagnostics** in the tray menu copies a short text summary (frames captured, encoded and dropped with the reason, queue and encode times). It contains no images, audio, window titles or file names, and nothing is sent anywhere.

## Saving and cancelling

While MP4, GIF, or WebP is being exported, the preview shows what it is doing: converting (with a percentage), writing the file, or saving. **Cancel** stops any of them between frames. Your recording, its crop, and its trim stay, so you can change them or pick another format and save again.

The recording is only removed once the file is saved. If you cancel the save dialog, or the file cannot be saved, the preview stays open with everything you had; use **Try Again** or **Export…** to save again. Closing the preview window during an export asks whether to keep exporting or cancel it.

## Output formats

| Format | Typical use |
|---|---|
| MP4 (H.264) | Tutorials, long recordings |
| GIF | Short looping demos |
| WebP | Lightweight animated snippets |

### MP4 export options

With MP4 selected, the **Export…** panel sets the exported file, with its size, frame rate and an estimated file size below the choices and beside **Export…**:

- **Size**: Original, 1080p, 720p or a custom width. The aspect ratio is kept, and the video is never made larger than it was recorded (or cropped).
- **Frame rate**: Original, 60, 30 or 15 fps, never above the recording's own rate.
- **Quality**: Default (the quality from Settings > Recording), Low, Medium or High.
- **Codec**: H.264, which plays everywhere, or HEVC, which makes smaller files at the same quality but does not open in some older players. HEVC is offered only when the system can encode it.

The estimate is the target bitrate over the exported length, so the real file is usually a little smaller. Your last choices are remembered. Choices that change nothing for a recording (for example 1080p for a 720p recording) keep an unedited recording on the fast path, saved without re-encoding. Pointer effects, zooms and masks are drawn on the full-size picture before it is scaled, so they stay in place in a smaller video. These options are available on macOS, Windows and Linux. HEVC availability depends on the installed native encoder or FFmpeg encoder.

## Audio options

Audio capture is available for MP4 recordings when supported by the current platform and source:

- Microphone
- System audio
- Mixed capture

GIF and WebP exports are silent.

### Microphone and system audio after recording

When a recording that opens in the preview has both the microphone and system audio, SnapTray also keeps each of them on its own next to the temporary recording, on the same timeline as the video. The video still has the usual mixed sound, so nothing changes unless you want it to.

In the preview, **Audio** (MP4) sets each one for the export:

- **Microphone** and **System Audio** volume in 6 dB steps from −24 to +12 dB, and **Mute** for either.
- **Reduce Noise** turns down hiss, hum and room noise between words on the microphone (a high-pass filter and a soft noise gate). It uses very little processor time.

The preview plays the new mix as you change it. With everything at 0 dB and nothing muted, the recording is saved as recorded. A changed mix re-encodes the sound, and stays in sync with the video, cuts and speed changes included (parts at another speed stay silent). The two tracks are deleted with the temporary recording, and crash recovery keeps them with the recording it recovers. Recordings with only one source, or made before this, have just the mixed sound; the Audio category explains why separate-track controls are unavailable. GIF and WebP have no sound, so Audio is available only with MP4. Separate tracks are supported on macOS, Windows and Linux. On Windows the new mix can be previewed for recordings with separate tracks; playback of older recordings with only a mixed track remains unchanged.

### Check audio before you record

Turn on **Show setup before recording** in Settings > Recording to see a short setup each time you record. On setups with more than one display it appears anyway when you pick the screen. It shows the screen, the microphone and its input device, system audio, and a summary of the output format, preview, and countdown. Changes are saved, so later quick recordings use them.

Click **Test Microphone** to see a live input level. The microphone is only opened while the test runs and is released when you stop the test, start recording, or cancel. Nothing is played back. Silence shows as an empty meter and does not stop the recording.

If a saved microphone is disconnected, it is listed as "Not connected" and recording falls back to the default microphone with a notice.

### Audio status while recording

The tray or menu bar tooltip lists the microphone and system audio separately for the whole recording: Recording, Starting, Off, Permission denied, Device not found, Unavailable, or Not supported by GIF/WebP. If audio falls back during a recording (for example to system audio only), the tooltip keeps showing it after the notification is gone.

## Quality tuning

Open Settings > Recording and adjust frame rate, quality, countdown, and preview behavior.

The quality slider sets the quality of the saved video. When preview is on, the recording itself is captured at high quality so trims and crops do not lose detail; saving converts it to the selected quality, or keeps the file as is when it is already small enough. If the drive holding temporary recordings has less than about ten minutes of high-quality space free, SnapTray records at the selected quality instead and shows a warning. With preview off, recordings are written directly at the selected quality.
