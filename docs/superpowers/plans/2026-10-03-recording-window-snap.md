# Recording Window Timeline and Snapping (Phase 2) Implementation Plan

## Implementation audit — 2026-10-04

- [x] The planned core implementation is present on dev-3 (baseline `7afc5b80`).
- [ ] Complete all native platform/hardware acceptance gates. Implementation presence is not runtime proof.

The historical step checkboxes below retain the original execution recipe. Current remaining work and evidence are tracked in [dev-3 completion](2026-10-04-dev-3-completion.md); the review tracker remains authoritative for review IDs.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** While a recording runs, remember where the top-level windows were; in the preview's crop editor, hovering highlights the window under the cursor at the playhead and a click snaps the crop to it, while dragging still draws a free rectangle.

**Architecture:**
- `WindowTimeline` is pure data in `snaptray_core`: sampled window rectangles in video pixels, keyed by media time, with delta compaction, z-ordered hit testing and a JSON form. `WindowFrameMapping` (same header) converts logical screen rectangles into the recorded frame's video pixels with the same mapping the recorder uses for the capture region.
- `WindowTimelineRecorder` (`snaptray_platform`) samples `WindowDetector` on the GUI thread every 250 ms while recording, pauses with the recording, and hands the finished timeline to `RecordingManager`, which writes it as a sidecar next to the temp MP4. The sidecar lives and dies with the temp MP4.
- `RecordingPreviewBackend` loads the sidecar and answers "which window is under this view point at this time" in view coordinates; `RecordingCropOverlay` shows a hover highlight and turns a click (no drag) into a snapped draft. A missing or corrupt sidecar leaves Phase 1 behaviour untouched.

**Tech Stack:** Qt 6.11.2 (Quick/QML, Concurrent, Test), C++17, CMake + Ninja. No new dependencies.

**Spec:** `docs/superpowers/plans/2026-10-02-region-recording-longshot-roadmap.md`, section "Phase 2: Window timeline and snapping" plus "Global Constraints". Deviations from the roadmap, decided while reading the code as it stands after Phase 1:
- Sampling runs on the GUI thread, not a background thread: on Windows `WindowDetector::refreshWindowListAsync()` already runs synchronously on the GUI thread because `CoordinateHelper::physicalToQtLogical()` needs `QGuiApplication` state. A `QTimer` on the GUI thread calling `refreshWindowList(TopLevelOnly)` (no titles, no child controls) costs a few milliseconds per sample.
- No separate "excluded window" list is needed: every platform's `WindowDetector` enumeration already skips SnapTray's own process (`currentProcessId` on Windows, `getpid()` on macOS and Linux), which removes the recording control bar and its tooltip.
- `WindowTimelineRecorder` takes a media-time clock callback instead of a `setMediaTimeMs()` feed, so it reads the same `m_elapsedTimer - m_pausedDuration` clock that stamps the encoded frames.

## Global Constraints

**Product rules** (roadmap)
- Recording always captures the full screen. Region choice happens after recording, in RecordingPreview. Do not touch Region Selector.
- `showPreview = false` (direct save) behaviour stays unchanged: no recorder, no sidecar.
- Sidecar data never records window titles; it never reaches final outputs or History.
- Deferred, do not implement: child-control snapping, a timeline indicator for stable ranges.

**Coordinates** (roadmap)
- All rects that cross from recording to Preview are in video pixels. The recorder converts from logical/DPR coordinates at capture time; Preview never handles DPI.
- Window rects are clipped to the recorded frame before they are stored.

**Platforms** (roadmap)
- macOS 14+ and Windows 10+. Linux beta keeps recording hidden; all new code compiles there (`WindowDetector_linux.cpp` exists) but is never started.

**Code rules** (AGENTS.md)
- Use settings managers instead of raw `QSettings`; use named constants instead of magic numbers; `qWarning()` for API failures, `qDebug()` for diagnostics; prefer `QPoint`/`QRect` over loose coordinate tuples.

**Translations**
- Every new user-facing string is translated in all 24 `translations/snaptray_*.ts` files and covered by `tests/Settings/tst_QmlTranslations.cpp`. Do not run a global `lupdate`; add `<message>` blocks by hand (no `<location>` line needed).

**Build and test commands**
- Build: `./scripts/build.sh` (macOS) or `scripts\build.bat` (Windows; run inside an MSVC developer prompt, or call `vcvars64.bat` first).
- One test: `ctest --test-dir build -R <TestName> --output-on-failure`. On Windows prepend `%QT_PATH%\bin` to `PATH` first (Qt lives at `C:\Qt\6.11.2\msvc2022_64`), and when running a test exe directly pass `-o <file>,txt` because its stdout is not captured by the tool shells.
- Full suite: `./scripts/run-tests.sh` or `scripts\run-tests.bat`.

## Review Focus

Inputs the spec implies but no task's tests would otherwise exercise, most likely to bite first. Each has a test pinned to the task that owns the code:
1. A sidecar from another recording (different frame size) next to the preview's video: the backend must drop it and fall back to free-rect cropping (Task 7, `sidecarFrameSizeMismatchDisablesTimeline`).
2. A window straddling the edge of the recorded screen, or spanning two screens: the stored rect is the clipped part, and a fully off-screen window produces no sample (Task 2, "spanning window is clipped" and "window on another screen is dropped").
3. A playhead before the first sample or after the last one: `windowsAt()` answers with the first and last entry respectively, so hovering at 0 ms or at the very end still works (Task 1, `windowsAtOutsideRange`).
4. A clicked window smaller than the minimum crop in view pixels (a tiny dialog on a 4K recording shown small): the snapped draft is grown around its centre to the minimum, inside the content (Task 8, `snapToRectGrowsToMinimum`).
5. Pause: no samples are taken while paused and media time does not advance, so the resumed recording's samples line up with the video (Task 3, `pauseStopsSamplingAndResumeContinues`).

---

## File Structure

| File | Status | Responsibility |
| --- | --- | --- |
| `include/recording/WindowTimeline.h`, `src/recording/WindowTimeline.cpp` | new (snaptray_core) | `WindowSample`, `WindowTimelineEntry`, `WindowTimeline` (append with compaction, `windowsAt`, `hitTest`, JSON), `WindowFrameMapping` |
| `tests/RecordingManager/tst_WindowTimeline.cpp` | new | table-driven timeline and JSON tests |
| `tests/RecordingManager/tst_WindowFrameMapping.cpp` | new | logical to video-pixel mapping tests (Retina, 125 %, 150 % with non-zero origin, clipping) |
| `include/WindowDetector.h` | modify | `topLevelWindowsSnapshot()` accessor |
| `tests/Detection/tst_WindowDetectorQueryMode.cpp` | modify | accessor test |
| `include/recording/WindowTimelineRecorder.h`, `src/recording/WindowTimelineRecorder.cpp` | new (snaptray_platform, Q_OBJECT) | 250 ms GUI-thread sampler over an injectable enumerator and clock |
| `tests/RecordingManager/tst_WindowTimelineRecorder.cpp` | new | fake enumerator + fake clock tests |
| `include/recording/WindowTimelineSidecar.h`, `src/recording/WindowTimelineSidecar.cpp` | new (snaptray_core) | sidecar path, atomic write, read, remove |
| `tests/RecordingManager/tst_WindowTimelineSidecar.cpp` | new | sidecar round trip, missing, corrupt, remove |
| `include/RecordingManager.h`, `src/RecordingManager.cpp` | modify | own the recorder, write the sidecar on stop, remove it on save, stale-file filter |
| `tests/RecordingManager/tst_Lifecycle.cpp` | modify | recorder lifecycle, sidecar on finish, stale cleanup, save removes sidecar |
| `src/MainApplication.cpp` | modify | discard removes the sidecar |
| `include/utils/VideoCropGeometry.h`, `src/utils/VideoCropGeometry.cpp` | modify | `viewPointToVideo()` |
| `tests/Utils/tst_VideoCropGeometry.cpp` | modify | point mapping test |
| `include/qml/RecordingPreviewBackend.h`, `src/qml/RecordingPreviewBackend.mm` | modify | load sidecar, `hasWindowTimeline`, `windowRectInViewAt`, `windowAppAt`, remove sidecar with the source |
| `tests/Qml/tst_RecordingPreviewCrop.cpp` | modify | backend timeline API tests |
| `tests/Qml/tst_RecordingPreviewExport.cpp` | modify | export success removes the sidecar |
| `src/qml/recording/RecordingCropOverlay.qml` | modify | hover highlight, app label, click-to-snap, `snapToRect` |
| `src/qml/recording/RecordingPreview.qml` | modify | hover wiring to the backend, snap hint chip |
| `tests/Qml/tst_RecordingCropOverlay.cpp` | modify | stub backend timeline, snap unit tests, preview hover/click tests |
| `translations/snaptray_*.ts` (24), `tests/Settings/tst_QmlTranslations.cpp` | modify | one new string |
| `docs/docs/recording.md`, `docs/zh-tw/docs/recording.md`, `CHANGELOG.md` | modify | user docs, release note |
| `CMakeLists.txt`, `tests/CMakeLists.txt` | modify | new sources and test targets |

---

### Task 1: `WindowTimeline` data, compaction, hit testing and JSON

**Files:**
- Create: `include/recording/WindowTimeline.h`, `src/recording/WindowTimeline.cpp`
- Create: `tests/RecordingManager/tst_WindowTimeline.cpp`
- Modify: `CMakeLists.txt` (snaptray_core source list, after `src/utils/VideoCropGeometry.cpp`), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ElementType` from `include/WindowDetector.h` (header-only enum), Qt JSON.
- Produces (used by Tasks 2 to 9):

```cpp
namespace SnapTray {

// One top-level window as seen at a sample time, in video pixels of the recording.
struct WindowSample {
    quint32 windowId = 0;
    QRect rect;                                  // video pixels, clipped to the recorded frame
    int z = 0;                                   // 0 = topmost
    QString ownerApp;                            // never a window title
    ElementType type = ElementType::Window;      // Window or Dialog only
    bool operator==(const WindowSample& other) const;
    bool operator!=(const WindowSample& other) const { return !(*this == other); }
};

struct WindowTimelineEntry {
    qint64 tMs = 0;                              // media time: recording clock minus pauses
    std::vector<WindowSample> windows;           // sorted by z ascending
};

class WindowTimeline {
public:
    static constexpr int kFormatVersion = 1;

    void setFrameSize(const QSize& size);
    QSize frameSize() const;
    // Appends unless `windows` equals the last entry's windows (delta compaction).
    void append(qint64 tMs, std::vector<WindowSample> windows);
    const std::vector<WindowTimelineEntry>& entries() const;
    bool isEmpty() const;
    // Last entry with t <= tMs; the first entry for a time before it; nullptr when empty.
    const std::vector<WindowSample>* windowsAt(qint64 tMs) const;
    // The window with the smallest z containing videoPoint at tMs.
    std::optional<WindowSample> hitTest(const QPoint& videoPoint, qint64 tMs) const;
    QByteArray toJson() const;
    // nullopt for malformed JSON, a wrong version or a missing frame size.
    static std::optional<WindowTimeline> fromJson(const QByteArray& json);
};

} // namespace SnapTray
```

JSON layout written by `toJson()`:

```json
{"version":1,"frameSize":[2560,1440],
 "entries":[{"t":0,"windows":[{"id":197172,"rect":[0,0,1280,1440],"z":0,"app":"Code","type":"window"}]}]}
```

- [ ] **Step 1: Write the failing tests**

Create `tests/RecordingManager/tst_WindowTimeline.cpp`:

```cpp
#include <QtTest/QtTest>

#include "recording/WindowTimeline.h"

using SnapTray::WindowSample;
using SnapTray::WindowTimeline;

namespace {

WindowSample sample(quint32 id, const QRect& rect, int z, const char* app = "App",
                    ElementType type = ElementType::Window)
{
    WindowSample s;
    s.windowId = id;
    s.rect = rect;
    s.z = z;
    s.ownerApp = QString::fromLatin1(app);
    s.type = type;
    return s;
}

} // namespace

class tst_WindowTimeline : public QObject
{
    Q_OBJECT

private slots:
    void appendCompactsUnchangedEntries();
    void windowsAtPicksLastEntryAtOrBefore();
    void windowsAtOutsideRange();
    void hitTestPrefersTopmost();
    void hitTestMissesOutsideEveryWindow();
    void jsonRoundTrip();
    void fromJsonRejectsBadInput_data();
    void fromJsonRejectsBadInput();
};

void tst_WindowTimeline::appendCompactsUnchangedEntries()
{
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    const std::vector<WindowSample> a{sample(1, QRect(0, 0, 800, 600), 0)};
    const std::vector<WindowSample> b{sample(1, QRect(10, 0, 800, 600), 0)};
    timeline.append(0, a);
    timeline.append(250, a);   // unchanged: dropped
    timeline.append(500, a);   // unchanged: dropped
    timeline.append(750, b);   // moved: kept
    timeline.append(1000, a);  // moved back: kept (differs from the last entry)
    QCOMPARE(timeline.entries().size(), size_t(3));
    QCOMPARE(timeline.entries()[0].tMs, qint64(0));
    QCOMPARE(timeline.entries()[1].tMs, qint64(750));
    QCOMPARE(timeline.entries()[2].tMs, qint64(1000));
    QVERIFY(!timeline.isEmpty());
}

void tst_WindowTimeline::windowsAtPicksLastEntryAtOrBefore()
{
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    timeline.append(0, {sample(1, QRect(0, 0, 100, 100), 0)});
    timeline.append(1000, {sample(2, QRect(0, 0, 100, 100), 0)});
    QCOMPARE(timeline.windowsAt(999)->front().windowId, quint32(1));
    QCOMPARE(timeline.windowsAt(1000)->front().windowId, quint32(2));
    QCOMPARE(timeline.windowsAt(5000)->front().windowId, quint32(2));
}

