#ifndef COREAUDIOCAPTUREENGINE_H
#define COREAUDIOCAPTUREENGINE_H

#include "IAudioCaptureEngine.h"
#include "capture/TimestampedPcmMixer.h"

#ifdef Q_OS_MAC

#include <QMutex>
#include <atomic>
#include <memory>

/**
 * @brief macOS CoreAudio/AVFoundation-based audio capture engine
 *
 * Uses AVFoundation for microphone capture and ScreenCaptureKit (macOS 13+)
 * for system audio capture.
 */
class CoreAudioCaptureEngine : public IAudioCaptureEngine
{
    Q_OBJECT

public:
    explicit CoreAudioCaptureEngine(QObject *parent = nullptr);
    ~CoreAudioCaptureEngine() override;

    // IAudioCaptureEngine interface
    bool setAudioSource(AudioSource source) override;
    AudioSource audioSource() const override { return m_source; }
    bool setDevice(const QString &deviceId) override;
    AudioFormat audioFormat() const override { return m_format; }

    QList<AudioDevice> availableInputDevices() const override;
    QString defaultInputDevice() const override;

    bool start() override;
    void stop() override;
    void disposeAsync() override;
    void pause() override;
    void resume() override;
    bool isRunning() const override { return m_running; }
    bool isPaused() const override { return m_paused; }

    QString engineName() const override;
    bool isAvailable() const override;
    bool isSystemAudioSupported() const override;

    // Internal bridge used by the Objective-C capture delegates.
    void processCapturedAudio(SnapTray::Audio::Source source,
                              const QByteArray& pcm,
                              const SnapTray::Audio::Pcm16Format& format,
                              qint64 timestampNs,
                              qint64 activeTimeNs);
    void reportCapturedAudioFormatFailure(SnapTray::Audio::Source source,
                                          qint64 effectiveTimeNs);
    void handleSystemAudioCaptureFailure(const QString& details);

private:
    AudioSource activeAudioSource() const;
    void notifyActiveSourceChanged();
    void deliverMixerOutput(
        const SnapTray::Audio::TimestampedPcmMixer::ProcessResult& result);

    class Private;
    Private *d;
    std::unique_ptr<SnapTray::Audio::TimestampedPcmMixer> m_mixer;
    bool m_reportedMixerDrop = false;
    std::atomic<bool> m_reportedMicrophoneFormatFailure{false};
    std::atomic<bool> m_reportedSystemFormatFailure{false};
    std::atomic<bool> m_microphoneActive{false};
    std::atomic<bool> m_systemAudioActive{false};
    std::atomic<int> m_lastNotifiedActiveSource{
        static_cast<int>(AudioSource::None)};
    std::atomic<bool> m_starting{false};
    std::atomic<bool> m_stopping{false};
    bool m_systemAudioFailed = false;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_paused{false};

    // Timing
    qint64 m_startTime = 0;
    qint64 m_pausedDuration = 0;
    qint64 m_pauseStartTime = 0;
    mutable QMutex m_timingMutex;
};

#endif // Q_OS_MAC

#endif // COREAUDIOCAPTUREENGINE_H
