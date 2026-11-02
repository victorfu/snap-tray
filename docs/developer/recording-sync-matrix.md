---
last_modified_at: 2026-11-02
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

- **Video**: each frame carries its frame ID as a 16-cell barcode, a moving
  square, and a full white flash at fixed wall times (every 700 ms from 350 ms).
  A static span repeats one ID, as an unchanged screen does.
- **Audio**: a 40 ms, 1 kHz pulse at every flash time, from the microphone,
  system audio, or both (mixed), delivered as 10 ms capture callbacks
  timestamped the way the capture engines do.

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
| Slow encoder (stalls) | `slowEncoderMs` |

## What is checked (decoded media)

- Every expected frame is present, in order, and presented at its expected time
  (within half a frame); no frame from inside a pause; no gap where a pause was.
- The tail is complete: last frame ID and end time.
- Unique frames are counted apart from repeats (`uniqueFps`); repeated frames
  never make up the frame rate.
- Audio is present when requested, reaches the end of the video, and every
  flash has a pulse within one frame interval + 25 ms (`worstAvOffsetMs`).

The expected values come from a reference run of the same events through the
same timing pieces without the injected pipeline bug.

## Proving the checks work

`detect-*` rows inject a bug or a fault the pipeline does not correct, and the
analysis must report a failure: a pause that is not removed, audio shifted by
120 ms, a tail cut by 500 ms, backward timestamps reaching the encoder (it
clamps them, so frames appear at the wrong time), and an audio engine whose
clock starts 150 ms late.

## Running

```bash
./scripts/run-tests.sh                         # includes the fast rows (about 10 s)
./scripts/run-sync-matrix.sh                   # fast rows, JSON reports in build/sync-matrix/<commit>
./scripts/run-sync-matrix.sh reports/a --full  # adds 4K30, 4K60 and a long 1080p30 row
SNAPTRAY_SYNC_LONG_S=600 ./scripts/run-sync-matrix.sh reports/b --full   # a 10-minute long row
scripts/sync-matrix-compare.py reports/a reports/b   # A/B across commits
```

Each report keeps the scenario (configuration), the expected and measured
values, failures and skipped checks, and the cost: wall and CPU time, resident
memory, file size, frames written, and frames the encoder rejected. The compare
script exits with status 1 when a passing row fails or the A/V offset grows by
more than 10 ms; cost changes are listed, not judged, until a baseline per
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
