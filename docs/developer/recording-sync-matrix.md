---
last_modified_at: 2026-11-17
layout: docs
title: Recording Sync Matrix
seo_title: "SnapTray Recording Sync Matrix: End-to-End A/V and Fault Injection Tests"
description: How SnapTray verifies recordings end to end with synthetic media, injected faults, decoded-output checks, A/B reports and a real-device procedure.
permalink: /developer/recording-sync-matrix/
lang: en
route_key: developer_recording_sync_matrix
nav_data: developer_nav
docs_copy_key: developer
---

# Recording Sync Matrix

`Encoding_SyncMatrix` checks recordings end to end. Synthetic frames and audio
go through the recording's real timing pieces (`SessionClock`, `FramePacer`,
`TimestampedPcmMixer`) into the platform encoder and muxer, with faults
injected along the way. The written file is then decoded and checked frame by
frame. A row passes only when the decoded media is right; a state machine
that ran, or a player that opens the file, is not evidence.

Sources: `tests/Encoding/SyncMatrix.{h,cpp}` (harness and analysis),
`tests/Encoding/tst_SyncMatrix.cpp` (rows).

## What is generated

- **Video**: each frame carries its frame ID as a 32-bit barcode (two rows of
  16 cells, so long rows never wrap), a moving
  square, and a full white flash at fixed wall times (every 700 ms from 350 ms).
  A static span repeats one ID, as an unchanged screen does.
- **Audio**: a 40 ms pulse at every flash time, 1 kHz from the microphone and
  3 kHz from system audio, so a mixed recording shows each source apart;
  delivered as 10 ms capture callbacks timestamped the way the capture
  engines do.

## Faults

Source timestamps and callback arrival are controlled separately.

