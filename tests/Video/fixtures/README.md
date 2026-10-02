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
