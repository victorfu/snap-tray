#ifndef MOCKAUDIOCAPTUREENGINE_H
#define MOCKAUDIOCAPTUREENGINE_H

#include "capture/IAudioCaptureEngine.h"
#include <QList>

/**
 * @brief Mock implementation of IAudioCaptureEngine for testing
 *
 * Allows controlling audio capture behavior and tracking method calls
 * without actual audio device access.
 */
class MockAudioCaptureEngine : public IAudioCaptureEngine
{
    Q_OBJECT

public:
    explicit MockAudioCaptureEngine(QObject *parent = nullptr);
    ~MockAudioCaptureEngine() override = default;

    // IAudioCaptureEngine interface implementation
    bool setAudioSource(AudioSource source) override;
    AudioSource audioSource() const override;
    bool setDevice(const QString &deviceId) override;
    AudioFormat audioFormat() const override;
    QList<AudioDevice> availableInputDevices() const override;
    QString defaultInputDevice() const override;
    bool start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    bool isRunning() const override;
    bool isPaused() const override;
    QString engineName() const override { return QStringLiteral("MockAudioEngine"); }
    bool isAvailable() const override { return m_available; }
    bool isSystemAudioSupported() const override { return m_systemAudioSupported; }

    // ========== Mock Control Methods ==========

    /**
     * @brief Control whether start() succeeds
     */
    void setStartSucceeds(bool succeeds);

    /**
     * @brief Control engine availability
     */
    void setAvailable(bool available);

    /**
     * @brief Control system audio support
     */
    void setSystemAudioSupported(bool supported);

    /**
     * @brief Set the audio format returned
     */
    void setAudioFormat(const AudioFormat &format);

    /**
     * @brief Set the list of available devices
     */
    void setAvailableDevices(const QList<AudioDevice> &devices);

    /**
     * @brief Emit audioDataReady signal with test data
     */
    void simulateAudioData(const QByteArray &data, qint64 timestampMs);

    /**
     * @brief Emit error signal
     */
    void simulateError(const QString &message);

    /**
     * @brief Emit warning signal
     */
    void simulateWarning(const QString &message);

    /**
     * @brief Emit deviceLost signal
     */
    void simulateDeviceLost();

    /**
     * @brief Emit levelChanged signal
     */
    void simulateLevelChanged(float level);

    // ========== Spy Methods ==========

    int startCallCount() const { return m_startCalls; }
    int stopCallCount() const { return m_stopCalls; }
    int pauseCallCount() const { return m_pauseCalls; }
    int resumeCallCount() const { return m_resumeCalls; }
    int setAudioSourceCallCount() const { return m_setSourceCalls; }
    int setDeviceCallCount() const { return m_setDeviceCalls; }

    AudioSource lastAudioSource() const { return m_source; }
    QString lastDeviceId() const { return m_deviceId; }

    /**
     * @brief Reset all call counters
     */
    void resetCounters();

private:
    bool m_running = false;
    bool m_paused = false;
    bool m_startSucceeds = true;
    bool m_available = true;
    bool m_systemAudioSupported = true;

    QList<AudioDevice> m_devices;

    // Call counters
    int m_startCalls = 0;
    int m_stopCalls = 0;
    int m_pauseCalls = 0;
    int m_resumeCalls = 0;
    int m_setSourceCalls = 0;
    int m_setDeviceCalls = 0;
};

#endif // MOCKAUDIOCAPTUREENGINE_H
