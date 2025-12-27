#include "MockAudioCaptureEngine.h"

MockAudioCaptureEngine::MockAudioCaptureEngine(QObject *parent)
    : IAudioCaptureEngine(parent)
{
    // Set default audio format
    m_format.sampleRate = 48000;
    m_format.channels = 2;
    m_format.bitsPerSample = 16;

    // Add a default mock device
    AudioDevice defaultDevice;
    defaultDevice.id = QStringLiteral("mock-device-1");
    defaultDevice.name = QStringLiteral("Mock Audio Device");
    defaultDevice.isDefault = true;
    m_devices.append(defaultDevice);
}

bool MockAudioCaptureEngine::setAudioSource(AudioSource source)
{
    m_setSourceCalls++;
    m_source = source;
    return true;
}

IAudioCaptureEngine::AudioSource MockAudioCaptureEngine::audioSource() const
{
    return m_source;
}

bool MockAudioCaptureEngine::setDevice(const QString &deviceId)
{
    m_setDeviceCalls++;
    m_deviceId = deviceId;
    return true;
}

IAudioCaptureEngine::AudioFormat MockAudioCaptureEngine::audioFormat() const
{
    return m_format;
}

QList<IAudioCaptureEngine::AudioDevice> MockAudioCaptureEngine::availableInputDevices() const
{
    return m_devices;
}

QString MockAudioCaptureEngine::defaultInputDevice() const
{
    for (const auto &device : m_devices) {
        if (device.isDefault) {
            return device.id;
        }
    }
    return m_devices.isEmpty() ? QString() : m_devices.first().id;
}

bool MockAudioCaptureEngine::start()
{
    m_startCalls++;

    if (!m_startSucceeds) {
        emit error(QStringLiteral("Mock audio start failure"));
        return false;
    }

    m_running = true;
    m_paused = false;
    return true;
}

void MockAudioCaptureEngine::stop()
{
    m_stopCalls++;
    m_running = false;
    m_paused = false;
}

void MockAudioCaptureEngine::pause()
{
    m_pauseCalls++;
    if (m_running && !m_paused) {
        m_paused = true;
    }
}

void MockAudioCaptureEngine::resume()
{
    m_resumeCalls++;
    if (m_running && m_paused) {
        m_paused = false;
    }
}

bool MockAudioCaptureEngine::isRunning() const
{
    return m_running;
}

bool MockAudioCaptureEngine::isPaused() const
{
    return m_paused;
}

void MockAudioCaptureEngine::setStartSucceeds(bool succeeds)
{
    m_startSucceeds = succeeds;
}

void MockAudioCaptureEngine::setAvailable(bool available)
{
    m_available = available;
}

void MockAudioCaptureEngine::setSystemAudioSupported(bool supported)
{
    m_systemAudioSupported = supported;
}

void MockAudioCaptureEngine::setAudioFormat(const AudioFormat &format)
{
    m_format = format;
}

void MockAudioCaptureEngine::setAvailableDevices(const QList<AudioDevice> &devices)
{
    m_devices = devices;
}

void MockAudioCaptureEngine::simulateAudioData(const QByteArray &data, qint64 timestampMs)
{
    emit audioDataReady(data, timestampMs);
}

void MockAudioCaptureEngine::simulateError(const QString &message)
{
    emit error(message);
}

void MockAudioCaptureEngine::simulateWarning(const QString &message)
{
    emit warning(message);
}

void MockAudioCaptureEngine::simulateDeviceLost()
{
    emit deviceLost();
}

void MockAudioCaptureEngine::simulateLevelChanged(float level)
{
    emit levelChanged(level);
}

void MockAudioCaptureEngine::resetCounters()
{
    m_startCalls = 0;
    m_stopCalls = 0;
    m_pauseCalls = 0;
    m_resumeCalls = 0;
    m_setSourceCalls = 0;
    m_setDeviceCalls = 0;
}
