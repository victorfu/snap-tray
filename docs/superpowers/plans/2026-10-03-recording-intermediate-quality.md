# Recording Intermediate Quality and Smart Save Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When Preview is on, record to a high-quality intermediate MP4 (constant quality where the platform supports it, 1 s keyframes, bounded by free disk space), and make every MP4 export from the preview — trim, crop, both, or an unedited save — produce a file at the user's configured quality, moving the intermediate untouched only when it is already small enough.

**Architecture:** A header-only `SnapTray::IntermediateQuality` policy module (core) holds every number and decision: the intermediate bitrate ceiling, the disk-space floor, the output bitrate for a given size/fps/quality, and the smart-save "move or re-encode" rule. `IVideoEncoder` gains a rate-control mode and keyframe interval that `EncoderFactory` applies from `EncoderConfig`; `RecordingInitTask` maps a single `intermediateQuality` flag onto those fields and `RecordingManager` sets the flag from `showPreview` plus a disk-space check. `IVideoTranscoder::probe` learns the source frame rate and codec so `RecordingPreviewBackend` can set an explicit `videoBitrate` on every re-encode and decide, inside its existing export worker, whether an unedited save moves or re-encodes.

**Tech Stack:** Qt 6.11 (C++17, Qt Quick), Media Foundation (`IMFSinkWriter`, `ICodecAPI`) on Windows, AVFoundation (`AVAssetWriter`) on macOS, Qt Test; CMake + Ninja.

**Spec:** `docs/superpowers/plans/2026-10-02-region-recording-longshot-roadmap.md`, sections "Global Constraints", "Order and dependencies" and "Phase 3: Intermediate quality and smart save". Phase 1 (`2026-10-02-recording-preview-crop.md`) is merged and provides `IVideoTranscoder`, `VideoTranscodeRequest::videoBitrate` and the preview export worker this plan extends.

## Global Constraints

- Recording always captures the full screen. Region choice happens after recording, in RecordingPreview.
- Do not touch Region Selector.
- `showPreview = false` (direct save) behaviour stays unchanged in every phase.
- macOS 14+ and Windows 10+. Linux beta keeps recording hidden; new code compiles there but stays inactive.
- All rects that cross from recording to Preview are in **video pixels**. Preview never handles DPI.
- Sidecar data never records window titles, and it never reaches final outputs or History.
- Phase 3 rules table: `showPreview` on → temp file is intermediate (constant quality, 1 s keyframes); on save, edits → `IVideoTranscoder`, no edits → smart rule. `showPreview` off → user quality, unchanged on save.
- Output quality contract: read user quality through `RecordingSettingsManager` and snapshot it when Save begins. Every MP4 re-encode explicitly sets `videoBitrate = VideoBitrate::forQuality(outputSize, sourceFps, userQuality)` for H.264. `outputSize` is the normalized crop size, or the full video size without crop. Probe source fps; extend `VideoFileProbe` accordingly. Quality 80 (`kDefaultTranscodeQuality`) is only a legacy fallback and must not silently override the user's setting. Preserve source audio on every path.
- Smart rule for no-edit saves: move the file if the intermediate is H.264 at the same dimensions and its average bitrate is at or below the user's target bitrate; otherwise re-encode with `IVideoTranscoder`, explicitly setting that target bitrate. Do not compare HEVC against the H.264 formula.
- Intermediate rate control: macOS VideoToolbox quality / Windows `CODECAPI_AVEncCommonRateControlMode = Quality` + `CODECAPI_AVEncCommonQuality`; fallback VBR at about 0.5–0.6 bpp with an ~100 Mbps cap. First verify support per platform. Add no new user setting for intermediate quality.
- Free-space check at start: if `QStorageInfo` free space is below the estimated per-minute size times a named minimum duration, fall back to user quality and show a warning.
- Keep H.264 until the 5K/6K pre-check establishes a real limitation; no resolution scaling, no silent codec switch. Unsupported exports fail with the source retained.

### Platform decision recorded by this plan

The spec asks to "first verify support per platform". This plan can only verify Windows in the executing environment. Decisions:

- **Windows:** request `eAVEncCommonRateControlMode_Quality` + `CODECAPI_AVEncCommonQuality` through `ICodecAPI` obtained from the sink writer; if the encoder MFT rejects either call, fall back to VBR with the intermediate bitrate ceiling. The encoder reports which mode took effect (`effectiveRateControl()`), and Task 4's test asserts the recording succeeds in either case and that the 1 s keyframe interval is honoured.
- **macOS:** `AVVideoQualityKey` under `AVVideoCompressionPropertiesKey` is documented for JPEG/ProRes, not H.264, and cannot be validated on the executing host. The macOS intermediate therefore uses the spec's fallback (VBR at 0.55 bpp, 100 Mbps cap, 1 s keyframes via `AVVideoMaxKeyFrameIntervalKey`) and `effectiveRateControl()` reports `Bitrate`. Task 5 writes the VideoToolbox-quality path behind a compile-time constant that stays off, plus the macOS test that must be run on a Mac before release. This is recorded in the CHANGELOG entry wording ("high quality") rather than "constant quality".

## Review Focus

1. A probed source whose frame rate is 0 or variable (screen recordings with dropped frames) — the output bitrate must fall back to a named default frame rate, never divide by zero or request 0 bps. → Task 2 `effectiveFrameRate` rows.
2. An unedited save of a zero-byte, truncated or unprobeable temp file — the smart rule must choose re-encode (which then fails safely with the source retained), never "move" a broken file as if it were final. → Task 2 `smartSaveShouldMove` rows "invalid probe" and "zero bytes".
3. A hardware H.264 MFT that rejects Quality mode — recording must still start and finish, at VBR, with a warning in the log, not fail. → Task 4 test accepts either effective mode and asserts a valid output.
4. A user quality stored outside 0–100 (the settings manager does not clamp) — `forQuality` clamps, and the smart-save target must use the clamped value. → Task 2 row "quality above 100".
5. A recording made before this change (user-quality temp file) saved unedited after upgrading — its bitrate is at or below the target, so it must be moved, not re-encoded and degraded. → Task 7 `smartSaveMovesLowBitrateRecording` (a user-quality fixture).

---

## File Structure

| File | Responsibility |
| --- | --- |
| `include/encoding/VideoRateControl.h` (create) | `enum class VideoRateControl { Bitrate, ConstantQuality }` shared by encoder interface, factory config and policy header. |
| `include/encoding/IntermediateQuality.h` (create, header-only) | All Phase 3 numbers and pure decisions: intermediate bitrate, bytes per minute, disk-room check, average bitrate, output bitrate, smart-save rule. |
| `include/video/IVideoTranscoder.h` (modify) | `VideoFileProbe` gains `frameRate` and `videoCodec`. |
| `src/video/MediaFoundationTranscoder_win.cpp`, `src/video/AVFoundationTranscoder_mac.mm` (modify) | Fill the two new probe fields. |
| `include/IVideoEncoder.h` (modify) | `setRateControl`, `setKeyFrameIntervalSeconds`, `effectiveRateControl`. |
| `include/encoding/EncoderFactory.h`, `src/encoding/EncoderFactory.cpp` (modify) | `EncoderConfig::rateControl`, `keyFrameIntervalSeconds`; factory applies them. |
| `src/MediaFoundationEncoder.cpp` (modify) | `ICodecAPI` rate control + GOP; intermediate bitrate ceiling. |
| `src/AVFoundationEncoder.mm` (modify) | Keyframe interval + intermediate bitrate ceiling; VideoToolbox quality path behind a constant. |
| `include/RecordingInitTask.h`, `src/RecordingInitTask.cpp` (modify) | `Config::intermediateQuality` → encoder config. |
| `include/RecordingManager.h`, `src/RecordingManager.cpp` (modify) | Disk-space policy, warning, flag into init config. |
| `include/qml/RecordingPreviewBackend.h`, `src/qml/RecordingPreviewBackend.mm` (modify) | Quality snapshot at save, explicit `videoBitrate`, smart save inside the export worker. |
| `src/qml/settings/RecordingSettings.qml` (modify) | Caption explaining the quality setting is the output quality. |
| `tests/Encoding/tst_IntermediateQuality.cpp` (create), `tests/Encoding/tst_MediaFoundationEncoderIntermediate_win.cpp` (create), `tests/Encoding/tst_AVFoundationEncoderIntermediate_mac.mm` (create) | New tests. |
| `tests/Encoding/FakeAudioEncoder.h`, `tests/Encoding/tst_EncoderFactory.cpp`, `tests/Video/tst_VideoTranscoder.cpp`, `tests/RecordingManager/tst_InitTask.cpp`, `tests/RecordingManager/tst_Lifecycle.cpp`, `tests/Qml/tst_RecordingPreviewExport.cpp`, `tests/Settings/tst_QmlTranslations.cpp`, `tests/CMakeLists.txt` (modify) | Extended tests and targets. |
| `translations/snaptray_*.ts` (24 files), `docs/docs/recording.md`, `docs/zh-tw/docs/recording.md`, `CHANGELOG.md` (modify) | Copy. |

Build helper (Windows, MSVC not on PATH): `& cmd.exe /c "`"C:\Users\Victor\AppData\Local\Temp\claude\D--Documents-snap-tray\104aec7a-7d62-409a-ac94-46fade8841b2\scratchpad\snap-vsenv.bat`" cmake --build build --target <Target>"` from PowerShell; test executables are in `build\bin\` and must be run with `-o <file>,txt` because their stdout is not captured. The worktree is `D:\Documents\snap-tray\.claude\worktrees\recording-intermediate-quality`; configure `build/` there first with the same generator the sibling worktrees use (`cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` through the helper).

---

### Task 1: Probe reports source frame rate and codec

**Files:**
- Modify: `include/video/IVideoTranscoder.h` (`VideoFileProbe`)
- Modify: `src/video/MediaFoundationTranscoder_win.cpp:587-633` (`MediaFoundationTranscoder::probe`)
- Modify: `src/video/AVFoundationTranscoder_mac.mm:279-303` (`AVFoundationTranscoder::probe`)
- Test: `tests/Video/tst_VideoTranscoder.cpp` (`probeReportsFixture`)

**Interfaces:**
- Consumes: existing `VideoFileProbe`, `IVideoTranscoder::probe`.
- Produces: `VideoFileProbe::frameRate` (`double`, 0.0 when unknown) and `VideoFileProbe::videoCodec` (`QString`, `"h264"`, `"hevc"` or empty). Tasks 2 and 7 read both.

- [ ] **Step 1: Extend the failing test**

In `tests/Video/tst_VideoTranscoder.cpp`, add two assertions at the end of `probeReportsFixture()` (after `QVERIFY(probe.hasAudio);`):

```cpp
    // The fixture is encoded at kFrameRate fps as H.264; Phase 3's output
    // bitrate contract needs both facts from the probe.
    QVERIFY2(qAbs(probe.frameRate - kFrameRate) < kFrameRateTolerance,
             qPrintable(QStringLiteral("probed fps %1").arg(probe.frameRate)));
    QCOMPARE(probe.videoCodec, QStringLiteral("h264"));
```

and add to the anonymous namespace constants (next to `kDurationToleranceMs`):

```cpp
constexpr double kFrameRateTolerance = 0.5; // nominal rates are exact on both platforms
```

- [ ] **Step 2: Run the test to verify it fails**

Build and run `Video_VideoTranscoder` with `-o <scratch>/t1.txt,txt`.
Expected: compile error `'frameRate': is not a member of 'VideoFileProbe'`.

- [ ] **Step 3: Add the fields**

In `include/video/IVideoTranscoder.h`, change `VideoFileProbe` to:

```cpp
struct VideoFileProbe {
    bool valid = false;
    QSize videoSize;
    qint64 durationMs = 0;
    bool hasAudio = false;
    qint64 audioStartMs = -1;
    qint64 audioEndMs = -1;
    double frameRate = 0.0;   // nominal fps of the first video track; 0 when unknown
    QString videoCodec;       // "h264", "hevc", or empty when unknown
};
```

