# Recording Preview Crop (Phase 1) Implementation Plan

## Implementation audit — 2026-10-04

- [x] The planned core implementation is present on dev-3 (baseline `7afc5b80`).
- [ ] Complete all native platform/hardware acceptance gates. Implementation presence is not runtime proof.

The historical step checkboxes below retain the original execution recipe. Current remaining work and evidence are tracked in [dev-3 completion](2026-10-04-dev-3-completion.md); the review tracker remains authoritative for review IDs.

> Implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This document contains the Phase 1 specification and does not require an external design document or workflow skill.

**Goal:** Let users choose a region of a finished screen recording by cropping in RecordingPreview, and export MP4/GIF/WebP with that crop. MP4 trims and crops must keep their audio.

**Architecture:**
- Recording stays screen-first: it is always full screen. The crop is chosen afterwards, in the Preview window.
- The crop rect is stored in **video pixels**, even-aligned, with a minimum side of 64. Pure helpers in `snaptray_core` do the geometry.
- GIF/WebP crop every frame on the existing conversion path.
- MP4 trim and crop move off `VideoTrimmer` onto a new blocking `IVideoTranscoder`. On macOS it uses AVAssetReader + AVAssetWriter; on Windows, the Media Foundation Source Reader + Sink Writer. Both attempt AAC passthrough. If audio cannot be preserved, export fails and retains the original; a verified Windows PCM-to-AAC fallback is permitted.

**Tech Stack:** Qt 6.11.2 (Quick/QML, Concurrent, Test), C++17, Objective-C++ with ARC, AVFoundation/CoreMedia/CoreVideo, Media Foundation, CMake + Ninja.

**Specification ownership:** This document is the complete Phase 1 contract. The repository-local `docs/superpowers/plans/2026-10-02-region-recording-longshot-roadmap.md` owns the cross-phase requirements. Design decisions are recorded here as of 2026-10-02.

## Interaction and export contract

- States are no crop, editing a draft, and committed crop. First entry with no crop starts with an empty draft: dragging anywhere inside the video creates a selection. An existing selection can be moved inside, resized with handles, or replaced by dragging outside it.
- Empty drafts have no handles. Enter applies; Escape cancels only the draft and restores the committed state. Applying an empty draft is a no-op. Clearing a committed crop uses the toolbar clear action.
- Video playback and scrubbing remain available while editing; use Space or the play button because video clicks belong to the crop editor.
- Toolbar Save and the existing save shortcut share one guarded save function that applies the draft first. Enter during editing only applies; Enter outside editing saves.
- Snapping must preserve the content bounds and minimum size. Only legal snap targets are eligible. The committed crop is normalized to even video pixels with the backend minimum; the committed size chip is authoritative.
- MP4 without edits uses the original file in Phase 1. Trim or crop uses the transcoder. GIF/WebP use the existing conversion path, cropping frames before encoding; Windows keeps its player fallback.
- For an input with audio, a successful MP4 output must retain usable audio over the selected interval. Unsupported passthrough, sample retiming/write errors, or invalid output must fail safely, unless a verified audio re-encode succeeds. Never silently downgrade to a silent output.
- Validate the completed working file before promotion and before deleting the source. Failure/cancellation removes partial output, preserves the source, and leaves export retryable.
- Phase 1 keeps the legacy edit quality default of 80. Phase 3 must explicitly set output bitrate from the user's quality for every MP4 re-encode, including trim/crop; this default must not become the Phase 3 product policy.

## Global Constraints

**Product rules**
- Recording capture stays full screen. Do not touch Region Selector, and do not add any region-recording UI before recording starts.
- Crop aspect presets: **Free only**.
- `showPreview = false` (direct save) behaviour is unchanged.

**Crop geometry**
- Crop coordinates are video pixels.
- `x`, `y`, `width` and `height` are all even.
- Minimum side is `kMinCropSide = 64`, or the even-floored frame size if the frame is smaller.
- A crop that covers the full frame means "no crop".

**Platforms**
- Windows GIF/WebP keep the existing `IVideoPlayer` fallback. Do not add a Media Foundation frame reader.
- macOS 14+ and Windows 10+.
- Linux beta must still compile. `IVideoTranscoder::create()` returns `nullptr` there, and recording stays hidden.

**Code rules**
- Use settings managers instead of raw `QSettings`.
- Use named constants instead of magic numbers.
- Use `qWarning()` for API failures and `qDebug()` for diagnostics.

**Translations**
- Every new user-facing QML/C++ string is translated in all 24 `translations/snaptray_*.ts` files.
- Each new string is covered by an all-locales check in `tests/Settings/tst_QmlTranslations.cpp`.

**Build and test commands**
- Build on macOS: `./scripts/build.sh`. Build on Windows: `scripts\build.bat`.
- Run a single test on macOS: `ctest --test-dir build -R <TestName> --output-on-failure`.
- Run a single test on Windows: prepend `%QT_PATH%\bin` to `PATH` first, then run the same `ctest` command.
- Run the full suite with `./scripts/run-tests.sh` or `scripts\run-tests.bat`.

---

## File Structure

| File | Status | Responsibility |
| --- | --- | --- |
| `include/utils/VideoCropGeometry.h`, `src/utils/VideoCropGeometry.cpp` | new (snaptray_core) | aspect-fit rect, crop normalization, view↔video mapping |
| `tests/Utils/tst_VideoCropGeometry.cpp` | new | table-driven geometry tests |
| `include/encoding/VideoBitrate.h` | new, header-only | single H.264 bitrate formula shared by encoders + transcoder |
| `tests/Encoding/tst_VideoBitrate.cpp` | new | bitrate formula tests |
| `src/AVFoundationEncoder.mm`, `src/MediaFoundationEncoder.cpp` | modify | use `VideoBitrate::forQuality` |
| `include/qml/VideoPlaybackItem.h`, `src/qml/VideoPlaybackItem.cpp` | modify | `contentRect` property |
| `include/qml/RecordingPreviewBackend.h`, `src/qml/RecordingPreviewBackend.mm` | modify | crop state, cropped GIF/WebP, MP4 export via transcoder |
| `tests/Qml/tst_RecordingPreviewCrop.cpp` | new | backend crop state tests (all platforms) |
| `tests/Qml/tst_RecordingPreviewExport.cpp` | modify | cropped GIF/WebP and MP4 edit export tests (macOS) |
| `include/video/IVideoTranscoder.h`, `src/video/IVideoTranscoder.cpp` | new (snaptray_platform) | transcoder interface + platform factory |
| `src/video/AVFoundationTranscoder_mac.mm` | new | macOS implementation |
| `src/video/MediaFoundationTranscoder_win.cpp` | new | Windows implementation |
| `tests/Video/tst_VideoTranscoder.cpp` | new | cross-platform transcoder integration tests |
| `src/qml/recording/RecordingCropOverlay.qml` | new | crop editing overlay |
| `tests/Qml/tst_RecordingCropOverlay.cpp` | new | overlay geometry function tests |
| `src/qml/recording/RecordingPreview.qml` | modify | crop button, overlay wiring, keys |
| `translations/snaptray_*.ts` (24), `tests/Settings/tst_QmlTranslations.cpp` | modify | new strings |
| `docs/docs/recording.md`, `docs/zh-tw/docs/recording.md`, `CHANGELOG.md`, `CLAUDE.md` | modify | user docs + rule clarification |

---

### Task 1: Crop geometry helpers

**Files:**
- Create: `include/utils/VideoCropGeometry.h`
- Create: `src/utils/VideoCropGeometry.cpp`
- Modify: `CMakeLists.txt`, the snaptray_core source list. Add the new file next to `src/utils/ScreenCaptureRegionUtils.cpp` at line 341.
- Test: `tests/Utils/tst_VideoCropGeometry.cpp`, registered in `tests/CMakeLists.txt` after the `Utils_ScreenCaptureRegionUtils` block at lines 250-253.

**Interfaces:**
- Produces:
  ```cpp
  namespace SnapTray::VideoCropGeometry {
  constexpr int kMinCropSide = 64;
  QRectF aspectFitRect(const QSize& frameSize, const QSizeF& itemSize);
  QRect normalizeCropRect(const QRect& rect, const QSize& frameSize);
  QRect viewToVideo(const QRectF& viewRect, const QRectF& contentRect, const QSize& frameSize);
  QRectF videoToView(const QRect& videoRect, const QRectF& contentRect, const QSize& frameSize);
  }
  ```

- [ ] **Step 1: Write the failing test** at `tests/Utils/tst_VideoCropGeometry.cpp`

```cpp
#include <QtTest/QtTest>

#include "utils/VideoCropGeometry.h"

using namespace SnapTray::VideoCropGeometry;

class tst_VideoCropGeometry : public QObject
{
    Q_OBJECT

private slots:
    void aspectFitRect_data();
    void aspectFitRect();
    void normalizeCropRect_data();
    void normalizeCropRect();
    void viewToVideoMapsThroughContentRect();
    void videoToViewIsInverseOfViewToVideo();
    void mappingRejectsEmptyContent();
};

void tst_VideoCropGeometry::aspectFitRect_data()
{
    QTest::addColumn<QSize>("frame");
    QTest::addColumn<QSizeF>("item");
    QTest::addColumn<QRectF>("expected");
    QTest::newRow("wide frame letterboxed") << QSize(200, 100) << QSizeF(400, 400) << QRectF(0, 100, 400, 200);
    QTest::newRow("tall frame pillarboxed") << QSize(100, 200) << QSizeF(400, 300) << QRectF(125, 0, 150, 300);
    QTest::newRow("exact fit") << QSize(320, 180) << QSizeF(320, 180) << QRectF(0, 0, 320, 180);
    QTest::newRow("empty frame") << QSize() << QSizeF(400, 300) << QRectF();
    QTest::newRow("empty item") << QSize(320, 180) << QSizeF() << QRectF();
}

void tst_VideoCropGeometry::aspectFitRect()
{
    QFETCH(QSize, frame);
    QFETCH(QSizeF, item);
    QFETCH(QRectF, expected);
    QCOMPARE(SnapTray::VideoCropGeometry::aspectFitRect(frame, item), expected);
}

void tst_VideoCropGeometry::normalizeCropRect_data()
{
    QTest::addColumn<QRect>("input");
    QTest::addColumn<QSize>("frame");
    QTest::addColumn<QRect>("expected");
    const QSize hd(1920, 1080);
    QTest::newRow("even rect unchanged") << QRect(100, 100, 640, 360) << hd << QRect(100, 100, 640, 360);
    QTest::newRow("odd origin and size") << QRect(101, 51, 641, 361) << hd << QRect(100, 50, 642, 362);
    QTest::newRow("clamped to frame") << QRect(1800, 1000, 400, 400) << hd << QRect(1800, 1000, 120, 80);
    QTest::newRow("too small grows") << QRect(10, 10, 20, 20) << hd << QRect(10, 10, 64, 64);
    QTest::newRow("too small at corner shifts in") << QRect(1900, 1060, 10, 10) << hd << QRect(1856, 1016, 64, 64);
    QTest::newRow("full frame means no crop") << QRect(0, 0, 1920, 1080) << hd << QRect();
    QTest::newRow("odd frame full means no crop") << QRect(0, 0, 1921, 1081) << QSize(1921, 1081) << QRect();
    QTest::newRow("outside frame") << QRect(3000, 3000, 10, 10) << hd << QRect();
    QTest::newRow("empty frame") << QRect(0, 0, 100, 100) << QSize() << QRect();
    // 48x40 frame: minimum side shrinks to the frame, so the crop covers it all.
    QTest::newRow("tiny frame full means no crop") << QRect(0, 0, 10, 10) << QSize(48, 40) << QRect();
}

void tst_VideoCropGeometry::normalizeCropRect()
{
    QFETCH(QRect, input);
    QFETCH(QSize, frame);
    QFETCH(QRect, expected);
    QCOMPARE(SnapTray::VideoCropGeometry::normalizeCropRect(input, frame), expected);
}

void tst_VideoCropGeometry::viewToVideoMapsThroughContentRect()
{
    // 800x400 video drawn at half scale, 100 px below the item top.
    const QRectF content(0, 100, 400, 200);
    const QSize frame(800, 400);
    QCOMPARE(viewToVideo(QRectF(100, 150, 100, 50), content, frame), QRect(200, 100, 200, 100));
}

void tst_VideoCropGeometry::videoToViewIsInverseOfViewToVideo()
{
    const QRectF content(40, 0, 320, 180);
    const QSize frame(1920, 1080);
    const QRect video(600, 300, 640, 360);
    QCOMPARE(viewToVideo(videoToView(video, content, frame), content, frame), video);
}

void tst_VideoCropGeometry::mappingRejectsEmptyContent()
{
    QCOMPARE(viewToVideo(QRectF(0, 0, 10, 10), QRectF(), QSize(100, 100)), QRect());
    QCOMPARE(videoToView(QRect(0, 0, 10, 10), QRectF(), QSize(100, 100)), QRectF());
}

QTEST_MAIN(tst_VideoCropGeometry)
#include "tst_VideoCropGeometry.moc"
```

Register it in `tests/CMakeLists.txt` right after line 253:

```cmake
add_executable(Utils_VideoCropGeometry Utils/tst_VideoCropGeometry.cpp)
target_link_libraries(Utils_VideoCropGeometry PRIVATE snaptray_core Qt6::Test)
add_test(NAME Utils_VideoCropGeometry COMMAND Utils_VideoCropGeometry)
set_tests_properties(Utils_VideoCropGeometry PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `./scripts/build.sh`
Expected: the build FAILS with `utils/VideoCropGeometry.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`include/utils/VideoCropGeometry.h`:

```cpp
#pragma once

#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>

// Geometry for cropping a recorded video in the preview window.
// All crop rects are in video pixels; view rects are in the preview item's coordinates.
namespace SnapTray::VideoCropGeometry {

constexpr int kMinCropSide = 64;

// Aspect-fit `frameSize` inside `itemSize`, centred. Matches VideoPlaybackItem letterboxing.
QRectF aspectFitRect(const QSize& frameSize, const QSizeF& itemSize);

// Clamp to the frame, floor origin and size to even pixels, enforce kMinCropSide.
// Returns an empty QRect when the frame is invalid, the rect misses the frame,
// or the result covers the whole (even-floored) frame.
QRect normalizeCropRect(const QRect& rect, const QSize& frameSize);

QRect viewToVideo(const QRectF& viewRect, const QRectF& contentRect, const QSize& frameSize);
QRectF videoToView(const QRect& videoRect, const QRectF& contentRect, const QSize& frameSize);

} // namespace SnapTray::VideoCropGeometry
```

`src/utils/VideoCropGeometry.cpp`:

```cpp
#include "utils/VideoCropGeometry.h"

#include <QtGlobal>

#include <algorithm>

namespace SnapTray::VideoCropGeometry {

namespace {
constexpr int kEvenMask = ~1;
}

QRectF aspectFitRect(const QSize& frameSize, const QSizeF& itemSize)
{
    if (frameSize.isEmpty() || itemSize.isEmpty()) {
        return {};
    }
    const QSize item(static_cast<int>(itemSize.width()), static_cast<int>(itemSize.height()));
    const QSize scaled = frameSize.scaled(item, Qt::KeepAspectRatio);
    const int x = (item.width() - scaled.width()) / 2;
    const int y = (item.height() - scaled.height()) / 2;
    return QRectF(x, y, scaled.width(), scaled.height());
}

QRect normalizeCropRect(const QRect& rect, const QSize& frameSize)
{
    if (frameSize.isEmpty() || rect.isEmpty()) {
        return {};
    }
    const QRect clipped = rect.intersected(QRect(QPoint(0, 0), frameSize));
    if (clipped.isEmpty()) {
        return {};
    }

    const int maxWidth = frameSize.width() & kEvenMask;
    const int maxHeight = frameSize.height() & kEvenMask;
    const int minWidth = std::min(kMinCropSide, maxWidth);
    const int minHeight = std::min(kMinCropSide, maxHeight);

    int x = clipped.x() & kEvenMask;
    int y = clipped.y() & kEvenMask;
    int width = std::max(minWidth, (clipped.x() + clipped.width() - x) & kEvenMask);
    int height = std::max(minHeight, (clipped.y() + clipped.height() - y) & kEvenMask);
    width = std::min(width, maxWidth);
    height = std::min(height, maxHeight);
    x = std::min(x, maxWidth - width);
    y = std::min(y, maxHeight - height);

    if (x == 0 && y == 0 && width == maxWidth && height == maxHeight) {
        return {};
    }
    return QRect(x, y, width, height);
}

QRect viewToVideo(const QRectF& viewRect, const QRectF& contentRect, const QSize& frameSize)
{
    if (contentRect.isEmpty() || frameSize.isEmpty()) {
        return {};
    }
    const qreal scaleX = frameSize.width() / contentRect.width();
    const qreal scaleY = frameSize.height() / contentRect.height();
    const QRectF local = viewRect.normalized().translated(-contentRect.topLeft());
    const int left = qRound(local.left() * scaleX);
    const int top = qRound(local.top() * scaleY);
    const int right = qRound(local.right() * scaleX);
    const int bottom = qRound(local.bottom() * scaleY);
    return QRect(left, top, right - left, bottom - top);
}

QRectF videoToView(const QRect& videoRect, const QRectF& contentRect, const QSize& frameSize)
{
    if (contentRect.isEmpty() || frameSize.isEmpty() || videoRect.isEmpty()) {
        return {};
    }
    const qreal scaleX = contentRect.width() / frameSize.width();
    const qreal scaleY = contentRect.height() / frameSize.height();
    return QRectF(contentRect.x() + videoRect.x() * scaleX,
                  contentRect.y() + videoRect.y() * scaleY,
                  videoRect.width() * scaleX,
                  videoRect.height() * scaleY);
}

} // namespace SnapTray::VideoCropGeometry
```

Add `src/utils/VideoCropGeometry.cpp` to the snaptray_core list in `CMakeLists.txt`, right after `src/utils/ScreenCaptureRegionUtils.cpp` (line 341).

- [ ] **Step 4: Run the test and verify it passes**

Run: `./scripts/build.sh && ctest --test-dir build -R Utils_VideoCropGeometry --output-on-failure`
Expected: PASS (all rows).

- [ ] **Step 5: Commit**

```bash
git add include/utils/VideoCropGeometry.h src/utils/VideoCropGeometry.cpp CMakeLists.txt tests/Utils/tst_VideoCropGeometry.cpp tests/CMakeLists.txt
git commit -m "feat(recording): add video crop geometry helpers"
```

---

### Task 2: `VideoPlaybackItem.contentRect`

**Files:**
- Modify: `include/qml/VideoPlaybackItem.h`
- Modify: `src/qml/VideoPlaybackItem.cpp`. The edits go in `paint()` at lines 53-68 and `refreshScaledFrameForCurrentSize()` at lines 235-260.

**Interfaces:**
- Consumes: `SnapTray::VideoCropGeometry::aspectFitRect` (Task 1).
- Produces: QML property `contentRect` (`QRectF`, NOTIFY `contentRectChanged`). It is the rect, in item coordinates, where the video frame is drawn.

The geometry is already covered by Task 1. This task is a refactor plus a new property, so it is verified with the build and the existing Qml tests.

- [ ] **Step 1: Add the property to the header**

In `include/qml/VideoPlaybackItem.h`, add this next to the other `Q_PROPERTY` lines:

```cpp
    Q_PROPERTY(QRectF contentRect READ contentRect NOTIFY contentRectChanged)
```

Add a public getter:

```cpp
    QRectF contentRect() const { return m_contentRect; }
```

Add a signal next to `videoLoaded`:

```cpp
    void contentRectChanged();
```

Add a private member:

```cpp
    QRectF m_contentRect;
```

Add `#include <QRectF>` if it is not already included.

- [ ] **Step 2: Compute it in one place**

In `src/qml/VideoPlaybackItem.cpp`, add `#include "utils/VideoCropGeometry.h"`. Then, inside the `if (itemSize != m_lastItemSize || m_currentFrame.size() != m_lastFrameSize)` block of `refreshScaledFrameForCurrentSize()`, right after `m_targetScaledSize` is assigned, add:

```cpp
        const QRectF contentRect = SnapTray::VideoCropGeometry::aspectFitRect(
            m_currentFrame.size(), QSizeF(width(), height()));
        if (contentRect != m_contentRect) {
            m_contentRect = contentRect;
            emit contentRectChanged();
        }
```

In `paint()`, replace the letterbox computation

```cpp
    int x = (width() - m_scaledFrame.width()) / 2;
    int y = (height() - m_scaledFrame.height()) / 2;
    painter->drawImage(x, y, m_scaledFrame);
```

with

```cpp
    painter->drawImage(m_contentRect.topLeft().toPoint(), m_scaledFrame);
```

This way the overlay and the painted frame share a single source of truth.

- [ ] **Step 3: Build and run the existing Qml tests**

Run: `./scripts/build.sh && ctest --test-dir build -R "Qml_" --output-on-failure`
Expected: build succeeds; all `Qml_*` tests PASS.

- [ ] **Step 4: Commit**

```bash
git add include/qml/VideoPlaybackItem.h src/qml/VideoPlaybackItem.cpp
git commit -m "feat(recording): expose VideoPlaybackItem contentRect"
```

---

### Task 3: Backend crop state

**Files:**
- Modify: `include/qml/RecordingPreviewBackend.h`
- Modify: `src/qml/RecordingPreviewBackend.mm`
- Test: `tests/Qml/tst_RecordingPreviewCrop.cpp`, registered in `tests/CMakeLists.txt` right after `Qml_ScreenPickerViewModel` (lines 178-181). Register it outside `if(APPLE)`, because it does not encode video.

**Interfaces:**
- Consumes: `normalizeCropRect`, `viewToVideo`, `videoToView` (Task 1).
- Produces:
  - Properties (Q_PROPERTY):
    - `QRect cropRect`, NOTIFY `cropRectChanged`
    - `bool hasCrop`, NOTIFY `cropRectChanged`
    - `QSize videoSize`, NOTIFY `videoSizeChanged`
  - Invokables (Q_INVOKABLE):
    - `void updateVideoSize(const QSize&)`
    - `void setCropRect(const QRect& videoRect)`
    - `void setCropFromView(const QRectF& viewRect, const QRectF& contentRect)`
    - `QRectF cropRectInView(const QRectF& contentRect) const`, which returns `contentRect` when there is no crop
    - `void clearCrop()`
  - Private member: `QRect m_cropRect`, readable by the friend test class.

- [ ] **Step 1: Write the failing test** at `tests/Qml/tst_RecordingPreviewCrop.cpp`

```cpp
#include <QtTest/QtTest>

#include "qml/RecordingPreviewBackend.h"

#include <QSignalSpy>

class tst_RecordingPreviewCrop : public QObject
{
    Q_OBJECT

private slots:
    void cropIgnoredUntilVideoSizeKnown();
    void setCropRectNormalizes();
    void fullFrameCropClears();
    void clearCropResets();
    void setCropFromViewMapsToVideoPixels();
    void cropRectInViewFallsBackToContent();
    void videoSizeChangeRenormalizes();
};

void tst_RecordingPreviewCrop::cropIgnoredUntilVideoSizeKnown()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.setCropRect(QRect(100, 100, 640, 360));
    QVERIFY(!backend.hasCrop());
    QCOMPARE(backend.cropRect(), QRect());
}

void tst_RecordingPreviewCrop::setCropRectNormalizes()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    QSignalSpy spy(&backend, &RecordingPreviewBackend::cropRectChanged);
    backend.setCropRect(QRect(101, 51, 641, 361));
    QCOMPARE(backend.cropRect(), QRect(100, 50, 642, 362));
    QVERIFY(backend.hasCrop());
    QCOMPARE(spy.count(), 1);
    backend.setCropRect(QRect(101, 51, 641, 361));
    QCOMPARE(spy.count(), 1); // unchanged value does not re-notify
}

void tst_RecordingPreviewCrop::fullFrameCropClears()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    backend.setCropRect(QRect(100, 100, 640, 360));
    backend.setCropRect(QRect(0, 0, 1920, 1080));
    QVERIFY(!backend.hasCrop());
}

void tst_RecordingPreviewCrop::clearCropResets()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    backend.setCropRect(QRect(100, 100, 640, 360));
    QSignalSpy spy(&backend, &RecordingPreviewBackend::cropRectChanged);
    backend.clearCrop();
    QVERIFY(!backend.hasCrop());
    QCOMPARE(spy.count(), 1);
    backend.clearCrop();
    QCOMPARE(spy.count(), 1);
}

void tst_RecordingPreviewCrop::setCropFromViewMapsToVideoPixels()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(800, 400));
    backend.setCropFromView(QRectF(100, 150, 100, 50), QRectF(0, 100, 400, 200));
    QCOMPARE(backend.cropRect(), QRect(200, 100, 200, 100));
}

void tst_RecordingPreviewCrop::cropRectInViewFallsBackToContent()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(800, 400));
    const QRectF content(0, 100, 400, 200);
    QCOMPARE(backend.cropRectInView(content), content);
    backend.setCropRect(QRect(200, 100, 200, 100));
    QCOMPARE(backend.cropRectInView(content), QRectF(100, 150, 100, 50));
}

void tst_RecordingPreviewCrop::videoSizeChangeRenormalizes()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(1920, 1080));
    backend.setCropRect(QRect(1200, 600, 640, 360));
    backend.updateVideoSize(QSize(1280, 720));
    QCOMPARE(backend.cropRect(), QRect(1200, 600, 80, 120));
}

QTEST_MAIN(tst_RecordingPreviewCrop)
#include "tst_RecordingPreviewCrop.moc"
```

Here is why the last test expects `QRect(1200, 600, 80, 120)`. Clipping (1200,600,640,360) to 1280x720 gives (1200,600,80,120). Both values are already even and at least 64, so normalization keeps them.

Register it:

```cmake
add_executable(Qml_RecordingPreviewCrop Qml/tst_RecordingPreviewCrop.cpp)
target_link_libraries(Qml_RecordingPreviewCrop PRIVATE snaptray_ui snaptray_qmlplugin Qt6::Test)
add_test(NAME Qml_RecordingPreviewCrop COMMAND Qml_RecordingPreviewCrop)
set_tests_properties(Qml_RecordingPreviewCrop PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `./scripts/build.sh`
Expected: compile FAILS with `no member named 'setCropRect' in 'RecordingPreviewBackend'`.

- [ ] **Step 3: Implement**

In `include/qml/RecordingPreviewBackend.h`, add `#include <QRect>`, `#include <QRectF>` and `#include <QSize>`. Then add the properties after the trim properties:

```cpp
    // Crop (video pixels; empty = no crop)
    Q_PROPERTY(QRect cropRect READ cropRect NOTIFY cropRectChanged)
    Q_PROPERTY(bool hasCrop READ hasCrop NOTIFY cropRectChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
```

Add the public getters:

```cpp
    QRect cropRect() const { return m_cropRect; }
    bool hasCrop() const { return !m_cropRect.isEmpty(); }
    QSize videoSize() const { return m_videoSize; }
```

Add these next to the other `Q_INVOKABLE`s:

```cpp
    Q_INVOKABLE void updateVideoSize(const QSize &size);
    Q_INVOKABLE void setCropRect(const QRect &videoRect);
    Q_INVOKABLE void setCropFromView(const QRectF &viewRect, const QRectF &contentRect);
    Q_INVOKABLE QRectF cropRectInView(const QRectF &contentRect) const;
    Q_INVOKABLE void clearCrop();
```

Add the signals:

```cpp
    void cropRectChanged();
    void videoSizeChanged();
```

Add the private members next to `m_trimEnd`:

```cpp
    QRect m_cropRect;
    QSize m_videoSize;
```

In `src/qml/RecordingPreviewBackend.mm`, add `#include "utils/VideoCropGeometry.h"`, then add the following after `toggleTrim()`:

```cpp
// ---------- Crop ----------

void RecordingPreviewBackend::updateVideoSize(const QSize &size)
{
    if (m_videoSize == size) {
        return;
    }
    m_videoSize = size;
    emit videoSizeChanged();
    setCropRect(m_cropRect);
}

void RecordingPreviewBackend::setCropRect(const QRect &videoRect)
{
    const QRect normalized = SnapTray::VideoCropGeometry::normalizeCropRect(videoRect, m_videoSize);
    if (normalized == m_cropRect) {
        return;
    }
    m_cropRect = normalized;
    emit cropRectChanged();
}

void RecordingPreviewBackend::setCropFromView(const QRectF &viewRect, const QRectF &contentRect)
{
    setCropRect(SnapTray::VideoCropGeometry::viewToVideo(viewRect, contentRect, m_videoSize));
}

QRectF RecordingPreviewBackend::cropRectInView(const QRectF &contentRect) const
{
    if (!hasCrop()) {
        return contentRect;
    }
    return SnapTray::VideoCropGeometry::videoToView(m_cropRect, contentRect, m_videoSize);
}

void RecordingPreviewBackend::clearCrop()
{
    if (m_cropRect.isNull()) {
        return;
    }
    m_cropRect = QRect();
    emit cropRectChanged();
}
```

- [ ] **Step 4: Run the test and verify it passes**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_RecordingPreviewCrop --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/qml/RecordingPreviewBackend.h src/qml/RecordingPreviewBackend.mm tests/Qml/tst_RecordingPreviewCrop.cpp tests/CMakeLists.txt
git commit -m "feat(recording): add crop state to recording preview backend"
```

---

### Task 4: Cropped GIF/WebP export

**Files:**
- Modify: `src/qml/RecordingPreviewBackend.mm`, in `performFormatConversion()`. The edit points are the capture list (lines 430-459), the encoder start (lines 553 and 558) and the frame loop (lines 626-633).
- Test: `tests/Qml/tst_RecordingPreviewExport.cpp`. It is macOS-only and already registered.

**Interfaces:**
- Consumes: `m_cropRect` (Task 3).

- [ ] **Step 1: Write the failing test**

In `tests/Qml/tst_RecordingPreviewExport.cpp`:

1. Change the fixture signature to `QString createRecording(const QString& path, qint64 firstFrameMs, const QSize& frameSize = kFrameSize)`.
2. Inside it, replace both uses of `kFrameSize` (the `encoder->start(...)` call and `QImage frame(...)`) with `frameSize`.
3. Add the slots `void saveCroppedAnimation_data();` and `void saveCroppedAnimation();` to the class.
4. Add the following above `QTEST_MAIN`:

```cpp
void tst_RecordingPreviewExport::saveCroppedAnimation_data()
{
    QTest::addColumn<int>("format");
    QTest::newRow("gif") << int(RecordingPreviewBackend::GIF);
    QTest::newRow("webp") << int(RecordingPreviewBackend::WebP);
}

