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
    // True when at least one entry lists a window.
    bool hasWindows() const;

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
// region. `physicalRegion` is the mapped capture region in physical pixels,
// expressed in the same space as `physicalScreen` (native desktop pixels);
// its origin is the frame's (0, 0). `physicalScreen` may be empty on platforms
// that expose no native desktop bounds (macOS), in which case both rects are
// physical pixels relative to the logical screen's origin.
struct WindowFrameMapping {
    QRect logicalScreen;
    QRect physicalScreen;
    qreal devicePixelRatio = 1.0;
    QRect physicalRegion; // Recorded region in physical pixels; origin maps to video (0, 0) on every platform.

    // Builds the mapping for a capture of `logicalRegion` (logical screen coordinates).
    // `mappedPhysicalRegion` is the native physical capture region. With a physical screen its origin is
    // used as-is; without one (macOS) the region sits at (0, 0), so its origin is restored from the
    // logical region to keep window rects and the region in one coordinate space.
    static WindowFrameMapping fromCapture(const QRect& logicalRegion, const QRect& logicalScreen,
                                          const QRect& physicalScreen, qreal dpr,
                                          const QRect& mappedPhysicalRegion);

    bool isValid() const { return !logicalScreen.isEmpty() && !physicalRegion.isEmpty() && devicePixelRatio > 0.0; }
    // The part of `logicalBounds` inside the recorded frame, in video pixels; empty when none.
    QRect toVideoRect(const QRect& logicalBounds) const;
};

} // namespace SnapTray