Add, below `kAudioCoverageToleranceMs`:

```cpp
// Probe codec identifiers. The smart-save rule only trusts the H.264 bitrate
// formula, so anything else re-encodes.
constexpr auto kVideoCodecH264 = "h264";
constexpr auto kVideoCodecHevc = "hevc";
```

- [ ] **Step 4: Windows probe**

In `src/video/MediaFoundationTranscoder_win.cpp`, inside `probe()`, after `result.durationMs = hnsToMs(durationHns);`:

```cpp
    UINT32 fpsNumerator = 0;
    UINT32 fpsDenominator = 0;
    if (SUCCEEDED(MFGetAttributeRatio(videoType.Get(), MF_MT_FRAME_RATE, &fpsNumerator, &fpsDenominator))
        && fpsDenominator != 0) {
        result.frameRate = double(fpsNumerator) / double(fpsDenominator);
    }
    GUID subtype = GUID_NULL;
    if (SUCCEEDED(videoType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
        if (subtype == MFVideoFormat_H264) {
            result.videoCodec = QLatin1String(kVideoCodecH264);
        } else if (subtype == MFVideoFormat_HEVC) {
            result.videoCodec = QLatin1String(kVideoCodecHevc);
        }
    }
```

- [ ] **Step 5: macOS probe**

In `src/video/AVFoundationTranscoder_mac.mm`, inside `probe()`, after `result.durationMs = toMs(asset.duration);`:

```objc
        result.frameRate = video.nominalFrameRate;
        if (CMFormatDescriptionRef description =
                (__bridge CMFormatDescriptionRef)video.formatDescriptions.firstObject) {
            const FourCharCode codec = CMFormatDescriptionGetMediaSubType(description);
            if (codec == kCMVideoCodecType_H264) {
                result.videoCodec = QLatin1String(kVideoCodecH264);
            } else if (codec == kCMVideoCodecType_HEVC) {
                result.videoCodec = QLatin1String(kVideoCodecHevc);
            }
        }
```

- [ ] **Step 6: Run the test to verify it passes**

Build and run `Video_VideoTranscoder` again. Expected: all slots pass (the AAC-dependent ones may `QSKIP` on hosts without AAC; `probeReportsFixture` must pass, not skip, on Windows and macOS).

- [ ] **Step 7: Commit**

```bash
git add include/video/IVideoTranscoder.h src/video/MediaFoundationTranscoder_win.cpp src/video/AVFoundationTranscoder_mac.mm tests/Video/tst_VideoTranscoder.cpp
git commit -m "feat(video): report frame rate and codec from the transcoder probe"
```

---

### Task 2: Intermediate-quality policy header

**Files:**
- Create: `include/encoding/VideoRateControl.h`
- Create: `include/encoding/IntermediateQuality.h`
- Create: `tests/Encoding/tst_IntermediateQuality.cpp`
- Modify: `tests/CMakeLists.txt` (after the `Encoding_VideoBitrate` block, line ~587)

**Interfaces:**
- Consumes: `SnapTray::VideoBitrate::forQuality(QSize, int, int)`, `kMinBitrate` (`include/encoding/VideoBitrate.h`); `VideoFileProbe` from Task 1.
- Produces (namespace `SnapTray::IntermediateQuality`): constants `kConstantQualityValue = 85`, `kKeyFrameIntervalSeconds = 1`, `kIntermediateBitsPerPixel = 0.55`, `kIntermediateMaxBitrate = 100000000`, `kMinimumRecordingMinutes = 10`, `kFallbackFrameRate = 30`; functions `int intermediateBitrate(const QSize&, int frameRate)`, `qint64 estimatedBytesPerMinute(int bitrate)`, `bool hasRoomForIntermediate(qint64 freeBytes, const QSize&, int frameRate)`, `qint64 averageBitrate(qint64 fileBytes, qint64 durationMs)`, `int effectiveFrameRate(double probedFps)`, `int outputBitrateFor(const QSize& outputSize, double probedFps, int userQuality)`, `bool smartSaveShouldMove(const VideoFileProbe&, qint64 fileBytes, int userQuality)`. `enum class VideoRateControl { Bitrate, ConstantQuality }` in its own header. Tasks 3–7 use these names exactly.

- [ ] **Step 1: Write the failing test**

`tests/Encoding/tst_IntermediateQuality.cpp`:

```cpp
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoBitrate.h"
#include "video/IVideoTranscoder.h"

#include <QtTest>

using namespace SnapTray;

class tst_IntermediateQuality : public QObject
{
    Q_OBJECT
private slots:
    void intermediateBitrate_data();
    void intermediateBitrate();
    void bytesPerMinuteAndRoom();
    void averageBitrate_data();
    void averageBitrate();
    void effectiveFrameRate_data();
    void effectiveFrameRate();
    void outputBitrateUsesClampedQualityAndFallbackFps();
    void smartSaveShouldMove_data();
    void smartSaveShouldMove();
};

void tst_IntermediateQuality::intermediateBitrate_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<int>("fps");
    QTest::addColumn<int>("expected");
    // 1920*1080*30*0.55 = 34,214,400
    QTest::newRow("1080p30") << QSize(1920, 1080) << 30 << 34214400;
    // 5120*2880*60*0.55 = 486,604,800 -> capped
    QTest::newRow("5k60 caps at 100 Mbps") << QSize(5120, 2880) << 60 << IntermediateQuality::kIntermediateMaxBitrate;
    // 64*48*10*0.55 = 16,896 -> floored to kMinBitrate
    QTest::newRow("tiny floors") << QSize(64, 48) << 10 << VideoBitrate::kMinBitrate;
}

void tst_IntermediateQuality::intermediateBitrate()
{
    QFETCH(QSize, size);
    QFETCH(int, fps);
    QFETCH(int, expected);
    QCOMPARE(IntermediateQuality::intermediateBitrate(size, fps), expected);
}

void tst_IntermediateQuality::bytesPerMinuteAndRoom()
{
    // 34,214,400 bps = 4,276,800 B/s = 256,608,000 B/min
    QCOMPARE(IntermediateQuality::estimatedBytesPerMinute(34214400), qint64(256608000));
    const QSize size(1920, 1080);
    const qint64 needed = qint64(256608000) * IntermediateQuality::kMinimumRecordingMinutes;
    QVERIFY(IntermediateQuality::hasRoomForIntermediate(needed, size, 30));
    QVERIFY(!IntermediateQuality::hasRoomForIntermediate(needed - 1, size, 30));
    QVERIFY(!IntermediateQuality::hasRoomForIntermediate(0, size, 30));
    QVERIFY(!IntermediateQuality::hasRoomForIntermediate(-1, size, 30));
}

void tst_IntermediateQuality::averageBitrate_data()
{
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<qint64>("durationMs");
    QTest::addColumn<qint64>("expected");
    QTest::newRow("1 MB over 1 s") << qint64(1000000) << qint64(1000) << qint64(8000000);
    QTest::newRow("250 KB over 2 s") << qint64(250000) << qint64(2000) << qint64(1000000);
    QTest::newRow("zero bytes") << qint64(0) << qint64(2000) << qint64(0);
    QTest::newRow("zero duration") << qint64(250000) << qint64(0) << qint64(0);
    QTest::newRow("negative duration") << qint64(250000) << qint64(-5) << qint64(0);
}

void tst_IntermediateQuality::averageBitrate()
{
    QFETCH(qint64, bytes);
    QFETCH(qint64, durationMs);
    QFETCH(qint64, expected);
    QCOMPARE(IntermediateQuality::averageBitrate(bytes, durationMs), expected);
}

void tst_IntermediateQuality::effectiveFrameRate_data()
{
    QTest::addColumn<double>("probed");
    QTest::addColumn<int>("expected");
    QTest::newRow("exact 30") << 30.0 << 30;
    QTest::newRow("29.97 rounds") << 29.97 << 30;
    QTest::newRow("10.4 rounds down") << 10.4 << 10;
    QTest::newRow("zero falls back") << 0.0 << IntermediateQuality::kFallbackFrameRate;
    QTest::newRow("negative falls back") << -1.0 << IntermediateQuality::kFallbackFrameRate;
    QTest::newRow("below half rounds to one") << 0.4 << IntermediateQuality::kFallbackFrameRate;
}

void tst_IntermediateQuality::effectiveFrameRate()
{
    QFETCH(double, probed);
    QFETCH(int, expected);
    QCOMPARE(IntermediateQuality::effectiveFrameRate(probed), expected);
}

void tst_IntermediateQuality::outputBitrateUsesClampedQualityAndFallbackFps()
{
    const QSize size(1280, 720);
    QCOMPARE(IntermediateQuality::outputBitrateFor(size, 30.0, 55), VideoBitrate::forQuality(size, 30, 55));
    QCOMPARE(IntermediateQuality::outputBitrateFor(size, 0.0, 55),
             VideoBitrate::forQuality(size, IntermediateQuality::kFallbackFrameRate, 55));
    // The settings manager does not clamp; the formula does.
    QCOMPARE(IntermediateQuality::outputBitrateFor(size, 30.0, 150), VideoBitrate::forQuality(size, 30, 100));
    QCOMPARE(IntermediateQuality::outputBitrateFor(size, 30.0, -3), VideoBitrate::forQuality(size, 30, 0));
}

void tst_IntermediateQuality::smartSaveShouldMove_data()
{
    QTest::addColumn<bool>("valid");
    QTest::addColumn<QString>("codec");
    QTest::addColumn<QSize>("size");
    QTest::addColumn<double>("fps");
    QTest::addColumn<qint64>("durationMs");
    QTest::addColumn<qint64>("bytes");
    QTest::addColumn<int>("quality");
    QTest::addColumn<bool>("expected");

    // 1280x720 @ 10 fps, quality 100 -> 2,764,800 bps target; quality 0 -> 921,600 -> floored to 1,000,000.
    const QSize hd(1280, 720);
    QTest::newRow("at target moves") << true << "h264" << hd << 10.0 << qint64(2000) << qint64(691200) << 100 << true; // 2,764,800 bps
    QTest::newRow("one byte over re-encodes") << true << "h264" << hd << 10.0 << qint64(2000) << qint64(691201) << 100 << false;
    QTest::newRow("intermediate vs low quality re-encodes") << true << "h264" << hd << 10.0 << qint64(2000) << qint64(691200) << 0 << false;
    QTest::newRow("static recording under floor moves") << true << "h264" << hd << 10.0 << qint64(2000) << qint64(20000) << 0 << true; // 80 kbps
    QTest::newRow("hevc never moves") << true << "hevc" << hd << 10.0 << qint64(2000) << qint64(20000) << 100 << false;
    QTest::newRow("unknown codec never moves") << true << "" << hd << 10.0 << qint64(2000) << qint64(20000) << 100 << false;
    QTest::newRow("invalid probe re-encodes") << false << "h264" << hd << 10.0 << qint64(2000) << qint64(20000) << 100 << false;
    QTest::newRow("zero bytes re-encodes") << true << "h264" << hd << 10.0 << qint64(2000) << qint64(0) << 100 << false;
    QTest::newRow("unknown fps uses fallback 30") << true << "h264" << hd << 0.0 << qint64(2000) << qint64(2073600) << 100 << true; // 8,294,400 bps == forQuality(hd, 30, 100)
}

void tst_IntermediateQuality::smartSaveShouldMove()
{
    QFETCH(bool, valid);
    QFETCH(QString, codec);
    QFETCH(QSize, size);
    QFETCH(double, fps);
    QFETCH(qint64, durationMs);
    QFETCH(qint64, bytes);
    QFETCH(int, quality);
    QFETCH(bool, expected);
    VideoFileProbe probe;
    probe.valid = valid;
    probe.videoCodec = codec;
    probe.videoSize = size;
    probe.frameRate = fps;
    probe.durationMs = durationMs;
    QCOMPARE(IntermediateQuality::smartSaveShouldMove(probe, bytes, quality), expected);
}

QTEST_APPLESS_MAIN(tst_IntermediateQuality)
#include "tst_IntermediateQuality.moc"
```