void tst_RecordingPreviewExport::saveCroppedAnimation()
{
    QFETCH(int, format);
    const QSize sourceSize(160, 120);
    const QRect crop(32, 24, 96, 72);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(format);
    backend.updateVideoSize(sourceSize);
    backend.setCropRect(crop);
    QCOMPARE(backend.cropRect(), crop);

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);

    QImageReader reader(savedSpy.first().at(0).toString());
    QVERIFY2(reader.canRead(), qPrintable(reader.errorString()));
    const QImage first = reader.read();
    QCOMPARE(first.size(), crop.size());
    QVERIFY(isRed(first.pixelColor(first.rect().center())));
    QVERIFY(QDir(directory.path()).entryList(QStringList(QStringLiteral("*.part-*")), QDir::Files).isEmpty());
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_RecordingPreviewExport --output-on-failure`
Expected: `saveCroppedAnimation` FAILS with `Compared values are not the same: Actual (first.size()): QSize(160, 120) Expected (crop.size()): QSize(96, 72)`.

- [ ] **Step 3: Implement**

In `performFormatConversion()`:
1. Next to `const qint64 requestedEndMs = m_trimEnd;`, add `const QRect cropRect = m_cropRect;`, and add `cropRect` to the `QtConcurrent::run` lambda capture list.
2. After `const QSize vidSize = ...` (line 530), add:

```cpp
        const QSize outputSize = cropRect.isEmpty() ? vidSize : cropRect.size();
```

3. In the two encoder `start(workingOutputPath, vidSize, frameRateInt)` calls (lines 553 and 558), replace `vidSize` with `outputSize`.
4. Directly after `if (!capturedFrame.isNull()) {` (line 626), add:

```cpp
                if (!cropRect.isEmpty()) {
                    capturedFrame = capturedFrame.copy(cropRect);
                }
```

- [ ] **Step 4: Run the test and verify it passes**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_RecordingPreviewExport --output-on-failure`
Expected: PASS, including the existing `saveAnimation` rows.

- [ ] **Step 5: Commit**

```bash
git add src/qml/RecordingPreviewBackend.mm tests/Qml/tst_RecordingPreviewExport.cpp
git commit -m "feat(recording): apply preview crop to GIF and WebP export"
```

---

### Task 5: Shared bitrate formula and the `IVideoTranscoder` interface

**Files:**
- Create: `include/encoding/VideoBitrate.h` (header-only)
- Create: `include/video/IVideoTranscoder.h`
- Create: `src/video/IVideoTranscoder.cpp`
- Modify: `src/AVFoundationEncoder.mm`, `calculateBitrate()` at lines 57-64
- Modify: `src/MediaFoundationEncoder.cpp`, `calculateBitrate()` at lines 50-56
- Modify: `CMakeLists.txt`, snaptray_platform common sources. Add the new files after `src/video/IVideoFrameReader.cpp` (line 501).
- Test: `tests/Encoding/tst_VideoBitrate.cpp`, registered in `tests/CMakeLists.txt` near `Encoding_EncoderFactory` (lines 559-567).

**Interfaces:**
- Produces:
  ```cpp
  namespace SnapTray::VideoBitrate {
  constexpr int kMinBitrate = 1000000;
  constexpr int kMaxBitrate = 50000000;
  int forQuality(const QSize& frameSize, int frameRate, int quality);
  }
  constexpr int kDefaultTranscodeQuality = 80;
  struct VideoTranscodeRequest { QString inputPath; QString outputPath; qint64 startMs = 0; qint64 endMs = -1; QRect cropRect; int videoBitrate = 0; };
  struct VideoTranscodeResult { bool success = false; bool audioCopied = false; QString errorMessage; };
  struct VideoFileProbe { bool valid = false; QSize videoSize; qint64 durationMs = 0; bool hasAudio = false; };
  class IVideoTranscoder {
      using ProgressCallback = std::function<bool(int percent)>;
      virtual VideoTranscodeResult transcode(const VideoTranscodeRequest&, const ProgressCallback&) = 0;
      virtual VideoFileProbe probe(const QString& filePath) = 0;
      static std::unique_ptr<IVideoTranscoder> create();
  };
  ```

- [ ] **Step 1: Write the failing test** at `tests/Encoding/tst_VideoBitrate.cpp`

```cpp
#include <QtTest/QtTest>

#include "encoding/VideoBitrate.h"

class tst_VideoBitrate : public QObject
{
    Q_OBJECT

private slots:
    void forQuality_data();
    void forQuality();
};

void tst_VideoBitrate::forQuality_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<int>("fps");
    QTest::addColumn<int>("quality");
    QTest::addColumn<int>("expected");
    // 1920*1080*30 * (0.1 + 0.55*0.2 = 0.21) = 13,063,680
    QTest::newRow("default quality 1080p30") << QSize(1920, 1080) << 30 << 55 << 13063680;
    QTest::newRow("tiny clamps to minimum") << QSize(64, 48) << 10 << 0 << SnapTray::VideoBitrate::kMinBitrate;
    QTest::newRow("4k60 clamps to maximum") << QSize(3840, 2160) << 60 << 100 << SnapTray::VideoBitrate::kMaxBitrate;
    QTest::newRow("quality above 100 is clamped") << QSize(1280, 720) << 30 << 150 << 8294400;
}

void tst_VideoBitrate::forQuality()
{
    QFETCH(QSize, size);
    QFETCH(int, fps);
    QFETCH(int, quality);
    QFETCH(int, expected);
    QCOMPARE(SnapTray::VideoBitrate::forQuality(size, fps, quality), expected);
}

QTEST_GUILESS_MAIN(tst_VideoBitrate)
#include "tst_VideoBitrate.moc"
```

The arithmetic for the last row is 1280 × 720 × 30 × 0.3 = 8,294,400.

Register it:

```cmake
add_executable(Encoding_VideoBitrate Encoding/tst_VideoBitrate.cpp)
target_include_directories(Encoding_VideoBitrate PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(Encoding_VideoBitrate PRIVATE Qt6::Core Qt6::Test)
add_test(NAME Encoding_VideoBitrate COMMAND Encoding_VideoBitrate)
set_tests_properties(Encoding_VideoBitrate PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `./scripts/build.sh`
Expected: the build FAILS with `encoding/VideoBitrate.h: No such file or directory`.

- [ ] **Step 3: Implement**

`include/encoding/VideoBitrate.h`:

```cpp
#pragma once

#include <QSize>
#include <QtGlobal>

// H.264 bitrate used by the recording encoders and the preview transcoder.
namespace SnapTray::VideoBitrate {

constexpr int kMinBitrate = 1000000;
constexpr int kMaxBitrate = 50000000;
constexpr double kMinBitsPerPixel = 0.1;
constexpr double kBitsPerPixelRange = 0.2;
constexpr int kMaxQuality = 100;

inline int forQuality(const QSize& frameSize, int frameRate, int quality)
{
    const double bitsPerPixel = kMinBitsPerPixel
        + (qBound(0, quality, kMaxQuality) / static_cast<double>(kMaxQuality)) * kBitsPerPixelRange;
    const double bitrate = static_cast<double>(frameSize.width()) * frameSize.height() * frameRate * bitsPerPixel;
    return static_cast<int>(qBound(static_cast<double>(kMinBitrate), bitrate, static_cast<double>(kMaxBitrate)));
}

} // namespace SnapTray::VideoBitrate
```

Replace the encoder formulas:
- `src/AVFoundationEncoder.mm`: the body of `calculateBitrate()` becomes `return SnapTray::VideoBitrate::forQuality(frameSize, frameRate, quality);`. Add `#include "encoding/VideoBitrate.h"`.
- `src/MediaFoundationEncoder.cpp`: the body becomes `return static_cast<UINT32>(SnapTray::VideoBitrate::forQuality(frameSize, frameRate, quality));`. Add the same include.

`include/video/IVideoTranscoder.h`:

```cpp
#pragma once

#include <QRect>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>

// Quality used for preview edits; matches the previous VideoTrimmer export.
constexpr int kDefaultTranscodeQuality = 80;

struct VideoTranscodeRequest {
    QString inputPath;
    QString outputPath;
    qint64 startMs = 0;
    qint64 endMs = -1;      // -1 = end of input
    QRect cropRect;         // video pixels, even-aligned; empty = full frame
    int videoBitrate = 0;   // bits/s; 0 = VideoBitrate::forQuality(output size, fps, kDefaultTranscodeQuality)
};

struct VideoTranscodeResult {
    bool success = false;
    bool audioCopied = false; // Source audio preserved, including verified re-encode fallback.
    QString errorMessage;
};

struct VideoFileProbe {
    bool valid = false;
    QSize videoSize;
    qint64 durationMs = 0;
    bool hasAudio = false;
};

// Offline MP4 trim + crop + H.264 re-encode with AAC passthrough.
class IVideoTranscoder
{
public:
    // Called from the transcoder's worker threads; return false to cancel.
    using ProgressCallback = std::function<bool(int percent)>;

    virtual ~IVideoTranscoder() = default;

    // Blocking. Run off the GUI thread in production code. On failure or
    // cancellation the output file is removed; the input is never removed here.
    // An input with audio may succeed only if its audio is preserved in the output.
    virtual VideoTranscodeResult transcode(const VideoTranscodeRequest& request,
                                           const ProgressCallback& progress) = 0;
    virtual VideoFileProbe probe(const QString& filePath) = 0;

    // nullptr on platforms without a native implementation (Linux beta).
    static std::unique_ptr<IVideoTranscoder> create();
};
```

`src/video/IVideoTranscoder.cpp` (Task 7 adds the Windows branch):

```cpp
#include "video/IVideoTranscoder.h"

#ifdef Q_OS_MACOS
std::unique_ptr<IVideoTranscoder> createAVFoundationTranscoder();
#endif

std::unique_ptr<IVideoTranscoder> IVideoTranscoder::create()
{
#if defined(Q_OS_MACOS)
    return createAVFoundationTranscoder();
#else
    return nullptr;
#endif
}
```

This file will not link on macOS until Task 6 provides `createAVFoundationTranscoder()`. So in this task, add **only** `include/encoding/VideoBitrate.h` and the encoder edits to the build. Create the transcoder header and `.cpp` now, but add `src/video/IVideoTranscoder.cpp` to `CMakeLists.txt` in Task 6, Step 3.

- [ ] **Step 4: Run the test and verify it passes**

Run: `./scripts/build.sh && ctest --test-dir build -R "Encoding_" --output-on-failure`
Expected: `Encoding_VideoBitrate` PASS; existing `Encoding_*` tests still PASS.

- [ ] **Step 5: Commit**

```bash
git add include/encoding/VideoBitrate.h include/video/IVideoTranscoder.h src/video/IVideoTranscoder.cpp src/AVFoundationEncoder.mm src/MediaFoundationEncoder.cpp tests/Encoding/tst_VideoBitrate.cpp tests/CMakeLists.txt
git commit -m "refactor(encoding): share H.264 bitrate formula and add transcoder interface"
```

---

### Task 6: macOS transcoder (AVAssetReader + AVAssetWriter)

**Required audio regressions (in addition to the baseline tests below):**
- Inject unsupported audio setup, failed retiming, and failed audio sample writes through a narrow test seam. Each must return failure, remove partial output, and retain the source. These cases must also run for Windows where applicable.
- Use a deterministic non-silent audio fixture with timed pulses; decode output audio and verify its presence, selected interval and A/V alignment within a documented packet/frame tolerance. A track-presence probe alone does not prove audio preservation.
- Exercise a trim start between AAC packet boundaries, cancellation with audio, and retry after failure. The backend integration test must verify no save signal and no source deletion for a claimed success whose output is missing audio.
- AAC-dependent skips are not release acceptance. Run the audio contract on an audio-capable macOS and Windows environment before accepting the phase.

**Files:**
- Create: `src/video/AVFoundationTranscoder_mac.mm`
- Modify: `CMakeLists.txt`, in three places:
  - Add `src/video/IVideoTranscoder.cpp` to the snaptray_platform common sources after line 501.
  - Add `src/video/AVFoundationTranscoder_mac.mm` to the `$<$<PLATFORM_ID:Darwin>:` block next to `src/video/AVFoundationFrameReader_mac.mm` (line 538).
  - Add it to the `-fobjc-arc` `set_source_files_properties` list (lines 586-595).
- Test: `tests/Video/tst_VideoTranscoder.cpp`, registered in `tests/CMakeLists.txt` after `Video_VideoTrimmerSafety`, outside any platform `if`.

**Interfaces:**
- Consumes: `IVideoTranscoder`, `VideoBitrate::forQuality`, `kDefaultTranscodeQuality` (Task 5).
- Produces: `std::unique_ptr<IVideoTranscoder> createAVFoundationTranscoder();`

- [ ] **Step 1: Write the failing test** at `tests/Video/tst_VideoTranscoder.cpp`

This one file is the cross-platform contract. Task 7 runs the same file on Windows.

```cpp
#include <QtTest/QtTest>

#include "IVideoEncoder.h"
#include "video/IVideoFrameReader.h"
#include "video/IVideoTranscoder.h"

#include <QFileInfo>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

namespace {

constexpr int kFrameRate = 10;
constexpr int kFrameCount = 20;                  // 2 s
constexpr int kFrameIntervalMs = 1000 / kFrameRate;
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioChannels = 2;
constexpr int kAudioBytesPerSample = 2;
constexpr int kAudioFramesPerVideoFrame = kAudioSampleRate / kFrameRate;
constexpr qint64 kDurationToleranceMs = 150;
const QSize kSourceSize(160, 120);

// Quadrants: top-left red, top-right green, bottom-left blue, bottom-right white.
QImage quadrantFrame()
{
    QImage frame(kSourceSize, QImage::Format_ARGB32);
    QPainter painter(&frame);
    const int halfW = kSourceSize.width() / 2;
    const int halfH = kSourceSize.height() / 2;
    painter.fillRect(0, 0, halfW, halfH, Qt::red);
    painter.fillRect(halfW, 0, halfW, halfH, Qt::green);
    painter.fillRect(0, halfH, halfW, halfH, Qt::blue);
    painter.fillRect(halfW, halfH, halfW, halfH, Qt::white);
    return frame;
}

QString createFixture(const QString& path, bool withAudio)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) {
        return QStringLiteral("No native encoder");
    }
    if (withAudio) {
        encoder->setAudioFormat(kAudioSampleRate, kAudioChannels, kAudioBytesPerSample * 8);
    }
    if (!encoder->start(path, kSourceSize, kFrameRate)) {
        return encoder->lastError();
    }
    if (withAudio && !encoder->isAudioEnabled()) {
        encoder->abort();
        return QStringLiteral("SKIP: AAC encoding unavailable");
    }
    const QImage frame = quadrantFrame();
    const QByteArray silence(kAudioFramesPerVideoFrame * kAudioChannels * kAudioBytesPerSample, '\0');
    for (int i = 0; i < kFrameCount; ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer waitTimer;
        waitTimer.start();
        do {
            encoder->writeFrame(frame, i * kFrameIntervalMs);
            if (encoder->framesWritten() != before) {
                break;
            }
            QTest::qWait(5);
        } while (waitTimer.elapsed() < 2000);
        if (encoder->framesWritten() != before + 1) {
            return QStringLiteral("Encoder rejected fixture frame %1").arg(i);
        }
        if (withAudio) {
            encoder->writeAudioSamples(silence, qint64(i) * kAudioFramesPerVideoFrame);
        }
    }
    QSignalSpy finishedSpy(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finishedSpy.isEmpty() && !finishedSpy.wait(10000)) {
        return QStringLiteral("Encoder did not finish");
    }
    return finishedSpy.first().at(0).toBool() ? QString() : encoder->lastError();
}

bool isGreen(const QColor& c) { return c.green() > 180 && c.red() < 60 && c.blue() < 60; }

} // namespace

class tst_VideoTranscoder : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void probeReportsFixture();
    void cropAndTrimKeepsAudio();
    void trimWithoutAudio();
    void cancelRemovesOutput();
    void invalidInputFails();

private:
    std::unique_ptr<IVideoTranscoder> m_transcoder;
    QTemporaryDir m_dir;
    QString fixture(bool withAudio);
};

void tst_VideoTranscoder::init()
{
    m_transcoder = IVideoTranscoder::create();
    if (!m_transcoder) {
        QSKIP("No native transcoder on this platform");
    }
    QVERIFY(m_dir.isValid());
}

QString tst_VideoTranscoder::fixture(bool withAudio)
{
    const QString path = m_dir.filePath(withAudio ? QStringLiteral("av.mp4") : QStringLiteral("v.mp4"));
    if (QFileInfo::exists(path)) {
        return path;
    }
    const QString error = createFixture(path, withAudio);
    if (error.startsWith(QLatin1String("SKIP:"))) {
        return QString();
    }
    return error.isEmpty() ? path : QStringLiteral("ERROR:") + error;
}

void tst_VideoTranscoder::probeReportsFixture()
{
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    const VideoFileProbe probe = m_transcoder->probe(input);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, kSourceSize);
    QVERIFY(qAbs(probe.durationMs - kFrameCount * kFrameIntervalMs) <= kDurationToleranceMs);
    QVERIFY(probe.hasAudio);
}

void tst_VideoTranscoder::cropAndTrimKeepsAudio()
{
    const QString input = fixture(true);
    if (input.isEmpty()) QSKIP("AAC encoding unavailable");
    QVERIFY2(!input.startsWith(QLatin1String("ERROR:")), qPrintable(input));

    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("out.mp4"));
    request.startMs = 500;
    request.endMs = 1500;
    request.cropRect = QRect(80, 0, 80, 60); // top-right = green
    int lastPercent = -1;
    const VideoTranscodeResult result = m_transcoder->transcode(request, [&](int percent) {
        lastPercent = percent;
        return true;
    });
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(result.audioCopied);
    QCOMPARE(lastPercent, 100);

    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, QSize(80, 60));
    QVERIFY2(qAbs(probe.durationMs - 1000) <= kDurationToleranceMs,
             qPrintable(QString::number(probe.durationMs)));
    QVERIFY(probe.hasAudio);

    if (auto reader = IVideoFrameReader::create()) {
        QVERIFY(reader->load(request.outputPath));
        const QImage frame = reader->frameAt(100);
        QVERIFY(!frame.isNull());
        QVERIFY2(isGreen(frame.pixelColor(frame.rect().center())),
                 qPrintable(frame.pixelColor(frame.rect().center()).name()));
    }
}

void tst_VideoTranscoder::trimWithoutAudio()
{
    const QString input = fixture(false);
    QVERIFY2(!input.isEmpty() && !input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("trim.mp4"));
    request.startMs = 1000;
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QVERIFY(!result.audioCopied);
    const VideoFileProbe probe = m_transcoder->probe(request.outputPath);
    QCOMPARE(probe.videoSize, kSourceSize);
    QVERIFY(qAbs(probe.durationMs - 1000) <= kDurationToleranceMs);
    QVERIFY(!probe.hasAudio);
}

void tst_VideoTranscoder::cancelRemovesOutput()
{
    const QString input = fixture(false);
    QVERIFY2(!input.isEmpty() && !input.startsWith(QLatin1String("ERROR:")), qPrintable(input));
    VideoTranscodeRequest request;
    request.inputPath = input;
    request.outputPath = m_dir.filePath(QStringLiteral("cancelled.mp4"));
    const VideoTranscodeResult result = m_transcoder->transcode(request, [](int) { return false; });
    QVERIFY(!result.success);
    QVERIFY(!QFileInfo::exists(request.outputPath));
}

void tst_VideoTranscoder::invalidInputFails()
{
    VideoTranscodeRequest request;
    request.inputPath = m_dir.filePath(QStringLiteral("missing.mp4"));
    request.outputPath = m_dir.filePath(QStringLiteral("never.mp4"));
    const VideoTranscodeResult result = m_transcoder->transcode(request, {});
    QVERIFY(!result.success);
    QVERIFY(!result.errorMessage.isEmpty());
    QVERIFY(!QFileInfo::exists(request.outputPath));
    QVERIFY(!m_transcoder->probe(request.inputPath).valid);
}

QTEST_MAIN(tst_VideoTranscoder)
#include "tst_VideoTranscoder.moc"
```

Register it:

```cmake
add_executable(Video_VideoTranscoder Video/tst_VideoTranscoder.cpp)
target_link_libraries(Video_VideoTranscoder PRIVATE snaptray_platform Qt6::Test)
add_test(NAME Video_VideoTranscoder COMMAND Video_VideoTranscoder)
set_tests_properties(Video_VideoTranscoder PROPERTIES TIMEOUT 120 LABELS "integration;slow")
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `./scripts/build.sh`
Expected: link FAILS with `undefined symbol: IVideoTranscoder::create()`, because `IVideoTranscoder.cpp` is not in the build yet.

- [ ] **Step 3: Implement** `src/video/AVFoundationTranscoder_mac.mm`

```objc
#include "video/IVideoTranscoder.h"

#include "encoding/VideoBitrate.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <QDebug>
#include <QFile>
#include <QtGlobal>

#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

#if !__has_feature(objc_arc)
#error "AVFoundationTranscoder_mac.mm must be compiled with ARC"
#endif

namespace {

constexpr int kBytesPerPixel = 4;
constexpr int kMsPerSecond = 1000;
constexpr int kKeyFrameIntervalSeconds = 2;
constexpr int kMaxProgressBeforeFinish = 99;
constexpr int kCompletePercent = 100;
constexpr int64_t kBrokenPollIntervalMs = 100;

// Shared with the GCD blocks; outlives transcode() if a block runs late.
struct TranscodeState {
    std::atomic<bool> cancelled{false};
    std::atomic<bool> videoFailed{false};
    std::atomic<bool> audioFailed{false};
    std::atomic<bool> videoDone{false};
    std::atomic<bool> audioDone{false};
    std::atomic<int> lastPercent{-1};
};

NSURL* fileUrl(const QString& path)
{
    return [NSURL fileURLWithPath:path.toNSString()];
}

QString errorText(NSError* error, const char* fallback)
{
    return error ? QString::fromNSString(error.localizedDescription) : QString::fromLatin1(fallback);
}

CVPixelBufferRef copyCroppedPixelBuffer(CVPixelBufferRef source, const QRect& crop, CVPixelBufferPoolRef pool)
{
    CVPixelBufferRef target = nullptr;
    if (!pool || CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, pool, &target) != kCVReturnSuccess) {
        return nullptr;
    }
    CVPixelBufferLockBaseAddress(source, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferLockBaseAddress(target, 0);
    const auto* src = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(source));
    auto* dst = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(target));
    const size_t srcStride = CVPixelBufferGetBytesPerRow(source);
    const size_t dstStride = CVPixelBufferGetBytesPerRow(target);
    const size_t rowBytes = static_cast<size_t>(crop.width()) * kBytesPerPixel;
    for (int row = 0; row < crop.height(); ++row) {
        std::memcpy(dst + row * dstStride,
                    src + (crop.y() + row) * srcStride + static_cast<size_t>(crop.x()) * kBytesPerPixel,
                    rowBytes);
    }
    CVPixelBufferUnlockBaseAddress(target, 0);
    CVPixelBufferUnlockBaseAddress(source, kCVPixelBufferLock_ReadOnly);
    return target;
}