void tst_WindowTimeline::windowsAtOutsideRange()
{
    WindowTimeline empty;
    QVERIFY(empty.windowsAt(0) == nullptr);
    QVERIFY(!empty.hitTest(QPoint(0, 0), 0).has_value());

    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    timeline.append(40, {sample(7, QRect(0, 0, 100, 100), 0)});
    // Before the first sample (the first frame is stamped a few ms after start): use it.
    QCOMPARE(timeline.windowsAt(0)->front().windowId, quint32(7));
    QCOMPARE(timeline.windowsAt(-5)->front().windowId, quint32(7));
}

void tst_WindowTimeline::hitTestPrefersTopmost()
{
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    // Entry windows are stored in z order; hitTest must not rely on it.
    timeline.append(0, {sample(2, QRect(0, 0, 1920, 1080), 1, "Back"),
                        sample(1, QRect(100, 100, 400, 300), 0, "Front")});
    const auto hit = timeline.hitTest(QPoint(150, 150), 0);
    QVERIFY(hit.has_value());
    QCOMPARE(hit->windowId, quint32(1));
    QCOMPARE(hit->ownerApp, QStringLiteral("Front"));
    const auto back = timeline.hitTest(QPoint(50, 50), 0);
    QVERIFY(back.has_value());
    QCOMPARE(back->windowId, quint32(2));
}

void tst_WindowTimeline::hitTestMissesOutsideEveryWindow()
{
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    timeline.append(0, {sample(1, QRect(100, 100, 400, 300), 0)});
    QVERIFY(!timeline.hitTest(QPoint(50, 50), 0).has_value());
    // QRect::contains is inclusive of right/bottom - 1 only.
    QVERIFY(timeline.hitTest(QPoint(499, 399), 0).has_value());
    QVERIFY(!timeline.hitTest(QPoint(500, 400), 0).has_value());
}

void tst_WindowTimeline::jsonRoundTrip()
{
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(2560, 1440));
    timeline.append(0, {sample(197172, QRect(0, 0, 1280, 1440), 0, "Code"),
                        sample(5, QRect(1280, 0, 1280, 1440), 1, "Safari", ElementType::Dialog)});
    timeline.append(1250, {sample(197172, QRect(0, 0, 1280, 1440), 0, "Code")});
    const QByteArray json = timeline.toJson();
    QVERIFY(!json.contains("title"));
    const auto parsed = WindowTimeline::fromJson(json);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->frameSize(), QSize(2560, 1440));
    QCOMPARE(parsed->entries().size(), size_t(2));
    QCOMPARE(parsed->entries()[0].windows.size(), size_t(2));
    QVERIFY(parsed->entries()[0].windows[1] == timeline.entries()[0].windows[1]);
    QCOMPARE(parsed->entries()[0].windows[1].type, ElementType::Dialog);
    QCOMPARE(parsed->entries()[1].tMs, qint64(1250));
    QCOMPARE(parsed->toJson(), json);
}

void tst_WindowTimeline::fromJsonRejectsBadInput_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::newRow("not json") << QByteArray("hello");
    QTest::newRow("array root") << QByteArray("[]");
    QTest::newRow("wrong version") << QByteArray(R"({"version":2,"frameSize":[1,1],"entries":[]})");
    QTest::newRow("missing frame size") << QByteArray(R"({"version":1,"entries":[]})");
    QTest::newRow("empty frame size") << QByteArray(R"({"version":1,"frameSize":[0,0],"entries":[]})");
    QTest::newRow("rect not four numbers")
        << QByteArray(R"({"version":1,"frameSize":[10,10],"entries":[{"t":0,"windows":[{"id":1,"rect":[1,2,3],"z":0,"app":"a","type":"window"}]}]})");
    QTest::newRow("unknown type")
        << QByteArray(R"({"version":1,"frameSize":[10,10],"entries":[{"t":0,"windows":[{"id":1,"rect":[0,0,5,5],"z":0,"app":"a","type":"menu"}]}]})");
}

void tst_WindowTimeline::fromJsonRejectsBadInput()
{
    QFETCH(QByteArray, json);
    QVERIFY(!WindowTimeline::fromJson(json).has_value());
}