Add to `tests/CMakeLists.txt` directly after the `Encoding_VideoBitrate` block:

```cmake
add_executable(Encoding_IntermediateQuality Encoding/tst_IntermediateQuality.cpp)
target_include_directories(Encoding_IntermediateQuality PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(Encoding_IntermediateQuality PRIVATE Qt6::Core Qt6::Test)
add_test(NAME Encoding_IntermediateQuality COMMAND Encoding_IntermediateQuality)
set_tests_properties(Encoding_IntermediateQuality PROPERTIES TIMEOUT 60 LABELS "unit")
```

(`IVideoTranscoder.h` only needs QtCore types — `QRect`, `QSize`, `QString`, `std::function` — so no QtGui link.)

- [ ] **Step 2: Run the test to verify it fails**

Build `Encoding_IntermediateQuality`. Expected: `Cannot open include file: 'encoding/IntermediateQuality.h'`.

- [ ] **Step 3: Write the headers**

`include/encoding/VideoRateControl.h`:

```cpp
#pragma once

namespace SnapTray {

// How a video encoder is asked to spend bits. Bitrate is the historical mode
// (average bitrate from VideoBitrate::forQuality). ConstantQuality asks the
// platform encoder for a quality target with a bitrate ceiling; encoders that
// cannot honour it fall back to Bitrate and report that through
// IVideoEncoder::effectiveRateControl().
enum class VideoRateControl {
    Bitrate,
    ConstantQuality,
};

} // namespace SnapTray
```

`include/encoding/IntermediateQuality.h`:

```cpp
#pragma once

#include "encoding/VideoBitrate.h"
#include "video/IVideoTranscoder.h"

#include <QLatin1String>
#include <QSize>
#include <QtGlobal>

// Phase 3 policy: every number behind "record a high-quality intermediate when
// the preview is on, then save at the user's quality" lives here, as pure
// functions, so RecordingManager, the encoders and the preview backend agree.
namespace SnapTray::IntermediateQuality {

// Quality target handed to encoders that support constant-quality rate
// control (0-100 on both CODECAPI_AVEncCommonQuality and VideoToolbox).
constexpr int kConstantQualityValue = 85;
// Short GOPs keep preview seeking responsive and trims accurate.
constexpr int kKeyFrameIntervalSeconds = 1;
// VBR fallback / bitrate ceiling for the intermediate: the spec's 0.5-0.6 bpp.
constexpr double kIntermediateBitsPerPixel = 0.55;
constexpr int kIntermediateMaxBitrate = 100000000; // ~100 Mbps
// Free space needed at start, in minutes of intermediate recording, before
// falling back to the user's quality.
constexpr int kMinimumRecordingMinutes = 10;
// Used when a probe cannot report a frame rate.
constexpr int kFallbackFrameRate = 30;

constexpr qint64 kBitsPerByte = 8;
constexpr qint64 kMsPerSecond = 1000;
constexpr qint64 kSecondsPerMinute = 60;

inline int intermediateBitrate(const QSize& frameSize, int frameRate)
{
    const double bitrate = double(frameSize.width()) * double(frameSize.height())
                           * double(qMax(1, frameRate)) * kIntermediateBitsPerPixel;
    return static_cast<int>(qBound<double>(VideoBitrate::kMinBitrate, bitrate, kIntermediateMaxBitrate));
}

inline qint64 estimatedBytesPerMinute(int bitrate)
{
    return qint64(bitrate) / kBitsPerByte * kSecondsPerMinute;
}

inline bool hasRoomForIntermediate(qint64 freeBytes, const QSize& frameSize, int frameRate)
{
    if (freeBytes <= 0) {
        return false;
    }
    const qint64 needed = estimatedBytesPerMinute(intermediateBitrate(frameSize, frameRate))
                          * kMinimumRecordingMinutes;
    return freeBytes >= needed;
}

// Total (video + audio + container) average bitrate in bits/s; 0 when the
// inputs cannot support an estimate. Deliberately conservative: it includes
// audio, so a borderline file re-encodes rather than moves.
inline qint64 averageBitrate(qint64 fileBytes, qint64 durationMs)
{
    if (fileBytes <= 0 || durationMs <= 0) {
        return 0;
    }
    return fileBytes * kBitsPerByte * kMsPerSecond / durationMs;
}

inline int effectiveFrameRate(double probedFps)
{
    const int rounded = qRound(probedFps);
    return rounded >= 1 ? rounded : kFallbackFrameRate;
}

// The output quality contract: the H.264 bitrate for the final dimensions at
// the source frame rate and the user's quality (clamped by forQuality).
inline int outputBitrateFor(const QSize& outputSize, double probedFps, int userQuality)
{
    return VideoBitrate::forQuality(outputSize, effectiveFrameRate(probedFps), userQuality);
}

// Smart rule for an unedited save: move the intermediate only when it is
// H.264 (the only codec the bitrate formula describes) and already at or
// below the user's target. Anything unknown re-encodes.
inline bool smartSaveShouldMove(const VideoFileProbe& probe, qint64 fileBytes, int userQuality)
{
    if (!probe.valid || probe.videoCodec != QLatin1String(kVideoCodecH264)) {
        return false;
    }
    const qint64 actual = averageBitrate(fileBytes, probe.durationMs);
    if (actual <= 0) {
        return false;
    }
    return actual <= outputBitrateFor(probe.videoSize, probe.frameRate, userQuality);
}

} // namespace SnapTray::IntermediateQuality
```

- [ ] **Step 4: Run the test to verify it passes**

Build and run `Encoding_IntermediateQuality` with `-o <scratch>/t2.txt,txt`. Expected: 6 slots, all rows pass. If "at target moves" fails by rounding, recompute: 691200 B × 8 × 1000 / 2000 ms = 2,764,800 bps = 1280×720×10×0.3 exactly.

- [ ] **Step 5: Commit**

```bash
git add include/encoding/VideoRateControl.h include/encoding/IntermediateQuality.h tests/Encoding/tst_IntermediateQuality.cpp tests/CMakeLists.txt
git commit -m "feat(encoding): add the intermediate quality and smart save policy"
```

---

### Task 3: Encoder interface and factory carry rate control and keyframe interval

**Files:**
- Modify: `include/IVideoEncoder.h` (after `setQuality`)
- Modify: `include/encoding/EncoderFactory.h:38-62` (`EncoderConfig`)
- Modify: `src/encoding/EncoderFactory.cpp:109-111` (`tryCreateNativeEncoder`)
- Modify: `tests/Encoding/FakeAudioEncoder.h`
- Test: `tests/Encoding/tst_EncoderFactory.cpp`

**Interfaces:**
- Consumes: `SnapTray::VideoRateControl` (Task 2).
- Produces: `virtual void IVideoEncoder::setRateControl(SnapTray::VideoRateControl mode, int qualityValue)`, `virtual void IVideoEncoder::setKeyFrameIntervalSeconds(int seconds)` (0 = encoder default), `virtual SnapTray::VideoRateControl IVideoEncoder::effectiveRateControl() const`; `EncoderConfig::rateControl` (default `Bitrate`), `EncoderConfig::keyFrameIntervalSeconds` (default 0). `AudioEncoderTestState::rateControl`, `::rateControlQuality`, `::keyFrameIntervalSeconds` record what the factory applied. Tasks 4–6 use these.

- [ ] **Step 1: Write the failing test**

In `tests/Encoding/tst_EncoderFactory.cpp`, add `#include "FakeAudioEncoder.h"` and `#include "encoding/VideoRateControl.h"`, declare two slots after `testCreateNativeEncoderWithQuality();`:

```cpp
    void testNativeEncoderReceivesRateControl_data();
    void testNativeEncoderReceivesRateControl();
```

and implement:

```cpp
void TestEncoderFactory::testNativeEncoderReceivesRateControl_data()
{
    QTest::addColumn<int>("rateControl");
    QTest::addColumn<int>("quality");
    QTest::addColumn<int>("keyFrameIntervalSeconds");
    QTest::newRow("defaults") << int(SnapTray::VideoRateControl::Bitrate) << 55 << 0;
    QTest::newRow("intermediate") << int(SnapTray::VideoRateControl::ConstantQuality) << 85 << 1;
}

void TestEncoderFactory::testNativeEncoderReceivesRateControl()
{
    QFETCH(int, rateControl);
    QFETCH(int, quality);
    QFETCH(int, keyFrameIntervalSeconds);
    auto config = createTestConfig(EncoderFactory::Format::MP4);
    config.rateControl = static_cast<SnapTray::VideoRateControl>(rateControl);
    config.quality = quality;
    config.keyFrameIntervalSeconds = keyFrameIntervalSeconds;
    auto state = std::make_shared<AudioEncoderTestState>();
    auto result = EncoderFactoryTestAccess::create(config, this, state);
    QVERIFY(result.success);
    QVERIFY(result.isNative);
    QCOMPARE(int(state->rateControl), rateControl);
    QCOMPARE(state->rateControlQuality, quality);
    QCOMPARE(state->keyFrameIntervalSeconds, keyFrameIntervalSeconds);
    QCOMPARE(state->quality, quality);
    delete result.nativeEncoder;
}
```

Extend `AudioEncoderTestState` in `FakeAudioEncoder.h`:

```cpp
    int quality = -1;
    SnapTray::VideoRateControl rateControl = SnapTray::VideoRateControl::Bitrate;
    int rateControlQuality = -1;
    int keyFrameIntervalSeconds = -1;
```

and in `FakeAudioEncoder` add overrides (include `encoding/VideoRateControl.h`):

```cpp
    void setQuality(int quality) override { m_state->quality = quality; }
    void setRateControl(SnapTray::VideoRateControl mode, int qualityValue) override {
        m_state->rateControl = mode; m_state->rateControlQuality = qualityValue;
    }
    void setKeyFrameIntervalSeconds(int seconds) override { m_state->keyFrameIntervalSeconds = seconds; }
```

- [ ] **Step 2: Run the test to verify it fails**

Build `Encoding_EncoderFactory`. Expected: compile error, `rateControl` is not a member of `EncoderConfig` / no member `setRateControl` to override.

- [ ] **Step 3: Extend the interface and config**

`include/IVideoEncoder.h`: add `#include "encoding/VideoRateControl.h"` and, after `setQuality`:

```cpp
    /**
     * @brief Choose how bits are spent. ConstantQuality asks the platform
     * encoder for @p qualityValue (0-100) with a bitrate ceiling; encoders
     * that cannot honour it stay in Bitrate mode (see effectiveRateControl()).
     * Must be called before start().
     */
    virtual void setRateControl(SnapTray::VideoRateControl mode, int qualityValue)
    {
        Q_UNUSED(mode);
        Q_UNUSED(qualityValue);
    }

    /**
     * @brief Keyframe (GOP) interval in seconds; 0 keeps the encoder default.
     * Must be called before start().
     */
    virtual void setKeyFrameIntervalSeconds(int seconds) { Q_UNUSED(seconds); }

    /** @brief The rate control actually in effect after start(). */
    virtual SnapTray::VideoRateControl effectiveRateControl() const
    {
        return SnapTray::VideoRateControl::Bitrate;
    }
```

`include/encoding/EncoderFactory.h`: include `encoding/VideoRateControl.h`; in `EncoderConfig` after `int quality = 55; // 0-100` add:

```cpp
        // Native (MP4) encoders only. ConstantQuality uses `quality` as the
        // quality target and VideoBitrate as a ceiling; 0 keyframe seconds
        // keeps the encoder default.
        SnapTray::VideoRateControl rateControl = SnapTray::VideoRateControl::Bitrate;
        int keyFrameIntervalSeconds = 0;
```

