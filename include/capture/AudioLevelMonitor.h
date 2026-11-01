#ifndef AUDIOLEVELMONITOR_H
#define AUDIOLEVELMONITOR_H

#include "capture/IAudioCaptureEngine.h"
#include "utils/ResourceCleanupHelper.h"

#include <QObject>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

namespace SnapTray {

/**
 * Short-lived microphone level meter for the recording setup.
 *
 * Owns its own capture engine only between start() and stop(); recording
 * must not begin until the monitor is stopped, so the two never compete for
 * the device. Audio callbacks only update an atomic peak. A GUI timer reads
 * it at a fixed rate, so callbacks are never queued to the GUI thread. The
 * captured audio is never played back.
 */
class AudioLevelMonitor : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        WaitingForPermission,
        Listening,
        PermissionDenied,
        DeviceMissing,  // The chosen device is not connected
        Unavailable,    // No audio engine, or it failed to start
    };
    Q_ENUM(State)

    using EngineFactory = std::function<IAudioCaptureEngine*()>;
    using PermissionCheck = std::function<IAudioCaptureEngine::MicrophonePermission()>;
    using PermissionRequest = std::function<void(std::function<void(bool)>)>;

    explicit AudioLevelMonitor(QObject* parent = nullptr);
    ~AudioLevelMonitor() override;

    // Starts metering `deviceId` (empty = default input). Restarts if running.
    void start(const QString& deviceId);
    void stop();

    State state() const { return m_state; }
    float level() const { return m_level; }

    // Peak of 16-bit little-endian PCM as 0..1. Exposed for tests.
    static float peakOf16BitPcm(const QByteArray& data);

    // Test seams; production uses IAudioCaptureEngine's factory and permission API.
    void setEngineFactory(EngineFactory factory) { m_createEngine = std::move(factory); }
    void setPermissionFunctions(PermissionCheck check, PermissionRequest request)
    {
        m_checkPermission = std::move(check);
        m_requestPermission = std::move(request);
    }

    static constexpr int kPollIntervalMs = 66;  // ~15 updates per second

signals:
    void stateChanged();
    void levelChanged();

private:
    void setState(State state);
    void setLevel(float level);
    void openEngine(quint64 generation);
    void pollPeak();

    EngineFactory m_createEngine;
    PermissionCheck m_checkPermission;
    PermissionRequest m_requestPermission;

    AudioEnginePtr m_engine;
    // Shared with the audio callback, which may outlive a stop() briefly while
    // the engine drains on its own thread.
    std::shared_ptr<std::atomic<float>> m_peak;
    QTimer m_pollTimer;
    QString m_deviceId;
    State m_state = State::Idle;
    float m_level = 0.0f;
    quint64 m_generation = 0;
};

} // namespace SnapTray

#endif // AUDIOLEVELMONITOR_H
