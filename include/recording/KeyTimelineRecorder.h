#pragma once

#include "recording/KeyProbe.h"
#include "recording/KeyTimeline.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>

namespace SnapTray {

// Writes the keys pressed during a recording into its key sidecar (#187),
// keeping only what the mode allows and nothing during secure input. Runs on
// the GUI thread; records nothing while paused.
class KeyTimelineRecorder : public QObject
{
    Q_OBJECT
public:
    using Clock = std::function<qint64()>; // media time in ms
    static constexpr int kFlushIntervalMs = 1000;

    KeyTimelineRecorder(std::unique_ptr<KeyProbe> probe, Clock clock, KeyRecordingMode mode,
                        const QString& sidecarPath, bool macSymbols, QObject* parent = nullptr);
    ~KeyTimelineRecorder() override;

    // False when the sidecar cannot be written (the recording is unaffected).
    bool start();
    void pause();
    void resume();
    void stop();

    bool isRunning() const { return m_running && !m_paused; }
    int recordedCount() const { return m_writer.count(); }
    bool detectsSecureInput() const { return m_probe && m_probe->detectsSecureInput(); }
    QString probeName() const { return m_probe ? m_probe->name() : QString(); }

private:
    void onKeyPressed(int key, Qt::KeyboardModifiers modifiers, const QString& text, qint64 ageMs);

    std::unique_ptr<KeyProbe> m_probe;
    Clock m_clock;
    KeyRecordingMode m_mode;
    KeyTimelineWriter m_writer;
    bool m_macSymbols = false;
    QTimer m_flushTimer;
    bool m_running = false;
    bool m_paused = false;
    qint64 m_floorMs = 0; // keys stay in order and after start/resume
};

} // namespace SnapTray