`src/encoding/EncoderFactory.cpp`, in `tryCreateNativeEncoder` right after `encoder->setQuality(config.quality);`:

```cpp
    encoder->setRateControl(config.rateControl, config.quality);
    encoder->setKeyFrameIntervalSeconds(config.keyFrameIntervalSeconds);
    qDebug() << "EncoderFactory: Native encoder rate control"
             << (config.rateControl == SnapTray::VideoRateControl::ConstantQuality ? "constant quality" : "bitrate")
             << "keyframe interval (s):" << config.keyFrameIntervalSeconds;
```

- [ ] **Step 4: Run the test to verify it passes**

Build and run `Encoding_EncoderFactory` (`-o <scratch>/t3.txt,txt`). Expected: both new rows pass; existing slots unchanged (some may `QSKIP` without a native encoder).

- [ ] **Step 5: Commit**

```bash
git add include/IVideoEncoder.h include/encoding/EncoderFactory.h src/encoding/EncoderFactory.cpp tests/Encoding/FakeAudioEncoder.h tests/Encoding/tst_EncoderFactory.cpp
git commit -m "feat(encoding): let encoder configs request rate control and keyframe interval"
```

---

### Task 4: Windows encoder applies constant quality and GOP through ICodecAPI

**Files:**
- Modify: `src/MediaFoundationEncoder.cpp` (private class L22-135, `setQuality` L463)
- Modify: `include/MediaFoundationEncoder.h` (declare the three overrides)
- Create: `tests/Encoding/tst_MediaFoundationEncoderIntermediate_win.cpp`
- Modify: `tests/CMakeLists.txt` (inside the existing `if(WIN32)` block after `Video_MediaFoundationPipeline`)

**Interfaces:**
- Consumes: Task 3 overrides; `IntermediateQuality::intermediateBitrate` (Task 2).
- Produces: `MediaFoundationEncoder::effectiveRateControl()` reports `ConstantQuality` only when both CODECAPI calls succeeded.

- [ ] **Step 1: Write the failing test**

`tests/Encoding/tst_MediaFoundationEncoderIntermediate_win.cpp`:

```cpp
#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoRateControl.h"
#include "video/IVideoTranscoder.h"

#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace SnapTray;

namespace {

constexpr int kFrameRate = 30;
constexpr int kSeconds = 3;
constexpr int kFrameCount = kFrameRate * kSeconds;
const QSize kFrameSize(320, 240);
// A 1 s GOP over 3 s yields keyframes at 0, 1, 2 s (3) plus possibly one at
// the end; the Media Foundation default GOP is several seconds, so 3 is the
// smallest count that proves the interval was applied.
constexpr int kMinimumKeyFrames = kSeconds;
constexpr int kFrameAcceptTimeoutMs = 2000;
constexpr int kFinishTimeoutMs = 10000;

// Moving content: a static image lets the encoder emit nothing but the GOP
// structure, which would still pass, but real motion is the honest case.
QImage frameAt(int index)
{
    QImage image(kFrameSize, QImage::Format_ARGB32);
    image.fill(Qt::darkGray);
    QPainter painter(&image);
    painter.fillRect(QRect((index * 7) % kFrameSize.width(), (index * 3) % kFrameSize.height(), 40, 40), Qt::yellow);
    return image;
}

QString encode(const QString& path, VideoRateControl mode, int keyFrameIntervalSeconds)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    encoder->setQuality(IntermediateQuality::kConstantQualityValue);
    encoder->setRateControl(mode, IntermediateQuality::kConstantQualityValue);
    encoder->setKeyFrameIntervalSeconds(keyFrameIntervalSeconds);
    if (!encoder->start(path, kFrameSize, kFrameRate)) return encoder->lastError();
    for (int i = 0; i < kFrameCount; ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer timer;
        timer.start();
        do {
            encoder->writeFrame(frameAt(i), qint64(i) * 1000 / kFrameRate);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(5);
        } while (timer.elapsed() < kFrameAcceptTimeoutMs);
        if (encoder->framesWritten() != before + 1) return QStringLiteral("frame %1 rejected").arg(i);
    }
    QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finished.isEmpty() && !finished.wait(kFinishTimeoutMs)) return QStringLiteral("did not finish");
    if (!finished.first().at(0).toBool()) return encoder->lastError();
    qInfo() << "effective rate control:" << int(encoder->effectiveRateControl());
    return {};
}

// Counts compressed video samples flagged as clean points (IDR frames).
int countKeyFrames(const QString& path)
{
    ComPtr<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, &reader))) return -1;
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    int keyFrames = 0;
    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr, &sample))) return -1;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        if (MFGetAttributeUINT32(sample.Get(), MFSampleExtension_CleanPoint, FALSE)) ++keyFrames;
    }
    return keyFrames;
}

} // namespace

class tst_MediaFoundationEncoderIntermediate : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(SUCCEEDED(MFStartup(MF_VERSION))); }
    void cleanupTestCase() { MFShutdown(); }
    void intermediateHasOneSecondKeyFrames();
    void defaultModeStillRecords();
};

void tst_MediaFoundationEncoderIntermediate::intermediateHasOneSecondKeyFrames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("intermediate.mp4"));
    const QString error = encode(path, VideoRateControl::ConstantQuality, IntermediateQuality::kKeyFrameIntervalSeconds);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    const VideoFileProbe probe = transcoder->probe(path);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, kFrameSize);
    QCOMPARE(probe.videoCodec, QString::fromLatin1(kVideoCodecH264));
    const int keyFrames = countKeyFrames(path);
    QVERIFY2(keyFrames >= kMinimumKeyFrames, qPrintable(QStringLiteral("key frames: %1").arg(keyFrames)));
}

void tst_MediaFoundationEncoderIntermediate::defaultModeStillRecords()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("default.mp4"));
    const QString error = encode(path, VideoRateControl::Bitrate, 0);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    QVERIFY(transcoder->probe(path).valid);
    QVERIFY(countKeyFrames(path) >= 1);
}

QTEST_MAIN(tst_MediaFoundationEncoderIntermediate)
#include "tst_MediaFoundationEncoderIntermediate_win.moc"
```

Add inside the `if(WIN32)` block in `tests/CMakeLists.txt`:

```cmake
    add_executable(Encoding_MediaFoundationEncoderIntermediate Encoding/tst_MediaFoundationEncoderIntermediate_win.cpp)
    target_link_libraries(Encoding_MediaFoundationEncoderIntermediate PRIVATE snaptray_platform Qt6::Gui Qt6::Test mfreadwrite mfplat mfuuid ole32)
    add_test(NAME Encoding_MediaFoundationEncoderIntermediate COMMAND Encoding_MediaFoundationEncoderIntermediate)
    set_tests_properties(Encoding_MediaFoundationEncoderIntermediate PROPERTIES TIMEOUT 120 LABELS "integration;slow")
```

- [ ] **Step 2: Run the test to verify it fails**

Build and run `Encoding_MediaFoundationEncoderIntermediate` (`-o <scratch>/t4.txt,txt`). Expected: `intermediateHasOneSecondKeyFrames` fails on the keyframe count (default GOP gives 1–2 keyframes over 3 s) — record the actual count in the report. If it already passes because the default GOP is ≤ 1 s on this machine's encoder, raise `kSeconds` to 6 and `kMinimumKeyFrames` to 6 and re-run to confirm the failure, then keep the raised values.

- [ ] **Step 3: Implement in the encoder**

`include/MediaFoundationEncoder.h`: declare

```cpp
    void setRateControl(SnapTray::VideoRateControl mode, int qualityValue) override;
    void setKeyFrameIntervalSeconds(int seconds) override;
    SnapTray::VideoRateControl effectiveRateControl() const override;
```

`src/MediaFoundationEncoder.cpp`: add `#include "encoding/IntermediateQuality.h"` and `#include "encoding/VideoRateControl.h"`. In `MediaFoundationEncoderPrivate` add members after `int quality = 55;`:

```cpp
    SnapTray::VideoRateControl requestedRateControl = SnapTray::VideoRateControl::Bitrate;
    SnapTray::VideoRateControl effectiveRateControl = SnapTray::VideoRateControl::Bitrate;
    int keyFrameIntervalSeconds = 0; // 0 = encoder default
```

Replace `calculateBitrate()`:

```cpp
    // In constant-quality mode MF_MT_AVG_BITRATE is the VBR ceiling (and the
    // whole budget if the MFT refuses quality mode).
    UINT32 calculateBitrate() const {
        if (requestedRateControl == SnapTray::VideoRateControl::ConstantQuality) {
            return static_cast<UINT32>(SnapTray::IntermediateQuality::intermediateBitrate(frameSize, frameRate));
        }
        return static_cast<UINT32>(SnapTray::VideoBitrate::forQuality(frameSize, frameRate, quality));
    }

    // Codec properties live on the encoder MFT behind the sink writer; it only
    // exists after SetInputMediaType. Failures here never fail the recording:
    // the stream type already carries a usable average bitrate.
    void applyCodecProperties() {
        effectiveRateControl = SnapTray::VideoRateControl::Bitrate;
        ICodecAPI *codecApi = nullptr;
        HRESULT hr = sinkWriter->GetServiceForStream(videoStreamIndex, GUID_NULL, IID_PPV_ARGS(&codecApi));
        if (FAILED(hr) || !codecApi) {
            qWarning() << "MediaFoundationEncoder: ICodecAPI unavailable, keeping encoder defaults (hr =" << Qt::hex << hr << ")";
            return;
        }
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_UI4;
        if (requestedRateControl == SnapTray::VideoRateControl::ConstantQuality) {
            value.ulVal = eAVEncCommonRateControlMode_Quality;
            hr = codecApi->SetValue(&CODECAPI_AVEncCommonRateControlMode, &value);
            if (SUCCEEDED(hr)) {
                value.ulVal = static_cast<ULONG>(quality);
                hr = codecApi->SetValue(&CODECAPI_AVEncCommonQuality, &value);
            }
            if (SUCCEEDED(hr)) {
                effectiveRateControl = SnapTray::VideoRateControl::ConstantQuality;
                qDebug() << "MediaFoundationEncoder: constant quality" << quality;
            } else {
                qWarning() << "MediaFoundationEncoder: quality rate control unsupported (hr =" << Qt::hex << hr
                           << "), falling back to VBR at" << calculateBitrate() << "bps";
                value.ulVal = eAVEncCommonRateControlMode_UnconstrainedVBR;
                (void)codecApi->SetValue(&CODECAPI_AVEncCommonRateControlMode, &value);
            }
        }
        if (keyFrameIntervalSeconds > 0) {
            value.ulVal = static_cast<ULONG>(frameRate * keyFrameIntervalSeconds);
            hr = codecApi->SetValue(&CODECAPI_AVEncMPVGOPSize, &value);
            if (FAILED(hr)) {
                qWarning() << "MediaFoundationEncoder: GOP size rejected (hr =" << Qt::hex << hr << ")";
            }
        }
        codecApi->Release();
    }
```

In `configureVideoStream()`, after `hr = sinkWriter->SetInputMediaType(videoStreamIndex, inputType, nullptr);` add:

```cpp
        if (SUCCEEDED(hr)) {
            applyCodecProperties();
        }
```

Add the public overrides next to `setQuality`:

```cpp
void MediaFoundationEncoder::setRateControl(SnapTray::VideoRateControl mode, int qualityValue)
{
    d->requestedRateControl = mode;
    d->quality = qBound(0, qualityValue, 100);
}

void MediaFoundationEncoder::setKeyFrameIntervalSeconds(int seconds)
{
    d->keyFrameIntervalSeconds = qMax(0, seconds);
}

SnapTray::VideoRateControl MediaFoundationEncoder::effectiveRateControl() const
{
    return d->effectiveRateControl;
}
```

