# Region Recording and Long Screenshot Roadmap

> Deferred: long screenshot development continues on `dev-win`. It is excluded from `dev-3` and PR #130.

> 2026-10-04 audit: Phases 1–3 and 4a have implementations on dev-3. Phase 4b integration and remaining validation are tracked in [dev-3 completion](2026-10-04-dev-3-completion.md). Native corpus, cross-platform and 5K/6K acceptance must not be inferred from source completion.

> This is the roadmap for the whole effort. Phase 1 has a detailed, executable plan in `2026-10-02-recording-preview-crop.md`. Phases 2-4 are only broken down into tasks and interfaces here. Each phase gets its own detailed plan once the phase before it has merged. That way the details are based on the code as it actually stands.

**Goal:** Region recording that needs no selection before recording starts, and a long screenshot (scrolling capture) built on top of it.

**Specification ownership:** This document contains the cross-phase specification and decisions as of 2026-10-02. Together with the repository-local Phase 1 plan, it is sufficient to implement the effort; no external document is required.

## Design rationale and Phase 1 contract

Previous scrolling-capture approaches coupled capture with immediate, one-direction stitching. This design records first, solves positions globally offline, and uses reverse scrolling as additional evidence. Uncertain joins must be reported rather than guessed. Build the evaluation harness and real-recording corpus before the longshot UI.

Phase 1 provides independently useful recording edits:
- Full-screen capture, then free-aspect crop in Preview; no pre-recording selection.
- Crop state uses even-aligned video pixels, with a 64-pixel minimum side or the smaller even-floored frame dimension. Full frame means no crop.
- First crop entry has an empty draft so dragging inside the video draws a selection. Existing selections support moving, handle resizing, replacement from outside, and bounded snapping to video edges and centre lines.
- Enter applies a draft; Escape cancels it. The committed-size chip includes a clear action. Playback and scrubbing remain available. Save button and shortcut both apply the visible draft before exporting.
- MP4 edits use `IVideoTranscoder`; audio must be preserved or export fails with the source intact. GIF/WebP crop frames on their existing paths; Windows retains its player fallback.
- Output validation precedes source deletion. Audio tests must cover failure as well as non-silent, non-packet-aligned trims.
- Preset aspect ratios, window snapping and intermediate quality are excluded from Phase 1. The local Phase 1 plan defines its detailed implementation and regression tests.

## Global Constraints

**Capture and UI**
- Recording always captures the full screen. Region choice happens after recording, in RecordingPreview.
- Do not touch Region Selector.
- `showPreview = false` (direct save) behaviour stays unchanged in every phase.

**Platforms**
- macOS 14+ and Windows 10+. Linux beta keeps recording hidden; new code compiles there but stays inactive.

**Coordinates and data**
- All rects that cross from recording to Preview are in **video pixels**. Recorder-side code converts from logical/DPR coordinates at capture time; Preview never handles DPI.
- Sidecar data never records window titles, and it never reaches final outputs or History.

**Engineering**
- Write the evaluation harness before UI (phase 4).
- When stitching cannot be done confidently, report it. Never guess a join.

## Order and dependencies

| Step | Depends on | Notes |
| --- | --- | --- |
| Pre-check: record on a 5K/6K display | none | Validate actual dimensions, fps and encoder capabilities for recording and export before phase 3. If H.264 fails, evaluate HEVC and output scaling as a complete pipeline; do not automatically switch the intermediate codec. |
| Independent: Windows MF rate-control mode | none | `MediaFoundationEncoder` only sets `MF_MT_AVG_BITRATE`, and the default may be CBR. This can hurt current recordings. |
| Phase 1: Preview crop + `IVideoTranscoder` | none | detailed plan exists |
| Phase 2: window timeline + snapping | 1 | |
| Phase 3: intermediate quality + smart save | 1, pre-check | |
| Phase 4: long screenshot | 1, 3 (2 optional) | |
| Follow-up: remove `VideoTrimmer` | 1 | after phase 1 ships; update `tests/Video/tst_VideoTrimmerSafety.cpp` and `tst_MediaFoundationPipeline::mp4TrimRejectsAudioInput` |

---

## Phase 2: Window timeline and snapping

**Outcome:** In crop editing mode, hovering highlights the topmost window at the current playhead, a click snaps the crop to it, and a drag still draws a free rect.

**Interfaces**