CMSampleBufferRef copyRetimed(CMSampleBufferRef sample, CMTime offset)
{
    CMItemCount count = 0;
    if (CMSampleBufferGetSampleTimingInfoArray(sample, 0, nullptr, &count) != noErr || count <= 0) {
        return nullptr;
    }
    std::vector<CMSampleTimingInfo> timings(static_cast<size_t>(count));
    if (CMSampleBufferGetSampleTimingInfoArray(sample, count, timings.data(), &count) != noErr) {
        return nullptr;
    }
    for (auto& timing : timings) {
        timing.presentationTimeStamp = CMTimeSubtract(timing.presentationTimeStamp, offset);
        if (CMTIME_IS_VALID(timing.decodeTimeStamp)) {
            timing.decodeTimeStamp = CMTimeSubtract(timing.decodeTimeStamp, offset);
        }
    }
    CMSampleBufferRef retimed = nullptr;
    if (CMSampleBufferCreateCopyWithNewTiming(kCFAllocatorDefault, sample, count, timings.data(), &retimed) != noErr) {
        return nullptr;
    }
    return retimed;
}

class AVFoundationTranscoder final : public IVideoTranscoder
{
public:
    VideoTranscodeResult transcode(const VideoTranscodeRequest& request, const ProgressCallback& progress) override;
    VideoFileProbe probe(const QString& filePath) override;
};

VideoFileProbe AVFoundationTranscoder::probe(const QString& filePath)
{
    VideoFileProbe result;
    if (!QFile::exists(filePath)) {
        return result;
    }
    @autoreleasepool {
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:fileUrl(filePath) options:nil];
        AVAssetTrack* video = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!video) {
            return result;
        }
        const CGSize size = CGSizeApplyAffineTransform(video.naturalSize, video.preferredTransform);
        result.videoSize = QSize(qAbs(qRound(size.width)), qAbs(qRound(size.height)));
        result.durationMs = static_cast<qint64>(CMTimeGetSeconds(asset.duration) * kMsPerSecond);
        result.hasAudio = [asset tracksWithMediaType:AVMediaTypeAudio].count > 0;
        result.valid = !result.videoSize.isEmpty();
    }
    return result;
}

