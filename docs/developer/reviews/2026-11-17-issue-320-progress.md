# Recording roadmap #320 implementation and review

Branch: `codex/issue-320-recording-roadmap`, based on `9829ed34`.
Reviewed November 17–18, 2026. This report distinguishes implemented behavior,
automated evidence, and hardware acceptance that cannot be established on Linux.

## Scope

The user explicitly deferred **#334 (local captions)**. Automatic zoom on a new
recording defaults to off. Issues #321, #322, #323, #326 and #338 were already
merged in the starting main; this change retains those implementations.

| Issue | Implementation and principal regression coverage |
| --- | --- |
| #324 | Pure interaction classifier, typing/menu holds, cut/speed-aware grouping, actual insertion counts and four result reasons; threshold, cut, conflict and performance tests |
| #325 | SettingsManager option in recording setup/settings; one-shot fresh-recording eligibility after crop initialization; undo/redo and dismiss/disable notice; Library/recovery exclusion tests |
| #327 | Optional standard cursor kind in platform probes and sidecars; cached SVG rendering above 1.25× with recorded bitmap fallback; hotspot, legacy and custom-cursor tests |
| #328 | Shape debounce with click/visibility protection and bounded path straightening before spring smoothing; drag, gap endpoints, seek determinism and 72,000-sample tests |
| #329 | Shared output-time camera/pointer return in the final kept part for GIF/WebP; hidden/cropped endpoints, Still End, cut/speed and actual output tests; MP4 unchanged |
| #330 | Deterministic 4/8-view temporal sampling during camera movement; masking precedes blur and cursor/overlays follow it; static identity, crop/mask tests and real export benchmark |
| #331 | Shared streaming PCM16 WSOLA, exact per-part lengths and seam fades, default Keep sound option; all-speed pitch/chunk/seam tests and speed-only legacy preview cache |
| #332 | Native bounded audio import into project-owned PCM, clip editing/history, source-relative mixing and preview/export; deletion of original file, reopen and decoded export tests |
| #333 | Shared screenshot annotation renderers, six shapes, content/screen coordinates, draw/move/resize/time/fade editing and undo; coordinate, masking, QML interaction and MP4/GIF/WebP tests |
| #335 | Atomic previous valid manifest, corruption fallback, visible unreadable entries, raw recovery, open-project cleanup protection and backup asset retention; corruption/I/O/save/cleanup tests |
| #336 | Independent GIF/WebP size/FPS/quality/loop controls, frame sampling, GIF identical-frame coalescing, exact final hold and actual output size warnings; encoder/options tests |
| #337 | WGC default, sparse keep-alive plus settling delivery, nanosecond encoder boundary, final sample duration and CPU/GPU BT.709 metadata; worker/clock/MF sparse CPU/GPU fixtures and Windows sync-matrix rows |
| #339 | WASAPI event delivery, MMCSS, asynchronous startup, bounded bad-timestamp fallback, 5 ms gap fades and per-source diagnostics; mixer, startup and diagnostic tests |
| #340 | DXGI worker push delivery, QPC present timestamps, pointer-only suppression, copy/drop counters, bounded recovery and compatibility status; timing/backoff/lifecycle tests and encoder device-manager recovery |