```cpp
struct WindowSample {
    quint32 windowId;
    QRect rect;            // video pixels, clipped to the recorded screen
    int z;                 // 0 = topmost
    QString ownerApp;      // no window titles
    ElementType type;      // from WindowDetector.h
};
struct WindowTimelineEntry { qint64 tMs; std::vector<WindowSample> windows; };  // media timeline time

class WindowTimeline {                        // pure data, snaptray_core
public:
    void append(qint64 tMs, std::vector<WindowSample> windows);   // drops unchanged entries
    const std::vector<WindowSample>* windowsAt(qint64 tMs) const; // last entry with t <= tMs
    std::optional<WindowSample> hitTest(const QPoint& videoPoint, qint64 tMs) const;
    QByteArray toJson() const;
    static std::optional<WindowTimeline> fromJson(const QByteArray&);
};

class WindowTimelineRecorder : public QObject { // owned by RecordingManager
public:
    using Enumerator = std::function<std::vector<DetectedElement>()>;  // injectable for tests
    WindowTimelineRecorder(Enumerator, QRect logicalScreen, QRect physicalScreen, qreal dpr);
    void start(); void pause(); void resume(); void stop();
    void setMediaTimeMs(qint64);                 // fed from RecordingManager's media clock
    WindowTimeline timeline() const;
};
```

**Tasks**
1. `WindowTimeline`: delta compaction, `windowsAt`, `hitTest` (z-order), and a JSON round-trip, with table-driven tests.
2. Logical to video-pixel conversion helper. Tests cover Retina, Windows 125% and 150%, a screen origin that is not at (0,0), and windows that span screens and are clipped. Reuse the physical mapping used by `RecordingRegionNormalizer` and the capture engines.
3. `WindowTimelineRecorder`:
   - Samples immediately at recording start, then every 250 ms on a background thread, using `WindowDetector` with `QueryMode::TopLevelOnly`.
   - Excludes SnapTray's own windows, using the recording excluded-window list.
   - Pauses with the recording; countdown time is excluded.
   - Tests use a fake enumerator and a fake clock.
4. Sidecar lifecycle:
   - Write `<temp>.mp4.windows.json` on stop.
   - Delete it alongside the temp MP4 on save and on discard.
   - Add `SnapTray_Recording_*.windows.json` to the filters in `RecordingManager::cleanupStaleTempFiles()`.
5. Preview:
   - The backend loads the sidecar and exposes `Q_INVOKABLE QRectF windowRectInViewAt(QPointF viewPoint, QRectF contentRect)` and `QString windowAppAt(...)`.
   - `RecordingCropOverlay` gets hover highlighting, and a click-versus-drag threshold.
   - A missing or corrupt sidecar falls back to free-rect cropping.
6. Translations and docs.

**Deferred:** child-control snapping (if done later, it will likely be image-based in Preview) and a timeline indicator showing stable ranges.

---

## Phase 3: Intermediate quality and smart save

**Outcome:** When Preview is enabled, the temp recording is a high-quality intermediate. Saving with no edits either moves the file or re-encodes it to the user's quality, whichever is better.

**Rules**

| `showPreview` at record start | Temp file quality | On save |
| --- | --- | --- |
| on | intermediate (constant quality, 1 s keyframes) | edits → `IVideoTranscoder`; no edits → smart rule |
| off | user quality (unchanged) | unchanged |

**Output quality contract**
- Read user quality through `RecordingSettingsManager` and snapshot it when Save begins. Every MP4 re-encode (trim-only, crop-only, combined edits, and no-edit smart save) explicitly sets `videoBitrate = VideoBitrate::forQuality(outputSize, sourceFps, userQuality)` for H.264.
- `outputSize` is the normalized crop size, or the full video size without crop, adjusted for any explicit scaling policy validated below. Probe source fps; extend `VideoFileProbe` accordingly in this phase.
- Phase 1's default quality 80 is only a legacy fallback and must not silently override the user's setting in Phase 3. Preserve source audio on every path.

The smart rule for no-edit saves:
- If the intermediate is compatible with the approved output codec/dimensions policy and its average bitrate is at or below the user's target bitrate, move the file.
- Otherwise, re-encode with `IVideoTranscoder`, explicitly setting that target bitrate. Codec changes require a separately validated bitrate policy; do not compare HEVC against the H.264 formula and treat the results as equivalent quality.