VideoTranscodeResult AVFoundationTranscoder::transcode(const VideoTranscodeRequest& request,
                                                       const ProgressCallback& progress)
{
    VideoTranscodeResult result;
    auto fail = [&](const QString& message) {
        qWarning() << "AVFoundationTranscoder:" << message;
        QFile::remove(request.outputPath);
        result.success = false;
        result.errorMessage = message;
        return result;
    };

    @autoreleasepool {
        if (!QFile::exists(request.inputPath)) {
            return fail(QStringLiteral("Input file does not exist"));
        }
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:fileUrl(request.inputPath) options:nil];
        AVAssetTrack* videoTrack = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        if (!videoTrack) {
            return fail(QStringLiteral("Input has no video track"));
        }
        AVAssetTrack* audioTrack = [[asset tracksWithMediaType:AVMediaTypeAudio] firstObject];

        const QRect frameRect(0, 0, qRound(videoTrack.naturalSize.width), qRound(videoTrack.naturalSize.height));
        const QRect crop = request.cropRect.isEmpty() ? frameRect : request.cropRect.intersected(frameRect);
        if (crop.isEmpty()) {
            return fail(QStringLiteral("Crop rectangle is outside the video"));
        }

        const qint64 durationMs = static_cast<qint64>(CMTimeGetSeconds(asset.duration) * kMsPerSecond);
        const qint64 startMs = qBound<qint64>(0, request.startMs, durationMs);
        const qint64 endMs = request.endMs < 0 ? durationMs : qBound(startMs, request.endMs, durationMs);
        if (endMs <= startMs) {
            return fail(QStringLiteral("Invalid time range"));
        }
        const CMTime startTime = CMTimeMake(startMs, kMsPerSecond);
        const double spanMs = static_cast<double>(endMs - startMs);

        QFile::remove(request.outputPath);
        NSError* error = nil;
        AVAssetWriter* writer = [[AVAssetWriter alloc] initWithURL:fileUrl(request.outputPath)
                                                          fileType:AVFileTypeMPEG4
                                                             error:&error];
        if (!writer) {
            return fail(errorText(error, "Cannot create writer"));
        }

        const int frameRate = qMax(1, qRound(videoTrack.nominalFrameRate));
        const int bitrate = request.videoBitrate > 0
            ? request.videoBitrate
            : SnapTray::VideoBitrate::forQuality(crop.size(), frameRate, kDefaultTranscodeQuality);
        AVAssetWriterInput* videoInput = [[AVAssetWriterInput alloc]
            initWithMediaType:AVMediaTypeVideo
               outputSettings:@{
                   AVVideoCodecKey: AVVideoCodecTypeH264,
                   AVVideoWidthKey: @(crop.width()),
                   AVVideoHeightKey: @(crop.height()),
                   AVVideoCompressionPropertiesKey: @{
                       AVVideoAverageBitRateKey: @(bitrate),
                       AVVideoExpectedSourceFrameRateKey: @(frameRate),
                       AVVideoMaxKeyFrameIntervalKey: @(frameRate * kKeyFrameIntervalSeconds),
                       AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
                       AVVideoAllowFrameReorderingKey: @NO
                   }
               }];
        videoInput.expectsMediaDataInRealTime = NO;
        videoInput.transform = videoTrack.preferredTransform;
        AVAssetWriterInputPixelBufferAdaptor* adaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc]
            initWithAssetWriterInput:videoInput
         sourcePixelBufferAttributes:@{
             (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
             (NSString*)kCVPixelBufferWidthKey: @(crop.width()),
             (NSString*)kCVPixelBufferHeightKey: @(crop.height())
         }];
        [writer addInput:videoInput];

        AVAssetWriterInput* audioInput = nil;
        if (audioTrack) {
            CMFormatDescriptionRef hint =
                (__bridge CMFormatDescriptionRef)audioTrack.formatDescriptions.firstObject;
            AVAssetWriterInput* candidate = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeAudio
                                                                           outputSettings:nil
                                                                         sourceFormatHint:hint];
            candidate.expectsMediaDataInRealTime = NO;
            if ([writer canAddInput:candidate]) {
                [writer addInput:candidate];
                audioInput = candidate;
            } else {
                return fail(QStringLiteral("Cannot preserve source audio"));
            }
        }

        AVAssetReader* reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (!reader) {
            return fail(errorText(error, "Cannot create reader"));
        }
        reader.timeRange = CMTimeRangeFromTimeToTime(startTime, CMTimeMake(endMs, kMsPerSecond));
        AVAssetReaderTrackOutput* videoOutput = [[AVAssetReaderTrackOutput alloc]
            initWithTrack:videoTrack
           outputSettings:@{ (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA) }];
        videoOutput.alwaysCopiesSampleData = NO;
        [reader addOutput:videoOutput];
        AVAssetReaderTrackOutput* audioOutput = nil;
        if (audioInput) {
            audioOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:audioTrack outputSettings:nil];
            [reader addOutput:audioOutput];
        }

        if (![reader startReading]) {
            return fail(errorText(reader.error, "Cannot start reading"));
        }
        if (![writer startWriting]) {
            [reader cancelReading];
            return fail(errorText(writer.error, "Cannot start writing"));
        }
        [writer startSessionAtSourceTime:kCMTimeZero];

        auto state = std::make_shared<TranscodeState>();
        const ProgressCallback progressCopy = progress;
        dispatch_group_t group = dispatch_group_create();

        dispatch_group_enter(group);
        dispatch_queue_t videoQueue = dispatch_queue_create("snaptray.transcode.video", DISPATCH_QUEUE_SERIAL);
        __block CMTime lastVideoTime = kCMTimeInvalid;
        [videoInput requestMediaDataWhenReadyOnQueue:videoQueue usingBlock:^{
            auto finishVideo = [&]() {
                if (!state->videoDone.exchange(true)) {
                    [videoInput markAsFinished];
                    dispatch_group_leave(group);
                }
            };
            while (videoInput.isReadyForMoreMediaData) {
                CMSampleBufferRef sample = state->cancelled.load() ? nullptr : [videoOutput copyNextSampleBuffer];
                if (!sample) {
                    finishVideo();
                    return;
                }
                CMTime pts = CMTimeSubtract(CMSampleBufferGetPresentationTimeStamp(sample), startTime);
                if (CMTIME_COMPARE_INLINE(pts, <, kCMTimeZero)) {
                    pts = kCMTimeZero;
                }
                if (CMTIME_IS_VALID(lastVideoTime) && CMTIME_COMPARE_INLINE(pts, <=, lastVideoTime)) {
                    CFRelease(sample);
                    continue;
                }
                CVPixelBufferRef source = CMSampleBufferGetImageBuffer(sample);
                CVPixelBufferRef cropped = source ? copyCroppedPixelBuffer(source, crop, adaptor.pixelBufferPool) : nullptr;
                const bool appended = cropped && [adaptor appendPixelBuffer:cropped withPresentationTime:pts];
                if (cropped) {
                    CVPixelBufferRelease(cropped);
                }
                CFRelease(sample);
                if (!appended) {
                    state->videoFailed.store(true);
                    finishVideo();
                    return;
                }
                lastVideoTime = pts;
                const int percent = qBound(0, static_cast<int>(CMTimeGetSeconds(pts) * kMsPerSecond * 100.0 / spanMs),
                                           kMaxProgressBeforeFinish);
                if (state->lastPercent.exchange(percent) != percent && progressCopy && !progressCopy(percent)) {
                    state->cancelled.store(true);
                }
            }
        }];

        if (audioInput) {
            dispatch_group_enter(group);
            dispatch_queue_t audioQueue = dispatch_queue_create("snaptray.transcode.audio", DISPATCH_QUEUE_SERIAL);
            [audioInput requestMediaDataWhenReadyOnQueue:audioQueue usingBlock:^{
                while (audioInput.isReadyForMoreMediaData) {
                    CMSampleBufferRef sample = state->cancelled.load() ? nullptr : [audioOutput copyNextSampleBuffer];
                    if (!sample) {
                        if (!state->audioDone.exchange(true)) {
                            [audioInput markAsFinished];
                            dispatch_group_leave(group);
                        }
                        return;
                    }
                    CMSampleBufferRef retimed = copyRetimed(sample, startTime);
                    CFRelease(sample);
                    if (!retimed) {
                        state->audioFailed.store(true);
                        state->cancelled.store(true);
                        continue;
                    }
                    const CMTime pts = CMSampleBufferGetPresentationTimeStamp(retimed);
                    if (CMTIME_COMPARE_INLINE(pts, >=, kCMTimeZero)) {
                        if (![audioInput appendSampleBuffer:retimed]) {
                            state->audioFailed.store(true);
                            state->cancelled.store(true);
                        }
                    }
                    CFRelease(retimed);
                }
            }];
        }

        bool broken = false;
        while (dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, kBrokenPollIntervalMs * NSEC_PER_MSEC)) != 0) {
            if (writer.status == AVAssetWriterStatusFailed || reader.status == AVAssetReaderStatusFailed) {
                broken = true;
                break;
            }
        }

        broken = broken || reader.status == AVAssetReaderStatusFailed
                        || writer.status == AVAssetWriterStatusFailed;
        if (broken || state->cancelled.load() || state->videoFailed.load() || state->audioFailed.load()) {
            state->cancelled.store(true);
            [reader cancelReading];
            [writer cancelWriting];
            if (broken || state->videoFailed.load() || state->audioFailed.load()) {
                return fail(errorText(writer.error ?: reader.error, "Transcode failed"));
            }
            return fail(QStringLiteral("Cancelled"));
        }

        dispatch_semaphore_t finished = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(finished);
        }];
        dispatch_semaphore_wait(finished, DISPATCH_TIME_FOREVER);
        if (writer.status != AVAssetWriterStatusCompleted) {
            return fail(errorText(writer.error, "Cannot finish writing"));
        }
    }

    if (progress) {
        progress(kCompletePercent);
    }
    const VideoFileProbe sourceProbe = probe(request.inputPath);
    const VideoFileProbe outputProbe = probe(request.outputPath);
    if (!sourceProbe.valid || !outputProbe.valid || outputProbe.durationMs <= 0
        || (sourceProbe.hasAudio && !outputProbe.hasAudio)) {
        return fail(QStringLiteral("Output validation failed; source retained"));
    }
    result.success = true;
    result.audioCopied = sourceProbe.hasAudio && outputProbe.hasAudio;
    return result;
}

} // namespace

std::unique_ptr<IVideoTranscoder> createAVFoundationTranscoder()
{
    return std::make_unique<AVFoundationTranscoder>();
}
```

The `finishVideo` lambda captures by reference inside the block. That is safe, because it is only invoked synchronously within the same block execution. In `cancelRemovesOutput`, the cancel happens on the first progress callback, so the test exercises the cancel path.

Update `CMakeLists.txt` as listed under **Files**.

- [ ] **Step 4: Run the test and verify it passes**

Run: `./scripts/build.sh && ctest --test-dir build -R Video_VideoTranscoder --output-on-failure`
Expected: the baseline tests and required audio regressions PASS on macOS.

- [ ] **Step 5: Commit**

```bash
git add src/video/AVFoundationTranscoder_mac.mm src/video/IVideoTranscoder.cpp CMakeLists.txt tests/Video/tst_VideoTranscoder.cpp tests/CMakeLists.txt
git commit -m "feat(video): add AVFoundation transcoder with crop, trim and AAC passthrough"
```

---

### Task 7: Windows transcoder (Media Foundation), starting with the passthrough spike

**Files:**
- Create: `src/video/MediaFoundationTranscoder_win.cpp`
- Modify: `src/video/IVideoTranscoder.cpp` (add the Windows branch)
- Modify: `CMakeLists.txt`. Add the new file to the `$<$<PLATFORM_ID:Windows>:` block next to `src/video/MediaFoundationPlayer_win.cpp` (line 563).
- Test: `tests/Video/tst_VideoTranscoder.cpp`. It is unchanged and already registered for all platforms.

**Interfaces:**
- Consumes: Task 5 interface and the Task 6 test contract.
- Produces: `std::unique_ptr<IVideoTranscoder> createMediaFoundationTranscoder();`

**Spike gate:** AAC passthrough through the Sink Writer, and trim timestamps, are the biggest unknowns in this phase. Step 3 implements passthrough first. If `cropAndTrimKeepsAudio` fails in the audio part, apply the Step 3b fallback. Do not continue to Task 8 until the baseline tests and required audio regressions pass on Windows.

- [ ] **Step 1: Add the Windows factory branch**

In `src/video/IVideoTranscoder.cpp`:

```cpp
#ifdef Q_OS_WIN
std::unique_ptr<IVideoTranscoder> createMediaFoundationTranscoder();
#endif
```

Then, in `create()`, add before `#else`:

```cpp
#elif defined(Q_OS_WIN)
    return createMediaFoundationTranscoder();
```

- [ ] **Step 2: Run the test on Windows and verify it fails**

Run: `scripts\build.bat`
Expected: link FAILS with `unresolved external symbol createMediaFoundationTranscoder`.

- [ ] **Step 3: Implement** `src/video/MediaFoundationTranscoder_win.cpp`