QTEST_GUILESS_MAIN(tst_WindowTimeline)
#include "tst_WindowTimeline.moc"
```

Add to `tests/CMakeLists.txt`, after the `RecordingManager_FrameRateUpdateState` block:

```cmake
add_executable(RecordingManager_WindowTimeline RecordingManager/tst_WindowTimeline.cpp)
target_link_libraries(RecordingManager_WindowTimeline PRIVATE snaptray_core Qt6::Test)
add_test(NAME RecordingManager_WindowTimeline COMMAND RecordingManager_WindowTimeline)
set_tests_properties(RecordingManager_WindowTimeline PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 2: Build and verify the test fails to compile**

Run: `./scripts/build.sh` (or `scripts\build.bat`)
Expected: FAIL, `recording/WindowTimeline.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `include/recording/WindowTimeline.h`:

```cpp
#pragma once

#include "WindowDetector.h" // ElementType

#include <QByteArray>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>
#include <vector>

namespace SnapTray {

// One top-level window as seen at a sample time, in video pixels of the recording.
struct WindowSample {
    quint32 windowId = 0;
    QRect rect;                                  // video pixels, clipped to the recorded frame
    int z = 0;                                   // 0 = topmost
    QString ownerApp;                            // never a window title
    ElementType type = ElementType::Window;      // Window or Dialog only
    bool operator==(const WindowSample& other) const;
    bool operator!=(const WindowSample& other) const { return !(*this == other); }
};

struct WindowTimelineEntry {
    qint64 tMs = 0;                              // media time: recording clock minus pauses
    std::vector<WindowSample> windows;           // sorted by z ascending
};

// Where the top-level windows were during a recording, on the recording's
// own timeline and in its video pixels. Pure data: no Qt GUI, no platform code.
class WindowTimeline {
public:
    static constexpr int kFormatVersion = 1;

    void setFrameSize(const QSize& size) { m_frameSize = size; }
    QSize frameSize() const { return m_frameSize; }

    // Appends unless `windows` equals the last entry's windows (delta compaction).
    void append(qint64 tMs, std::vector<WindowSample> windows);
    const std::vector<WindowTimelineEntry>& entries() const { return m_entries; }
    bool isEmpty() const { return m_entries.empty(); }

    // Last entry with t <= tMs; the first entry for a time before it; nullptr when empty.
    const std::vector<WindowSample>* windowsAt(qint64 tMs) const;
    // The window with the smallest z containing videoPoint at tMs.
    std::optional<WindowSample> hitTest(const QPoint& videoPoint, qint64 tMs) const;

    QByteArray toJson() const;
    // nullopt for malformed JSON, a wrong version or a missing frame size.
    static std::optional<WindowTimeline> fromJson(const QByteArray& json);

private:
    QSize m_frameSize;
    std::vector<WindowTimelineEntry> m_entries;
};

// Converts logical screen rectangles (QScreen coordinates) into the recorded
// frame's video pixels, with the mapping RecordingManager used for the capture
// region. `physicalRegion` is the mapped capture region in native desktop
// pixels (its origin is the frame's (0, 0)); `physicalScreen` may be empty on
// platforms that expose no native desktop bounds (macOS), in which case the
// logical screen origin is the frame origin.
struct WindowFrameMapping {
    QRect logicalScreen;
    QRect physicalScreen;
    qreal devicePixelRatio = 1.0;
    QRect physicalRegion;

    bool isValid() const { return !logicalScreen.isEmpty() && !physicalRegion.isEmpty() && devicePixelRatio > 0.0; }
    // The part of `logicalBounds` inside the recorded frame, in video pixels; empty when none.
    QRect toVideoRect(const QRect& logicalBounds) const;
};

} // namespace SnapTray
```

- [ ] **Step 4: Write the implementation (timeline part; mapping comes in Task 2)**

Create `src/recording/WindowTimeline.cpp`:

```cpp
#include "recording/WindowTimeline.h"

#include "utils/CoordinateHelper.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace SnapTray {

namespace {

constexpr auto kKeyVersion = "version";
constexpr auto kKeyFrameSize = "frameSize";
constexpr auto kKeyEntries = "entries";
constexpr auto kKeyTime = "t";
constexpr auto kKeyWindows = "windows";
constexpr auto kKeyId = "id";
constexpr auto kKeyRect = "rect";
constexpr auto kKeyZ = "z";
constexpr auto kKeyApp = "app";
constexpr auto kKeyType = "type";
constexpr auto kTypeWindow = "window";
constexpr auto kTypeDialog = "dialog";
constexpr int kRectComponents = 4;

QJsonArray rectToJson(const QRect& rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

std::optional<QRect> rectFromJson(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != kRectComponents) {
        return std::nullopt;
    }
    for (const QJsonValue& component : array) {
        if (!component.isDouble()) {
            return std::nullopt;
        }
    }
    return QRect(array[0].toInt(), array[1].toInt(), array[2].toInt(), array[3].toInt());
}

std::optional<ElementType> typeFromJson(const QString& type)
{
    if (type == QLatin1String(kTypeWindow)) {
        return ElementType::Window;
    }
    if (type == QLatin1String(kTypeDialog)) {
        return ElementType::Dialog;
    }
    return std::nullopt;
}

} // namespace

bool WindowSample::operator==(const WindowSample& other) const
{
    return windowId == other.windowId && rect == other.rect && z == other.z && ownerApp == other.ownerApp
        && type == other.type;
}

void WindowTimeline::append(qint64 tMs, std::vector<WindowSample> windows)
{
    if (!m_entries.empty() && m_entries.back().windows == windows) {
        return;
    }
    m_entries.push_back({tMs, std::move(windows)});
}

const std::vector<WindowSample>* WindowTimeline::windowsAt(qint64 tMs) const
{
    if (m_entries.empty()) {
        return nullptr;
    }
    // First entry with t > tMs; the one before it is the entry in force.
    const auto after = std::upper_bound(m_entries.begin(), m_entries.end(), tMs,
                                        [](qint64 t, const WindowTimelineEntry& entry) { return t < entry.tMs; });
    if (after == m_entries.begin()) {
        return &m_entries.front().windows;
    }
    return &std::prev(after)->windows;
}

std::optional<WindowSample> WindowTimeline::hitTest(const QPoint& videoPoint, qint64 tMs) const
{
    const std::vector<WindowSample>* windows = windowsAt(tMs);
    if (!windows) {
        return std::nullopt;
    }
    std::optional<WindowSample> best;
    for (const WindowSample& window : *windows) {
        if (window.rect.contains(videoPoint) && (!best || window.z < best->z)) {
            best = window;
        }
    }
    return best;
}

QByteArray WindowTimeline::toJson() const
{
    QJsonArray entries;
    for (const WindowTimelineEntry& entry : m_entries) {
        QJsonArray windows;
        for (const WindowSample& window : entry.windows) {
            windows.append(QJsonObject{
                {QLatin1String(kKeyId), static_cast<qint64>(window.windowId)},
                {QLatin1String(kKeyRect), rectToJson(window.rect)},
                {QLatin1String(kKeyZ), window.z},
                {QLatin1String(kKeyApp), window.ownerApp},
                {QLatin1String(kKeyType),
                 QLatin1String(window.type == ElementType::Dialog ? kTypeDialog : kTypeWindow)},
            });
        }
        entries.append(QJsonObject{{QLatin1String(kKeyTime), entry.tMs}, {QLatin1String(kKeyWindows), windows}});
    }
    const QJsonObject root{
        {QLatin1String(kKeyVersion), kFormatVersion},
        {QLatin1String(kKeyFrameSize), QJsonArray{m_frameSize.width(), m_frameSize.height()}},
        {QLatin1String(kKeyEntries), entries},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

std::optional<WindowTimeline> WindowTimeline::fromJson(const QByteArray& json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    if (root.value(QLatin1String(kKeyVersion)).toInt() != kFormatVersion) {
        return std::nullopt;
    }
    const QJsonArray frameSize = root.value(QLatin1String(kKeyFrameSize)).toArray();
    if (frameSize.size() != 2) {
        return std::nullopt;
    }
    WindowTimeline timeline;
    timeline.m_frameSize = QSize(frameSize[0].toInt(), frameSize[1].toInt());
    if (timeline.m_frameSize.isEmpty()) {
        return std::nullopt;
    }
    for (const QJsonValue& entryValue : root.value(QLatin1String(kKeyEntries)).toArray()) {
        const QJsonObject entryObject = entryValue.toObject();
        WindowTimelineEntry entry;
        entry.tMs = static_cast<qint64>(entryObject.value(QLatin1String(kKeyTime)).toDouble());
        for (const QJsonValue& windowValue : entryObject.value(QLatin1String(kKeyWindows)).toArray()) {
            const QJsonObject windowObject = windowValue.toObject();
            const std::optional<QRect> rect = rectFromJson(windowObject.value(QLatin1String(kKeyRect)));
            const std::optional<ElementType> type = typeFromJson(windowObject.value(QLatin1String(kKeyType)).toString());
            if (!rect || !type) {
                return std::nullopt;
            }
            WindowSample window;
            window.windowId = static_cast<quint32>(windowObject.value(QLatin1String(kKeyId)).toDouble());
            window.rect = *rect;
            window.z = windowObject.value(QLatin1String(kKeyZ)).toInt();
            window.ownerApp = windowObject.value(QLatin1String(kKeyApp)).toString();
            window.type = *type;
            entry.windows.push_back(window);
        }
        timeline.m_entries.push_back(std::move(entry));
    }
    return timeline;
}

QRect WindowFrameMapping::toVideoRect(const QRect& logicalBounds) const
{
    Q_UNUSED(logicalBounds);
    return {}; // Task 2
}

} // namespace SnapTray
```

Add `src/recording/WindowTimeline.cpp` to the `snaptray_core` source list in `CMakeLists.txt`, right after `src/utils/VideoCropGeometry.cpp`.

- [ ] **Step 5: Build, run the test and verify it passes**

Run: `ctest --test-dir build -R RecordingManager_WindowTimeline --output-on-failure`
Expected: PASS, 8 test functions (the data-driven one counts its rows).

- [ ] **Step 6: Commit**

```bash
git add include/recording/WindowTimeline.h src/recording/WindowTimeline.cpp tests/RecordingManager/tst_WindowTimeline.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(recording): add WindowTimeline data model with JSON form"
```

---

### Task 2: Logical to video-pixel mapping

**Files:**
- Modify: `src/recording/WindowTimeline.cpp` (`WindowFrameMapping::toVideoRect`)
- Create: `tests/RecordingManager/tst_WindowFrameMapping.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CoordinateHelper::toPhysicalScreenRect(logicalRect, logicalScreenGeometry, physicalScreenGeometry, dpr)` and `CoordinateHelper::toPhysicalCoveringRect(logical, dpr)` from `include/utils/CoordinateHelper.h`. Read both before implementing: `toPhysicalScreenRect` returns an **empty rect when the logical rect is not fully inside the logical screen**, so the window bounds must be clipped to the screen first. Both map outwards (floor left/top, ceil right/bottom) and `toPhysicalScreenRect` clamps to the native monitor bounds.
- Produces: `QRect WindowFrameMapping::toVideoRect(const QRect& logicalBounds) const` for Task 3.

- [ ] **Step 1: Write the failing tests**

Create `tests/RecordingManager/tst_WindowFrameMapping.cpp`:

```cpp
#include <QtTest/QtTest>

#include "recording/WindowTimeline.h"

using SnapTray::WindowFrameMapping;

// Needed by QTest::addColumn<WindowFrameMapping>() below.
Q_DECLARE_METATYPE(SnapTray::WindowFrameMapping)

class tst_WindowFrameMapping : public QObject
{
    Q_OBJECT

private slots:
    void mapsLogicalWindowsToVideoPixels_data();
    void mapsLogicalWindowsToVideoPixels();
    void invalidMappingProducesNothing();
};

void tst_WindowFrameMapping::mapsLogicalWindowsToVideoPixels_data()
{
    QTest::addColumn<WindowFrameMapping>("mapping");
    QTest::addColumn<QRect>("logicalBounds");
    QTest::addColumn<QRect>("expected");

    // macOS Retina: no native desktop bounds, 2x, screen at the logical origin.
    const WindowFrameMapping retina{QRect(0, 0, 1440, 900), QRect(), 2.0, QRect(0, 0, 2880, 1800)};
    QTest::newRow("retina window") << retina << QRect(100, 50, 300, 200) << QRect(200, 100, 600, 400);
    QTest::newRow("retina full screen") << retina << QRect(0, 0, 1440, 900) << QRect(0, 0, 2880, 1800);

    // Windows 125 %: 1920x1080 native shown as 1536x864 logical.
    const WindowFrameMapping scaled125{QRect(0, 0, 1536, 864), QRect(0, 0, 1920, 1080), 1.25, QRect(0, 0, 1920, 1080)};
    QTest::newRow("125% window") << scaled125 << QRect(16, 8, 160, 80) << QRect(20, 10, 200, 100);
    // Odd logical coordinates map outwards: floor the origin, ceil the far edge.
    QTest::newRow("125% rounds outwards") << scaled125 << QRect(1, 1, 1, 1) << QRect(1, 1, 2, 2);

    // Windows 150 % secondary screen whose logical origin (1707, 0) is not its
    // native origin (2560, 0): origins translate, sizes scale.
    const WindowFrameMapping secondary150{QRect(1707, 0, 1024, 768), QRect(2560, 0, 1536, 1152), 1.5,
                                          QRect(2560, 0, 1536, 1152)};
    QTest::newRow("150% secondary window") << secondary150 << QRect(1807, 100, 200, 100) << QRect(150, 150, 300, 150);
    // A window that starts on the primary screen and ends on this one keeps its part on this screen.
    QTest::newRow("spanning window is clipped") << secondary150 << QRect(1600, 100, 300, 100) << QRect(0, 150, 290, 150);
    // Entirely on the other screen: nothing to store.
    QTest::newRow("window on another screen is dropped") << secondary150 << QRect(0, 0, 100, 100) << QRect();
    // Hanging off the bottom-right of the recorded screen is clipped to the frame.
    QTest::newRow("overhanging window is clipped") << secondary150 << QRect(2531, 568, 400, 400) << QRect(1236, 852, 300, 300);
}

void tst_WindowFrameMapping::mapsLogicalWindowsToVideoPixels()
{
    QFETCH(WindowFrameMapping, mapping);
    QFETCH(QRect, logicalBounds);
    QFETCH(QRect, expected);
    QVERIFY(mapping.isValid());
    QCOMPARE(mapping.toVideoRect(logicalBounds), expected);
}

void tst_WindowFrameMapping::invalidMappingProducesNothing()
{
    WindowFrameMapping mapping;
    QVERIFY(!mapping.isValid());
    QCOMPARE(mapping.toVideoRect(QRect(0, 0, 100, 100)), QRect());
    mapping.logicalScreen = QRect(0, 0, 100, 100);
    mapping.physicalRegion = QRect(0, 0, 100, 100);
    mapping.devicePixelRatio = 0.0;
    QVERIFY(!mapping.isValid());
    QCOMPARE(mapping.toVideoRect(QRect(0, 0, 50, 50)), QRect());
}

QTEST_GUILESS_MAIN(tst_WindowFrameMapping)
#include "tst_WindowFrameMapping.moc"
```

Add to `tests/CMakeLists.txt` after the `RecordingManager_WindowTimeline` block:

```cmake
add_executable(RecordingManager_WindowFrameMapping RecordingManager/tst_WindowFrameMapping.cpp)
target_link_libraries(RecordingManager_WindowFrameMapping PRIVATE snaptray_core Qt6::Test)
add_test(NAME RecordingManager_WindowFrameMapping COMMAND RecordingManager_WindowFrameMapping)
set_tests_properties(RecordingManager_WindowFrameMapping PROPERTIES TIMEOUT 60 LABELS "unit")
```

Expected values, derived from `CoordinateHelper`: "spanning window" clips `(1600,100,300,100)` to the logical screen `(1707,100,193,100)`, screen-relative `(0,100,193,100)`, times 1.5 outwards gives `(0,150,290,150)` (289.5 ceils to 290). "overhanging" clips `(2531,568,400,400)` to `(2531,568,200,200)`, relative `(824,568,200,200)`, times 1.5 gives `(1236,852,300,300)`.

- [ ] **Step 2: Build and verify the tests fail**

Run: `ctest --test-dir build -R RecordingManager_WindowFrameMapping --output-on-failure`
Expected: FAIL, `Compared values are not the same` with an empty actual rect for every row except the two "dropped"/invalid cases.

- [ ] **Step 3: Implement the mapping**

Replace the `WindowFrameMapping::toVideoRect` stub in `src/recording/WindowTimeline.cpp`:

```cpp
QRect WindowFrameMapping::toVideoRect(const QRect& logicalBounds) const
{
    if (!isValid()) {
        return {};
    }
    // toPhysicalScreenRect() refuses a rect that leaves the screen, so clip
    // first: a window spanning two screens keeps the part on this one.
    const QRect onScreen = logicalBounds.intersected(logicalScreen);
    if (onScreen.isEmpty()) {
        return {};
    }
    QRect physical;
    if (!physicalScreen.isEmpty()) {
        physical = CoordinateHelper::toPhysicalScreenRect(onScreen, logicalScreen, physicalScreen, devicePixelRatio)
                       .translated(-physicalRegion.topLeft());
    } else {
        physical = CoordinateHelper::toPhysicalCoveringRect(onScreen.translated(-logicalScreen.topLeft()),
                                                           devicePixelRatio);
    }
    return physical.intersected(QRect(QPoint(0, 0), physicalRegion.size()));
}
```

- [ ] **Step 4: Run the tests and verify they pass**

Run: `ctest --test-dir build -R RecordingManager_WindowFrameMapping --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/recording/WindowTimeline.cpp tests/RecordingManager/tst_WindowFrameMapping.cpp tests/CMakeLists.txt
git commit -m "feat(recording): map logical window bounds into recorded video pixels"
```

---

### Task 3: `WindowTimelineRecorder` and the detector snapshot accessor

**Files:**
- Modify: `include/WindowDetector.h` (add `topLevelWindowsSnapshot()`)
- Modify: `tests/Detection/tst_WindowDetectorQueryMode.cpp`
- Create: `include/recording/WindowTimelineRecorder.h`, `src/recording/WindowTimelineRecorder.cpp`
- Create: `tests/RecordingManager/tst_WindowTimelineRecorder.cpp`
- Modify: `CMakeLists.txt` (snaptray_platform: source after `src/recording/ScreenSourceService.cpp`, header in the platform "Headers with Q_OBJECT" list), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `WindowDetector` (`setScreen`, `setEnabled`, `refreshWindowList(QueryMode::TopLevelOnly)`), `DetectedElement`, `WindowFrameMapping::toVideoRect`, `WindowTimeline::append`.
- Produces (used by Task 5):

```cpp
// include/WindowDetector.h, public:
    // Copy of the cached top-level elements after refreshWindowList(TopLevelOnly).
    // Empty before a refresh or when the cache holds child controls too.
    std::vector<DetectedElement> topLevelWindowsSnapshot() const;

// include/recording/WindowTimelineRecorder.h
namespace SnapTray {
class WindowTimelineRecorder : public QObject {
    Q_OBJECT
public:
    using Enumerator = std::function<std::vector<DetectedElement>()>;  // runs on this object's thread
    using Clock = std::function<qint64()>;                            // media time in ms
    static constexpr int kSampleIntervalMs = 250;

    WindowTimelineRecorder(Enumerator enumerator, Clock clock, WindowFrameMapping mapping, QObject* parent = nullptr);
    // An enumerator over a WindowDetector bound to `screen` (owned by the closure).
    static Enumerator detectorEnumerator(QScreen* screen);

    void start();    // samples immediately, then every kSampleIntervalMs
    void pause();    // stops sampling; the next sample after resume() is immediate
    void resume();
    void stop();     // stops sampling; timeline() stays available
    void sampleNow();
    bool isRunning() const;
    WindowTimeline timeline() const;
};
}
```

Sampling rules: keep elements whose `elementType` is `Window` or `Dialog`; map `bounds` through the mapping and drop empty results; order by `windowLayer` descending then enumeration order (Windows' `EnumWindows` is topmost first; macOS lists front to back and uses `kCGWindowLayer`), then assign `z` by position; `ownerApp` is copied, `windowTitle` is never read.

- [ ] **Step 1: Write the failing detector accessor test**

In `tests/Detection/tst_WindowDetectorQueryMode.cpp`, add the slot `void testTopLevelSnapshotOnlyAfterTopLevelRefresh();` after `testTopLevelCacheDoesNotPretendChildControlsAreReady();` and the body:

```cpp
void tst_WindowDetectorQueryMode::testTopLevelSnapshotOnlyAfterTopLevelRefresh()
{
    TestWindowDetector detector;
    QVERIFY(detector.topLevelWindowsSnapshot().empty()); // nothing cached yet

    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen != nullptr);
    detector.setScreen(screen);
    detector.m_cacheReady = true;
    detector.m_cacheScreen = screen;
    detector.m_cacheQueryMode = WindowDetector::QueryMode::TopLevelOnly;
    detector.m_windowCache = {makeElement(QRect(0, 0, 100, 100), 0), makeElement(QRect(50, 50, 100, 100), 3)};
    const auto snapshot = detector.topLevelWindowsSnapshot();
    QCOMPARE(snapshot.size(), size_t(2));
    QCOMPARE(snapshot[1].windowLayer, 3);

    // A cache that also holds child controls is not a top-level snapshot.
    detector.m_cacheQueryMode = WindowDetector::QueryMode::IncludeChildControls;
    QVERIFY(detector.topLevelWindowsSnapshot().empty());
}
```

- [ ] **Step 2: Write the failing recorder tests**

Create `tests/RecordingManager/tst_WindowTimelineRecorder.cpp`:

```cpp
#include <QtTest/QtTest>

#include "recording/WindowTimelineRecorder.h"

#include <functional>
#include <vector>

using SnapTray::WindowFrameMapping;
using SnapTray::WindowTimeline;
using SnapTray::WindowTimelineRecorder;

namespace {

// 2x screen at the logical origin: logical (x, y, w, h) -> video (2x, 2y, 2w, 2h).
const WindowFrameMapping kMapping{QRect(0, 0, 1000, 500), QRect(), 2.0, QRect(0, 0, 2000, 1000)};

DetectedElement element(quint32 id, const QRect& bounds, ElementType type = ElementType::Window, int layer = 0,
                        const char* app = "App")
{
    DetectedElement e;
    e.bounds = bounds;
    e.windowTitle = QStringLiteral("Secret title - must never be stored");
    e.ownerApp = QString::fromLatin1(app);
    e.windowLayer = layer;
    e.windowId = id;
    e.elementType = type;
    e.ownerPid = 100 + int(id);
    return e;
}

struct Fakes {
    std::vector<DetectedElement> elements;
    qint64 nowMs = 0;
    int enumerations = 0;
    WindowTimelineRecorder::Enumerator enumerator()
    {
        return [this]() { ++enumerations; return elements; };
    }
    WindowTimelineRecorder::Clock clock()
    {
        return [this]() { return nowMs; };
    }
};

} // namespace

class tst_WindowTimelineRecorder : public QObject
{
    Q_OBJECT

private slots:
    void sampleMapsFiltersAndOrders();
    void startSamplesImmediatelyThenOnInterval();
    void pauseStopsSamplingAndResumeContinues();
    void stopKeepsTimeline();
    void unchangedSamplesCompact();
};

void tst_WindowTimelineRecorder::sampleMapsFiltersAndOrders()
{
    Fakes fakes;
    fakes.elements = {
        element(1, QRect(0, 0, 500, 500), ElementType::Window, 0, "Back"),
        element(2, QRect(100, 100, 200, 100), ElementType::PopupMenu, 5, "Menu"),   // filtered out
        element(3, QRect(200, 200, 100, 100), ElementType::Dialog, 2, "Dialog"),   // higher layer: topmost
        element(4, QRect(900, 0, 300, 300), ElementType::Window, 0, "Edge"),       // clipped to the screen
        element(5, QRect(2000, 0, 100, 100), ElementType::Window, 0, "Elsewhere"), // off screen: dropped
    };
    fakes.nowMs = 1234;
    WindowTimelineRecorder recorder(fakes.enumerator(), fakes.clock(), kMapping);
    recorder.sampleNow();

    const WindowTimeline timeline = recorder.timeline();
    QCOMPARE(timeline.frameSize(), QSize(2000, 1000));
    QCOMPARE(timeline.entries().size(), size_t(1));
    const auto& entry = timeline.entries().front();
    QCOMPARE(entry.tMs, qint64(1234));
    QCOMPARE(entry.windows.size(), size_t(3));
    QCOMPARE(entry.windows[0].windowId, quint32(3)); // layer 2 first
    QCOMPARE(entry.windows[0].z, 0);
    QCOMPARE(entry.windows[0].rect, QRect(400, 400, 200, 200));
    QCOMPARE(entry.windows[0].type, ElementType::Dialog);
    QCOMPARE(entry.windows[1].windowId, quint32(1)); // then enumeration order
    QCOMPARE(entry.windows[1].z, 1);
    QCOMPARE(entry.windows[2].windowId, quint32(4));
    QCOMPARE(entry.windows[2].rect, QRect(1800, 0, 200, 600));
    QCOMPARE(entry.windows[1].ownerApp, QStringLiteral("Back"));
    QVERIFY(!timeline.toJson().contains("Secret title"));
}

void tst_WindowTimelineRecorder::startSamplesImmediatelyThenOnInterval()
{
    Fakes fakes;
    fakes.elements = {element(1, QRect(0, 0, 100, 100))};
    WindowTimelineRecorder recorder(fakes.enumerator(), fakes.clock(), kMapping);
    QVERIFY(!recorder.isRunning());
    recorder.start();
    QVERIFY(recorder.isRunning());
    QCOMPARE(fakes.enumerations, 1);
    // The timer fires on the GUI thread's event loop.
    QTRY_VERIFY_WITH_TIMEOUT(fakes.enumerations >= 3, WindowTimelineRecorder::kSampleIntervalMs * 4);
    recorder.stop();
    QVERIFY(!recorder.isRunning());
}

void tst_WindowTimelineRecorder::pauseStopsSamplingAndResumeContinues()
{
    Fakes fakes;
    fakes.elements = {element(1, QRect(0, 0, 100, 100))};
    WindowTimelineRecorder recorder(fakes.enumerator(), fakes.clock(), kMapping);
    recorder.start();
    recorder.pause();
    QVERIFY(!recorder.isRunning());
    const int before = fakes.enumerations;
    QTest::qWait(WindowTimelineRecorder::kSampleIntervalMs * 2);
    QCOMPARE(fakes.enumerations, before);

    fakes.nowMs = 5000;                       // media time when the recording resumes
    fakes.elements = {element(2, QRect(0, 0, 100, 100))};
    recorder.resume();
    QVERIFY(recorder.isRunning());
    QCOMPARE(fakes.enumerations, before + 1); // immediate sample on resume
    const WindowTimeline timeline = recorder.timeline();
    QCOMPARE(timeline.entries().back().tMs, qint64(5000));
    QCOMPARE(timeline.entries().back().windows.front().windowId, quint32(2));
    recorder.stop();
}

void tst_WindowTimelineRecorder::stopKeepsTimeline()
{
    Fakes fakes;
    fakes.elements = {element(1, QRect(0, 0, 100, 100))};
    WindowTimelineRecorder recorder(fakes.enumerator(), fakes.clock(), kMapping);
    recorder.start();
    recorder.stop();
    QCOMPARE(recorder.timeline().entries().size(), size_t(1));
    const int before = fakes.enumerations;
    QTest::qWait(WindowTimelineRecorder::kSampleIntervalMs * 2);
    QCOMPARE(fakes.enumerations, before);
    recorder.sampleNow(); // explicit samples after stop are ignored
    QCOMPARE(recorder.timeline().entries().size(), size_t(1));
}

void tst_WindowTimelineRecorder::unchangedSamplesCompact()
{
    Fakes fakes;
    fakes.elements = {element(1, QRect(0, 0, 100, 100))};
    WindowTimelineRecorder recorder(fakes.enumerator(), fakes.clock(), kMapping);
    recorder.start();
    fakes.nowMs = 250;
    recorder.sampleNow();
    fakes.nowMs = 500;
    recorder.sampleNow();
    fakes.elements = {element(1, QRect(10, 0, 100, 100))};
    fakes.nowMs = 750;
    recorder.sampleNow();
    recorder.stop();
    const WindowTimeline timeline = recorder.timeline();
    QCOMPARE(timeline.entries().size(), size_t(2));
    QCOMPARE(timeline.entries()[1].tMs, qint64(750));
}

QTEST_MAIN(tst_WindowTimelineRecorder)
#include "tst_WindowTimelineRecorder.moc"
```

Add to `tests/CMakeLists.txt` after the `RecordingManager_WindowFrameMapping` block (the recorder is in the platform library):

```cmake
add_executable(RecordingManager_WindowTimelineRecorder RecordingManager/tst_WindowTimelineRecorder.cpp)
target_link_libraries(RecordingManager_WindowTimelineRecorder PRIVATE snaptray_platform Qt6::Test)
add_test(NAME RecordingManager_WindowTimelineRecorder COMMAND RecordingManager_WindowTimelineRecorder)
set_tests_properties(RecordingManager_WindowTimelineRecorder PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 3: Build and verify both fail**

Run: `./scripts/build.sh`
Expected: FAIL, `recording/WindowTimelineRecorder.h: No such file` and `no member named 'topLevelWindowsSnapshot'`.

- [ ] **Step 4: Add the detector accessor**

In `include/WindowDetector.h`, after `detectWindowAt(...)` in the public section:

```cpp
    // Copy of the cached top-level elements after refreshWindowList(TopLevelOnly).
    // Empty before a refresh or when the cache holds child controls too.
    std::vector<DetectedElement> topLevelWindowsSnapshot() const
    {
        QMutexLocker locker(&m_cacheMutex);
        if (!m_cacheReady || m_cacheQueryMode != QueryMode::TopLevelOnly) {
            return {};
        }
        return m_windowCache;
    }
```

- [ ] **Step 5: Write the recorder**

Create `include/recording/WindowTimelineRecorder.h`:

```cpp
#pragma once

#include "WindowDetector.h"
#include "recording/WindowTimeline.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <vector>

class QScreen;

namespace SnapTray {

// Samples where the top-level windows are while a recording runs. Sampling
// happens on this object's thread (the GUI thread: on Windows the detector's
// coordinate mapping needs QGuiApplication state) every kSampleIntervalMs,
// immediately at start() and resume(), and never while paused or stopped.
class WindowTimelineRecorder : public QObject
{
    Q_OBJECT

public:
    using Enumerator = std::function<std::vector<DetectedElement>()>;
    using Clock = std::function<qint64()>; // media time in ms (recording clock minus pauses)
    static constexpr int kSampleIntervalMs = 250;

    WindowTimelineRecorder(Enumerator enumerator, Clock clock, WindowFrameMapping mapping, QObject* parent = nullptr);

    // An enumerator over a WindowDetector bound to `screen` (owned by the closure).
    static Enumerator detectorEnumerator(QScreen* screen);

    void start();
    void pause();
    void resume();
    void stop();
    void sampleNow();
    bool isRunning() const { return m_running; }
    WindowTimeline timeline() const { return m_timeline; }

private:
    Enumerator m_enumerator;
    Clock m_clock;
    WindowFrameMapping m_mapping;
    WindowTimeline m_timeline;
    QTimer m_timer;
    bool m_running = false;
    bool m_stopped = false;
};

} // namespace SnapTray
```

Create `src/recording/WindowTimelineRecorder.cpp`:

```cpp
#include "recording/WindowTimelineRecorder.h"

#include <QDebug>
#include <QScreen>

#include <algorithm>
#include <memory>
#include <utility>

namespace SnapTray {

namespace {

bool isSnapTarget(ElementType type)
{
    return type == ElementType::Window || type == ElementType::Dialog;
}

} // namespace

WindowTimelineRecorder::WindowTimelineRecorder(Enumerator enumerator, Clock clock, WindowFrameMapping mapping,
                                               QObject* parent)
    : QObject(parent)
    , m_enumerator(std::move(enumerator))
    , m_clock(std::move(clock))
    , m_mapping(mapping)
{
    m_timeline.setFrameSize(m_mapping.physicalRegion.size());
    m_timer.setInterval(kSampleIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &WindowTimelineRecorder::sampleNow);
}

WindowTimelineRecorder::Enumerator WindowTimelineRecorder::detectorEnumerator(QScreen* screen)
{
    auto detector = std::make_shared<WindowDetector>();
    detector->setScreen(screen);
    detector->setEnabled(true);
    return [detector]() {
        // Top level only: no titles, no child controls, a few milliseconds.
        detector->refreshWindowList(WindowDetector::QueryMode::TopLevelOnly);
        return detector->topLevelWindowsSnapshot();
    };
}

void WindowTimelineRecorder::start()
{
    if (m_running || m_stopped) {
        return;
    }
    m_running = true;
    sampleNow();
    m_timer.start();
}

void WindowTimelineRecorder::pause()
{
    if (!m_running) {
        return;
    }
    m_running = false;
    m_timer.stop();
}

void WindowTimelineRecorder::resume()
{
    if (m_running || m_stopped) {
        return;
    }
    m_running = true;
    sampleNow();
    m_timer.start();
}

void WindowTimelineRecorder::stop()
{
    m_running = false;
    m_stopped = true;
    m_timer.stop();
}

void WindowTimelineRecorder::sampleNow()
{
    if (m_stopped || !m_enumerator || !m_clock || !m_mapping.isValid()) {
        return;
    }
    const qint64 tMs = m_clock();
    std::vector<DetectedElement> elements = m_enumerator();

    // Topmost first: higher layers win, then the enumeration order (already
    // front to back on both platforms).
    std::stable_sort(elements.begin(), elements.end(),
                     [](const DetectedElement& a, const DetectedElement& b) { return a.windowLayer > b.windowLayer; });

    std::vector<WindowSample> windows;
    windows.reserve(elements.size());
    for (const DetectedElement& element : elements) {
        if (!isSnapTarget(element.elementType)) {
            continue;
        }
        const QRect rect = m_mapping.toVideoRect(element.bounds);
        if (rect.isEmpty()) {
            continue;
        }
        WindowSample window;
        window.windowId = element.windowId;
        window.rect = rect;
        window.z = static_cast<int>(windows.size());
        window.ownerApp = element.ownerApp; // never the title
        window.type = element.elementType;
        windows.push_back(std::move(window));
    }
    m_timeline.append(tMs, std::move(windows));
}

} // namespace SnapTray
```

Add to `CMakeLists.txt`: `src/recording/WindowTimelineRecorder.cpp` after `src/recording/ScreenSourceService.cpp` in `snaptray_platform`, and `include/recording/WindowTimelineRecorder.h` in that library's "Headers with Q_OBJECT" list (next to `include/video/IVideoPlayer.h`).

- [ ] **Step 6: Build and run both tests**

Run: `ctest --test-dir build -R "RecordingManager_WindowTimelineRecorder|Detection_WindowDetectorQueryMode" --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add include/WindowDetector.h tests/Detection/tst_WindowDetectorQueryMode.cpp include/recording/WindowTimelineRecorder.h src/recording/WindowTimelineRecorder.cpp tests/RecordingManager/tst_WindowTimelineRecorder.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(recording): sample top-level windows into a WindowTimeline while recording"
```

---

### Task 4: Sidecar file helpers

**Files:**
- Create: `include/recording/WindowTimelineSidecar.h`, `src/recording/WindowTimelineSidecar.cpp`
- Create: `tests/RecordingManager/tst_WindowTimelineSidecar.cpp`
- Modify: `CMakeLists.txt` (snaptray_core), `tests/CMakeLists.txt`

**Interfaces:**
- Produces (used by Tasks 5, 6, 7):

```cpp
namespace SnapTray::WindowTimelineSidecar {
constexpr auto kSuffix = ".windows.json";                 // "<video>.mp4.windows.json"
QString pathFor(const QString& videoPath);
bool write(const QString& videoPath, const WindowTimeline& timeline);  // atomic (QSaveFile); false + qWarning on failure
std::optional<WindowTimeline> read(const QString& videoPath);          // nullopt when missing or corrupt (qDebug for corrupt)
void remove(const QString& videoPath);                                  // no-op when missing; qWarning when removal fails
}
```

- [ ] **Step 1: Write the failing tests**

Create `tests/RecordingManager/tst_WindowTimelineSidecar.cpp`:

```cpp
#include <QtTest/QtTest>

#include "recording/WindowTimeline.h"
#include "recording/WindowTimelineSidecar.h"

#include <QFile>
#include <QTemporaryDir>

using SnapTray::WindowSample;
using SnapTray::WindowTimeline;
namespace Sidecar = SnapTray::WindowTimelineSidecar;

class tst_WindowTimelineSidecar : public QObject
{
    Q_OBJECT

private slots:
    void pathSitsNextToTheVideo();
    void writeReadRoundTrip();
    void readMissingOrCorruptIsEmpty();
    void removeIsSafe();
};

void tst_WindowTimelineSidecar::pathSitsNextToTheVideo()
{
    QCOMPARE(Sidecar::pathFor(QStringLiteral("C:/tmp/SnapTray_Recording_x.mp4")),
             QStringLiteral("C:/tmp/SnapTray_Recording_x.mp4.windows.json"));
}

void tst_WindowTimelineSidecar::writeReadRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    WindowSample window;
    window.windowId = 42;
    window.rect = QRect(0, 0, 960, 1080);
    window.ownerApp = QStringLiteral("Code");
    timeline.append(0, {window});

    QVERIFY(Sidecar::write(video, timeline));
    QVERIFY(QFile::exists(Sidecar::pathFor(video)));
    const auto read = Sidecar::read(video);
    QVERIFY(read.has_value());
    QCOMPARE(read->frameSize(), QSize(1920, 1080));
    QCOMPARE(read->entries().front().windows.front().windowId, quint32(42));
}

void tst_WindowTimelineSidecar::readMissingOrCorruptIsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    QVERIFY(!Sidecar::read(video).has_value());
    QFile corrupt(Sidecar::pathFor(video));
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("{not json");
    corrupt.close();
    QVERIFY(!Sidecar::read(video).has_value());
}

void tst_WindowTimelineSidecar::removeIsSafe()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    Sidecar::remove(video); // nothing there: no warning, no crash
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(16, 16));
    QVERIFY(Sidecar::write(video, timeline));
    Sidecar::remove(video);
    QVERIFY(!QFile::exists(Sidecar::pathFor(video)));
}

QTEST_GUILESS_MAIN(tst_WindowTimelineSidecar)
#include "tst_WindowTimelineSidecar.moc"
```

Add to `tests/CMakeLists.txt` after the recorder test block:

```cmake
add_executable(RecordingManager_WindowTimelineSidecar RecordingManager/tst_WindowTimelineSidecar.cpp)
target_link_libraries(RecordingManager_WindowTimelineSidecar PRIVATE snaptray_core Qt6::Test)
add_test(NAME RecordingManager_WindowTimelineSidecar COMMAND RecordingManager_WindowTimelineSidecar)
set_tests_properties(RecordingManager_WindowTimelineSidecar PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 2: Build and verify it fails**

Run: `./scripts/build.sh`
Expected: FAIL, `recording/WindowTimelineSidecar.h: No such file`.

- [ ] **Step 3: Implement the helpers**

Create `include/recording/WindowTimelineSidecar.h`:

```cpp
#pragma once

#include "recording/WindowTimeline.h"

#include <QString>

#include <optional>

// The window timeline travels next to the temporary recording as
// "<video>.windows.json" and lives exactly as long as that file: written when
// encoding finishes, removed when the recording is saved, discarded or
// replaced by an export. It never reaches a final output or History.
namespace SnapTray::WindowTimelineSidecar {

constexpr auto kSuffix = ".windows.json";

QString pathFor(const QString& videoPath);
// Atomic: a crash mid-write leaves no half file. False (with qWarning) on failure.
bool write(const QString& videoPath, const WindowTimeline& timeline);
// nullopt when there is no sidecar or it cannot be parsed.
std::optional<WindowTimeline> read(const QString& videoPath);
// No-op when there is no sidecar.
void remove(const QString& videoPath);

} // namespace SnapTray::WindowTimelineSidecar
```

Create `src/recording/WindowTimelineSidecar.cpp`:

```cpp
#include "recording/WindowTimelineSidecar.h"

#include <QDebug>
#include <QFile>
#include <QSaveFile>

namespace SnapTray::WindowTimelineSidecar {

QString pathFor(const QString& videoPath)
{
    return videoPath + QLatin1String(kSuffix);
}

bool write(const QString& videoPath, const WindowTimeline& timeline)
{
    QSaveFile file(pathFor(videoPath));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "WindowTimelineSidecar: cannot open" << file.fileName() << file.errorString();
        return false;
    }
    const QByteArray json = timeline.toJson();
    if (file.write(json) != json.size() || !file.commit()) {
        qWarning() << "WindowTimelineSidecar: cannot write" << file.fileName() << file.errorString();
        return false;
    }
    return true;
}

std::optional<WindowTimeline> read(const QString& videoPath)
{
    QFile file(pathFor(videoPath));
    if (!file.exists()) {
        return std::nullopt;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "WindowTimelineSidecar: cannot read" << file.fileName() << file.errorString();
        return std::nullopt;
    }
    std::optional<WindowTimeline> timeline = WindowTimeline::fromJson(file.readAll());
    if (!timeline) {
        qDebug() << "WindowTimelineSidecar: ignoring unreadable sidecar" << file.fileName();
    }
    return timeline;
}

void remove(const QString& videoPath)
{
    const QString path = pathFor(videoPath);
    if (QFile::exists(path) && !QFile::remove(path)) {
        qWarning() << "WindowTimelineSidecar: cannot remove" << path;
    }
}

} // namespace SnapTray::WindowTimelineSidecar
```

Add `src/recording/WindowTimelineSidecar.cpp` to `snaptray_core` in `CMakeLists.txt`, after `src/recording/WindowTimeline.cpp`.

- [ ] **Step 4: Run the test**

Run: `ctest --test-dir build -R RecordingManager_WindowTimelineSidecar --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add include/recording/WindowTimelineSidecar.h src/recording/WindowTimelineSidecar.cpp tests/RecordingManager/tst_WindowTimelineSidecar.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(recording): read and write the window timeline sidecar"
```

---

### Task 5: Record the timeline in `RecordingManager` and write the sidecar on stop

**Files:**
- Modify: `include/RecordingManager.h`, `src/RecordingManager.cpp`
- Modify: `tests/RecordingManager/tst_Lifecycle.cpp`

**Interfaces:**
- Consumes: `WindowTimelineRecorder` (Task 3), `WindowTimelineSidecar::write` (Task 4), `WindowFrameMapping` (Task 2).
- Produces (private, reached by `TestRecordingManagerLifecycle` through the existing friend declaration):

```cpp
    // Window timeline (only when the preview will open; the sidecar is its input)
    void startWindowTimeline();                                   // after m_elapsedTimer.start()
    void finishWindowTimeline(const QString& outputPath, bool success); // writes the sidecar on success
    std::unique_ptr<SnapTray::WindowTimelineRecorder> m_windowTimelineRecorder;
    SnapTray::WindowFrameMapping m_windowFrameMapping;            // set in beginAsyncInitialization()
    std::function<SnapTray::WindowTimelineRecorder::Enumerator(QScreen*)> m_createWindowEnumerator =
        &SnapTray::WindowTimelineRecorder::detectorEnumerator;    // tests inject a fake
```

Lifecycle hooks in `src/RecordingManager.cpp`:
- `beginAsyncInitialization()`: after `mappedPhysicalRegion` is computed, `m_windowFrameMapping = {screenInfo.geometry, screenInfo.physicalGeometry, screenInfo.devicePixelRatio, mappedPhysicalRegion};`
- `startRecordingAfterCountdown()`: right after `m_frameCount = 0;` call `startWindowTimeline();`
- `pauseRecording()`: before `setState(State::Paused)`, `if (m_windowTimelineRecorder) m_windowTimelineRecorder->pause();`
- `resumeRecording()`: before `setState(State::Recording)`, `if (m_windowTimelineRecorder) m_windowTimelineRecorder->resume();`
- `stopFrameCapture()`: first line `if (m_windowTimelineRecorder) m_windowTimelineRecorder->stop();`
- `cancelRecording()`: after `stopFrameCapture();`, `m_windowTimelineRecorder.reset();`
- `onEncodingFinished(success, outputPath)`: after `teardownEncodingWorker(false);`, `finishWindowTimeline(outputPath, success);`
- `cleanupStaleTempFiles()`: add `"SnapTray_Recording_*.windows.json"` to `filters`.

- [ ] **Step 1: Write the failing tests**

In `tests/RecordingManager/tst_Lifecycle.cpp` add includes `#include "recording/WindowTimelineSidecar.h"` and `#include "recording/WindowTimelineRecorder.h"`, these slots after `testSaveNameUsesCroppedOutputSize();`:

```cpp
    void testWindowTimelineOnlyRecordedForPreview();
    void testWindowTimelinePausesWithRecording();
    void testFinishWritesSidecarOnlyOnSuccess();
    void testStaleSidecarsAreCleanedUp();
```

and these bodies at the end (before `QTEST_MAIN`):

```cpp
namespace {
SnapTray::WindowTimelineRecorder::Enumerator fakeEnumerator(int* calls)
{
    return [calls]() {
        ++*calls;
        DetectedElement e;
        e.bounds = QRect(10, 10, 100, 100);
        e.ownerApp = QStringLiteral("Code");
        e.windowId = 7;
        return std::vector<DetectedElement>{e};
    };
}
} // namespace

void TestRecordingManagerLifecycle::testWindowTimelineOnlyRecordedForPreview()
{
    int calls = 0;
    m_manager->m_createWindowEnumerator = [&calls](QScreen*) { return fakeEnumerator(&calls); };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 2.0, QRect(0, 0, 2000, 1000)};
    m_manager->m_elapsedTimer.start();

    m_manager->m_startSettings.showPreview = false;
    m_manager->startWindowTimeline();
    QVERIFY(!m_manager->m_windowTimelineRecorder);
    QCOMPARE(calls, 0);

    m_manager->m_startSettings.showPreview = true;
    m_manager->startWindowTimeline();
    QVERIFY(m_manager->m_windowTimelineRecorder);
    QCOMPARE(calls, 1); // sampled immediately
    const auto timeline = m_manager->m_windowTimelineRecorder->timeline();
    QCOMPARE(timeline.frameSize(), QSize(2000, 1000));
    QCOMPARE(timeline.entries().front().windows.front().rect, QRect(20, 20, 200, 200));
    m_manager->m_windowTimelineRecorder.reset();
}

void TestRecordingManagerLifecycle::testWindowTimelinePausesWithRecording()
{
    int calls = 0;
    m_manager->m_createWindowEnumerator = [&calls](QScreen*) { return fakeEnumerator(&calls); };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 1.0, QRect(0, 0, 1000, 500)};
    m_manager->m_startSettings.showPreview = true;
    m_manager->m_elapsedTimer.start();
    m_manager->startWindowTimeline();
    QVERIFY(m_manager->m_windowTimelineRecorder->isRunning());

    // pauseRecording()/resumeRecording() are state-gated; drive the recorder the way they do.
    m_manager->m_state = RecordingManager::State::Recording;
    m_manager->pauseRecording();
    QVERIFY(!m_manager->m_windowTimelineRecorder->isRunning());
    m_manager->resumeRecording();
    QVERIFY(m_manager->m_windowTimelineRecorder->isRunning());
    m_manager->m_state = RecordingManager::State::Idle;
    m_manager->m_windowTimelineRecorder.reset();
}

void TestRecordingManagerLifecycle::testFinishWritesSidecarOnlyOnSuccess()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString output = dir.filePath(QStringLiteral("rec.mp4"));
    int calls = 0;
    m_manager->m_createWindowEnumerator = [&calls](QScreen*) { return fakeEnumerator(&calls); };
    m_manager->m_windowFrameMapping = {QRect(0, 0, 1000, 500), QRect(), 1.0, QRect(0, 0, 1000, 500)};
    m_manager->m_startSettings.showPreview = true;
    m_manager->m_elapsedTimer.start();

    m_manager->startWindowTimeline();
    m_manager->finishWindowTimeline(output, false);
    QVERIFY(!QFile::exists(SnapTray::WindowTimelineSidecar::pathFor(output)));
    QVERIFY(!m_manager->m_windowTimelineRecorder);

    m_manager->startWindowTimeline();
    m_manager->finishWindowTimeline(output, true);
    QVERIFY(!m_manager->m_windowTimelineRecorder);
    const auto sidecar = SnapTray::WindowTimelineSidecar::read(output);
    QVERIFY(sidecar.has_value());
    QCOMPARE(sidecar->frameSize(), QSize(1000, 500));
    QCOMPARE(sidecar->entries().front().windows.front().ownerApp, QStringLiteral("Code"));

    // Without a recorder (direct save) finishing writes nothing.
    QFile::remove(SnapTray::WindowTimelineSidecar::pathFor(output));
    m_manager->finishWindowTimeline(output, true);
    QVERIFY(!QFile::exists(SnapTray::WindowTimelineSidecar::pathFor(output)));
}

void TestRecordingManagerLifecycle::testStaleSidecarsAreCleanedUp()
{
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString stale = QDir(tempDir).filePath(QStringLiteral("SnapTray_Recording_stale-test.mp4.windows.json"));
    const QString fresh = QDir(tempDir).filePath(QStringLiteral("SnapTray_Recording_fresh-test.mp4.windows.json"));
    for (const QString& path : {stale, fresh}) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{}");
    }
    {
        QFile file(stale);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTime().addDays(-2), QFileDevice::FileModificationTime));
    }
    m_manager->cleanupStaleTempFiles();
    QVERIFY(!QFile::exists(stale));
    QVERIFY(QFile::exists(fresh));
    QFile::remove(fresh);
}
```

`tst_Lifecycle.cpp` needs `#include <QStandardPaths>` and `#include <QDir>` for the last test.

- [ ] **Step 2: Build and verify it fails**

Run: `./scripts/build.sh`
Expected: FAIL, `no member named 'startWindowTimeline' in 'RecordingManager'`.

- [ ] **Step 3: Add the members and hooks**

In `include/RecordingManager.h`: include `"recording/WindowTimelineRecorder.h"`; in the private method block after `void teardownEncodingWorker(bool abortEncoding);` add:

```cpp
    // Window timeline for preview snapping. Only recorded when the preview
    // will open (the sidecar is its input); written next to the temp MP4.
    void startWindowTimeline();
    void finishWindowTimeline(const QString& outputPath, bool success);
```

and in the private data block after `WatermarkRenderer::Settings m_watermarkSettings;`:

```cpp
    std::unique_ptr<SnapTray::WindowTimelineRecorder> m_windowTimelineRecorder;
    SnapTray::WindowFrameMapping m_windowFrameMapping;
    std::function<SnapTray::WindowTimelineRecorder::Enumerator(QScreen*)> m_createWindowEnumerator =
        &SnapTray::WindowTimelineRecorder::detectorEnumerator;
```

In `src/RecordingManager.cpp`: include `"recording/WindowTimelineSidecar.h"`; apply the lifecycle hooks listed under Interfaces; add the two methods next to `cleanupStaleTempFiles()`:

```cpp
void RecordingManager::startWindowTimeline()
{
    m_windowTimelineRecorder.reset();
    if (!m_startSettings.showPreview || !m_windowFrameMapping.isValid()) {
        return;
    }
    // The same clock that stamps the encoded frames: raw elapsed minus pauses.
    auto mediaTime = [this]() {
        QMutexLocker locker(&m_durationMutex);
        return qMax<qint64>(0, m_elapsedTimer.elapsed() - m_pausedDuration);
    };
    m_windowTimelineRecorder = std::make_unique<SnapTray::WindowTimelineRecorder>(
        m_createWindowEnumerator(m_targetScreen.data()), mediaTime, m_windowFrameMapping, nullptr);
    m_windowTimelineRecorder->start();
}

void RecordingManager::finishWindowTimeline(const QString& outputPath, bool success)
{
    if (!m_windowTimelineRecorder) {
        return;
    }
    m_windowTimelineRecorder->stop();
    const SnapTray::WindowTimeline timeline = m_windowTimelineRecorder->timeline();
    m_windowTimelineRecorder.reset();
    if (!success || timeline.isEmpty() || outputPath.isEmpty()) {
        return;
    }
    if (!SnapTray::WindowTimelineSidecar::write(outputPath, timeline)) {
        qWarning() << "RecordingManager: window timeline sidecar not written; preview keeps free-rect cropping";
    }
}
```

In `cleanupStaleTempFiles()` change the filter line to:

```cpp
    filters << "SnapTray_Recording_*.mp4" << "SnapTray_Recording_*.gif" << "SnapTray_Recording_*.webp"
            << "SnapTray_Recording_*.windows.json";
```

- [ ] **Step 4: Build and run**

Run: `ctest --test-dir build -R RecordingManager_Lifecycle --output-on-failure`
Expected: PASS, including the four new functions. Also run `ctest --test-dir build -R RecordingManager --output-on-failure` to check the state-machine and startup suites still pass.

- [ ] **Step 5: Commit**

```bash
git add include/RecordingManager.h src/RecordingManager.cpp tests/RecordingManager/tst_Lifecycle.cpp
git commit -m "feat(recording): record the window timeline during preview recordings"
```

---

### Task 6: The sidecar dies with the temp recording

**Files:**
- Modify: `src/RecordingManager.cpp` (`showSaveDialog`), `src/MainApplication.cpp` (`onPreviewDiscardRequested`), `src/qml/RecordingPreviewBackend.mm` (export success and animated conversion success)
- Modify: `tests/RecordingManager/tst_Lifecycle.cpp`, `tests/Qml/tst_RecordingPreviewExport.cpp`

**Interfaces:**
- Consumes: `WindowTimelineSidecar::remove(videoPath)`.

The temp MP4 leaves the preview in four places; each also removes the sidecar:

1. `RecordingManager::showSaveDialog()` (`src/RecordingManager.cpp`, the function starting at the line `void RecordingManager::showSaveDialog(const QString &tempOutputPath, const QSize &outputSize)`): every exit either moves the temp file to its final place, keeps it in place as the final file, or deletes it on cancel, so the sidecar is dropped unconditionally. Add `#include <QScopeGuard>` and `#include "recording/WindowTimelineSidecar.h"` to the file, and as the first statement of the function:

```cpp
    // Whatever happens below, the temp recording stops being a preview input:
    // it is moved, kept as the final file, or deleted. Its sidecar goes with it.
    const auto dropSidecar = qScopeGuard([&tempOutputPath]() {
        SnapTray::WindowTimelineSidecar::remove(tempOutputPath);
    });
```

2. `MainApplication::onPreviewDiscardRequested()` (`src/MainApplication.cpp`): add `#include "recording/WindowTimelineSidecar.h"` and change the body to:

```cpp
void MainApplication::onPreviewDiscardRequested(const QString& videoPath)
{
    if (!videoPath.isEmpty() && QFile::exists(videoPath)) {
        if (!QFile::remove(videoPath)) {
            qWarning() << "MainApplication: Failed to delete temp file:" << videoPath;
        }
    }
    SnapTray::WindowTimelineSidecar::remove(videoPath);
}
```

3. `RecordingPreviewBackend::performTranscode()` completion lambda (`src/qml/RecordingPreviewBackend.mm`, the block ending in `emit weakThis->saveRequested(outputPath, croppedSize);` after the MP4 export): add `#include "recording/WindowTimelineSidecar.h"` and replace `QFile::remove(inputPath);` with:

```cpp
            QFile::remove(inputPath);
            SnapTray::WindowTimelineSidecar::remove(inputPath);
```

4. `RecordingPreviewBackend::performFormatConversion()` completion (same file, the block that checks `outInfo.exists() && outInfo.size() > 0`): replace `QFile::remove(sourceVideoPath);` with:

```cpp
                QFile::remove(sourceVideoPath);
                SnapTray::WindowTimelineSidecar::remove(sourceVideoPath);
```

- [ ] **Step 1: Write the failing tests**

In `tests/RecordingManager/tst_Lifecycle.cpp`, extend `testSaveNameUsesCroppedOutputSize()` : after the two fixture files are written, add a sidecar next to `uncropped` and assert it is gone after the save:

```cpp
    // (after the for loop that writes "video" into both files)
    SnapTray::WindowTimeline timeline;
    timeline.setFrameSize(QSize(16, 16));
    QVERIFY(SnapTray::WindowTimelineSidecar::write(uncropped, timeline));
    // ... existing triggerSaveDialog(cropped, ...) / triggerSaveDialog(uncropped) and their QCOMPAREs ...
    // The saved recording takes its sidecar with it.
    QVERIFY(!QFile::exists(SnapTray::WindowTimelineSidecar::pathFor(uncropped)));
```

In `tests/Qml/tst_RecordingPreviewExport.cpp`, include `"recording/WindowTimelineSidecar.h"` and `"recording/WindowTimeline.h"`; in `saveMp4Edits()` after `createRecording(...)` succeeds add:

```cpp
    SnapTray::WindowTimeline timeline;
    timeline.setFrameSize(sourceSize);
    QVERIFY(SnapTray::WindowTimelineSidecar::write(inputPath, timeline));
```

The existing final assertion `QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList(QFileInfo(outputPath).fileName()));` now also fails until the sidecar is removed with the source. Do the same in `saveAnimation()` after its `createRecording` call; its existing check that only the output remains (look for the `entryList(QDir::Files)` comparison in that test) covers the animated path.

- [ ] **Step 2: Build and run, verify the three tests fail**

Run: `ctest --test-dir build -R "RecordingManager_Lifecycle|Qml_RecordingPreviewExport" --output-on-failure`
Expected: FAIL on the directory listing / sidecar existence assertions.

- [ ] **Step 3: Add the four removals**

Apply the four edits listed under Interfaces. Each is one include plus one line.

- [ ] **Step 4: Run and verify they pass**

Run: `ctest --test-dir build -R "RecordingManager_Lifecycle|Qml_RecordingPreviewExport" --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/RecordingManager.cpp src/MainApplication.cpp src/qml/RecordingPreviewBackend.mm tests/RecordingManager/tst_Lifecycle.cpp tests/Qml/tst_RecordingPreviewExport.cpp
git commit -m "fix(recording): remove the window timeline sidecar with its temp recording"
```

---

### Task 7: Backend window lookup for the preview

**Files:**
- Modify: `include/utils/VideoCropGeometry.h`, `src/utils/VideoCropGeometry.cpp`, `tests/Utils/tst_VideoCropGeometry.cpp`
- Modify: `include/qml/RecordingPreviewBackend.h`, `src/qml/RecordingPreviewBackend.mm`
- Modify: `tests/Qml/tst_RecordingPreviewCrop.cpp`

**Interfaces:**
- Consumes: `WindowTimelineSidecar::read`, `WindowTimeline::hitTest`, `VideoCropGeometry::videoToView`.
- Produces (used by Task 8's QML):

```cpp
// VideoCropGeometry
QPoint viewPointToVideo(const QPointF& viewPoint, const QRectF& contentRect, const QSize& frameSize); // QPoint(-1,-1) when unmappable

// RecordingPreviewBackend
Q_PROPERTY(bool hasWindowTimeline READ hasWindowTimeline NOTIFY windowTimelineChanged)
bool hasWindowTimeline() const;
// Window under `viewPoint` at `positionMs`, in view coordinates; empty when none or no timeline.
Q_INVOKABLE QRectF windowRectInViewAt(const QPointF& viewPoint, const QRectF& contentRect, qint64 positionMs) const;
Q_INVOKABLE QString windowAppAt(const QPointF& viewPoint, const QRectF& contentRect, qint64 positionMs) const;
```

The backend reads the sidecar in its constructor (`m_windowTimeline = WindowTimelineSidecar::read(videoPath)`). `updateVideoSize()` drops a timeline whose `frameSize()` differs from the reported video size (Review Focus 1) and emits `windowTimelineChanged()`.

- [ ] **Step 1: Write the failing geometry test**

In `tests/Utils/tst_VideoCropGeometry.cpp` add slot `void viewPointToVideoMapsAndClamps();` and:

```cpp
void tst_VideoCropGeometry::viewPointToVideoMapsAndClamps()
{
    // 800x400 video drawn at half scale, 100 px below the item top.
    const QRectF content(0, 100, 400, 200);
    const QSize frame(800, 400);
    QCOMPARE(viewPointToVideo(QPointF(100, 150), content, frame), QPoint(200, 100));
    QCOMPARE(viewPointToVideo(QPointF(399.9, 299.9), content, frame), QPoint(799, 399)); // inside the last pixel
    QCOMPARE(viewPointToVideo(QPointF(-5, 150), content, frame), QPoint(-1, -1));         // outside the content
    QCOMPARE(viewPointToVideo(QPointF(100, 150), QRectF(), frame), QPoint(-1, -1));
}
```

- [ ] **Step 2: Write the failing backend tests**

In `tests/Qml/tst_RecordingPreviewCrop.cpp` include `"recording/WindowTimeline.h"`, `"recording/WindowTimelineSidecar.h"`, `<QTemporaryDir>`; add slots:

```cpp
    void windowLookupWithoutSidecar();
    void windowLookupMapsToView();
    void sidecarFrameSizeMismatchDisablesTimeline();
    void corruptSidecarIsIgnored();
```

and bodies:

```cpp
namespace {
QString writeTimeline(const QTemporaryDir& dir, const QSize& frameSize)
{
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    SnapTray::WindowTimeline timeline;
    timeline.setFrameSize(frameSize);
    SnapTray::WindowSample code;
    code.windowId = 1;
    code.rect = QRect(200, 100, 400, 200);
    code.ownerApp = QStringLiteral("Code");
    SnapTray::WindowSample safari;
    safari.windowId = 2;
    safari.rect = QRect(0, 0, 800, 400);
    safari.z = 1;
    safari.ownerApp = QStringLiteral("Safari");
    timeline.append(0, {code, safari});
    timeline.append(1000, {safari}); // Code closes at 1 s
    [&] { QVERIFY(SnapTray::WindowTimelineSidecar::write(video, timeline)); }();
    return video;
}
} // namespace

void tst_RecordingPreviewCrop::windowLookupWithoutSidecar()
{
    RecordingPreviewBackend backend(QStringLiteral("unused.mp4"));
    backend.updateVideoSize(QSize(800, 400));
    QVERIFY(!backend.hasWindowTimeline());
    QCOMPARE(backend.windowRectInViewAt(QPointF(100, 150), QRectF(0, 100, 400, 200), 0), QRectF());
    QCOMPARE(backend.windowAppAt(QPointF(100, 150), QRectF(0, 100, 400, 200), 0), QString());
}

void tst_RecordingPreviewCrop::windowLookupMapsToView()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingPreviewBackend backend(writeTimeline(dir, QSize(800, 400)));
    QVERIFY(backend.hasWindowTimeline());
    backend.updateVideoSize(QSize(800, 400));
    QVERIFY(backend.hasWindowTimeline());
    // The video is drawn at half scale, 100 px down: video (200,100 400x200) is view (100,150 200x100).
    const QRectF content(0, 100, 400, 200);
    QCOMPARE(backend.windowRectInViewAt(QPointF(150, 175), content, 500), QRectF(100, 150, 200, 100));
    QCOMPARE(backend.windowAppAt(QPointF(150, 175), content, 500), QStringLiteral("Code"));
    // Outside Code but inside Safari: the window behind.
    QCOMPARE(backend.windowRectInViewAt(QPointF(20, 120), content, 500), QRectF(0, 100, 400, 200));
    QCOMPARE(backend.windowAppAt(QPointF(20, 120), content, 500), QStringLiteral("Safari"));
    // After Code closed only Safari is there.
    QCOMPARE(backend.windowRectInViewAt(QPointF(150, 175), content, 1500), QRectF(0, 100, 400, 200));
    // Outside the content: nothing.
    QCOMPARE(backend.windowRectInViewAt(QPointF(-10, 175), content, 500), QRectF());
}

void tst_RecordingPreviewCrop::sidecarFrameSizeMismatchDisablesTimeline()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingPreviewBackend backend(writeTimeline(dir, QSize(800, 400)));
    QSignalSpy spy(&backend, &RecordingPreviewBackend::windowTimelineChanged);
    QVERIFY(backend.hasWindowTimeline());
    backend.updateVideoSize(QSize(1920, 1080)); // a sidecar from some other recording
    QVERIFY(!backend.hasWindowTimeline());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(backend.windowRectInViewAt(QPointF(150, 175), QRectF(0, 100, 400, 200), 500), QRectF());
}

void tst_RecordingPreviewCrop::corruptSidecarIsIgnored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    QFile corrupt(SnapTray::WindowTimelineSidecar::pathFor(video));
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("nope");
    corrupt.close();
    RecordingPreviewBackend backend(video);
    QVERIFY(!backend.hasWindowTimeline());
}
```

- [ ] **Step 3: Build and verify they fail**

Run: `./scripts/build.sh`
Expected: FAIL, `viewPointToVideo` undeclared and `hasWindowTimeline` not a member.

- [ ] **Step 4: Implement**

`include/utils/VideoCropGeometry.h`, after `videoToView`:

```cpp
// The video pixel under a view point; QPoint(-1, -1) when the point is outside the content.
QPoint viewPointToVideo(const QPointF& viewPoint, const QRectF& contentRect, const QSize& frameSize);
```

`src/utils/VideoCropGeometry.cpp`:

```cpp
QPoint viewPointToVideo(const QPointF& viewPoint, const QRectF& contentRect, const QSize& frameSize)
{
    if (contentRect.isEmpty() || frameSize.isEmpty() || !contentRect.contains(viewPoint)) {
        return QPoint(-1, -1);
    }
    const QPointF local = viewPoint - contentRect.topLeft();
    const int x = qBound(0, static_cast<int>(local.x() * frameSize.width() / contentRect.width()), frameSize.width() - 1);
    const int y = qBound(0, static_cast<int>(local.y() * frameSize.height() / contentRect.height()), frameSize.height() - 1);
    return QPoint(x, y);
}
```

`include/qml/RecordingPreviewBackend.h`: include `"recording/WindowTimeline.h"` and `<optional>`; add after the `minCropSide` property:

```cpp
    // Where the top-level windows were while recording (from the sidecar), for snapping.
    Q_PROPERTY(bool hasWindowTimeline READ hasWindowTimeline NOTIFY windowTimelineChanged)
```

public methods next to `cropRectInView`:

```cpp
    bool hasWindowTimeline() const { return m_windowTimeline.has_value(); }
    // Window under `viewPoint` at `positionMs`, in view coordinates; empty when none.
    Q_INVOKABLE QRectF windowRectInViewAt(const QPointF &viewPoint, const QRectF &contentRect, qint64 positionMs) const;
    Q_INVOKABLE QString windowAppAt(const QPointF &viewPoint, const QRectF &contentRect, qint64 positionMs) const;
```

signal `void windowTimelineChanged();`, private member `std::optional<SnapTray::WindowTimeline> m_windowTimeline;` and private helper `std::optional<SnapTray::WindowSample> windowAt(const QPointF &viewPoint, const QRectF &contentRect, qint64 positionMs) const;`.

`src/qml/RecordingPreviewBackend.mm`: include `"recording/WindowTimelineSidecar.h"`; in the constructor body `m_windowTimeline = SnapTray::WindowTimelineSidecar::read(videoPath);`; in `updateVideoSize()` before `setCropRect(m_cropRect);`:

```cpp
    if (m_windowTimeline && m_windowTimeline->frameSize() != size) {
        qDebug() << "RecordingPreviewBackend: window timeline frame size" << m_windowTimeline->frameSize()
                 << "does not match the video" << size << "- snapping disabled";
        m_windowTimeline.reset();
        emit windowTimelineChanged();
    }
```

and the lookups (next to `cropRectInView`):

```cpp
std::optional<SnapTray::WindowSample> RecordingPreviewBackend::windowAt(const QPointF &viewPoint,
                                                                         const QRectF &contentRect,
                                                                         qint64 positionMs) const
{
    if (!m_windowTimeline || m_videoSize.isEmpty()) {
        return std::nullopt;
    }
    const QPoint videoPoint = SnapTray::VideoCropGeometry::viewPointToVideo(viewPoint, contentRect, m_videoSize);
    if (videoPoint.x() < 0) {
        return std::nullopt;
    }
    return m_windowTimeline->hitTest(videoPoint, positionMs);
}

QRectF RecordingPreviewBackend::windowRectInViewAt(const QPointF &viewPoint, const QRectF &contentRect,
                                                   qint64 positionMs) const
{
    const auto window = windowAt(viewPoint, contentRect, positionMs);
    return window ? SnapTray::VideoCropGeometry::videoToView(window->rect, contentRect, m_videoSize) : QRectF();
}

QString RecordingPreviewBackend::windowAppAt(const QPointF &viewPoint, const QRectF &contentRect,
                                             qint64 positionMs) const
{
    const auto window = windowAt(viewPoint, contentRect, positionMs);
    return window ? window->ownerApp : QString();
}
```

- [ ] **Step 5: Run the tests**

Run: `ctest --test-dir build -R "Utils_VideoCropGeometry|Qml_RecordingPreviewCrop" --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add include/utils/VideoCropGeometry.h src/utils/VideoCropGeometry.cpp tests/Utils/tst_VideoCropGeometry.cpp include/qml/RecordingPreviewBackend.h src/qml/RecordingPreviewBackend.mm tests/Qml/tst_RecordingPreviewCrop.cpp
git commit -m "feat(recording): expose the window under the cursor to the preview"
```

---

### Task 8: Hover highlight and click-to-snap in the crop overlay

**Files:**
- Modify: `src/qml/recording/RecordingCropOverlay.qml`, `src/qml/recording/RecordingPreview.qml`
- Modify: `tests/Qml/tst_RecordingCropOverlay.cpp`

**Interfaces:**
- Consumes: `backend.hasWindowTimeline`, `backend.windowRectInViewAt(point, contentRect, positionMs)`, `backend.windowAppAt(...)` (Task 7); `videoPlayer.position`, `videoPlayer.contentRect`.
- Produces, on `RecordingCropOverlay`:

```qml
property rect hoverRect: Qt.rect(0, 0, 0, 0)   // set by RecordingPreview from the backend; empty = no window
property string hoverLabel: ""                  // app name shown next to the highlight
property point hoverPoint: Qt.point(0, 0)        // last pointer position while editing
property bool hovering: false
signal hoverChanged()                            // pointer moved or left while editing (not during a drag)
function snapToRect(r)                           // r clamped into the content and grown to the minimum size
```

Behaviour: while editing and not dragging, the pointer position is published through `hoverPoint`/`hovering` and `hoverChanged()`; RecordingPreview answers by setting `hoverRect`/`hoverLabel`. A highlight frame (`objectName: "cropHoverFrame"`) and label chip (`cropHoverLabel`) show `hoverRect` when non-empty and no button is pressed. A click (press and release within `createDragThreshold` in create or move mode) with a non-empty `hoverRect` sets `draftRect = snapToRect(hoverRect)`; a drag behaves exactly as before. Resize-handle clicks never snap. RecordingPreview also recomputes the hover when `videoPlayer.position` changes, so playback under a resting pointer updates the highlight, and shows a hint chip (`previewWindowSnapHint`, text `qsTr("Click a window to crop to it, or drag to draw")`) while editing with a timeline and no draft.

- [ ] **Step 1: Extend the stub backend and write the failing tests**

In `tests/Qml/tst_RecordingCropOverlay.cpp`, `StubPreviewBackend` gains:

```cpp
    Q_PROPERTY(bool hasWindowTimeline READ hasWindowTimeline NOTIFY windowTimelineChanged)
    // ...
    bool hasWindowTimeline() const { return !m_stubWindowRect.isEmpty(); }
    // The one "window" of the stub timeline, in video pixels; empty = no timeline.
    void setStubWindow(const QRect& videoRect, const QString& app)
    {
        m_stubWindowRect = videoRect;
        m_stubWindowApp = app;
        emit windowTimelineChanged();
    }
    Q_INVOKABLE QRectF windowRectInViewAt(const QPointF& viewPoint, const QRectF& contentRect, qint64 positionMs) const
    {
        lookupCount++;
        lastLookupPositionMs = positionMs;
        const QPoint video = SnapTray::VideoCropGeometry::viewPointToVideo(viewPoint, contentRect, m_videoSize);
        if (m_stubWindowRect.isEmpty() || video.x() < 0 || !m_stubWindowRect.contains(video)) {
            return {};
        }
        return SnapTray::VideoCropGeometry::videoToView(m_stubWindowRect, contentRect, m_videoSize);
    }
    Q_INVOKABLE QString windowAppAt(const QPointF& viewPoint, const QRectF& contentRect, qint64 positionMs) const
    {
        return windowRectInViewAt(viewPoint, contentRect, positionMs).isEmpty() ? QString() : m_stubWindowApp;
    }
    mutable int lookupCount = 0;
    mutable qint64 lastLookupPositionMs = -1;
signals:
    void windowTimelineChanged();
private:
    QRect m_stubWindowRect;
    QString m_stubWindowApp;
```

(Keep the existing members; add the new ones in the matching sections.) The stub needs `m_videoSize`, which it already tracks through `updateVideoSize`.

Add slots:

```cpp
    // Window snapping (Phase 2).
    void snapToRectClampsAndGrows_data();
    void snapToRectClampsAndGrows();
    void previewHoverHighlightsWindow();
    void previewClickSnapsToWindow();
    void previewDragStillDrawsFreeRect();
    void previewNoTimelineNoHoverNoHint();
```

Unit test (init(): content `(0,50,400,200)`, video 800x400, `minVideoSide` 0 so `minViewSide` 8 applies):

```cpp
void tst_RecordingCropOverlay::snapToRectClampsAndGrows_data()
{
    QTest::addColumn<QRectF>("input");
    QTest::addColumn<QRectF>("expected");
    QTest::newRow("inside") << QRectF(100, 100, 100, 50) << QRectF(100, 100, 100, 50);
    QTest::newRow("clamped to content") << QRectF(-20, 40, 100, 50) << QRectF(0, 50, 80, 40);
    QTest::newRow("grows around centre") << QRectF(200, 150, 2, 2) << QRectF(197, 147, 8, 8);
    QTest::newRow("grows inside the corner") << QRectF(398, 248, 2, 2) << QRectF(392, 242, 8, 8);
}

void tst_RecordingCropOverlay::snapToRectClampsAndGrows()
{
    QFETCH(QRectF, input);
    QFETCH(QRectF, expected);
    QCOMPARE(call("snapToRect", {input}).toRectF(), expected);
}
```

Preview tests (helpers `OPEN_PREVIEW_OR_FAIL`, `previewItem`, `overlayPoint`, `click`, `drag`, `editing`, `previewContentRect` exist in this file):

```cpp
void tst_RecordingCropOverlay::previewHoverHighlightsWindow()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setStubWindow(QRect(400, 100, 400, 200), QStringLiteral("Code"));
    QQuickItem* frame = previewItem("cropHoverFrame");
    QQuickItem* label = previewItem("cropHoverLabel");
    QVERIFY(frame && label);
    QVERIFY(!frame->isVisible()); // not editing

    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    QVERIFY(previewItem("previewWindowSnapHint")->isVisible());
    const QRectF content = previewContentRect();
    const QRectF expected = m_backend->windowRectInViewAt(
        QPointF(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4), content, 0);
    QVERIFY(!expected.isEmpty());

    QTest::mouseMove(m_view.get(), overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4));
    QTRY_VERIFY(frame->isVisible());
    QObject* overlay = previewOverlay();
    QCOMPARE(overlay->property("hoverRect").toRectF(), expected);
    QCOMPARE(overlay->property("hoverLabel").toString(), QStringLiteral("Code"));
    QVERIFY(label->isVisible());

    // Off the window: highlight gone.
    QTest::mouseMove(m_view.get(), overlayPoint(content.x() + 5, content.y() + 5));
    QTRY_VERIFY(!frame->isVisible());
    QCOMPARE(overlay->property("hoverRect").toRectF(), QRectF());
}

void tst_RecordingCropOverlay::previewClickSnapsToWindow()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setStubWindow(QRect(400, 100, 400, 200), QStringLiteral("Code"));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    const QPoint inside = overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4);
    const QRectF expected = m_backend->windowRectInViewAt(
        QPointF(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4), content, 0);

    QTest::mouseMove(m_view.get(), inside);
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier, inside);
    QObject* overlay = previewOverlay();
    QTRY_COMPARE(overlay->property("draftRect").toRectF(), expected);
    QVERIFY(editing());
    QVERIFY(!previewItem("previewWindowSnapHint")->isVisible()); // a draft exists now

    // Enter commits the snapped window as the crop (through the usual view-to-video
    // rounding and even alignment, like any other draft).
    sendKey(Qt::Key_Return);
    QVERIFY(!editing());
    QCOMPARE(m_backend->cropRect(), expectedVideoCrop(expected));
}

void tst_RecordingCropOverlay::previewDragStillDrawsFreeRect()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setStubWindow(QRect(400, 100, 400, 200), QStringLiteral("Code"));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    // Start inside the window, drag well outside it: a free rect, not the window.
    drag(overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4),
         overlayPoint(content.x() + content.width() * 0.9, content.y() + content.height() * 0.9));
    QObject* overlay = previewOverlay();
    const QRectF draft = overlay->property("draftRect").toRectF();
    QVERIFY(draft.width() > content.width() * 0.5);
    QVERIFY(!previewItem("cropHoverFrame")->isVisible());
}

void tst_RecordingCropOverlay::previewNoTimelineNoHoverNoHint()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    click(previewItem("previewCropButton"));
    QVERIFY(!previewItem("previewWindowSnapHint")->isVisible());
    const QRectF content = previewContentRect();
    const QPoint inside = overlayPoint(content.x() + 100, content.y() + 100);
    QTest::mouseMove(m_view.get(), inside);
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier, inside);
    QTest::qWait(50);
    QVERIFY(!previewItem("cropHoverFrame")->isVisible());
    QCOMPARE(previewOverlay()->property("draftRect").toRectF(), QRectF(0, 0, 0, 0)); // a click alone draws nothing
    QCOMPARE(m_backend->lookupCount, 0);
}
```

- [ ] **Step 2: Build and verify they fail**

Run: `ctest --test-dir build -R Qml_RecordingCropOverlay --output-on-failure`
Expected: FAIL: `snapToRect` is not a function; `cropHoverFrame` is null.

- [ ] **Step 3: Implement the overlay changes**

In `src/qml/recording/RecordingCropOverlay.qml` add after `property int minVideoSide: 0`:

```qml
    // Window under the pointer at the playhead, in view coordinates, as the
    // preview looks it up from the recording's window timeline. Empty = none.
    property rect hoverRect: Qt.rect(0, 0, 0, 0)
    property string hoverLabel: ""
    property point hoverPoint: Qt.point(0, 0)
    property bool hovering: false
    // The pointer moved or left while editing (not during a drag).
    signal hoverChanged()
    readonly property bool showsHover: editing && hovering && isNonEmpty(hoverRect) && !cropMouse.pressed
```

Add the function after `mapRect`:

```qml
    // r clamped into the content and grown around its centre to the minimum
    // draft size, so a tiny window still gives a usable selection.
    function snapToRect(r) {
        const c = contentRect
        let left = clamp(r.x, c.x, c.x + c.width)
        let right = clamp(r.x + r.width, c.x, c.x + c.width)
        let top = clamp(r.y, c.y, c.y + c.height)
        let bottom = clamp(r.y + r.height, c.y, c.y + c.height)
        if (right - left < minViewWidth) {
            left = clamp((left + right - minViewWidth) / 2, c.x, c.x + c.width - minViewWidth)
            right = left + minViewWidth
        }
        if (bottom - top < minViewHeight) {
            top = clamp((top + bottom - minViewHeight) / 2, c.y, c.y + c.height - minViewHeight)
            bottom = top + minViewHeight
        }
        return Qt.rect(left, top, right - left, bottom - top)
    }
```

Add the highlight and label before the `MouseArea`:

```qml
    Rectangle {
        objectName: "cropHoverFrame"
        visible: overlay.showsHover
        x: overlay.hoverRect.x
        y: overlay.hoverRect.y
        width: overlay.hoverRect.width
        height: overlay.hoverRect.height
        color: Qt.rgba(overlay.accentColor.r, overlay.accentColor.g, overlay.accentColor.b, 0.12)
        border.color: overlay.accentColor
        border.width: overlay.borderWidth
    }

    GlassSurface {
        objectName: "cropHoverLabel"
        visible: overlay.showsHover && overlay.hoverLabel.length > 0
        width: hoverLabelText.implicitWidth + SemanticTokens.spacing16
        height: overlay.sizeChipHeight
        x: overlay.clamp(overlay.hoverRect.x + SemanticTokens.spacing4, 0, Math.max(0, overlay.width - width))
        y: overlay.clamp(overlay.hoverRect.y + SemanticTokens.spacing4, 0, Math.max(0, overlay.height - height))
        glassBg: ComponentTokens.tooltipBackground
        glassBgTop: ComponentTokens.tooltipBackgroundTop
        glassHighlight: ComponentTokens.tooltipHighlight
        glassBorder: ComponentTokens.tooltipBorder
        glassRadius: ComponentTokens.tooltipRadius

        Text {
            id: hoverLabelText
            anchors.centerIn: parent
            text: overlay.hoverLabel
            color: SemanticTokens.textPrimary
            font.pixelSize: SemanticTokens.fontSizeCaption
            font.family: SemanticTokens.fontFamily
        }
    }
```

In `cropMouse`, change `onPositionChanged` to publish the hover when no button is down, add `onExited`, and turn a click into a snap in `onReleased`:

```qml
        onPositionChanged: function(mouse) {
            if (!pressed) {
                overlay.hoverPoint = Qt.point(mouse.x, mouse.y)
                overlay.hovering = true
                overlay.hoverChanged()
                return
            }
            if (mode === modeNone)
                return
            // ... existing drag body unchanged ...
        }
        onExited: {
            overlay.hovering = false
            overlay.hoverChanged()
        }
        onReleased: function(mouse) {
            const clicked = Math.abs(mouse.x - pressX) < overlay.createDragThreshold
                    && Math.abs(mouse.y - pressY) < overlay.createDragThreshold
            if (clicked && (mode === modeCreate || mode === modeMove) && overlay.isNonEmpty(overlay.hoverRect))
                overlay.draftRect = overlay.snapToRect(overlay.hoverRect)
            mode = modeNone
            // The pointer may have left the window it hovered before the press:
            // re-evaluate at the release point instead of showing a stale highlight.
            overlay.hoverPoint = Qt.point(mouse.x, mouse.y)
            overlay.hoverChanged()
        }
```

In `src/qml/recording/RecordingPreview.qml`:
- In the `RecordingCropOverlay { ... }` block add `onHoverChanged: root.refreshWindowHover()`.
- In `VideoPlaybackItem`, extend `onPositionChanged: function(positionMs) { backend.updatePosition(positionMs); root.refreshWindowHover() }`.
- Add to the root item, next to `saveWithCrop()`:

```qml
    // The window under the resting pointer follows the playhead too.
    function refreshWindowHover() {
        if (!cropOverlay.editing || !cropOverlay.hovering || !backend.hasWindowTimeline) {
            cropOverlay.hoverRect = Qt.rect(0, 0, 0, 0)
            cropOverlay.hoverLabel = ""
            return
        }
        const p = cropOverlay.hoverPoint
        cropOverlay.hoverRect = backend.windowRectInViewAt(p, videoPlayer.contentRect, videoPlayer.position)
        cropOverlay.hoverLabel = backend.windowAppAt(p, videoPlayer.contentRect, videoPlayer.position)
    }
```

- Add the hint chip after `cropChip` (same `GlassSurface` styling as `cropChip`), anchored to the top centre of the video:

```qml
            GlassSurface {
                objectName: "previewWindowSnapHint"
                visible: cropOverlay.editing && backend.hasWindowTimeline && !cropOverlay.hasShownRect
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.margins: SemanticTokens.spacing12
                width: snapHintText.implicitWidth + SemanticTokens.spacing16
                height: root.overlayChipHeight
                z: 5
                glassBg: ComponentTokens.tooltipBackground
                glassBgTop: ComponentTokens.tooltipBackgroundTop
                glassHighlight: ComponentTokens.tooltipHighlight
                glassBorder: ComponentTokens.tooltipBorder
                glassRadius: ComponentTokens.tooltipRadius

                Text {
                    id: snapHintText
                    anchors.centerIn: parent
                    text: qsTr("Click a window to crop to it, or drag to draw")
                    color: root.textSecondary
                    font.pixelSize: SemanticTokens.fontSizeCaption
                    font.family: SemanticTokens.fontFamily
                }
            }
```

- [ ] **Step 4: Run the overlay suite**

Run: `ctest --test-dir build -R Qml_RecordingCropOverlay --output-on-failure`
Expected: PASS, including every pre-existing case (the drag paths are unchanged).

- [ ] **Step 5: Commit**

```bash
git add src/qml/recording/RecordingCropOverlay.qml src/qml/recording/RecordingPreview.qml tests/Qml/tst_RecordingCropOverlay.cpp
git commit -m "feat(recording): highlight and snap the crop to recorded windows"
```

---

### Task 9: Translations, docs and changelog

**Files:**
- Modify: all 24 `translations/snaptray_*.ts`, `tests/Settings/tst_QmlTranslations.cpp`
- Modify: `docs/docs/recording.md`, `docs/zh-tw/docs/recording.md`, `CHANGELOG.md`

- [ ] **Step 1: Write the failing translation test**

In `tests/Settings/tst_QmlTranslations.cpp`, add to the `strings` list of `testRecordingPreviewCropTranslatedForAllLocales()`:

```cpp
        {"RecordingPreview", "Click a window to crop to it, or drag to draw"},
```

Run: `ctest --test-dir build -R Settings_QmlTranslations --output-on-failure`
Expected: FAIL, `snaptray_ar.qm is missing a recording-crop string`.

- [ ] **Step 2: Add the translations**

In each `translations/snaptray_<locale>.ts`, inside `<context><name>RecordingPreview</name>`, add before its `</context>`:

```xml
    <message>
        <source>Click a window to crop to it, or drag to draw</source>
        <translation>…</translation>
    </message>
```

| Locale | Translation |
| --- | --- |
| ar | انقر على نافذة لقصّ التسجيل إليها، أو اسحب للرسم |
| cs | Klepnutím na okno ořízněte na něj, nebo tažením nakreslete výběr |
| de | Klicke auf ein Fenster, um darauf zuzuschneiden, oder ziehe zum Zeichnen |
| el | Κάντε κλικ σε ένα παράθυρο για περικοπή σε αυτό ή σύρετε για σχεδίαση |
| es_MX | Haz clic en una ventana para recortar a ella, o arrastra para dibujar |
| fi | Rajaa ikkunaan napsauttamalla sitä tai piirrä alue vetämällä |
| fr | Cliquez sur une fenêtre pour recadrer dessus, ou faites glisser pour dessiner |
| it | Fai clic su una finestra per ritagliare su di essa, oppure trascina per disegnare |
| ja | ウィンドウをクリックするとその範囲に切り抜き、ドラッグで自由に描けます |
| ko | 창을 클릭하면 그 창에 맞춰 자르고, 드래그하면 직접 그릴 수 있습니다 |
| lt | Spustelėkite langą, kad apkirptumėte pagal jį, arba vilkite ir nubrėžkite |
| nl | Klik op een venster om daarop bij te snijden, of sleep om te tekenen |
| pl | Kliknij okno, aby przyciąć do niego, lub przeciągnij, aby narysować |
| pt | Clique em uma janela para recortar nela ou arraste para desenhar |
| pt_PT | Clique numa janela para recortar para ela ou arraste para desenhar |
| ru | Щёлкните окно, чтобы обрезать по нему, или перетащите, чтобы нарисовать область |
| sr | Кликните на прозор да бисте исекли по њему или превуците да нацртате |
| sv | Klicka på ett fönster för att beskära till det, eller dra för att rita |
| th | คลิกหน้าต่างเพื่อครอบตัดตามหน้าต่างนั้น หรือลากเพื่อวาดเอง |
| tr | Bir pencereye tıklayarak ona göre kırpın veya sürükleyerek çizin |
| vi | Nhấp vào một cửa sổ để cắt theo cửa sổ đó, hoặc kéo để vẽ |
| zh_CN | 点击窗口即可裁切到该窗口，或拖曳自行绘制 |
| zh_HK | 點擊視窗即可裁切到該視窗，或拖曳自行繪製 |
| zh_TW | 點擊視窗即可裁切到該視窗，或拖曳自行繪製 |

A small script is fine for the insertion (`translations/` files are UTF-8 with LF line endings; keep them that way), the same way Phase 1's review fixes added "Export failed; the original recording was kept."

Run: `ctest --test-dir build -R Settings_QmlTranslations --output-on-failure`
Expected: PASS.

- [ ] **Step 3: Docs and changelog**

In `docs/docs/recording.md`, replace step 2 of "Crop a recording" with:

```markdown
2. Drag on the video to draw the area, then drag inside it to move or drag a handle to resize. Edges snap to the video edges and centre lines. Hover over a window and it lights up; click it to crop to that window as it was at the current moment of the video.
```

In `docs/zh-tw/docs/recording.md`, the matching step:

```markdown
2. 在影片上拖曳框出範圍，再於框內拖曳移動，或拖曳控點調整大小。邊緣會吸附到影片邊緣與中心線。將游標移到視窗上會亮起，點一下即可裁切成該視窗在影片目前時間點的範圍。
```

In `CHANGELOG.md` under `## [Unreleased]` / `### Added`, append:

```markdown
- In the recording preview's crop editor, hovering highlights the window that was under the cursor at that moment and clicking snaps the crop to it.
```

- [ ] **Step 4: Full verification**

Run: `./scripts/run-tests.sh` (macOS) and `scripts\run-tests.bat` (Windows).
Expected: the whole suite passes. Known on the Windows dev box: `Annotations_AnnotationLayer` takes about five minutes (its TIMEOUT is 600 s there) and the two `Packaging_*` tests need `NoDefaultCurrentDirectoryInExePath` cleared; neither is related to this plan.

- [ ] **Step 5: Commit**

```bash
git add translations tests/Settings/tst_QmlTranslations.cpp docs/docs/recording.md docs/zh-tw/docs/recording.md CHANGELOG.md
git commit -m "docs(recording): document window snapping and translate its hint"
```

---

## Self-review notes

- Spec coverage: roadmap tasks 1 (Task 1), 2 (Task 2), 3 (Task 3), 4 (Tasks 4, 5, 6), 5 (Tasks 7, 8), 6 (Task 9). Deferred items are not implemented. The roadmap's "background thread" and "excluded-window list" are consciously replaced; see the Spec paragraph.
- Names used across tasks: `WindowSample`, `WindowTimelineEntry`, `WindowTimeline::{append,windowsAt,hitTest,toJson,fromJson,frameSize,setFrameSize,entries,isEmpty}`, `WindowFrameMapping::{toVideoRect,isValid}`, `WindowDetector::topLevelWindowsSnapshot`, `WindowTimelineRecorder::{detectorEnumerator,start,pause,resume,stop,sampleNow,isRunning,timeline,kSampleIntervalMs}`, `WindowTimelineSidecar::{pathFor,write,read,remove}`, `RecordingManager::{startWindowTimeline,finishWindowTimeline,m_windowTimelineRecorder,m_windowFrameMapping,m_createWindowEnumerator}`, `VideoCropGeometry::viewPointToVideo`, `RecordingPreviewBackend::{hasWindowTimeline,windowRectInViewAt,windowAppAt,windowTimelineChanged}`, overlay `{hoverRect,hoverLabel,hoverPoint,hovering,hoverChanged,snapToRect,cropHoverFrame,cropHoverLabel}`, preview `{refreshWindowHover,previewWindowSnapHint}`.
- Review Focus items 1 to 5 are pinned to Tasks 7, 2, 1, 8 and 3 respectively.