**Tasks**
1. Add an `EncoderConfig` / `IVideoEncoder` "intermediate" option (for example `setRateControl(RateControl::ConstantQuality, qualityValue)`) together with `setKeyFrameIntervalSeconds(int)`.
   - macOS: VideoToolbox quality (`AVVideoQualityKey` / `kVTCompressionPropertyKey_Quality`) with a high bitrate ceiling.
   - Windows: `CODECAPI_AVEncCommonRateControlMode = Quality` and `CODECAPI_AVEncCommonQuality`.
   - Fallback: VBR at about 0.5-0.6 bpp with an ~100 Mbps cap.
   - First verify support per platform and macOS version.
2. `RecordingManager` picks the intermediate config when `showPreview` is on at start.
3. Free-space check at start: if `QStorageInfo` free space is below the estimated per-minute size times a named minimum duration, fall back to user quality and show a warning.
4. Apply the output quality contract to all MP4 export branches, including `performTranscode()`, then add the no-edit smart-save decision:
   - Extend `IVideoTranscoder::probe` to report source fps and codec. Use file size and valid nonzero duration for a conservative total average bitrate estimate; do not treat it as a video-only bitrate measurement.
   - When re-encoding, show the existing processing overlay. Validate output before removing the source.
   - Tests cover low/high user quality for trim-only, crop-only, combined edits and no-edit re-encode. Assert requested bitrate uses final output dimensions, and verify the no-edit move branch and unchanged direct-save path.
5. Change the Settings "Quality" description to mean the output file's quality, plus translations and docs. Add no new user setting for intermediate quality.

**High-resolution capability gate (before Phase 3, not a Phase 1 blocker)**
- Keep H.264 until the 5K/6K pre-check establishes a real limitation. Test actual recording dimensions and fps on each platform; do not infer complete support from a resolution label or hardware generation.
- Test recording, Preview playback, unedited save, full-resolution trim-only, crop-only, and smart-save re-encode, including audio. Record chosen codec, decoded/output dimensions, and failures.
- If H.264 fails, evaluate HEVC availability and decoding plus an explicit final output codec/scaling policy. An HEVC intermediate alone is insufficient: the Phase 1 transcoder still emits H.264 at the requested dimensions.
- Any selected codec/scaling policy must extend the transcoder request/probe and bitrate policy, pass the entire matrix, and document visible output changes. Do not silently reduce resolution or assume a hardware HEVC encoder exists. Until validated, keep the limitation explicit and fail unsupported exports with the source retained.

---

## Phase 4: Long screenshot from a recording

**Outcome:** RecordingPreview gets a "Long Screenshot" output. It takes the trim range and crop rect and solves a tall PNG offline. Back-and-forth scrolling is handled through global position solving, and break points are reported instead of guessed.

**Scope:**
- Vertical only, one main scroll region.
- The user scrolls by hand while recording.
- Recordings contain no cursor (SCK `showsCursor = NO`; DXGI does not draw the pointer).

**Modules (`src/longshot/`)**

```cpp
class LongshotFrameSource {           // macOS: IVideoFrameReader; Windows: IVideoTranscoder's MF Source Reader decode side
public:
    virtual bool open(const QString& path, qint64 startMs, qint64 endMs, QRect crop) = 0;
    virtual std::optional<QImage> next(qint64* tMs) = 0;  // sequential decode
};

struct StaticBands { int top; int bottom; int left; int right; }; // excluded crop-local pixels
struct FrameFeatures {
    qint64 tMs;
    std::vector<float> rowMean;
    std::vector<float> rowGradient;
    bool stationary;
    bool keyFrame;
    StaticBands excludedBands; // per-frame result; never one global mask for the recording
    QRect validContentRect;    // crop-local coordinates, same origin used by matching/rendering
};
struct PairShift { int from; int to; int dy; double confidence; };

class LongshotAnalyzer {              // pass 1
public:
    std::vector<FrameFeatures> extract(LongshotFrameSource&);
    std::optional<PairShift> estimateShift(const FrameFeatures&, const FrameFeatures&, /*full-res refine*/ ...);
    // Resolve pair observations into per-frame bands/validContentRect in extract().
    // Uncertain masks must reduce confidence or reject a match, never invent content.
    StaticBands detectStaticBands(...);
};

struct SolveResult { std::vector<std::optional<int>> positions; std::vector<qint64> breakTimesMs; };
class PositionSolver {                // pure; weighted least squares + loop-closure edges + outlier rejection
public:
    static SolveResult solve(const std::vector<qint64>& frameTimesMs,
                             const std::vector<PairShift>& edges, double maxResidualPx);
};

struct LongshotOptions { bool includeStickyHeader = false; int maxHeightPx = 30000; int tileRows = 64; };
class LongshotRenderer {              // pass 2
public:
    // Uses each frame's validContentRect/excludedBands; positions retain crop-local origin.
    QImage render(LongshotFrameSource&, const std::vector<FrameFeatures>&, const SolveResult&,
                  const LongshotOptions&);
};
```

