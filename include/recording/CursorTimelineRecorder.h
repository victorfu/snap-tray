#pragma once

#include "recording/CursorProbe.h"
#include "recording/CursorTimeline.h"
#include "recording/WindowTimeline.h"

#include <QHash>
#include <QObject>
#include <QTimer>

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

    bool isRunning() const { return m_running && !m_paused; }
    CursorProbe::Capabilities capabilities() const { return m_capabilities; }
    QString probeName() const;
    bool truncated() const { return m_writer.truncated(); }
    bool failed() const { return m_writer.failed(); }

private:
    void onButtonChanged(Qt::MouseButton button, bool down);
    quint32 currentShapeId();
    void releaseHeldButtons();

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
};

} // namespace SnapTray