`<codecapi.h>` is already included; `ICodecAPI` needs `<strmif.h>` (add it) and `VariantInit` needs `<oleauto.h>` (add it). If the link step complains about `IID_ICodecAPI`, add `strmiids` to the platform library's Windows link list in `CMakeLists.txt` next to the other MF libraries.

- [ ] **Step 4: Run the test to verify it passes**

Build and run `Encoding_MediaFoundationEncoderIntermediate`. Expected: both slots pass; the output file reports the effective mode. Then rebuild and run `Encoding_EncoderFactory` and `Video_VideoTranscoder` to confirm default-mode recordings are unchanged.

- [ ] **Step 5: Commit**

```bash
git add include/MediaFoundationEncoder.h src/MediaFoundationEncoder.cpp tests/Encoding/tst_MediaFoundationEncoderIntermediate_win.cpp tests/CMakeLists.txt CMakeLists.txt
git commit -m "feat(encoding): request constant quality and 1 s keyframes from Media Foundation"
```

---

### Task 5: macOS encoder honours keyframe interval and the intermediate ceiling

**Files:**
- Modify: `src/AVFoundationEncoder.mm` (private class L34-60, writer settings L183-195, `setQuality` L432)
- Modify: `include/AVFoundationEncoder.h` (declare the three overrides)
- Create: `tests/Encoding/tst_AVFoundationEncoderIntermediate_mac.mm`
- Modify: `tests/CMakeLists.txt` (new `if(APPLE)` block after the Windows encoder test block)

**Interfaces:**
- Consumes: Task 3 overrides; `IntermediateQuality::intermediateBitrate`.
- Produces: `AVFoundationEncoder::effectiveRateControl()` returns `Bitrate` (VBR fallback) unless `kUseVideoToolboxQuality` is turned on after validation on hardware.

This task cannot run in the Windows executing environment. The implementer writes the code and the test, confirms the Windows build is untouched, and the ledger records that `Encoding_AVFoundationEncoderIntermediate` must pass on a macOS 14+ machine before release.

- [ ] **Step 1: Write the failing test**

`tests/Encoding/tst_AVFoundationEncoderIntermediate_mac.mm`:

```objc
#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoRateControl.h"
#include "video/IVideoTranscoder.h"

#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#import <AVFoundation/AVFoundation.h>

using namespace SnapTray;

namespace {

constexpr int kFrameRate = 30;
constexpr int kSeconds = 3;
constexpr int kFrameCount = kFrameRate * kSeconds;
const QSize kFrameSize(320, 240);
constexpr int kMinimumKeyFrames = kSeconds;
constexpr int kFrameAcceptTimeoutMs = 2000;
constexpr int kFinishTimeoutMs = 10000;

QImage frameAt(int index)
{
    QImage image(kFrameSize, QImage::Format_ARGB32);
    image.fill(Qt::darkGray);
    QPainter painter(&image);
    painter.fillRect(QRect((index * 7) % kFrameSize.width(), (index * 3) % kFrameSize.height(), 40, 40), Qt::yellow);
    return image;
}

QString encode(const QString& path, VideoRateControl mode, int keyFrameIntervalSeconds)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    encoder->setQuality(IntermediateQuality::kConstantQualityValue);
    encoder->setRateControl(mode, IntermediateQuality::kConstantQualityValue);
    encoder->setKeyFrameIntervalSeconds(keyFrameIntervalSeconds);
    if (!encoder->start(path, kFrameSize, kFrameRate)) return encoder->lastError();
    for (int i = 0; i < kFrameCount; ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer timer;
        timer.start();
        do {
            encoder->writeFrame(frameAt(i), qint64(i) * 1000 / kFrameRate);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(5);
        } while (timer.elapsed() < kFrameAcceptTimeoutMs);
        if (encoder->framesWritten() != before + 1) return QStringLiteral("frame %1 rejected").arg(i);
    }
    QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finished.isEmpty() && !finished.wait(kFinishTimeoutMs)) return QStringLiteral("did not finish");
    if (!finished.first().at(0).toBool()) return encoder->lastError();
    return {};
}

// Counts sync samples (frames without kCMSampleAttachmentKey_NotSync) in the
// compressed video track.
int countKeyFrames(const QString& path)
{
    @autoreleasepool {
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:path.toNSString()] options:nil];
        AVAssetTrack* track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!track) return -1;
        NSError* error = nil;
        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (!reader) return -1;
        AVAssetReaderTrackOutput* output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:nil];
        [reader addOutput:output];
        if (![reader startReading]) return -1;
        int keyFrames = 0;
        while (CMSampleBufferRef sample = [output copyNextSampleBuffer]) {
            CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
            bool notSync = false;
            if (attachments && CFArrayGetCount(attachments) > 0) {
                CFDictionaryRef dict = (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
                CFBooleanRef value = (CFBooleanRef)CFDictionaryGetValue(dict, kCMSampleAttachmentKey_NotSync);
                notSync = value && CFBooleanGetValue(value);
            }
            if (!notSync) ++keyFrames;
            CFRelease(sample);
        }
        return keyFrames;
    }
}

} // namespace

class tst_AVFoundationEncoderIntermediate : public QObject
{
    Q_OBJECT
private slots:
    void intermediateHasOneSecondKeyFrames();
    void defaultModeStillRecords();
};

void tst_AVFoundationEncoderIntermediate::intermediateHasOneSecondKeyFrames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("intermediate.mp4"));
    const QString error = encode(path, VideoRateControl::ConstantQuality, IntermediateQuality::kKeyFrameIntervalSeconds);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    const VideoFileProbe probe = transcoder->probe(path);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, kFrameSize);
    QCOMPARE(probe.videoCodec, QString::fromLatin1(kVideoCodecH264));
    const int keyFrames = countKeyFrames(path);
    QVERIFY2(keyFrames >= kMinimumKeyFrames, qPrintable(QStringLiteral("key frames: %1").arg(keyFrames)));
}

void tst_AVFoundationEncoderIntermediate::defaultModeStillRecords()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("default.mp4"));
    const QString error = encode(path, VideoRateControl::Bitrate, 0);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    QVERIFY(transcoder->probe(path).valid);
    QVERIFY(countKeyFrames(path) >= 1);
}

QTEST_MAIN(tst_AVFoundationEncoderIntermediate)
#include "tst_AVFoundationEncoderIntermediate_mac.moc"
```

`tests/CMakeLists.txt`, after the Windows block:

```cmake
if(APPLE)
    add_executable(Encoding_AVFoundationEncoderIntermediate Encoding/tst_AVFoundationEncoderIntermediate_mac.mm)
    set_source_files_properties(Encoding/tst_AVFoundationEncoderIntermediate_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(Encoding_AVFoundationEncoderIntermediate PRIVATE snaptray_platform Qt6::Gui Qt6::Test
        "-framework AVFoundation" "-framework CoreMedia")
    add_test(NAME Encoding_AVFoundationEncoderIntermediate COMMAND Encoding_AVFoundationEncoderIntermediate)
    set_tests_properties(Encoding_AVFoundationEncoderIntermediate PROPERTIES TIMEOUT 120 LABELS "integration;slow")
endif()
```

- [ ] **Step 2: Verify the failure (macOS only)**

On macOS: build and run `Encoding_AVFoundationEncoderIntermediate`; expected failure on the keyframe count (default interval is `frameRate * 2`, so 3 s yields 2 keyframes). On Windows: confirm `cmake --build build` still configures and the new target is not generated.

- [ ] **Step 3: Implement**

`include/AVFoundationEncoder.h`: declare the same three overrides as Task 4.

`src/AVFoundationEncoder.mm`: include `encoding/IntermediateQuality.h` and `encoding/VideoRateControl.h`. Add to the private class after `int quality = 55;`:

```objc
    SnapTray::VideoRateControl requestedRateControl = SnapTray::VideoRateControl::Bitrate;
    SnapTray::VideoRateControl effectiveRateControl = SnapTray::VideoRateControl::Bitrate;
    int keyFrameIntervalSeconds = 0; // 0 = kDefaultKeyFrameIntervalSeconds
```

Add file-scope constants near the top of the file:

```objc
namespace {
// The historical AVAssetWriter default this encoder has always used.
constexpr int kDefaultKeyFrameIntervalSeconds = 2;
// AVVideoQualityKey is documented for JPEG/ProRes; its effect on H.264 via
// AVAssetWriter is unverified. Stays off until validated on hardware, so the
// intermediate uses the VBR fallback (IntermediateQuality::intermediateBitrate).
constexpr bool kUseVideoToolboxQuality = false;
} // namespace
```

Replace `calculateBitrate()`:

```objc
    int calculateBitrate() const {
        if (requestedRateControl == SnapTray::VideoRateControl::ConstantQuality) {
            return SnapTray::IntermediateQuality::intermediateBitrate(frameSize, frameRate);
        }
        return SnapTray::VideoBitrate::forQuality(frameSize, frameRate, quality);
    }
    int keyFrameInterval() const {
        const int seconds = keyFrameIntervalSeconds > 0 ? keyFrameIntervalSeconds : kDefaultKeyFrameIntervalSeconds;
        return frameRate * seconds;
    }
```

Replace the compression properties in `start()`:

```objc
    NSMutableDictionary *compression = [@{
        AVVideoAverageBitRateKey: @(bitrate),
        AVVideoExpectedSourceFrameRateKey: @(frameRate),
        AVVideoMaxKeyFrameIntervalKey: @(d->keyFrameInterval()),
        AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
        AVVideoAllowFrameReorderingKey: @NO
    } mutableCopy];
    d->effectiveRateControl = SnapTray::VideoRateControl::Bitrate;
    if (kUseVideoToolboxQuality && d->requestedRateControl == SnapTray::VideoRateControl::ConstantQuality) {
        compression[AVVideoQualityKey] = @(d->quality / 100.0);
        d->effectiveRateControl = SnapTray::VideoRateControl::ConstantQuality;
    }
    NSDictionary *videoSettings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(adjustedSize.width()),
        AVVideoHeightKey: @(adjustedSize.height()),
        AVVideoCompressionPropertiesKey: compression
    };
```

Add the overrides next to `setQuality`:

```objc
void AVFoundationEncoder::setRateControl(SnapTray::VideoRateControl mode, int qualityValue)
{
    d->requestedRateControl = mode;
    d->quality = qBound(0, qualityValue, 100);
}

void AVFoundationEncoder::setKeyFrameIntervalSeconds(int seconds)
{
    d->keyFrameIntervalSeconds = qMax(0, seconds);
}

SnapTray::VideoRateControl AVFoundationEncoder::effectiveRateControl() const
{
    return d->effectiveRateControl;
}
```

- [ ] **Step 4: Verify**

macOS: `Encoding_AVFoundationEncoderIntermediate` passes. Windows: full `cmake --build build` succeeds (the `.mm` is not compiled there).

- [ ] **Step 5: Commit**

```bash
git add include/AVFoundationEncoder.h src/AVFoundationEncoder.mm tests/Encoding/tst_AVFoundationEncoderIntermediate_mac.mm tests/CMakeLists.txt
git commit -m "feat(encoding): give the macOS intermediate 1 s keyframes and a high bitrate ceiling"
```

---

### Task 6: RecordingManager records the intermediate when preview is on and disk allows

**Files:**
- Modify: `include/RecordingInitTask.h:40-53` (`Config`)
- Modify: `src/RecordingInitTask.cpp:214-220`
- Modify: `include/RecordingManager.h` (near `m_captureControlsMayBeVisible`, L174)
- Modify: `src/RecordingManager.cpp:668-684` (`beginAsyncInitialization` config build)
- Test: `tests/RecordingManager/tst_InitTask.cpp`, `tests/RecordingManager/tst_Lifecycle.cpp`