**Coordinate and mask contract**
- Recompute fixed-region observations per frame pair, then retain the resolved per-frame bands and valid content rectangles in Pass 1 results. A header that appears only while scrolling up must not become a global top crop.
- Removing a header for matching does not reset the coordinate origin. Pair shifts and solved positions refer to the original crop-local frame origin; include mask offsets when comparing extracted patches.
- Rendering samples only valid pixels from the selected frame. Derive the common output column span from the contributing frames; if a tile has no reliable coverage, report a break instead of filling it by guesswork.
- Include a sticky header once at the top only when requested and a valid source observation exists. Report any automatic side crop in the result view.

**Cache contract**
- A bounded decode cache is separate from crop-dependent analysis. Reuse decoded source frames only when their source identity and timestamps match; do not retain all full-resolution frames in RAM.
- Analysis cache keys include source identity, normalized crop, and analysis version/options. Changing crop invalidates features, masks, shifts, positions and rendered tiles. Never reuse old crop-dependent results under a new crop.
- A trim change may reuse unchanged per-frame base features within the overlap. Recompute boundary/pair-dependent masks and edges as needed, drop out-of-range observations, and rerun global solving and rendering; extending the interval analyzes newly included frames.
- Rendering-only options may reuse compatible analysis but invalidate output tiles. Test every invalidation path.
- Feature and graph storage grows with analyzed frames; use a named memory budget and disk-backed cache or bounded sampling for long recordings. Small features do not make total memory constant.

**Tasks (in this order; the UI comes last)**
1. `PositionSolver`, with pure tests:
   - chains, loop closures, an outlier edge that must be rejected
   - disconnected islands reported as break times
   - integer rounding
2. Synthetic harness `tests/Longshot/`:
   - Start from a tall ground-truth image.
   - Simulate scroll trajectories: constant speed, flings, back-and-forth, pauses.
   - Overlay disturbances: a sticky header, a header that appears only when scrolling up, a sidebar, hover changes, lazy-load insertion.
   - **Encode with the real native encoder at intermediate quality.**
   - Compare the result row by row against ground truth, counting duplicated, missing and misaligned rows.
3. `LongshotAnalyzer`:
   - Row features; 1D phase correlation for coarse candidates; full-res NCC refinement.
   - Confidence is the best score plus its margin over the runner-up; an ambiguous match is rejected.
   - Static-band observations are recomputed for every frame pair and resolved into each frame's masks; retain them for rendering. Test an appearing/disappearing header without deleting scrolling rows or duplicating the header.
   - Vertical strip analysis finds the moving column span.
4. Global solve wiring:
   - Chain edges plus loop-closure edges for frames whose positions overlap but whose times are not adjacent.
   - Try to rejoin islands with a global search.
   - Otherwise report the break and export every reliably connected island as an independent section (2026-10-04 extension). Order sections by first recorded frame, never infer a join, and keep isolated frames reported rather than promoting them to a stitched section.
5. `LongshotRenderer`:
   - Picks one source frame per 64-row tile, scoring stationary frames, keyframes and tile position mid-frame higher, and rejecting frames that disagree with the majority.
   - Places seams on low-gradient rows; on conflicts, later observations win.
   - Sticky header is off by default; static side columns are auto-cropped.
   - PNG output, with the ~30000 px cap and an option to split.
6. Real-recording corpus of 20-30 clips (web, Slack/LINE, IDE, tables, settings, PDF), using Chrome DevTools full-page captures as ground truth for web pages, plus a dev-only evaluation CLI.
7. Windows frame source: expose the decode half of `MediaFoundationTranscoder` as a sequential reader.
8. Preview UX:
   - A "Long Screenshot" format; a hint to crop the scroll area (with window snap from phase 2 if available).
   - Progress for analyse, solve and render.
   - A result view with low-confidence and break markers, where clicking a marker seeks the video.
   - Save PNG, Copy, Pin, Back. Re-runs reuse only compatible cache entries under the cache contract; crop edits trigger fresh analysis and trim edits trigger a fresh solve.
   - The result view notes what was auto-cropped.
9. Translations and docs.
