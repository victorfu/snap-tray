#pragma once
#include "capture/IAudioCaptureEngine.h"
#include <memory>

class PulseAudioCaptureEngine final : public IAudioCaptureEngine
{
public:
    explicit PulseAudioCaptureEngine(QObject* parent = nullptr);
    ~PulseAudioCaptureEngine() override;
    bool setAudioSource(AudioSource source) override;
    AudioSource audioSource() const override { return m_source; }
    bool setDevice(const QString& id) override;
    AudioFormat audioFormat() const override { return m_format; }
    QList<AudioDevice> availableInputDevices() const override;
    QString defaultInputDevice() const override;
    bool start() override;
    void stop() override;
    void pause() override;
    void resume() override;
    bool isRunning() const override;
    bool isPaused() const override;
    QString engineName() const override { return QStringLiteral("PulseAudio / PipeWire"); }
    bool isAvailable() const override;
    bool isSystemAudioSupported() const override { return true; }
private:
    struct State;
    std::unique_ptr<State> d;
};
