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
