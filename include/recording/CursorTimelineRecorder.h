#pragma once

#include "recording/CursorProbe.h"
#include "recording/CursorTimeline.h"
#include "recording/WindowTimeline.h"

#include <QHash>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <deque>
#include <functional>
#include <memory>

namespace SnapTray {

// Records the pointer while a recording runs, into a cursor sidecar (#159).
// Positions are polled every kSampleIntervalMs and written only when they
// change; button presses arrive as events, so a click shorter than a poll is
// not missed. Runs on the GUI thread; never while paused or stopped.
class CursorTimelineRecorder : public QObject
{
    Q_OBJECT

public:
    using Clock = std::function<qint64()>; // media time in ms (recording clock minus pauses)
    static constexpr int kSampleIntervalMs = 16;
    static constexpr int kFlushIntervalMs = 1000;

    CursorTimelineRecorder(std::unique_ptr<CursorProbe> probe, Clock clock, WindowFrameMapping mapping,
                           const QString& sidecarPath, bool cursorInVideo, QObject* parent = nullptr);
    ~CursorTimelineRecorder() override;

    // Opens the sidecar and records the initial pointer. False when the file
    // cannot be written; the recording itself is unaffected.
    bool start();
    void pause();
    void resume();
    // Records the last position, releases held buttons and closes the file.
    void stop();
    void sampleNow();

    // Global Qt logical rectangles of visible, interactive SnapTray windows.
    using ExcludedRects = std::function<QList<QRect>()>;
    void setExcludedRectsProvider(ExcludedRects provider) { m_excludedRects = std::move(provider); }
    // Native tray menus have no QWindow; remember their wall-clock intervals,
    // including while paused and when an event arrives after the menu closes.
    // Opening one also drops the click that opened it: the tray icon is not
    // a SnapTray window either, and the press comes before the menu (#424).
    void setMenuOpen(bool open);
    // How long a click waits before it is written, so that a menu opening
    // right after it can still drop it.
    static constexpr qint64 kMenuOpenerGraceMs = 500;
    int skippedUiClicks() const { return m_skippedUiClicks; }

    bool isRunning() const { return m_running && !m_paused; }
    CursorProbe::Capabilities capabilities() const { return m_capabilities; }
    QString probeName() const;
    bool truncated() const { return m_writer.truncated(); }
    bool failed() const { return m_writer.failed(); }
    // How often the probe reported button events it could not deliver.
    int buttonEventLosses() const { return m_buttonEventLosses; }

private:
    void onButtonChanged(Qt::MouseButton button, bool down, qint64 ageMs,
                         std::optional<QPoint> physicalPosition, std::optional<QPoint> logicalPosition);
    void onButtonEventsLost();
    quint32 currentShapeId();
    void releaseHeldButtons();
    // Writes the held-back button events that happened at or before wallMs.
    void writePendingButtons(qint64 wallMs);
    void dropMenuOpeningClick(qint64 openedWallMs);
    void rememberSample(const CursorSample& sample);
    QPoint positionAt(qint64 tMs) const;

    void rememberExcludedRects();
    bool excludedAt(const QPoint& videoPosition, qint64 ageMs) const;
    ExcludedRects m_excludedRects;
    struct ExclusionSample { qint64 wallMs; QList<QRect> rects; };
    std::deque<ExclusionSample> m_exclusionHistory;
    struct MenuInterval { qint64 start; qint64 end; };
    std::deque<MenuInterval> m_menuIntervals;
    QElapsedTimer m_wallClock;
    bool m_menuOpen = false;
    Qt::MouseButtons m_ignored;
    int m_skippedUiClicks = 0;

    std::unique_ptr<CursorProbe> m_probe;
    Clock m_clock;
    WindowFrameMapping m_mapping;
    CursorTimelineWriter m_writer;
    CursorProbe::Capabilities m_capabilities;
    bool m_cursorInVideo = false;
    QTimer m_sampleTimer;
    QTimer m_flushTimer;
    bool m_running = false;
    bool m_paused = false;
    bool m_stopped = false;
    bool m_forceSample = true; // the first sample after start/resume rebuilds the state

    std::optional<CursorSample> m_last;
    quint64 m_lastSerial = 0;
    quint32 m_lastShapeId = 0;
    bool m_shapeHidden = false;
    QHash<QByteArray, quint32> m_shapeIds; // image + hotspot digest -> id
    quint32 m_nextShapeId = 1;
    Qt::MouseButtons m_held;
    struct PendingButton { CursorButtonEvent event; qint64 wallMs; };
    std::deque<PendingButton> m_pendingButtons; // in order, not yet written
    qint64 m_lastSkippedPressWallMs = -1; // the latest press on SnapTray UI
    int m_buttonEventLosses = 0;
    qint64 m_buttonFloorMs = 0; // button events stay in order and after start/resume
    qint64 m_activeSinceMs = 0; // last start/resume: earlier button events are dropped
    // Recent samples, for positioning events delivered late.
    static constexpr qint64 kRecentPositionsMs = 5000;
    std::deque<CursorSample> m_recent;
};

} // namespace SnapTray