```cpp
#include "video/IVideoTranscoder.h"

#include "encoding/VideoBitrate.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <QDebug>
#include <QDir>
#include <QFile>

#include <cstring>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

constexpr LONGLONG kHnsPerMs = 10000;
constexpr LONGLONG kHnsPerSecond = 10000000;
constexpr int kBytesPerPixel = 4;
constexpr int kMaxProgressBeforeFinish = 99;
constexpr int kCompletePercent = 100;
constexpr UINT32 kDefaultFrameRate = 30;

struct ComApartment {
    HRESULT hr;
    ComApartment() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

struct MfSession {
    HRESULT hr;
    MfSession() : hr(MFStartup(MF_VERSION)) {}
    ~MfSession() { if (SUCCEEDED(hr)) MFShutdown(); }
};

QString hrText(const char* step, HRESULT hr)
{
    return QStringLiteral("%1 failed (0x%2)").arg(QLatin1String(step)).arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

std::wstring nativePath(const QString& path)
{
    return QDir::toNativeSeparators(path).toStdWString();
}

HRESULT openReader(const QString& path, ComPtr<IMFSourceReader>& reader)
{
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 1);
    if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromURL(nativePath(path).c_str(), attributes.Get(), &reader);
    return hr;
}

qint64 durationMs(IMFSourceReader* reader)
{
    PROPVARIANT var;
    PropVariantInit(&var);
    qint64 ms = 0;
    if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var))
        && var.vt == VT_UI8) {
        ms = static_cast<qint64>(var.uhVal.QuadPart / kHnsPerMs);
    }
    PropVariantClear(&var);
    return ms;
}

bool nativeAudioType(IMFSourceReader* reader, ComPtr<IMFMediaType>& type)
{
    return SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &type));
}

// Copies `crop` from a decoded RGB32 sample into a bottom-up RGB32 buffer for the
// Sink Writer (the same orientation MediaFoundationEncoder writes).
HRESULT cropSample(IMFSample* sample, const QSize& frameSize, LONG defaultStride, const QRect& crop,
                   ComPtr<IMFMediaBuffer>& output)
{
    ComPtr<IMFMediaBuffer> input;
    HRESULT hr = sample->ConvertToContiguousBuffer(&input);
    if (FAILED(hr)) return hr;

    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    ComPtr<IMF2DBuffer> buffer2d;
    BYTE* raw = nullptr;
    const bool locked2d = SUCCEEDED(input.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch));
    if (!locked2d) {
        DWORD length = 0;
        hr = input->Lock(&raw, nullptr, &length);
        if (FAILED(hr)) return hr;
        const LONG stride = defaultStride != 0 ? defaultStride : frameSize.width() * kBytesPerPixel;
        pitch = stride;
        scan0 = stride < 0 ? raw + (frameSize.height() - 1) * static_cast<size_t>(-stride) : raw;
    }

    const DWORD rowBytes = static_cast<DWORD>(crop.width() * kBytesPerPixel);
    hr = MFCreateMemoryBuffer(rowBytes * crop.height(), &output);
    BYTE* dst = nullptr;
    if (SUCCEEDED(hr)) hr = output->Lock(&dst, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        for (int row = 0; row < crop.height(); ++row) {
            const BYTE* srcRow = scan0 + static_cast<ptrdiff_t>(crop.y() + row) * pitch + crop.x() * kBytesPerPixel;
            BYTE* dstRow = dst + static_cast<size_t>(crop.height() - 1 - row) * rowBytes;
            std::memcpy(dstRow, srcRow, rowBytes);
        }
        output->Unlock();
        output->SetCurrentLength(rowBytes * crop.height());
    }
    if (locked2d) buffer2d->Unlock2D(); else input->Unlock();
    return hr;
}

class MediaFoundationTranscoder final : public IVideoTranscoder
{
public:
    VideoTranscodeResult transcode(const VideoTranscodeRequest& request, const ProgressCallback& progress) override;
    VideoFileProbe probe(const QString& filePath) override;
};

VideoFileProbe MediaFoundationTranscoder::probe(const QString& filePath)
{
    VideoFileProbe result;
    if (!QFile::exists(filePath)) return result;
    ComApartment com;
    MfSession mf;
    if (FAILED(mf.hr)) return result;
    ComPtr<IMFSourceReader> reader;
    if (FAILED(openReader(filePath, reader))) return result;
    ComPtr<IMFMediaType> videoType;
    if (FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &videoType))) return result;
    UINT32 width = 0;
    UINT32 height = 0;
    MFGetAttributeSize(videoType.Get(), MF_MT_FRAME_SIZE, &width, &height);
    result.videoSize = QSize(static_cast<int>(width), static_cast<int>(height));
    result.durationMs = durationMs(reader.Get());
    ComPtr<IMFMediaType> audioType;
    result.hasAudio = nativeAudioType(reader.Get(), audioType);
    result.valid = !result.videoSize.isEmpty();
    return result;
}

VideoTranscodeResult MediaFoundationTranscoder::transcode(const VideoTranscodeRequest& request,
                                                          const ProgressCallback& progress)
{
    VideoTranscodeResult result;
    ComApartment com; // RPC_E_CHANGED_MODE (caller is STA) is fine: we just don't uninitialize.
    MfSession mf;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFSinkWriter> writer;
    auto fail = [&](const QString& message) {
        qWarning() << "MediaFoundationTranscoder:" << message;
        writer.Reset();   // release the file handle before deleting
        reader.Reset();
        QFile::remove(request.outputPath);
        result.success = false;
        result.errorMessage = message;
        return result;
    };

    if (FAILED(mf.hr)) return fail(hrText("MFStartup", mf.hr));
    if (!QFile::exists(request.inputPath)) return fail(QStringLiteral("Input file does not exist"));
    HRESULT hr = openReader(request.inputPath, reader);
    if (FAILED(hr)) return fail(hrText("MFCreateSourceReaderFromURL", hr));

    // Video: decode to RGB32.
    ComPtr<IMFMediaType> rgbType;
    hr = MFCreateMediaType(&rgbType);
    if (SUCCEEDED(hr)) hr = rgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = rgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, rgbType.Get());
    if (FAILED(hr)) return fail(hrText("SetCurrentMediaType(RGB32)", hr));
    ComPtr<IMFMediaType> decodedType;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &decodedType);
    if (FAILED(hr)) return fail(hrText("GetCurrentMediaType(video)", hr));
    UINT32 width = 0;
    UINT32 height = 0;
    MFGetAttributeSize(decodedType.Get(), MF_MT_FRAME_SIZE, &width, &height);
    UINT32 fpsNum = kDefaultFrameRate;
    UINT32 fpsDen = 1;
    MFGetAttributeRatio(decodedType.Get(), MF_MT_FRAME_RATE, &fpsNum, &fpsDen);
    const LONG defaultStride = static_cast<LONG>(MFGetAttributeUINT32(decodedType.Get(), MF_MT_DEFAULT_STRIDE, 0));
    const QSize frameSize(static_cast<int>(width), static_cast<int>(height));
    const QRect frameRect(QPoint(0, 0), frameSize);
    const QRect crop = request.cropRect.isEmpty() ? frameRect : request.cropRect.intersected(frameRect);
    if (crop.isEmpty()) return fail(QStringLiteral("Crop rectangle is outside the video"));
    const int frameRate = qMax(1, static_cast<int>(fpsNum / qMax<UINT32>(1, fpsDen)));

    const qint64 totalMs = durationMs(reader.Get());
    const qint64 startMs = qBound<qint64>(0, request.startMs, totalMs);
    const qint64 endMs = request.endMs < 0 ? totalMs : qBound(startMs, request.endMs, totalMs);
    if (endMs <= startMs) return fail(QStringLiteral("Invalid time range"));
    const LONGLONG startHns = startMs * kHnsPerMs;
    const LONGLONG endHns = endMs * kHnsPerMs;

    // Audio: keep the native (compressed AAC) type for passthrough.
    ComPtr<IMFMediaType> audioType;
    const bool hasAudio = nativeAudioType(reader.Get(), audioType);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, hasAudio ? TRUE : FALSE);

    // Writer. MPEG-4 container is explicit so the extension never matters.
    QFile::remove(request.outputPath);
    ComPtr<IMFAttributes> writerAttributes;
    hr = MFCreateAttributes(&writerAttributes, 3);
    if (SUCCEEDED(hr)) hr = writerAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (SUCCEEDED(hr)) hr = writerAttributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (SUCCEEDED(hr)) hr = writerAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    if (SUCCEEDED(hr)) hr = MFCreateSinkWriterFromURL(nativePath(request.outputPath).c_str(), nullptr,
                                                      writerAttributes.Get(), &writer);
    if (FAILED(hr)) return fail(hrText("MFCreateSinkWriterFromURL", hr));

    const int bitrate = request.videoBitrate > 0
        ? request.videoBitrate
        : SnapTray::VideoBitrate::forQuality(crop.size(), frameRate, kDefaultTranscodeQuality);
    DWORD videoStream = 0;
    ComPtr<IMFMediaType> h264Type;
    hr = MFCreateMediaType(&h264Type);
    if (SUCCEEDED(hr)) hr = h264Type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = h264Type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = h264Type->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate));
    if (SUCCEEDED(hr)) hr = h264Type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(h264Type.Get(), MF_MT_FRAME_SIZE, crop.width(), crop.height());
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(h264Type.Get(), MF_MT_FRAME_RATE, fpsNum, fpsDen);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(h264Type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = writer->AddStream(h264Type.Get(), &videoStream);
    ComPtr<IMFMediaType> inputType;
    if (SUCCEEDED(hr)) hr = MFCreateMediaType(&inputType);
    if (SUCCEEDED(hr)) hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, crop.width(), crop.height());
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, fpsNum, fpsDen);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(videoStream, inputType.Get(), nullptr);
    if (FAILED(hr)) return fail(hrText("configure video stream", hr));

    DWORD audioStream = 0;
    bool audioEnabled = false;
    if (hasAudio) {
        hr = writer->AddStream(audioType.Get(), &audioStream); // no SetInputMediaType = passthrough
        audioEnabled = SUCCEEDED(hr);
        if (!audioEnabled) {
            return fail(hrText("AddStream(AAC passthrough)", hr));
        }
    }

    hr = writer->BeginWriting();
    if (FAILED(hr)) return fail(hrText("BeginWriting", hr));

    PROPVARIANT position;
    InitPropVariantFromInt64(startHns, &position);
    hr = reader->SetCurrentPosition(GUID_NULL, position);
    PropVariantClear(&position);
    if (FAILED(hr)) return fail(hrText("SetCurrentPosition", hr));

    const LONGLONG frameDuration = kHnsPerSecond * fpsDen / qMax<UINT32>(1, fpsNum);
    bool videoDone = false;
    bool audioDone = !audioEnabled;
    LONGLONG lastVideoTs = -1;
    LONGLONG lastAudioTs = -1;
    int lastPercent = -1;
    while (!videoDone || !audioDone) {
        const bool readVideo = !videoDone && (audioDone || lastVideoTs <= lastAudioTs);
        const DWORD stream = readVideo ? MF_SOURCE_READER_FIRST_VIDEO_STREAM : MF_SOURCE_READER_FIRST_AUDIO_STREAM;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        hr = reader->ReadSample(stream, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) return fail(hrText("ReadSample", hr));
        const bool ended = (flags & MF_SOURCE_READERF_ENDOFSTREAM) || timestamp >= endHns;
        if (ended) {
            (readVideo ? videoDone : audioDone) = true;
            reader->SetStreamSelection(stream, FALSE);
            continue;
        }
        if (!sample) {
            continue; // stream tick or format change without data
        }
        if (readVideo) {
            lastVideoTs = timestamp;
            if (timestamp < startHns) continue; // decoded frames before the trim start
            ComPtr<IMFMediaBuffer> buffer;
            hr = cropSample(sample.Get(), frameSize, defaultStride, crop, buffer);
            ComPtr<IMFSample> outSample;
            if (SUCCEEDED(hr)) hr = MFCreateSample(&outSample);
            if (SUCCEEDED(hr)) hr = outSample->AddBuffer(buffer.Get());
            if (SUCCEEDED(hr)) hr = outSample->SetSampleTime(timestamp - startHns);
            if (SUCCEEDED(hr)) hr = outSample->SetSampleDuration(frameDuration);
            if (SUCCEEDED(hr)) hr = writer->WriteSample(videoStream, outSample.Get());
            if (FAILED(hr)) return fail(hrText("WriteSample(video)", hr));
            const int percent = qBound(0, static_cast<int>((timestamp - startHns) * 100 / (endHns - startHns)),
                                       kMaxProgressBeforeFinish);
            if (percent != lastPercent) {
                lastPercent = percent;
                if (progress && !progress(percent)) return fail(QStringLiteral("Cancelled"));
            }
        } else {
            lastAudioTs = timestamp;
            if (timestamp < startHns) continue;
            hr = sample->SetSampleTime(timestamp - startHns);
            if (SUCCEEDED(hr)) hr = writer->WriteSample(audioStream, sample.Get());
            if (FAILED(hr)) return fail(hrText("WriteSample(audio passthrough)", hr));
        }
    }

    hr = writer->Finalize();
    if (FAILED(hr)) return fail(hrText("Finalize", hr));
    writer.Reset();
    reader.Reset();
    if (progress) progress(kCompletePercent);
    const VideoFileProbe outputProbe = probe(request.outputPath);
    if (!outputProbe.valid || outputProbe.durationMs <= 0 || (hasAudio && !outputProbe.hasAudio)) {
        return fail(QStringLiteral("Output validation failed; source retained"));
    }
    result.success = true;
    result.audioCopied = hasAudio && outputProbe.hasAudio;
    return result;
}

} // namespace

std::unique_ptr<IVideoTranscoder> createMediaFoundationTranscoder()
{
    return std::make_unique<MediaFoundationTranscoder>();
}
```

Add `src/video/MediaFoundationTranscoder_win.cpp` to the Windows block in `CMakeLists.txt`. `snaptray_platform` already links `mfreadwrite mfplat mfuuid ole32 propsys`, so no new libraries are needed.

- [ ] **Step 3b (only if audio passthrough fails in Step 4): decode AAC to PCM and re-encode it**

Use this only after validating the complete fallback with the same audio contract tests. If passthrough fails at runtime, either return failure with the source intact or retry once with a fresh reader, writer and working file; never continue a partially written passthrough file. Check every HRESULT, including PCM negotiation, and fail if fallback setup or sample writing fails. The setup below shows the required types; implement checked calls for each operation:

```cpp
    UINT32 sampleRate = MFGetAttributeUINT32(audioType.Get(), MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
    UINT32 channels = MFGetAttributeUINT32(audioType.Get(), MF_MT_AUDIO_NUM_CHANNELS, 2);
    constexpr UINT32 kPcmBits = 16;
    constexpr UINT32 kAacBytesPerSecond = 16000; // 128 kbps, matches MediaFoundationEncoder
    ComPtr<IMFMediaType> pcmType;
    MFCreateMediaType(&pcmType);
    pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kPcmBits);
    pcmType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
    pcmType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    pcmType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * kPcmBits / 8);
    pcmType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, sampleRate * channels * kPcmBits / 8);
    reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcmType.Get());
    ComPtr<IMFMediaType> aacType;
    MFCreateMediaType(&aacType);
    aacType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    aacType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    aacType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, kPcmBits);
    aacType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate);
    aacType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
    aacType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAacBytesPerSecond);
    hr = writer->AddStream(aacType.Get(), &audioStream);
    if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(audioStream, pcmType.Get(), nullptr);
    if (FAILED(hr)) return fail(hrText("configure AAC re-encode", hr));
    audioEnabled = true;
```

After the fallback, `result.audioCopied` still means "the output has the source's audio". Note in the commit message that Windows re-encodes the audio.

- [ ] **Step 4: Run the test on Windows and verify it passes**

Run (cmd):

```
scripts\build.bat
set PATH=%QT_PATH%\bin;%PATH%
ctest --test-dir build -R Video_VideoTranscoder --output-on-failure
```

Expected: the baseline tests and required audio regressions PASS. On Windows `IVideoFrameReader::create()` returns nullptr, so the pixel check in `cropAndTrimKeepsAudio` is skipped.

- [ ] **Step 5: Run the macOS suite again**

Run on macOS: `./scripts/build.sh && ctest --test-dir build -R Video_ --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/video/MediaFoundationTranscoder_win.cpp src/video/IVideoTranscoder.cpp CMakeLists.txt
git commit -m "feat(video): add Media Foundation transcoder with crop, trim and AAC passthrough"
```

---

### Task 8: Route MP4 trim and crop through the transcoder

**Files:**
- Modify: `include/qml/RecordingPreviewBackend.h`, `src/qml/RecordingPreviewBackend.mm`
- Test: `tests/Qml/tst_RecordingPreviewExport.cpp` (macOS)

**Interfaces:**
- Consumes:
  - `IVideoTranscoder::create()`, `VideoTranscodeRequest`, `VideoTranscodeResult` (Tasks 5-7)
  - `m_cropRect`, `hasCrop()` (Task 3)
- Produces:
  - private `void performTranscode();`
  - private `std::shared_ptr<std::atomic_bool> m_exportCancelToken;`
  - removes `performTrim()`, `onTrimProgress()`, `onTrimFinished()`, `m_trimmer` and the `VideoTrimmer` include from the backend

`VideoTrimmer` itself stays in the tree, because `tests/Video/*` still uses it. Removing it is a follow-up in the roadmap.

- [ ] **Step 1: Write the failing test**

In `tests/Qml/tst_RecordingPreviewExport.cpp`, add `#include "video/IVideoTranscoder.h"`.

Give `createRecording` an audio option. The signature becomes `QString createRecording(const QString& path, qint64 firstFrameMs, const QSize& frameSize = kFrameSize, bool withAudio = false)`.

Add the audio constants:

```cpp
constexpr int kAudioSampleRate = 48000;
constexpr int kAudioFramesPerVideoFrame = kAudioSampleRate / kFrameRate;
```

Before `encoder->start(...)`, add `if (withAudio) encoder->setAudioFormat(kAudioSampleRate, 2, 16);`.

After `if (encoder->framesWritten() != before + 1) {...}`, add:

```cpp
        if (withAudio && encoder->isAudioEnabled()) {
            encoder->writeAudioSamples(QByteArray(kAudioFramesPerVideoFrame * 2 * 2, '\0'),
                                       qint64(i) * kAudioFramesPerVideoFrame);
        }
```

Then add the slots `saveMp4Edits_data()` and `saveMp4Edits()`:

```cpp
void tst_RecordingPreviewExport::saveMp4Edits_data()
{
    QTest::addColumn<bool>("trim");
    QTest::addColumn<bool>("crop");
    QTest::addColumn<bool>("withAudio");
    QTest::newRow("trim-with-audio") << true << false << true;
    QTest::newRow("crop-with-audio") << false << true << true;
    QTest::newRow("trim-and-crop-silent") << true << true << false;
}

void tst_RecordingPreviewExport::saveMp4Edits()
{
    QFETCH(bool, trim);
    QFETCH(bool, crop);
    QFETCH(bool, withAudio);
    const QSize sourceSize(160, 120);
    const QRect cropRect(32, 24, 96, 72);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("recording.mp4"));
    const QString fixtureError = createRecording(inputPath, 0, sourceSize, withAudio);
    QVERIFY2(fixtureError.isEmpty(), qPrintable(fixtureError));

    RecordingPreviewBackend backend(inputPath);
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    backend.updateVideoSize(sourceSize);
    backend.updateDuration(kFrameCount * kFrameIntervalMs);
    if (trim) {
        backend.setTrimStart(4 * kFrameIntervalMs);
        backend.setTrimEnd(10 * kFrameIntervalMs);
    }
    if (crop) {
        backend.setCropRect(cropRect);
    }

    QSignalSpy savedSpy(&backend, &RecordingPreviewBackend::saveRequested);
    backend.save();
    QTRY_VERIFY_WITH_TIMEOUT(!backend.isProcessing(), 20000);
    QVERIFY2(backend.errorMessage().isEmpty(), qPrintable(backend.errorMessage()));
    QCOMPARE(savedSpy.count(), 1);

    const QString outputPath = savedSpy.first().at(0).toString();
    QCOMPARE(QFileInfo(outputPath).suffix(), QStringLiteral("mp4"));
    QVERIFY(!QFileInfo::exists(inputPath));
    auto transcoder = IVideoTranscoder::create();
    QVERIFY(transcoder);
    const VideoFileProbe probe = transcoder->probe(outputPath);
    QVERIFY(probe.valid);
    QCOMPARE(probe.videoSize, crop ? cropRect.size() : sourceSize);
    QCOMPARE(probe.hasAudio, withAudio);
    if (trim) {
        QVERIFY2(qAbs(probe.durationMs - 6 * kFrameIntervalMs) <= 2 * kFrameIntervalMs,
                 qPrintable(QString::number(probe.durationMs)));
    }
    QVERIFY(QDir(directory.path()).entryList(QStringList(QStringLiteral("*.part-*")), QDir::Files).isEmpty());
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_RecordingPreviewExport --output-on-failure`
Expected:
- `trim-with-audio` fails with error `Trim failed: MP4 trimming cannot preserve this video's audio...`.
- `crop-with-audio` fails at `QCOMPARE(probe.videoSize, ...)` with `QSize(160, 120)`.

- [ ] **Step 3: Implement**

Header:
- Remove `class VideoTrimmer;`, `performTrim()`, `onTrimProgress()`, `onTrimFinished()` and `m_trimmer`.
- Add `#include <atomic>` and `#include <memory>`.
- Add the private members:

```cpp
    void performTranscode();
    std::shared_ptr<std::atomic_bool> m_exportCancelToken = std::make_shared<std::atomic_bool>(false);
```

In the `.mm`:
- Remove `#include "video/VideoTrimmer.h"` and add `#include "video/IVideoTranscoder.h"`.
- In the destructor, replace the `m_trimmer` cleanup with `m_exportCancelToken->store(true);`.
- In `save()`, replace `if (hasTrim()) { performTrim(); return; }` with:

```cpp
    if (hasTrim() || hasCrop()) {
        performTranscode();
        return;
    }
```

Delete `performTrim()`, `onTrimProgress()` and `onTrimFinished()`. Add:

```cpp
void RecordingPreviewBackend::performTranscode()
{
    const int dotIndex = m_videoPath.lastIndexOf('.');
    const QString base = dotIndex > 0 ? m_videoPath.left(dotIndex) : m_videoPath;
    const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString outputPath = base + QStringLiteral("_edited_") + stamp + QStringLiteral(".mp4");
    const QString workingPath = base + QStringLiteral("_edited_") + stamp + QStringLiteral(".part-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".mp4");

    m_isProcessing = true;
    m_processProgress = 0;
    m_processStatus = tr("Exporting video...");
    emit processingChanged();
    emit processProgressChanged();
    emit processStatusChanged();

    VideoTranscodeRequest request;
    request.inputPath = m_videoPath;
    request.outputPath = workingPath;
    request.startMs = m_trimStart;
    request.endMs = m_trimEnd;
    request.cropRect = m_cropRect;
    const QString unsupportedError = tr("Video export is not supported on this platform");
    const QString failedTemplate = tr("Export failed: %1");
    const auto cancelToken = m_exportCancelToken;
    QPointer<RecordingPreviewBackend> weakThis(this);

    (void)QtConcurrent::run([weakThis, request, outputPath, unsupportedError, failedTemplate, cancelToken]() {
        auto post = [](auto fn) {
            if (QCoreApplication* app = QCoreApplication::instance()) {
                QMetaObject::invokeMethod(app, fn, Qt::QueuedConnection);
            }
        };
        auto transcoder = IVideoTranscoder::create();
        VideoTranscodeResult result;
        if (!transcoder) {
            result.errorMessage = unsupportedError;
        } else {
            result = transcoder->transcode(request, [weakThis, cancelToken, post](int percent) {
                post([weakThis, percent]() {
                    if (weakThis) {
                        weakThis->m_processProgress = percent;
                        emit weakThis->processProgressChanged();
                    }
                });
                return !cancelToken->load();
            });
        }
        if (result.success) {
            const VideoFileProbe sourceProbe = transcoder->probe(request.inputPath);
            const VideoFileProbe outputProbe = transcoder->probe(request.outputPath);
            const QFileInfo outputInfo(request.outputPath);
            if (cancelToken->load() || !sourceProbe.valid || !outputProbe.valid
                || !outputInfo.exists() || outputInfo.size() <= 0 || outputProbe.durationMs <= 0
                || (sourceProbe.hasAudio && (!result.audioCopied || !outputProbe.hasAudio))) {
                QFile::remove(request.outputPath);
                result.success = false;
                result.errorMessage = QStringLiteral("Output validation failed; source retained");
            }
        }
        if (result.success && !QFile::rename(request.outputPath, outputPath)) {
            QFile::remove(request.outputPath);
            result.success = false;
            result.errorMessage = QStringLiteral("rename failed");
        }
        post([weakThis, result, outputPath, failedTemplate, inputPath = request.inputPath]() {
            if (!weakThis) {
                QFile::remove(outputPath);
                return;
            }
            weakThis->m_isProcessing = false;
            emit weakThis->processingChanged();
            if (!result.success) {
                weakThis->setErrorMessage(failedTemplate.arg(result.errorMessage));
                return;
            }
            QFile::remove(inputPath);
            weakThis->m_saved = true;
            weakThis->close();
            emit weakThis->saveRequested(outputPath);
        });
    });
}
```

Add the includes if they are missing: `<QPointer>`, `<QUuid>`, `<QDateTime>`, `<QFileInfo>`, `<QtConcurrent>`. On Windows the transcoder initializes COM itself, so no `ComGuard` is needed here.

- [ ] **Step 4: Run the tests and verify they pass**

Run: `./scripts/build.sh && ctest --test-dir build -R "Qml_RecordingPreview|Video_" --output-on-failure`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add include/qml/RecordingPreviewBackend.h src/qml/RecordingPreviewBackend.mm tests/Qml/tst_RecordingPreviewExport.cpp
git commit -m "feat(recording): export MP4 trim and crop via transcoder, keeping audio"
```

---

### Task 9: Crop overlay QML and preview wiring

**Required interaction regressions (in addition to the helper tests below):**
- Use a real QQuickWindow and mouse events to enter crop mode without a committed crop and drag from within the video to create the first selection, both with and without letterboxing. Verify moving, handle resizing, replacing, Escape rollback and empty-draft apply.
- Verify toolbar Save and Ctrl+S both export the visible draft; editing Enter applies without saving, while non-editing Enter saves. Processing must block all save entry points.
- Add bounded-snap cases: content width 400 and selection width 204 must never move beyond x=196; resizing near a centre-line snap must never violate the minimum size. Verify both axes and both edges.
- When content geometry changes during an edit (for example window resizing), cancel the draft and retain the committed video-pixel crop. This avoids reinterpreting stale view coordinates in the new content rectangle.

**Files:**
- Create: `src/qml/recording/RecordingCropOverlay.qml`
- Modify: `CMakeLists.txt`, in two places:
  - Add `src/qml/recording/RecordingCropOverlay.qml` to `QML_FILES` after `src/qml/recording/RecordingPreview.qml` (line 1076).
  - Add `set_source_files_properties(src/qml/recording/RecordingCropOverlay.qml PROPERTIES QT_RESOURCE_ALIAS recording/RecordingCropOverlay.qml)` after line 997.
- Modify: `src/qml/recording/RecordingPreview.qml`
- Test: `tests/Qml/tst_RecordingCropOverlay.cpp`

**Interfaces:**
- Consumes:
  - `videoPlayer.contentRect` (Task 2)
  - `backend.cropRect`, `hasCrop`, `videoSize`, `updateVideoSize`, `setCropFromView`, `cropRectInView`, `clearCrop` (Task 3)
- Produces the QML type `RecordingCropOverlay`:
  - Properties: `contentRect`, `committedRect`, `videoSize`, `accentColor`, `editing`, `draftRect`
  - Functions:
    - `beginEditing()`, `apply()`, `cancel()`
    - `createRect(px, py, x, y)`, `moveRect(r, dx, dy)`, `resizeRect(r, edges, dx, dy)`, `snapValue(v, targets)`
  - Signals: `applyRequested(rect viewRect)`, `cancelRequested()`
  - Edge flags: `edgeLeft = 1`, `edgeRight = 2`, `edgeTop = 4`, `edgeBottom = 8`

- [ ] **Step 1: Write the failing test** at `tests/Qml/tst_RecordingCropOverlay.cpp`

```cpp
#include <QtTest/QtTest>

#include "qml/QmlOverlayManager.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class tst_RecordingCropOverlay : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void beginEditingStartsEmpty();
    void createRectClampsToContent();
    void moveRectStaysInside();
    void resizeRectRespectsEdges();
    void snapValueSnapsWithinDistance();
    void applyEmitsDraft();
    void cancelLeavesEditing();

private:
    std::unique_ptr<QObject> m_overlay;
    QVariant call(const char* name, const QVariantList& args = {});
};

void tst_RecordingCropOverlay::init()
{
    QQmlComponent component(SnapTray::QmlOverlayManager::instance().engine(),
                            QUrl(QStringLiteral("qrc:/SnapTrayQml/recording/RecordingCropOverlay.qml")));
    m_overlay.reset(component.create());
    QVERIFY2(m_overlay, qPrintable(component.errorString()));
    m_overlay->setProperty("width", 400);
    m_overlay->setProperty("height", 300);
    m_overlay->setProperty("contentRect", QRectF(0, 50, 400, 200));
    m_overlay->setProperty("videoSize", QSize(800, 400));
}

QVariant tst_RecordingCropOverlay::call(const char* name, const QVariantList& args)
{
    QVariant ret;
    QGenericArgument a[4];
    for (int i = 0; i < args.size(); ++i) {
        a[i] = Q_ARG(QVariant, args.at(i));
    }
    const bool ok = QMetaObject::invokeMethod(m_overlay.get(), name, Q_RETURN_ARG(QVariant, ret),
                                              a[0], a[1], a[2], a[3]);
    if (!ok) {
        qWarning() << "invoke failed:" << name;
    }
    return ret;
}

void tst_RecordingCropOverlay::beginEditingStartsEmpty()
{
    call("beginEditing");
    QVERIFY(m_overlay->property("editing").toBool());
    QCOMPARE(m_overlay->property("draftRect").toRectF(), QRectF(0, 0, 0, 0));
}

void tst_RecordingCropOverlay::createRectClampsToContent()
{
    // Drag from inside the content to beyond its bottom-right corner.
    const QRectF r = call("createRect", {100, 100, 500, 400}).toRectF();
    QCOMPARE(r, QRectF(100, 100, 300, 150));
}

void tst_RecordingCropOverlay::moveRectStaysInside()
{
    const QRectF r = call("moveRect", {QRectF(300, 200, 80, 40), 100, 100}).toRectF();
    QCOMPARE(r, QRectF(320, 210, 80, 40));
}

void tst_RecordingCropOverlay::resizeRectRespectsEdges()
{
    constexpr int edgeRight = 2;
    constexpr int edgeBottom = 8;
    const QRectF r = call("resizeRect", {QRectF(100, 100, 100, 50), edgeRight | edgeBottom, 20, 10}).toRectF();
    QCOMPARE(r, QRectF(100, 100, 120, 60));
}

void tst_RecordingCropOverlay::snapValueSnapsWithinDistance()
{
    QCOMPARE(call("snapValue", {203, QVariantList{0, 200, 400}}).toReal(), 200.0);
    QCOMPARE(call("snapValue", {190, QVariantList{0, 200, 400}}).toReal(), 190.0);
}

void tst_RecordingCropOverlay::applyEmitsDraft()
{
    call("beginEditing");
    m_overlay->setProperty("draftRect", QRectF(10, 60, 100, 50));
    QSignalSpy spy(m_overlay.get(), SIGNAL(applyRequested(QRectF)));
    call("apply");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toRectF(), QRectF(10, 60, 100, 50));
    QVERIFY(!m_overlay->property("editing").toBool());
}

void tst_RecordingCropOverlay::cancelLeavesEditing()
{
    call("beginEditing");
    QSignalSpy spy(m_overlay.get(), SIGNAL(cancelRequested()));
    call("cancel");
    QCOMPARE(spy.count(), 1);
    QVERIFY(!m_overlay->property("editing").toBool());
}