**Interfaces:**
- Consumes: `EncoderConfig::rateControl/keyFrameIntervalSeconds` (Task 3), `IntermediateQuality::{kConstantQualityValue,kKeyFrameIntervalSeconds,hasRoomForIntermediate}` (Task 2).
- Produces: `RecordingInitTask::Config::intermediateQuality` (bool, default false); `bool RecordingManager::chooseIntermediateQuality(const QString& outputDirectory, const QSize& frameSize)`; `std::function<qint64(const QString&)> RecordingManager::m_freeBytesForPath` (test seam, default `QStorageInfo(path).bytesAvailable()`).

- [ ] **Step 1: Write the failing tests**

`tests/RecordingManager/tst_InitTask.cpp` — add `#include "encoding/IntermediateQuality.h"` and `#include "encoding/VideoRateControl.h"`, declare after `testConfigWithNativeEncoder();`:

```cpp
    void testIntermediateQualityMapsToEncoderConfig_data();
    void testIntermediateQualityMapsToEncoderConfig();
```

implement:

```cpp
void TestRecordingInitTask::testIntermediateQualityMapsToEncoderConfig_data()
{
    QTest::addColumn<bool>("intermediate");
    QTest::addColumn<int>("expectedRateControl");
    QTest::addColumn<int>("expectedQuality");
    QTest::addColumn<int>("expectedKeyFrameSeconds");
    QTest::newRow("user quality") << false << int(SnapTray::VideoRateControl::Bitrate) << 37 << 0;
    QTest::newRow("intermediate") << true << int(SnapTray::VideoRateControl::ConstantQuality)
                                  << SnapTray::IntermediateQuality::kConstantQualityValue
                                  << SnapTray::IntermediateQuality::kKeyFrameIntervalSeconds;
}

void TestRecordingInitTask::testIntermediateQualityMapsToEncoderConfig()
{
    QFETCH(bool, intermediate);
    QFETCH(int, expectedRateControl);
    QFETCH(int, expectedQuality);
    QFETCH(int, expectedKeyFrameSeconds);
    RecordingInitTask::Config config = createTestConfig();
    config.outputFormat = EncoderFactory::Format::MP4;
    config.quality = 37;
    config.intermediateQuality = intermediate;
    RecordingInitTask task(config);
    EncoderFactory::EncoderConfig captured;
    task.m_createEncoder = [&captured](const EncoderFactory::EncoderConfig& encoderConfig, QObject*) {
        captured = encoderConfig;
        EncoderFactory::EncoderResult result;
        result.success = false;
        result.errorMessage = QStringLiteral("captured");
        return result;
    };
    QVERIFY(!task.initializeEncoder());
    QCOMPARE(int(captured.rateControl), expectedRateControl);
    QCOMPARE(captured.quality, expectedQuality);
    QCOMPARE(captured.keyFrameIntervalSeconds, expectedKeyFrameSeconds);
}
```

(`m_createEncoder` and `initializeEncoder` are already reached from `tst_Startup.cpp`; if `TestRecordingInitTask` is not a friend of `RecordingInitTask`, add `friend class TestRecordingInitTask;` next to the existing friend declarations in `include/RecordingInitTask.h`.)

`tests/RecordingManager/tst_Lifecycle.cpp` — declare `void intermediateQualityFollowsFreeSpace_data(); void intermediateQualityFollowsFreeSpace();` and implement:

```cpp
void TestRecordingManagerLifecycle::intermediateQualityFollowsFreeSpace_data()
{
    QTest::addColumn<qint64>("freeBytes");
    QTest::addColumn<bool>("expectIntermediate");
    QTest::addColumn<int>("expectWarnings");
    const QSize frame(1920, 1080);
    const qint64 needed = SnapTray::IntermediateQuality::estimatedBytesPerMinute(
                              SnapTray::IntermediateQuality::intermediateBitrate(frame, 30))
                          * SnapTray::IntermediateQuality::kMinimumRecordingMinutes;
    QTest::newRow("enough space") << needed << true << 0;
    QTest::newRow("one byte short") << needed - 1 << false << 1;
    QTest::newRow("unknown space") << qint64(-1) << false << 1;
}

void TestRecordingManagerLifecycle::intermediateQualityFollowsFreeSpace()
{
    QFETCH(qint64, freeBytes);
    QFETCH(bool, expectIntermediate);
    QFETCH(int, expectWarnings);
    RecordingManager manager;
    manager.m_frameRate = 30;
    QString queriedPath;
    manager.m_freeBytesForPath = [&queriedPath, freeBytes](const QString& path) {
        queriedPath = path;
        return freeBytes;
    };
    QSignalSpy warnings(&manager, &RecordingManager::recordingWarning);
    const QString directory = QStringLiteral("C:/tmp/recordings");
    QCOMPARE(manager.chooseIntermediateQuality(directory, QSize(1920, 1080)), expectIntermediate);
    QCOMPARE(queriedPath, directory);
    QCOMPARE(warnings.count(), expectWarnings);
    if (expectWarnings) {
        QCOMPARE(warnings.first().at(0).toString(), RecordingManager::tr(
            "Not enough free disk space for high-quality recording. Recording at the selected quality instead."));
    }
}
```