Optional ducking (#332) and automatic default-output-device migration (#339)
remain follow-ups, as expressly allowed by those issues. Captions are absent.
The 13 pointer SVGs are original MIT SnapTray drawings; provenance is recorded in
`resources/recording-cursors/README.md`. No Recordly source or assets were copied.

## Review findings fixed before submission

- Preserve the last valid manifest and assets referenced by its backup, including
  after a failed/corrupt save; permission failures must not silently fall back.
- Keep auto-zoom classification and grouping inside visible crop/kept parts;
  report inserted segments after manual conflicts, and distinguish unusable scale.
- Preserve both cursor endpoints around a sample gap; do not straighten the held
  endpoint toward the next distant sample.
- Commit pending numeric shape/audio inspector fields before export or selection;
  invalid input keeps focus and blocks the action.
- Rebuild legacy mixed-audio preview caches for speed-only edits, preserving
  decoded packet sample positions instead of concatenating across source gaps.
- Count imported audio in export validation, even when the source has no audio.
- Split long GIF holds and validate each input timestamp against the preceding
  input, including when identical frames have been coalesced.
- Retain a real DXGI picture that arrives behind a keep-alive timestamp. A single
  settling delivery after the last change prevents the frame limiter from hiding
  a final update until the next two-second keep-alive; continuous motion does not
  cause a second full-rate stream.
- Check GDI capture failure, expose initial compatibility state, reset the MF
  device manager when a lost GPU is replaced, and report unrecoverable encode
  errors instead of silently losing the rest of the recording.
- Reject future audio timestamps without poisoning the next contiguous fallback;
  include all trimming paths in per-source diagnostics.
- Include the GPU readback declaration in both Windows capture engines; the
  first native Windows CI build identified the missing WGC include.
- Replace the new raw QML cursor enum with the shared cursor token required by
  the repository's cursor guard.
- Restore loop-return settings after output tests so later export cases are
  independent of their execution order.
- Run Linux preview export tests with the existing isolated PulseAudio fixture;
  hosted CI has no default output device, but still exercises real playback.
- Warm codec/device startup before the long audio comparison. This exposed a
  15.89% processing overhead hidden by the first cold export. Overlap Linux AAC
  encoding with video work using a single bounded 100 ms task; keep all muxer
  access on the owner thread and join before abort, failure or final flush.
  Compare synchronous/asynchronous AAC payloads, timestamps and side data.
- Accept the native resampler's one-sample rounding difference, while rounding
  imported clip coverage up to include its final partial millisecond. Verify
  that the final stored sample survives and the next sample is silent.

## Verification

Linux: Ubuntu 22.04, Qt 6.12.0, GCC 11.4, Debug application and tests; performance
measurements use the Release build. Canonical scripts are run with
`PATH=/snap/cmake/1534/bin:$PATH`; desktop/audio integration uses host X11/PulseAudio.

Main advanced to `29a807e6` during the work. After integrating it, the canonical
application build passed and the complete suite passed **262/262 tests**, with
zero failures in **567.54 seconds**. After the asynchronous AAC and isolated
audio-fixture fixes, the full suite again passed **262/262**, in **540.38 seconds**.
Focused tests cover cursor gap/settling timing,
numeric inspector commit, legacy audio preparation, imported audio persistence
and export, and actual shape/loop-return outputs in all three formats. The
optimized WSOLA Release suite passed all 16 cases, including pitch, chunk
independence and seam alignment. Native CI results are recorded separately below.

All 93 new UI context/source pairs have nonempty translations in each of the
24 language catalogs. XML, duplicate keys and substitution placeholders were
checked, and the application build compiles the catalogs. A 1024×768 rendered
shape-editor screenshot was inspected for layout and handle placement.

Release measurements on this Linux host (synthetic, reproducible fixtures):

| Workload | Measurement |
| --- | --- |
| Auto Zoom, 10 minutes / 300 clicks / 72,000 samples | 7.794 ms; requirement < 50 ms |
| Cursor spring + steady shape + High straightening, 10 minutes / 120 Hz | 10.099 ms; requirement < 30 ms |
| 1080p transition frame, blur Off / Low / High | 2.035 / 37.176 / 60.906 ms |
| Additional transition-frame cost, Low / High | 35.141 / 58.871 ms |
| 10-second 1080p export, four camera transitions, Off / Low / High | 2567.3 / 5569.2 / 6481.7 ms (1.00× / 2.17× / 2.52×) |
| 10-minute 1080p/30 fps source, all 2×, Keep sound Off / On / Off after warm-up | 59,590.7 / 60,406.6 / 59,130.7 ms; **1.76%** above the bracketed baseline (< 5%) |

The blur benchmark used CPU libx264 because no CUDA encoding device was available.
High blur is intentionally more expensive and remains opt-in; preview uses the
selected quality, with no unlabelled quality reduction.

The optional long audio benchmark uses `SNAPTRAY_LONG_AUDIO_BENCHMARK=1` and
`QTEST_FUNCTION_TIMEOUT=900000` when invoking
`release/bin/Video_VideoTranscoder longPitchExportBenchmark`. It generates a
600-second 1080p/30 fps source, warms the codec/device initialization with a short
export, then brackets Keep sound on with two off exports at 2×.
`SNAPTRAY_AUDIO_BENCHMARK_INPUT` can reuse that source and its two PCM
sidecars; dimensions, duration and frame rate are checked before measurement.
Run without concurrent builds for meaningful timing. The shorter blur benchmark
uses `SNAPTRAY_ROADMAP_BENCHMARK=1` and `cameraBlurExportBenchmark`.

## Remaining hardware acceptance evidence

Windows/macOS native compilation and hosted CI results are recorded separately
from Linux verification. A hosted build is not a substitute for device testing.
No Windows 10/11 UAC, physical GPU removal, multi-driver color/readback benchmark,
headphone waveform inspection, CPU-saturation recording, or cross-player manual
seek has been performed on this Linux workstation. Likewise, the requested three
real-recording Auto Zoom comparisons and three-platform voice listening tests
are not claimed as completed. The long audio-export benchmark above is synthetic.
The reproducible manual scenarios are in `docs/developer/recording-sync-matrix.md`.