QTEST_MAIN(tst_RecordingCropOverlay)
#include "tst_RecordingCropOverlay.moc"
```

Here is why `moveRectStaysInside` expects `(320,210,80,40)`. The content is x 0-400 and y 50-250. Moving by (+100,+100) is clamped so the rect stays inside, which gives x = 400-80 = 320 and y = 250-40 = 210.

Register it (mirrors `Qml_BeautifyPanelQml`, tests/CMakeLists.txt lines 110-114):

```cmake
add_executable(Qml_RecordingCropOverlay Qml/tst_RecordingCropOverlay.cpp)
target_link_libraries(Qml_RecordingCropOverlay PRIVATE snaptray_ui snaptray_qmlplugin Qt6::Qml Qt6::Quick Qt6::Test)
target_sources(Qml_RecordingCropOverlay PRIVATE ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
add_test(NAME Qml_RecordingCropOverlay COMMAND Qml_RecordingCropOverlay)
set_tests_properties(Qml_RecordingCropOverlay PROPERTIES TIMEOUT 60 LABELS "unit;integration")
```

- [ ] **Step 2: Run the test and verify it fails**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_RecordingCropOverlay --output-on-failure`
Expected: FAIL with `No such file or directory: qrc:/SnapTrayQml/recording/RecordingCropOverlay.qml`.

- [ ] **Step 3: Implement the overlay** at `src/qml/recording/RecordingCropOverlay.qml`

```qml
import QtQuick
import SnapTrayQml

/**
 * Crop editor drawn over the recording preview video.
 * Works in item coordinates; the backend converts to video pixels.
 */
Item {
    id: overlay

    property rect contentRect: Qt.rect(0, 0, 0, 0)
    property rect committedRect: Qt.rect(0, 0, 0, 0)
    property size videoSize: Qt.size(0, 0)
    property color accentColor: SemanticTokens.accentDefault
    property bool editing: false
    property rect draftRect: Qt.rect(0, 0, 0, 0)

    signal applyRequested(rect viewRect)
    signal cancelRequested()

    readonly property int edgeLeft: 1
    readonly property int edgeRight: 2
    readonly property int edgeTop: 4
    readonly property int edgeBottom: 8
    readonly property real handleSize: 10
    readonly property real snapDistance: 6
    readonly property real minViewSide: 8
    readonly property rect shownRect: editing ? draftRect : committedRect
    // committedRect is empty when there is no crop (RecordingPreview passes Qt.rect(0, 0, 0, 0)).
    readonly property bool hasShownRect: shownRect.width > 0 && shownRect.height > 0

    onContentRectChanged: {
        if (editing)
            cancel()
    }

    function beginEditing() {
        draftRect = committedRect
        editing = true
    }
    function apply() {
        if (!editing)
            return
        editing = false
        if (draftRect.width > 0 && draftRect.height > 0)
            applyRequested(draftRect)
    }
    function cancel() {
        if (!editing)
            return
        editing = false
        cancelRequested()
    }

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
    function snapValue(v, targets) {
        for (let i = 0; i < targets.length; ++i) {
            if (Math.abs(v - targets[i]) <= snapDistance)
                return targets[i]
        }
        return v
    }
    function snapWithin(v, targets, lo, hi) {
        const bounded = clamp(v, lo, hi)
        let best = bounded
        let distance = snapDistance + 1
        for (let i = 0; i < targets.length; ++i) {
            const target = targets[i]
            const delta = Math.abs(bounded - target)
            if (target >= lo && target <= hi && delta <= snapDistance && delta < distance) {
                best = target
                distance = delta
            }
        }
        return clamp(best, lo, hi)
    }
    function xTargets() { return [contentRect.x, contentRect.x + contentRect.width / 2, contentRect.x + contentRect.width] }
    function yTargets() { return [contentRect.y, contentRect.y + contentRect.height / 2, contentRect.y + contentRect.height] }

    function createRect(px, py, x, y) {
        const c = contentRect
        const x0 = snapValue(clamp(Math.min(px, x), c.x, c.x + c.width), xTargets())
        const y0 = snapValue(clamp(Math.min(py, y), c.y, c.y + c.height), yTargets())
        const x1 = snapValue(clamp(Math.max(px, x), c.x, c.x + c.width), xTargets())
        const y1 = snapValue(clamp(Math.max(py, y), c.y, c.y + c.height), yTargets())
        return Qt.rect(x0, y0, x1 - x0, y1 - y0)
    }
    function moveRect(r, dx, dy) {
        const c = contentRect
        const x = clamp(r.x + dx, c.x, c.x + c.width - r.width)
        const y = clamp(r.y + dy, c.y, c.y + c.height - r.height)
        // Either edge may align, but the rectangle must stay entirely inside content.
        const xs = xTargets().concat(xTargets().map(function(v) { return v - r.width }))
        const ys = yTargets().concat(yTargets().map(function(v) { return v - r.height }))
        return Qt.rect(snapWithin(x, xs, c.x, c.x + c.width - r.width),
                       snapWithin(y, ys, c.y, c.y + c.height - r.height), r.width, r.height)
    }
    function resizeRect(r, edges, dx, dy) {
        const c = contentRect
        let left = r.x, top = r.y, right = r.x + r.width, bottom = r.y + r.height
        if (edges & edgeLeft)
            left = snapWithin(left + dx, xTargets(), c.x, right - minViewSide)
        if (edges & edgeRight)
            right = snapWithin(right + dx, xTargets(), left + minViewSide, c.x + c.width)
        if (edges & edgeTop)
            top = snapWithin(top + dy, yTargets(), c.y, bottom - minViewSide)
        if (edges & edgeBottom)
            bottom = snapWithin(bottom + dy, yTargets(), top + minViewSide, c.y + c.height)
        return Qt.rect(left, top, right - left, bottom - top)
    }
    function edgesAt(x, y) {
        const r = draftRect
        if (r.width <= 0 || r.height <= 0)
            return 0
        let edges = 0
        if (Math.abs(x - r.x) <= handleSize) edges |= edgeLeft
        else if (Math.abs(x - (r.x + r.width)) <= handleSize) edges |= edgeRight
        if (Math.abs(y - r.y) <= handleSize) edges |= edgeTop
        else if (Math.abs(y - (r.y + r.height)) <= handleSize) edges |= edgeBottom
        const inside = x >= r.x - handleSize && x <= r.x + r.width + handleSize
                && y >= r.y - handleSize && y <= r.y + r.height + handleSize
        return inside ? edges : 0
    }
    function cursorFor(x, y) {
        const e = edgesAt(x, y)
        if ((e & edgeLeft && e & edgeTop) || (e & edgeRight && e & edgeBottom)) return Qt.SizeFDiagCursor
        if ((e & edgeRight && e & edgeTop) || (e & edgeLeft && e & edgeBottom)) return Qt.SizeBDiagCursor
        if (e & (edgeLeft | edgeRight)) return Qt.SizeHorCursor
        if (e & (edgeTop | edgeBottom)) return Qt.SizeVerCursor
        const r = draftRect
        if (x > r.x && x < r.x + r.width && y > r.y && y < r.y + r.height) return Qt.SizeAllCursor
        return Qt.CrossCursor
    }

    // Dim everything inside the video but outside the crop.
    Repeater {
        model: overlay.hasShownRect ? 4 : 0
        delegate: Rectangle {
            required property int index
            readonly property rect c: overlay.contentRect
            readonly property rect s: overlay.shownRect
            color: SemanticTokens.backgroundOverlay
            x: index === 2 ? s.x + s.width : c.x
            y: index === 0 ? c.y : index === 1 ? s.y + s.height : s.y
            width: index < 2 ? c.width : index === 2 ? c.x + c.width - (s.x + s.width) : s.x - c.x
            height: index === 0 ? s.y - c.y : index === 1 ? c.y + c.height - (s.y + s.height) : s.height
        }
    }

    Rectangle {
        visible: overlay.hasShownRect
        x: overlay.shownRect.x
        y: overlay.shownRect.y
        width: overlay.shownRect.width
        height: overlay.shownRect.height
        color: "transparent"
        border.color: overlay.accentColor
        border.width: 2
    }

    Repeater {
        model: overlay.editing && overlay.hasShownRect ? [[0, 0], [0.5, 0], [1, 0], [0, 0.5], [1, 0.5], [0, 1], [0.5, 1], [1, 1]] : []
        delegate: Rectangle {
            required property var modelData
            width: overlay.handleSize
            height: overlay.handleSize
            radius: 2
            color: overlay.accentColor
            x: overlay.draftRect.x + overlay.draftRect.width * modelData[0] - width / 2
            y: overlay.draftRect.y + overlay.draftRect.height * modelData[1] - height / 2
        }
    }

    Text {
        objectName: "cropSizeLabel"
        visible: overlay.editing && overlay.hasShownRect && overlay.contentRect.width > 0
        x: overlay.draftRect.x + overlay.draftRect.width - implicitWidth
        y: overlay.draftRect.y + overlay.draftRect.height + SemanticTokens.spacing4
        text: Math.round(overlay.draftRect.width * overlay.videoSize.width / overlay.contentRect.width)
              + " × " + Math.round(overlay.draftRect.height * overlay.videoSize.height / overlay.contentRect.height)
        color: SemanticTokens.textPrimary
        font.pixelSize: SemanticTokens.fontSizeCaption
        font.family: SemanticTokens.fontFamily
    }

    MouseArea {
        id: cropMouse
        anchors.fill: parent
        enabled: overlay.editing
        hoverEnabled: overlay.editing
        cursorShape: overlay.editing ? overlay.cursorFor(mouseX, mouseY) : Qt.ArrowCursor

        property int mode: 0 // 0 create, 1 move, 2 resize
        property int edges: 0
        property real pressX: 0
        property real pressY: 0
        property rect pressRect: Qt.rect(0, 0, 0, 0)

        onPressed: function(mouse) {
            pressX = mouse.x
            pressY = mouse.y
            pressRect = overlay.draftRect
            edges = overlay.edgesAt(mouse.x, mouse.y)
            const r = overlay.draftRect
            const inside = mouse.x > r.x && mouse.x < r.x + r.width && mouse.y > r.y && mouse.y < r.y + r.height
            mode = edges !== 0 ? 2 : inside ? 1 : 0
        }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return
            const dx = mouse.x - pressX
            const dy = mouse.y - pressY
            if (mode === 0)
                overlay.draftRect = overlay.createRect(pressX, pressY, mouse.x, mouse.y)
            else if (mode === 1)
                overlay.draftRect = overlay.moveRect(pressRect, dx, dy)
            else
                overlay.draftRect = overlay.resizeRect(pressRect, edges, dx, dy)
        }
    }
}
```

Before relying on `SemanticTokens.accentDefault`, `textPrimary` and `backgroundOverlay`, check that they exist with `grep -n "accentDefault\|textPrimary\|backgroundOverlay" src/qml/tokens/SemanticTokens.qml`. `RecordingPreview.qml` already uses `SemanticTokens.backgroundOverlay`. If `accentDefault` is named differently, use the same token `RecordingPreview.qml` uses for `root.accent` (lines 14-23).

- [ ] **Step 4: Run the overlay test and verify it passes**

Run: `./scripts/build.sh && ctest --test-dir build -R Qml_RecordingCropOverlay --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Wire the overlay into `src/qml/recording/RecordingPreview.qml`**

1. On `VideoPlaybackItem { id: videoPlayer ... }`, add:

```qml
                onVideoLoaded: backend.updateVideoSize(videoPlayer.videoSize)
```

2. On the click-to-play `MouseArea` (lines 123-127), add `enabled: !cropOverlay.editing`.

3. Right after that MouseArea, add:

```qml
            RecordingCropOverlay {
                id: cropOverlay
                objectName: "previewCropOverlay"
                anchors.fill: parent
                z: 4
                contentRect: videoPlayer.contentRect
                videoSize: backend.videoSize
                accentColor: root.accent
                // backend.cropRect is read so the binding re-evaluates when the crop changes.
                committedRect: {
                    backend.cropRect
                    return backend.hasCrop ? backend.cropRectInView(videoPlayer.contentRect) : Qt.rect(0, 0, 0, 0)
                }
                onApplyRequested: function(viewRect) { backend.setCropFromView(viewRect, videoPlayer.contentRect) }
            }
```

4. In the toolbar, directly before the trim `IconButton` (line 450), add:

```qml
                IconButton {
                    objectName: "previewCropButton"
                    iconSource: "qrc:/icons/icons/crop.svg"
                    highlighted: backend.hasCrop || cropOverlay.editing
                    tooltipText: cropOverlay.editing ? qsTr("Apply Crop (Enter)")
                                                     : backend.hasCrop ? qsTr("Edit Crop") : qsTr("Crop Recording")
                    onClicked: cropOverlay.editing ? cropOverlay.apply() : cropOverlay.beginEditing()
                }

                Text {
                    objectName: "previewCropSizeLabel"
                    visible: backend.hasCrop && !cropOverlay.editing
                    text: backend.cropRect.width + " × " + backend.cropRect.height
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                }

                IconButton {
                    objectName: "previewClearCropButton"
                    visible: backend.hasCrop && !cropOverlay.editing
                    iconText: "✕"
                    tooltipText: qsTr("Clear Crop")
                    onClicked: backend.clearCrop()
                }
```

5. Add one function on `root` and route the save `IconButton`, existing Ctrl+S handler, and non-editing Enter handler through it. Do not leave any direct `backend.save()` calls outside this function:

```qml
    function saveWithCrop() {
        if (backend.isProcessing)
            return
        if (cropOverlay.editing)
            cropOverlay.apply()
        backend.save()
    }
```

The save button uses `onClicked: root.saveWithCrop()`. The existing shortcut and non-editing Enter branches call `root.saveWithCrop()` and accept the event. Editing Enter is handled by step 6 and only applies the draft.

6. In `Keys.onPressed` (line 567), right after the `backend.isProcessing` early return, add:

```qml
        if (cropOverlay.editing) {
            if (event.key === Qt.Key_Escape) {
                cropOverlay.cancel()
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                cropOverlay.apply()
                event.accepted = true
                return
            }
        }
```

- [ ] **Step 6: Build, run the Qml tests, and check manually**

Run: `./scripts/build.sh && ctest --test-dir build -R "Qml_" --output-on-failure`
Expected: PASS.

Then check by hand with `./scripts/build-and-run.sh`. Record about 5 s of screen, then in Preview:
1. Click Crop with no existing crop. There is no selection or handles yet.
2. Drag inside the video. A new rect appears with handles and dims the area outside it. After creating it, dragging outside the selection replaces it.
3. Drag inside it. The rect moves.
4. Drag a handle. The rect resizes.
5. Move an edge near the video edge or a centre line. It snaps.
6. Press Esc. The crop is restored; the Preview does not close.
7. Draw again and press Enter. The chip shows the size, for example `1280 × 720`.
8. Export MP4 (with mic audio enabled), GIF and WebP. Each output has the cropped size, and the MP4 has audio.
9. Press ✕. The crop clears.

- [ ] **Step 7: Commit**

```bash
git add src/qml/recording/RecordingCropOverlay.qml src/qml/recording/RecordingPreview.qml CMakeLists.txt tests/Qml/tst_RecordingCropOverlay.cpp tests/CMakeLists.txt
git commit -m "feat(recording): add crop editing to recording preview"
```

---

### Task 10: Translations, docs, changelog, rule clarification

**Files:**
- Modify: all 24 `translations/snaptray_*.ts` files
- Modify: `tests/Settings/tst_QmlTranslations.cpp`
- Modify: `docs/docs/recording.md`, `docs/zh-tw/docs/recording.md`, `CHANGELOG.md`, `CLAUDE.md`

New strings:

| Context | Source | zh_TW |
| --- | --- | --- |
| `RecordingPreview` | Crop Recording | 裁切錄影 |
| `RecordingPreview` | Edit Crop | 編輯裁切 |
| `RecordingPreview` | Apply Crop (Enter) | 套用裁切 (Enter) |
| `RecordingPreview` | Clear Crop | 清除裁切 |
| `RecordingPreviewBackend` | Exporting video... | 正在匯出影片... |
| `RecordingPreviewBackend` | Video export is not supported on this platform | 此平台不支援匯出影片 |
| `RecordingPreviewBackend` | Export failed: %1 | 匯出失敗：%1 |

- [ ] **Step 1: Write the failing test**

In `tests/Settings/tst_QmlTranslations.cpp`, add a slot `testRecordingPreviewCropTranslatedForAllLocales()`. Model it on `testRecordingPreviewAudioNoticeTranslatedForAllLocales()` (lines 257-284): same `.qm` loop, same `QCOMPARE(qmFiles.size(), 24)`, same non-empty and differs-from-source assertions. It iterates over these pairs:

```cpp
    const QList<QPair<const char*, const char*>> strings = {
        {"RecordingPreview", "Crop Recording"},
        {"RecordingPreview", "Edit Crop"},
        {"RecordingPreview", "Apply Crop (Enter)"},
        {"RecordingPreview", "Clear Crop"},
        {"RecordingPreviewBackend", "Exporting video..."},
        {"RecordingPreviewBackend", "Video export is not supported on this platform"},
        {"RecordingPreviewBackend", "Export failed: %1"},
    };
```

For `Export failed: %1`, also assert that the translation contains `%1`.

- [ ] **Step 2: Run the test and verify it fails**

Run: `./scripts/build.sh && ctest --test-dir build -R Settings_QmlTranslations --output-on-failure`
Expected: FAIL with an empty translation for `Crop Recording`.

- [ ] **Step 3: Add the translations**

For each of the 24 `.ts` files, add a `<message>` for every string under the matching `<context>`. Translate each one into that file's language and keep `%1`. Use the zh_TW column above for `snaptray_zh_TW.ts`. Do not run a global `lupdate`, because it rewrites every `<location>` line.

- [ ] **Step 4: Run the test and verify it passes**

Run: `./scripts/build.sh && ctest --test-dir build -R Settings_QmlTranslations --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Docs and changelog**

In `docs/docs/recording.md`, add a section after "Recording lifecycle":

```markdown
## Crop a recording

Recording always captures the full screen. To keep only part of it, open the preview after you stop:

1. Click **Crop** in the preview toolbar.
2. Drag on the video to draw the area, then drag inside it to move or drag a handle to resize. Edges snap to the video edges and centre lines.
3. Press **Enter** to apply or **Esc** to cancel. The applied size appears next to the Crop button; click ✕ to clear it.
4. Save as MP4, GIF, or WebP. MP4 exports keep their audio.
```

Add the same section, translated, to `docs/zh-tw/docs/recording.md`.

In `CHANGELOG.md`, under `## [Unreleased]`:

```markdown
### Added

- Recording preview can crop the recording to a region before exporting MP4, GIF, or WebP.

### Fixed

- Trimming an MP4 recording with audio now keeps the audio instead of failing.
```

In `CLAUDE.md` under "### Recording is screen-first", append:

```markdown
Choosing a region happens after recording, by cropping in the Recording Preview; capture itself stays full screen.
```

- [ ] **Step 6: Full verification**

Run: `./scripts/run-tests.sh`
Expected: the whole suite PASSes. On Windows, run `scripts\run-tests.bat`.

- [ ] **Step 7: Commit**

```bash
git add translations tests/Settings/tst_QmlTranslations.cpp docs/docs/recording.md docs/zh-tw/docs/recording.md CHANGELOG.md CLAUDE.md
git commit -m "docs(recording): document preview crop and translate new strings"
```
