# Frame stepping fixture

`frame-gap.mp4` is a synthetic 64 × 64 H.264 video produced with SnapTray's
`MediaFoundationEncoder`: 20 frames at 10 fps, red through frame 10 and green
from frame 11 onward. There is no audio or captured user content.

The native encoder normalizes input timestamps, so the finalized MP4's `stts`
table was changed to preserve a real gap. In the track's 10,000 Hz timescale,
its `(sample_count, sample_delta)` entries are `(10, 1000)`, `(1, 2000)`, and
`(9, 1000)`. The movie, track, and media durations were increased from 20,000
to 21,000 ticks, and enclosing atom sizes were updated. The `moov` atom follows
`mdat`, so sample offsets and encoded picture data are unchanged.

Decoded PTS are 0, 100, …, 1000, 1200, 1300, …, 2000 ms. Media Foundation
reports an average frame interval of 105 ms, shorter than the 200 ms gap.
This exercises issue #112: stepping from the red
frame at 1000 ms must reach the green frame at 1200 ms, while an ordinary seek
to 1100 ms must keep the red frame at 1000 ms.

## Edited variable-rate fixture

`edit-vfr.mp4` contains eight synthetic 160 × 120 solid-color H.264 frames,
without audio or captured user content. Their PTS are 0, 100, 200, 1000, 1100,
1400, 1500 and 1600 ms; the first three are red, the next two green, and the
last three blue. The final frame lasts 100 ms. The nominal maximum is 10 fps
and the average is 80/17 fps. Unlike a file generated inside the test with a
native capture encoder, these timestamp gaps are independently fixed.

It was generated using FFmpeg 4.4.2/libx264 with these eight RGB24 frames on
stdin (each color repeated 160 × 120 times):

```bash
ffmpeg -f rawvideo -pixel_format rgb24 -video_size 160x120 -framerate 10 -i pipe:0 \
  -vf "setpts='if(lt(N,3),N,if(lt(N,5),N+7,N+9))'" -vsync vfr \
  -c:v libx264 -pix_fmt yuv420p -bf 0 -crf 18 edit-vfr.mp4
```

Check packet PTS and durations with `ffprobe -select_streams v -show_packets
edit-vfr.mp4`. The edited test joins a still-red span with a doubled-speed
green/blue span and checks the short blue frame immediately before the cut.

## Windows HEVC capability fixture

`hevc-main.mp4` contains two synthetic green 64 × 64 frames at 5 fps, with no
audio or captured user content. It is an 8-bit HEVC Main stream tagged `hvc1`.
The Windows capability test checks native playback independently of the
installed encoder: a GPU encoder alone must not enable HEVC export when the
Windows HEVC decoder is missing.

Generated with FFmpeg 9.0.2/libx265:

```bash
ffmpeg -f lavfi -i 'color=c=green:s=64x64:r=5' -frames:v 2 -an \
  -c:v libx265 -preset ultrafast \
  -x265-params 'log-level=error:pools=1:frame-threads=1' \
  -pix_fmt yuv420p -tag:v hvc1 -map_metadata -1 hevc-main.mp4
```

## Redaction interval fixtures

`redaction-30fps.mp4` and `redaction-5fps.mp4` contain two seconds of synthetic
white 64x64 video, without audio. They test masks at accelerated output frame
boundaries without relying on a live capture encoder. Generate with:

```sh
ffmpeg -f lavfi -i 'color=c=white:s=64x64:r=30:d=2' -c:v libx264 -pix_fmt yuv420p -an redaction-30fps.mp4
ffmpeg -f lavfi -i 'color=c=white:s=64x64:r=5:d=2' -c:v libx264 -pix_fmt yuv420p -an redaction-5fps.mp4
```