| Fault | Field |
| --- | --- |
| Capture timestamp jitter | `sourceJitterMs` |
| Late and bursty delivery | `arrivalDelayMs`, `burstEvery` |
| Dropped, duplicated and backward frames | `dropEvery`, `duplicateEvery`, `backwardEvery` |
| Static screen | `staticFromMs` / `staticToMs` |
| Early and repeated pause/resume | `pauses` |
| One audio source silent for a while | `audioGapFromMs` / `audioGapToMs` |
| Audio engine clock offset and drift | `audioClockOffsetMs`, `audioDriftPpm` |
| Encoder unavailable (stalls every 2 s on the simulated clock; frames wait in a model of the encoding worker's queue and are refused past its 30-frame / 3 s budget, as the worker does) | `slowEncoderMs` |

## What is checked (decoded media)

- Every expected frame is present (counted per occurrence, so lost repeats of a
  still screen count), in order, and presented at its expected time
  (within half a frame); no frame from inside a pause; no gap where a pause was.
- The tail is complete: last frame ID and end time.
- Unique frames are counted apart from repeats (`uniqueFps`); repeated frames
  never make up the frame rate.
- Audio is present when requested, reaches the end of the video, and every
  flash has a pulse within one frame interval + 25 ms (`worstAvOffsetMs`),
  from each enabled source on its own (told apart by pitch), except where
  the scenario silences that source.

The expected values come from a reference run of the same events through the
same session clock and encoder-queue model without the injected pipeline bug.
Frame pacing in the reference follows the rule `FramePacer` specifies, written
out separately, so a bug in `FramePacer` shows as missing frames instead of
shrinking the expectation with it.

## Proving the checks work

`detect-*` rows inject a bug or a fault the pipeline does not correct, and the
analysis must report a failure: a pause that is not removed, audio shifted by
120 ms, a tail cut by 500 ms, backward timestamps reaching the encoder (it
clamps them, so frames appear at the wrong time), an audio engine whose
clock starts 150 ms late, lost repeats of a still screen, a `FramePacer` that
paces at half the frame rate, and, in mixed rows, only the microphone 120 ms
late or system audio missing.

## Running

```bash
./scripts/run-tests.sh                         # includes the fast rows (about 10 s)
./scripts/run-sync-matrix.sh                   # fast rows, JSON reports in build/sync-matrix/<commit>
./scripts/run-sync-matrix.sh --full            # adds 4K30, 4K60 and a long 1080p30 row
./scripts/run-sync-matrix.sh reports/a --full  # ...into reports/a
SNAPTRAY_SYNC_LONG_S=600 ./scripts/run-sync-matrix.sh reports/b --full   # a 10-minute long row
scripts/sync-matrix-compare.py reports/a reports/b   # A/B across commits
```

A run replaces the reports in its directory: reports left from an earlier
run are removed first, so a row the new run skips (the 4K and long rows
without `--full`) shows as missing in a comparison instead of passing on its
old result.

Each report keeps the scenario (configuration), the expected and measured
values, failures and skipped checks, and the cost: wall and CPU time, resident
memory, file size, frames written, and frames the encoder rejected. The compare
script exits with status 1 when a passing row fails or the A/V offset grows by
more than 10 ms. A `detect-*` row counts as passing when its injected error
is reported (each report stores `mustPass` and `testSucceeded`), and a
scenario missing from the candidate, an empty candidate or a missing directory
is an error. Cost changes are listed, not judged, until a baseline per
machine exists. Compare runs from the same machine, build type, encoder and
power mode only.

## Platforms

| Platform | Result |
| --- | --- |
| Linux (FFmpeg/VA-API) | Full: encode, decode and analyse |
| macOS, Windows | **Skipped, not passed**: decoded-media analysis uses the FFmpeg readers of the Linux build. The test binary reports an explicit skip there. |

Native buffers, HDR to SDR, mixed DPI, control-window exclusion and real device
changes are not covered by mocks; they need the real-device procedure below.

## Real-device procedure (macOS, Windows, Linux hardware)

1. Make a reference clip with flashes and beeps:
   ```bash
   ffmpeg -f lavfi -i "color=c=gray:s=1920x1080:r=60,drawbox=enable='lt(mod(t,1),0.05)':c=white:t=fill" \
          -f lavfi -i "sine=f=1000:sample_rate=48000,volume=enable='gte(mod(t,1),0.05)':volume=0" \
          -t 60 -shortest -pix_fmt yuv420p -c:v libx264 -c:a aac reference.mp4
   ```
2. Play it full screen on the screen being recorded, with system audio on, and
   record it with SnapTray (also: pause and resume once, and once with the
   microphone near the speakers).
3. Measure flashes and beeps in the recording:
   ```bash
   ffmpeg -i recording.mp4 -vf "signalstats,metadata=print:key=lavfi.signalstats.YAVG" -f null - 2>&1 | grep YAVG
   ffmpeg -i recording.mp4 -af silencedetect=n=-30dB:d=0.02 -f null - 2>&1 | grep silence_end
   ```
   Flash frames have a high `YAVG`; each `silence_end` is a beep onset. Their
   difference is the A/V offset. Record machine, OS, SnapTray version,
   resolution, frame rate, encoder and power mode with the numbers.

## Windows pointer probe (#321)

`RecordingManager_WindowsCursorSupport` (every platform) covers the bitmap
conversion for each cursor kind, hook message and injected-event handling, the
queue between the hook thread and the GUI thread (order, ages, a drain from
inside a handler, overflow counting). `RecordingManager_WindowsCursorProbe`
(Windows) covers the factory, `poll()`, system cursors turned into pictures, a
generated inverting cursor, and creating and destroying the hook thread. Real
button events cannot be produced in a test without acting on the desktop;
check them on a PC. The diagnostics line `cursor: windows, shapes yes, clicks
yes` shows the hook was installed; "(button events lost: N times)" is added
when the queue overflowed.

| Check | Expected | Status |
| --- | --- | --- |
| Click, drag, text field, link, window edge | `shape` records for arrow, I-beam, hand and resize pointers; press and release for each click | Pending device test |
| Settings > Mouse > primary button: right | Primary (physical right) button recorded as `left`. The probe applies no swap, on the understanding that the hook already reports the user's order; if this fails, swap on `SM_SWAPBUTTON` | Pending device test |
| Middle, back and forward buttons | Recorded as `middle`, `back`, `forward` | Pending device test |
| 100%, 150% and 200% scaling; two monitors with different scaling | Click ring centre within 1 video px of the pointer tip | Pending device test |
| Enlarged pointer (Accessibility > Mouse pointer and touch > Size) | Recorded pointer matches the size on screen, or note the difference | Pending device test |
| Classic (monochrome, inverting) I-beam, and an application's custom cursor | Visible with the right outline in preview and exports | Pending device test |
| Pointer hidden by an application (full-screen video, typing) | Samples not visible; export draws no pointer | Pending device test |
| UAC prompt or lock screen during a recording | Log says the pointer cannot be read, then that it can again; the recording continues | Pending device test |
| Clicks through a remote-control tool or button remapper | Not recorded (injected); documented in the user docs | Pending device test |
| Heavy load on the GUI thread while moving and clicking | No perceptible mouse lag; clicks recorded; no "button events lost" in the diagnostics | Pending device test |
| Start and stop 20 recordings | No hook left behind (mouse responsiveness unchanged; process thread count returns to its baseline) | Pending device test |

## macOS pointer probe (#322)

`RecordingManager_CursorProbeMac` covers image conversion (pixel size, hotspot,
1x/2x representation, orientation), button and event-type mapping, event age,
and creating and destroying the probe. Real pointer shapes and real button
events cannot be produced in a test; check them on a Mac. The debug log line
`cursor: <probe>, shapes yes, clicks yes` names the click source in use:
`macos-event-tap` (Input Monitoring already granted) or `macos-event-monitor`
(no permission).

| Check | Expected | Status |
| --- | --- | --- |
| Fresh install, no Input Monitoring or Accessibility: record with preview | No system permission prompt; probe is `macos-event-monitor`; `button` records present; Auto Zoom enabled | Pending device test |
| Input Monitoring already granted (shortcut hints used before) | Probe is `macos-event-tap`; no prompt; `button` records present | Pending device test |
| Click, drag, text field, link, window edge | `shape` records for arrow, I-beam, hand and resize pointers; press and release for each click | Pending device test |
| Retina screen and external non-Retina screen, each recorded in turn | Pointer sharp on both; click ring centre within 1 video px of the pointer tip | Pending device test |
| Enlarged pointer (Accessibility > Display > Pointer size) | Recorded pointer matches the size on screen, or note the difference | Pending device test |
| Pointer hidden by an application (full-screen video, typing) | Samples not visible; export draws no pointer | Pending device test |
| System Settings > Mouse > primary button: right | Primary button recorded as `left` | Pending device test |
| Middle, back and forward buttons | Recorded as `middle`, `back`, `forward` | Pending device test |
| Main thread blocked for several seconds while clicking (event-tap source) | Log says the tap was disabled; tap re-enabled; later clicks recorded; diagnostics show "(button events lost: N times)" | Pending device test |
| Clicks with the event-tap source on Apple silicon and on Intel | `button` records present with times matching the clicks (the tap's timestamp unit differs between the two; both are handled, neither is confirmed on a device) | Pending device test |
| Input Monitoring revoked while recording with the event-tap source | Log says the tap cannot be re-enabled; the event monitor takes over; later clicks recorded | Pending device test |
| Stop the recording, then click | No further callbacks (tap, run-loop source and monitor removed); repeat start/stop 20 times | Pending device test |

## Click filtering and styles (#323, #326)

Automated coverage: `RecordingManager_CursorTimelineRecorder` injects exclusion
rectangles and native menu intervals, including release outside a control,
external drags released over controls, pauses, queued menu events, and 2x
screen/crop mapping. The version-1 sidecar keeps its existing event schema; an
optional `skippedUiClicks` counter in the end record is ignored by older readers.
`RecordingManager_CursorEffects` compares legacy Ring pixels with the original
renderer and checks every style for seek stability, cuts, color, crop, 3x size,
viewport clipping, and bounded, continuous bounce. `Qml_RecordingPreviewPointer`
checks the real controls, custom colors, availability, preferences, and undo.

| Device check | Expected | Status |
| --- | --- | --- |
| macOS/Windows: click Pause, Resume, Stop on the floating controls | No control clicks in the sidecar or Auto Zoom; skipped count includes all three | Pending device test |
| Linux: stop from the tray, including a submenu | No final menu press in the sidecar or Auto Zoom | Pending device test |
| Click through a pinned image on macOS/Windows | Underlying application clicks still recorded; recording controls still excluded | Pending device test |
| Drag from another app and release over SnapTray, using each Mac event source | Both events recorded; no held button afterward | Pending device test |
| Ring, Ripple, Spotlight, Burst; custom left/right colors; Low/High bounce | Same appearance at the same timestamp in preview, MP4, GIF and WebP | Pending device test |
| Zoom plus crop at 3x pointer size; click at an edge or over a mask | Tip/highlight remain aligned; spotlight stays in the video; masked information stays concealed | Pending device test |
| Rapid repeated clicks less than 150 ms apart; long drag | Bounce remains bounded, holds during the drag and recovers on release | Pending device test |

## Native editor follow-up (#185, #187, #188, #189, #190)

The native editor additions need the following macOS/Windows handoff checks.
Linux exercises the shared export loop through `Video_NativeEditExport`, the
native PCM packet reader, overlay font persistence/UI, and the remix worker's
seek/pause/mute/rate behavior. Those checks do not substitute for native device
or permission testing. Native CI runs the existing `Video_VideoTranscoder`
contract with the newly enabled capabilities.

| Check | macOS | Windows |
| --- | --- | --- |
| Multiple cuts + 0.5×/1.5×/2× parts; audio pulses at seams | Pending device test | Pending device test |
| Crop + zoom + masks + text/CJK fonts + frame; MP4/GIF/WebP parity | Pending device test | Pending device test |
| 1080p/720p/custom width, fractional source fps, 15/30 fps, codec choices | Pending device test, including HEVC available/unavailable | Pending device test; H.264 only |
| Both sources → separate gains/mutes/noise reduction in preview and MP4; recover project | Pending device test | Pending device test |
| Seek/pause/resume/speed changes with remixed audio and an output device disconnect | Pending device test | Pending device test |
| Keys off/shortcuts/all; pause boundary; permission refusal; protected input | Input Monitoring + Secure Event Input | UIA password field + secure desktop |

Measure A/V pulse error and output metadata using the procedure above. Check
that unedited saves still use the existing fast path. Check font fallback when
opening a project on a computer without the chosen family. #192 (the general
preview playback backend) remains separate.

## Roadmap #320 acceptance matrix

The following hardware checks supplement the synthetic suite. Record the OS,
GPU/driver, capture backend, audio endpoints, frame rate and build commit with
measurements. A Linux unit-test pass is not Windows or macOS hardware evidence.

| Scenario | Observe and retain |
| --- | --- |
| Windows 10 19041/22H2 and Windows 11, fresh settings | WGC selection or explicit fallback reason; existing explicit DXGI preference remains respected |
| Countdown, controls, area border | No SnapTray controls or border in captured pixels; compare CPU/readback count during three-second countdown and time to first admitted frame |
| 60 fps moving counter, then 20 seconds still | Source timestamps within one frame, at least 80% fewer still samples, exact final hold and duration; compare CPU/GPU load |
| Still desktop, pointer-only motion for 10 seconds | DXGI copy count stays in single digits; no missing sidecar pointer motion |
| UAC, lock/unlock, exclusive-full-screen transition | Compatibility/recovery events and times in diagnostics/tray; continuous playable output or explicit #158 termination with retained file |
| D3D device removed/reset | New capture device and encoder device manager; restored output or explicit encoder error, never silently accepted missing frames |
| CPU versus GPU encode of color chart | BT.709 primaries/transfer/matrix, limited-range output and matching decoded colors |
| Sparse MP4 playback and editing | Seek/play in Windows Media Player, Chrome, QuickTime and VLC; crop/trim/re-export and inspect first changed frame |
| WASAPI pause for two seconds then resume | Inspect waveform and listen on headphones for the 5 ms fade; retain waveform screenshot |
| WASAPI under ten-second CPU saturation | Per-source gap/trim counters explain every discontinuity; GUI startup block below 50 ms |
| Default output endpoint changes | Automatic endpoint migration is deferred; record explicit source degradation rather than claiming recovery on the new endpoint |
| Form, menu, code-editing recordings | Compare Auto Zoom framing before/after; verify classification, typing holds and cut/speed timing |
| Pitch-preserving voice/music at 0.5×–4× | Listen at seams, measure pulse alignment and pitch; compare preview at supported rates with exported audio |
| Shapes/camera blur/loop return | Compare shared-time preview and MP4/GIF/WebP frames, hidden endpoints, crop edges and cuts; masks must never leak into blur or background |

Automated additions cover classifier boundaries, 300-click auto-zoom and
72,000-sample cursor-preparation budgets, source-relative imported audio,
shape coordinate mapping, deterministic masked composition, gap fades,
contiguous bad timestamps, QPC conversion/backoff, nonblocking WASAPI startup,
DXGI push delivery and Media Foundation sparse nanosecond sample timing.
Windows sync-matrix rows `sparse-desktop-20s-mixed` and
`sparse-desktop-final-hold` decode sparse output and check sample placement,
audio alignment and the final held duration.
Windows-only tests must run on Windows; tests that skip for unavailable
hardware do not establish that hardware acceptance passed.

## Delayed clicks and the teleprompter

- Run `RecordingManager_CursorTimelineRecorder delayedLogicalClickBetweenPolls` for delayed press and release, a negative desktop origin, a cropped capture and 1×/2× conversion. It reads back the cursor sidecar and checks event time and position independently of both surrounding polls.
- On macOS, record a marked target on a Retina screen and on a second screen at a different scale. Exercise the event tap with permission and the fallback global monitor without input-monitoring permission. Click a mark, then move immediately while the GUI is busy. Compare press/release times and coordinates in the sidecar with the marked frame. Verify the click ripple and generated Auto Zoom center in preview and export. Record OS version, scaling, input path and observed offsets.
- On a dedicated extended desktop, set `SNAPTRAY_TEST_RECORDING_DISPLAYS=1` and run `Qml_ScreenPickerViewModel livePrompterStaysOutsideCapturedFrames`. The test shows an actual script on the second display, changes the first display’s color and checks frames from the selected capture engine. Record the engine name. Also check unplugging, pause/resume, cancellation, starting again, and both quick and edit mode on the target OS. The test takes over both screens temporarily.