(`m_frameRate` is the manager's frame-rate member; if it is named differently, use that name — the test only needs 30 fps.)

- [ ] **Step 2: Run the tests to verify they fail**

Build `RecordingManager_InitTask` and `RecordingManager_Lifecycle`. Expected: compile errors on `intermediateQuality`, `m_freeBytesForPath`, `chooseIntermediateQuality`.

- [ ] **Step 3: Implement**

`include/RecordingInitTask.h`, in `Config` after `int quality = 55;`:

```cpp
        // Preview recordings capture a high-quality intermediate (constant
        // quality where supported, 1 s keyframes); the user's quality is then
        // applied when the preview saves. See IntermediateQuality.h.
        bool intermediateQuality = false;
```

`src/RecordingInitTask.cpp`, include `encoding/IntermediateQuality.h` and `encoding/VideoRateControl.h`; replace `encoderConfig.quality = m_config.quality;` with:

```cpp
    if (m_config.intermediateQuality) {
        encoderConfig.rateControl = SnapTray::VideoRateControl::ConstantQuality;
        encoderConfig.quality = SnapTray::IntermediateQuality::kConstantQualityValue;
        encoderConfig.keyFrameIntervalSeconds = SnapTray::IntermediateQuality::kKeyFrameIntervalSeconds;
    } else {
        encoderConfig.quality = m_config.quality;
    }
```

`include/RecordingManager.h`: include `<functional>` (already) and add near `m_captureControlsMayBeVisible`:

```cpp
    // Free bytes on the volume holding `path`; replaced by tests.
    std::function<qint64(const QString&)> m_freeBytesForPath;
    bool chooseIntermediateQuality(const QString& outputDirectory, const QSize& frameSize);
```

`src/RecordingManager.cpp`: include `<QStorageInfo>`, `<QFileInfo>`, `encoding/IntermediateQuality.h`. In the constructor initialise:

```cpp
    m_freeBytesForPath = [](const QString& path) { return QStorageInfo(path).bytesAvailable(); };
```

Add the method:

```cpp
bool RecordingManager::chooseIntermediateQuality(const QString& outputDirectory, const QSize& frameSize)
{
    const qint64 freeBytes = m_freeBytesForPath ? m_freeBytesForPath(outputDirectory) : -1;
    if (SnapTray::IntermediateQuality::hasRoomForIntermediate(freeBytes, frameSize, m_frameRate)) {
        return true;
    }
    qWarning() << "RecordingManager: free space" << freeBytes << "bytes in" << outputDirectory
               << "is below" << SnapTray::IntermediateQuality::kMinimumRecordingMinutes
               << "minutes of intermediate recording; using the selected quality";
    emit recordingWarning(tr("Not enough free disk space for high-quality recording. Recording at the selected quality instead."));
    return false;
}
```

In `beginAsyncInitialization`, after `config.quality = m_startSettings.quality;`:

```cpp
    config.intermediateQuality = showPreview
        && chooseIntermediateQuality(QFileInfo(config.outputPath).absolutePath(), physicalSize);
```

- [ ] **Step 4: Run the tests to verify they pass**

Build and run `RecordingManager_InitTask`, `RecordingManager_Lifecycle`, `RecordingManager_Startup` (`-o` files). Expected: all pass; `Startup` is unchanged (direct-save configs keep `Bitrate` and the user quality).

- [ ] **Step 5: Commit**

```bash
git add include/RecordingInitTask.h src/RecordingInitTask.cpp include/RecordingManager.h src/RecordingManager.cpp tests/RecordingManager/tst_InitTask.cpp tests/RecordingManager/tst_Lifecycle.cpp
git commit -m "feat(recording): capture preview recordings as a high-quality intermediate when disk allows"
```

---

### Task 7: Preview exports use the user's quality and smart-save unedited recordings

**Files:**
- Modify: `include/qml/RecordingPreviewBackend.h` (L157-172 private section, members L180-191)
- Modify: `src/qml/RecordingPreviewBackend.mm` (`save()` L314-336, `performTranscode()` L829-966)
- Test: `tests/Qml/tst_RecordingPreviewExport.cpp`

**Interfaces:**
- Consumes: `IntermediateQuality::{outputBitrateFor, smartSaveShouldMove}` (Task 2), `VideoFileProbe::frameRate/videoCodec` (Task 1), `RecordingSettingsManager::instance().quality()`.
- Produces: `static std::function<int()>& RecordingPreviewBackend::outputQualityOverride()` (private, friend tests), `void performTranscode(bool smartSave)`, member `int m_outputQuality`.

- [ ] **Step 1: Write the failing tests**

In `tests/Qml/tst_RecordingPreviewExport.cpp` add includes `#include "encoding/IntermediateQuality.h"`, `#include "encoding/VideoBitrate.h"`, `<QRandomGenerator>`, and a noise fixture helper in the anonymous namespace after `createRecording`:

```cpp
// A recording the encoder cannot compress: random pixels at quality 100,
// large enough that its average bitrate sits far above the quality-0 target
// (1280x720 @ kFrameRate: target 1,000,000 bps after the kMinBitrate floor,
// encoder budget 2,764,800 bps). Used to force the smart-save re-encode branch.
const QSize kNoiseSize(1280, 720);
constexpr int kNoiseFrameCount = 40;
constexpr quint32 kNoiseSeed = 20261003;

QString createNoiseRecording(const QString& path)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    encoder->setQuality(SnapTray::VideoBitrate::kMaxQuality);
    if (!encoder->start(path, kNoiseSize, kFrameRate)) return encoder->lastError();
    QRandomGenerator random(kNoiseSeed);
    QImage frame(kNoiseSize, QImage::Format_ARGB32);
    for (int i = 0; i < kNoiseFrameCount; ++i) {
        auto* pixels = reinterpret_cast<quint32*>(frame.bits());
        const qsizetype count = qsizetype(frame.width()) * frame.height();
        for (qsizetype p = 0; p < count; ++p) pixels[p] = 0xFF000000u | (random.generate() & 0x00FFFFFFu);
        const qint64 before = encoder->framesWritten();
        QElapsedTimer waitTimer;
        waitTimer.start();
        do {
            encoder->writeFrame(frame, i * kFrameIntervalMs);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(5);
        } while (waitTimer.elapsed() < 2000);
        if (encoder->framesWritten() != before + 1) return QStringLiteral("noise frame %1 rejected").arg(i);
    }
    QSignalSpy finishedSpy(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finishedSpy.isEmpty() && !finishedSpy.wait(10000)) return QStringLiteral("noise fixture did not finish");
    return finishedSpy.first().at(0).toBool() ? QString() : encoder->lastError();
}
```

Declare slots:

```cpp
    void saveMp4EditsUseOutputQuality_data();
    void saveMp4EditsUseOutputQuality();
    void smartSaveMovesLowBitrateRecording();
    void smartSaveReencodesHighBitrateRecording();
```

Implement:

```cpp
void tst_RecordingPreviewExport::saveMp4EditsUseOutputQuality_data()
{
    QTest::addColumn<bool>("trim");
    QTest::addColumn<bool>("crop");
    QTest::addColumn<int>("quality");
    QTest::newRow("trim-only low") << true << false << 0;
    QTest::newRow("trim-only high") << true << false << 100;
    QTest::newRow("crop-only low") << false << true << 0;
    QTest::newRow("crop-only high") << false << true << 100;
    QTest::newRow("trim+crop mid") << true << true << 55;
}

void tst_RecordingPreviewExport::saveMp4EditsUseOutputQuality()
{
    QFETCH(bool, trim);
    QFETCH(bool, crop);
    QFETCH(int, quality);
    const QSize sourceSize(160, 120);
    const QRect cropRect(32, 24, 96, 72);
    const qint64 trimStartMs = 300;

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize, false);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto restore = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
        RecordingPreviewBackend::outputQualityOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(FakeTranscodeOutcome::Real, QString(), log));
    };
    RecordingPreviewBackend::outputQualityOverride() = [quality]() { return quality; };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    if (trim) backend.setTrimStart(trimStartMs);
    if (crop) backend.setCropRect(cropRect);

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QSize outputSize = crop ? cropRect.size() : sourceSize;
    const int expectedBitrate = SnapTray::IntermediateQuality::outputBitrateFor(outputSize, kFrameRate, quality);
    std::lock_guard<std::mutex> lock(log->mutex);
    QCOMPARE(log->calls, 1);
    QCOMPARE(log->lastRequest.videoBitrate, expectedBitrate);
    QVERIFY(log->lastRequest.videoBitrate > 0);
}

void tst_RecordingPreviewExport::smartSaveMovesLowBitrateRecording()
{
    // Solid-colour frames compress far below any target: the file is moved as is.
    const QSize sourceSize(160, 120);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize, true);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto restore = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
        RecordingPreviewBackend::outputQualityOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(FakeTranscodeOutcome::Real, QString(), log));
    };
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    QSignalSpy closedSpy(&backend, &RecordingPreviewBackend::closed);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QTRY_COMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.first().at(0).toString(), inputPath);
    QCOMPARE(savedSpy.first().at(1).toSize(), QSize());
    QCOMPARE(closedSpy.count(), 1);
    QVERIFY(QFileInfo::exists(inputPath)); // the consumer of saveRequested moves it
    std::lock_guard<std::mutex> lock(log->mutex);
    QCOMPARE(log->calls, 0);
}

void tst_RecordingPreviewExport::smartSaveReencodesHighBitrateRecording()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createNoiseRecording(inputPath);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    auto realTranscoder = IVideoTranscoder::create();
    QVERIFY(realTranscoder);
    const VideoFileProbe sourceProbe = realTranscoder->probe(inputPath);
    QVERIFY(sourceProbe.valid);
    const int target = SnapTray::IntermediateQuality::outputBitrateFor(kNoiseSize, sourceProbe.frameRate, 0);
    const qint64 actual = SnapTray::IntermediateQuality::averageBitrate(QFileInfo(inputPath).size(), sourceProbe.durationMs);
    QVERIFY2(actual > target, qPrintable(QStringLiteral("fixture %1 bps is not above target %2").arg(actual).arg(target)));

    auto restore = qScopeGuard([]() {
        RecordingPreviewBackend::transcoderFactoryOverride() = {};
        RecordingPreviewBackend::outputQualityOverride() = {};
    });
    const auto log = std::make_shared<FakeTranscodeLog>();
    RecordingPreviewBackend::transcoderFactoryOverride() = [log]() {
        return std::unique_ptr<IVideoTranscoder>(new FakeTranscoder(FakeTranscodeOutcome::Real, QString(), log));
    };
    RecordingPreviewBackend::outputQualityOverride() = []() { return 0; };

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(kNoiseSize);
    backend.updateDuration(kNoiseFrameCount * kFrameIntervalMs);
    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QVERIFY(backend.isProcessing());
    QCOMPARE(backend.processStatus(), RecordingPreviewBackend::tr("Exporting video..."));
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 60000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);
    const QString outputPath = savedSpy.first().at(0).toString();
    QVERIFY(outputPath != inputPath);
    QVERIFY(!QFileInfo::exists(inputPath));
    const VideoFileProbe outputProbe = realTranscoder->probe(outputPath);
    QVERIFY(outputProbe.valid);
    QCOMPARE(outputProbe.videoSize, kNoiseSize);
    QVERIFY(partFiles(directory.path()).isEmpty());
    std::lock_guard<std::mutex> lock(log->mutex);
    QCOMPARE(log->calls, 1);
    QCOMPARE(log->lastRequest.startMs, qint64(0));
    QCOMPARE(log->lastRequest.endMs, qint64(-1));
    QVERIFY(log->lastRequest.cropRect.isEmpty());
    QCOMPARE(log->lastRequest.videoBitrate, target);
}
```

(`FakeTranscoder` with `FakeTranscodeOutcome::Real` and an empty silent path delegates to the real transcoder — confirm in the fake's `Real` case that `m_silentFixturePath` is unused; if `kFrameRate` is not a named constant in this file, add `constexpr int kFrameRate = 1000 / kFrameIntervalMs;` next to the existing constants.)

- [ ] **Step 2: Run the tests to verify they fail**

Build `Qml_RecordingPreviewExport`. Expected: compile error on `outputQualityOverride`.

- [ ] **Step 3: Implement**

`include/qml/RecordingPreviewBackend.h`: in the private section next to `transcoderFactoryOverride()`:

```cpp
    // Test seam for the user's output quality; empty means read
    // RecordingSettingsManager at save time.
    static std::function<int()>& outputQualityOverride();
    // smartSave: no edits — move the file if it already meets the user's
    // target bitrate, otherwise re-encode the full range at that bitrate.
    void performTranscode(bool smartSave);
```

(replace the existing `void performTranscode();` declaration) and a member next to `m_videoPath`:

```cpp
    int m_outputQuality = kDefaultTranscodeQuality; // snapshot taken when save() begins
```

`src/qml/RecordingPreviewBackend.mm`: include `encoding/IntermediateQuality.h` and `settings/RecordingSettingsManager.h`. Add:

```cpp
std::function<int()>& RecordingPreviewBackend::outputQualityOverride()
{
    static std::function<int()> override;
    return override;
}
```

In `save()`, after the processing/closeHandled guard:

```cpp
    // Output quality contract: snapshot the user's setting when Save begins;
    // every MP4 re-encode below uses it. Quality 80 is only the legacy default.
    m_outputQuality = outputQualityOverride() ? outputQualityOverride()()
                                              : RecordingSettingsManager::instance().quality();
```

Replace the MP4 branches:

```cpp
    if (hasTrim() || hasCrop()) {
        performTranscode(false);
        return;
    }
    // MP4 with no edits: move the intermediate if it already meets the
    // user's quality, otherwise re-encode it to that quality.
    performTranscode(true);
```

In `performTranscode(bool smartSave)`:

1. Capture `const int outputQuality = m_outputQuality;` next to the other captured values and add `smartSave`, `outputQuality` and `inputPath = request.inputPath` to the worker lambda's capture list.
2. At the top of the worker, before `auto transcoder = createTranscoder();` keep the existing code; replace the `unsupported` handling and the start of the success block with:

```cpp
        auto transcoder = createTranscoder();
        const bool unsupported = !transcoder;
        VideoTranscodeResult result;
        VideoFileProbe sourceProbe;
        bool moveInstead = false;
        if (unsupported) {
            // Without a transcoder an unedited save still works as before.
            moveInstead = smartSave;
            if (!moveInstead) result.errorMessage = unsupportedError;
        } else {
            sourceProbe = transcoder->probe(request.inputPath);
            if (smartSave && SnapTray::IntermediateQuality::smartSaveShouldMove(
                                 sourceProbe, QFileInfo(request.inputPath).size(), outputQuality)) {
                moveInstead = true;
            }
        }
        if (moveInstead) {
            qDebug() << "RecordingPreviewBackend: unedited recording meets the output quality; moving it";
            post([weakThis, inputPath]() {
                if (!weakThis) return;
                weakThis->m_isProcessing = false;
                emit weakThis->processingChanged();
                weakThis->m_saved = true;
                weakThis->close();
                emit weakThis->saveRequested(inputPath, QSize());
            });
            return;
        }
        if (!unsupported) {
            const QSize outputSize = request.cropRect.isEmpty() ? sourceProbe.videoSize : request.cropRect.size();
            request.videoBitrate = SnapTray::IntermediateQuality::outputBitrateFor(
                outputSize, sourceProbe.frameRate, outputQuality);
            qDebug() << "RecordingPreviewBackend: exporting at" << request.videoBitrate << "bps for"
                     << outputSize << "quality" << outputQuality;
            result = transcoder->transcode(request, progressCallback);
        }
```

where `progressCallback` is the existing `[weakThis, cancelToken, post](int percent) { ... return !cancelToken->load(); }` lambda, unchanged, assigned to a local `const IVideoTranscoder::ProgressCallback progressCallback = ...;` just above this block. `request` must be captured by value as a mutable copy (`[..., request, ...]() mutable` or copy it into a local `VideoTranscodeRequest work = request;` and use `work` from there on). In the validation block, replace the second `transcoder->probe(request.inputPath)` with the `sourceProbe` already taken. `expectedSize` in the existing validation must keep using the crop size or `sourceProbe.videoSize`.

3. Status: keep `m_processStatus = tr("Exporting video...")` for both modes (the move branch finishes before the overlay is visible for long).

- [ ] **Step 4: Run the tests to verify they pass**

Build and run `Qml_RecordingPreviewExport` and `Qml_RecordingPreviewCrop` (`-o` files). Expected: new slots pass; existing `saveMp4Edits`, `invalidTranscodeKeepsSourceAndRetries`, `destroyWhileExportingKeepsSource` still pass. If `smartSaveReencodesHighBitrateRecording` fails its precondition (`actual > target`), the native encoder undershot its budget on noise: double `kNoiseFrameCount` or raise `kNoiseSize` to 1920x1080 and re-run; do not weaken the assertion.

- [ ] **Step 5: Commit**

```bash
git add include/qml/RecordingPreviewBackend.h src/qml/RecordingPreviewBackend.mm tests/Qml/tst_RecordingPreviewExport.cpp
git commit -m "feat(recording): export previews at the user's quality and smart-save unedited recordings"
```

---

### Task 8: Settings caption, docs and changelog

**Files:**
- Modify: `src/qml/settings/RecordingSettings.qml:106-113`
- Modify: `docs/docs/recording.md:56-58`, `docs/zh-tw/docs/recording.md:55-57`
- Modify: `CHANGELOG.md` (Unreleased → Added)
- Test: `tests/Settings/tst_QmlTranslations.cpp` (entry list only; translations arrive in Task 9 — this task's test addition fails until then, so Tasks 8 and 9 commit together as described in Task 9 Step 5. If executing strictly task by task, add the test entry in Task 9 instead.)

- [ ] **Step 1: Settings caption**

In `src/qml/settings/RecordingSettings.qml`, directly after the `SettingsSlider { label: qsTr("Recording quality") ... }` block, add:

```qml
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: settingsBackend.recordingShowPreview || settingsBackend.recordingOutputFormat === 0
            text: qsTr("Quality of the saved video. With preview on, recordings are captured at high quality and converted to this quality when you save.")
            color: SemanticTokens.textTertiary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
            font.letterSpacing: SemanticTokens.letterSpacingDefault
        }
```

(Match the `Column` child indentation used by the slider; `SemanticTokens` is already imported in this file via `SnapTrayQml`.)

- [ ] **Step 2: Docs**

`docs/docs/recording.md`, replace the "Quality tuning" paragraph with:

```markdown
## Quality tuning

Open Settings > Recording and adjust frame rate, quality, countdown, and preview behavior.

The quality slider sets the quality of the saved video. When preview is on, the recording itself is captured at high quality so trims and crops do not lose detail; saving converts it to the selected quality, or keeps the file as is when it is already small enough. If the drive holding temporary recordings has less than about ten minutes of high-quality space free, SnapTray records at the selected quality instead and shows a warning. With preview off, recordings are written directly at the selected quality.
```

`docs/zh-tw/docs/recording.md`, replace the 品質調校 paragraph with:

```markdown
## 品質調校

在 Settings > Recording 可調整 FPS、畫質、倒數計時與預覽行為。

畫質滑桿決定的是儲存後影片的畫質。開啟預覽時,錄影本身會以高畫質擷取,讓裁剪與修剪不會損失細節;儲存時再轉成所選畫質,若檔案本來就夠小則直接保留原檔。若暫存錄影所在的磁碟剩餘空間不足約十分鐘的高畫質錄影,SnapTray 會改以所選畫質錄製並顯示警告。關閉預覽時,錄影會直接以所選畫質寫入。
```

- [ ] **Step 3: CHANGELOG**

Under `## [Unreleased]` → `### Added`, append:

```markdown
- Recordings made with the preview on are captured at high quality and converted to the selected quality when saved, so trimming and cropping keep their detail; unedited recordings that already meet the selected quality are saved as they are. Low disk space falls back to the selected quality with a warning.
```

- [ ] **Step 4: Commit (together with Task 9, see below)**

---

### Task 9: Translations for the new strings

**Files:**
- Modify: `translations/snaptray_{ar,cs,de,el,es_MX,fi,fr,it,ja,ko,lt,nl,pl,pt,pt_PT,ru,sr,sv,th,tr,vi,zh_CN,zh_HK,zh_TW}.ts` (24 files)
- Test: `tests/Settings/tst_QmlTranslations.cpp`

Two new source strings:

- **S1** (QML, context `RecordingSettings`, location `../src/qml/settings/RecordingSettings.qml`): `Quality of the saved video. With preview on, recordings are captured at high quality and converted to this quality when you save.`
- **S2** (C++, context `RecordingManager`, location `../src/RecordingManager.cpp`): `Not enough free disk space for high-quality recording. Recording at the selected quality instead.`

Insert each as a `<message>` in the matching `<context>` block of every file, following the neighbours' format (`<location filename="..." line="N" />` with the real line number, `<source>`, `<translation>` without `type="unfinished"`). Preserve each file's CRLF line endings and UTF-8 encoding; insert textually (a small Python script), do not run lupdate.

| Locale | S1 | S2 |
| --- | --- | --- |
| ar | جودة الفيديو المحفوظ. عند تشغيل المعاينة، تُسجَّل التسجيلات بجودة عالية ثم تُحوَّل إلى هذه الجودة عند الحفظ. | لا توجد مساحة كافية على القرص للتسجيل بجودة عالية. سيتم التسجيل بالجودة المحددة بدلاً من ذلك. |
| cs | Kvalita uloženého videa. Při zapnutém náhledu se nahrávky pořizují ve vysoké kvalitě a při uložení se převedou na tuto kvalitu. | Nedostatek volného místa na disku pro nahrávání ve vysoké kvalitě. Nahrává se ve zvolené kvalitě. |
| de | Qualität des gespeicherten Videos. Bei aktivierter Vorschau werden Aufnahmen in hoher Qualität aufgezeichnet und beim Speichern in diese Qualität umgewandelt. | Nicht genügend freier Speicherplatz für eine Aufnahme in hoher Qualität. Es wird stattdessen in der gewählten Qualität aufgenommen. |
| el | Ποιότητα του αποθηκευμένου βίντεο. Με ενεργή προεπισκόπηση, οι εγγραφές καταγράφονται σε υψηλή ποιότητα και μετατρέπονται σε αυτή την ποιότητα κατά την αποθήκευση. | Δεν υπάρχει αρκετός ελεύθερος χώρος στον δίσκο για εγγραφή υψηλής ποιότητας. Η εγγραφή γίνεται στην επιλεγμένη ποιότητα. |
| es_MX | Calidad del video guardado. Con la vista previa activada, las grabaciones se capturan en alta calidad y se convierten a esta calidad al guardar. | No hay suficiente espacio libre en disco para grabar en alta calidad. Se grabará con la calidad seleccionada. |
| fi | Tallennetun videon laatu. Kun esikatselu on käytössä, tallenteet kaapataan korkealaatuisina ja muunnetaan tähän laatuun tallennettaessa. | Levytilaa ei ole tarpeeksi korkealaatuiseen tallennukseen. Tallennetaan valitulla laadulla. |
| fr | Qualité de la vidéo enregistrée. Avec l'aperçu activé, les enregistrements sont capturés en haute qualité puis convertis à cette qualité lors de la sauvegarde. | Espace disque insuffisant pour un enregistrement en haute qualité. L'enregistrement se fera à la qualité sélectionnée. |
| it | Qualità del video salvato. Con l'anteprima attiva, le registrazioni vengono acquisite in alta qualità e convertite a questa qualità al salvataggio. | Spazio su disco insufficiente per la registrazione in alta qualità. La registrazione userà la qualità selezionata. |
| ja | 保存する動画の画質です。プレビューがオンのときは高画質で録画し、保存時にこの画質へ変換します。 | 高画質で録画するための空き容量が不足しています。選択した画質で録画します。 |
| ko | 저장되는 동영상의 화질입니다. 미리보기가 켜져 있으면 고화질로 녹화한 뒤 저장할 때 이 화질로 변환합니다. | 고화질 녹화를 위한 디스크 여유 공간이 부족합니다. 선택한 화질로 녹화합니다. |
| lt | Įrašyto vaizdo įrašo kokybė. Įjungus peržiūrą, įrašai fiksuojami aukšta kokybe ir išsaugant konvertuojami į šią kokybę. | Nepakanka laisvos vietos diske aukštos kokybės įrašymui. Įrašoma pasirinkta kokybe. |
| nl | Kwaliteit van de opgeslagen video. Met voorbeeld aan worden opnamen in hoge kwaliteit vastgelegd en bij het opslaan naar deze kwaliteit omgezet. | Onvoldoende vrije schijfruimte voor opnemen in hoge kwaliteit. Er wordt opgenomen in de geselecteerde kwaliteit. |
| pl | Jakość zapisanego wideo. Przy włączonym podglądzie nagrania są rejestrowane w wysokiej jakości i konwertowane do tej jakości podczas zapisu. | Za mało wolnego miejsca na dysku na nagrywanie w wysokiej jakości. Nagrywanie w wybranej jakości. |
| pt | Qualidade do vídeo salvo. Com a pré-visualização ativada, as gravações são capturadas em alta qualidade e convertidas para esta qualidade ao salvar. | Não há espaço livre suficiente em disco para gravar em alta qualidade. A gravação usará a qualidade selecionada. |
| pt_PT | Qualidade do vídeo guardado. Com a pré-visualização ativa, as gravações são captadas em alta qualidade e convertidas para esta qualidade ao guardar. | Não há espaço livre suficiente no disco para gravar em alta qualidade. A gravação será feita na qualidade selecionada. |
| ru | Качество сохранённого видео. При включённом предпросмотре записи ведутся в высоком качестве и при сохранении преобразуются в это качество. | Недостаточно свободного места на диске для записи в высоком качестве. Запись ведётся в выбранном качестве. |
| sr | Квалитет сачуваног видеа. Када је преглед укључен, снимци се бележе у високом квалитету и при чувању претварају у овај квалитет. | Нема довољно слободног простора на диску за снимање у високом квалитету. Снима се у изабраном квалитету. |
| sv | Kvaliteten på den sparade videon. Med förhandsgranskning på spelas inspelningar in i hög kvalitet och konverteras till denna kvalitet när du sparar. | Inte tillräckligt med ledigt diskutrymme för inspelning i hög kvalitet. Spelar in i vald kvalitet i stället. |
| th | คุณภาพของวิดีโอที่บันทึก เมื่อเปิดตัวอย่าง การบันทึกจะจับภาพด้วยคุณภาพสูงและแปลงเป็นคุณภาพนี้เมื่อบันทึก | พื้นที่ว่างในดิสก์ไม่พอสำหรับการบันทึกคุณภาพสูง จะบันทึกด้วยคุณภาพที่เลือกแทน |
| tr | Kaydedilen videonun kalitesi. Önizleme açıkken kayıtlar yüksek kalitede alınır ve kaydederken bu kaliteye dönüştürülür. | Yüksek kaliteli kayıt için yeterli boş disk alanı yok. Bunun yerine seçilen kalitede kaydediliyor. |
| vi | Chất lượng của video đã lưu. Khi bật xem trước, bản ghi được quay ở chất lượng cao và chuyển về chất lượng này khi lưu. | Không đủ dung lượng đĩa trống để ghi ở chất lượng cao. Sẽ ghi ở chất lượng đã chọn. |
| zh_CN | 保存视频的画质。开启预览时,录制会以高画质采集,保存时再转换为此画质。 | 磁盘可用空间不足,无法以高画质录制。将改用所选画质录制。 |
| zh_HK | 儲存影片的畫質。開啟預覽時,錄影會以高畫質擷取,儲存時再轉換為此畫質。 | 磁碟可用空間不足,無法以高畫質錄影。將改以所選畫質錄影。 |
| zh_TW | 儲存影片的畫質。開啟預覽時,錄影會以高畫質擷取,儲存時再轉換為此畫質。 | 磁碟可用空間不足,無法以高畫質錄影。將改以所選畫質錄影。 |

- [ ] **Step 1: Extend the failing test**

In `tests/Settings/tst_QmlTranslations.cpp`, find the table that lists `{"RecordingSettings", ...}` entries (the slot that checks recording-settings strings for all locales; if none exists, add the entry to the slot used for `RecordingPreview` strings with context `RecordingSettings`) and add:

```cpp
        {"RecordingSettings", "Quality of the saved video. With preview on, recordings are captured at high quality and converted to this quality when you save."},
```

- [ ] **Step 2: Run the test to verify it fails**

Build and run `Settings_QmlTranslations`. Expected: the first locale fails on the missing S1 translation.

- [ ] **Step 3: Insert the 48 messages**

Use a Python script in the scratchpad that, for each file, locates `<context>\r\n    <name>RecordingSettings</name>` and `<name>RecordingManager</name>`, and inserts before that context's closing `</context>` a message block formatted exactly like its neighbours (indentation, CRLF, `<location ... line="N" />` with the line of the `qsTr`/`tr` call found by grepping the source).

- [ ] **Step 4: Verify**

Build the main target (lrelease runs; expect "0 unfinished" per locale and no warnings), run `Settings_QmlTranslations` (pass). `git diff --stat` must show exactly 24 `.ts` files with 10 insertions each (two 5-line messages) plus the test.

- [ ] **Step 5: Commit Tasks 8 and 9**

```bash
git add src/qml/settings/RecordingSettings.qml docs/docs/recording.md docs/zh-tw/docs/recording.md CHANGELOG.md translations/*.ts tests/Settings/tst_QmlTranslations.cpp
git commit -m "docs(recording): explain output quality and translate the new strings"
```

---

## Final verification

- `cmake --build build` (whole tree), then `ctest --test-dir build -j4 -E Annotations_AnnotationLayer` → 100 % (AnnotationLayer is a known five-minute test on this machine, not a regression).
- Manual check on Windows: Settings → preview on → record 10 s → the temp MP4 in `%TEMP%` is noticeably larger than a preview-off recording of the same content; Save without edits shows the export overlay briefly and the saved file is smaller; Save with a crop produces a file at the selected quality.
- Open items for the ledger: `Encoding_AVFoundationEncoderIntermediate` must run on macOS before release; `kUseVideoToolboxQuality` stays off until that run also validates `AVVideoQualityKey` on H.264.
