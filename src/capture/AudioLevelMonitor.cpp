#include "capture/AudioLevelMonitor.h"

#include <QPointer>
#include <QtEndian>

#include <cstdlib>

namespace SnapTray {

namespace {

constexpr float kInt16FullScale = 32768.0f;
constexpr float kLevelChangeEpsilon = 0.005f;
constexpr int kBytesPerInt16Sample = 2;

} // namespace

AudioLevelMonitor::AudioLevelMonitor(QObject* parent)
    : QObject(parent)
    , m_createEngine([] { return IAudioCaptureEngine::createBestEngine(nullptr); })
    , m_checkPermission(&IAudioCaptureEngine::checkMicrophonePermission)
    , m_requestPermission(&IAudioCaptureEngine::requestMicrophonePermission)
{
    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &AudioLevelMonitor::pollPeak);
}

AudioLevelMonitor::~AudioLevelMonitor()
{
    stop();
}

float AudioLevelMonitor::peakOf16BitPcm(const QByteArray& data)
{
    const auto* bytes = reinterpret_cast<const uchar*>(data.constData());
    const qsizetype sampleCount = data.size() / kBytesPerInt16Sample;
    int peak = 0;
    for (qsizetype i = 0; i < sampleCount; ++i) {
        const int sample = qFromLittleEndian<qint16>(bytes + i * kBytesPerInt16Sample);
        peak = qMax(peak, std::abs(sample));
    }
    return qMin(1.0f, static_cast<float>(peak) / kInt16FullScale);
}

void AudioLevelMonitor::start(const QString& deviceId)
{
    stop();
    m_deviceId = deviceId;
    const quint64 generation = ++m_generation;

    using Permission = IAudioCaptureEngine::MicrophonePermission;
    const Permission permission = m_checkPermission ? m_checkPermission() : Permission::Authorized;
    if (permission == Permission::Denied || permission == Permission::Restricted) {
        setState(State::PermissionDenied);
        return;
    }
    if (permission == Permission::NotDetermined && m_requestPermission) {
        setState(State::WaitingForPermission);
        const QPointer<AudioLevelMonitor> guard(this);
        // The OS prompt answers asynchronously, possibly on another thread.
        m_requestPermission([guard, generation](bool granted) {
            if (!guard) return;  // The setup closed while the OS prompt was open.
            QMetaObject::invokeMethod(guard, [guard, generation, granted] {
                if (!guard || generation != guard->m_generation) return;
                if (granted) guard->openEngine(generation);
                else guard->setState(State::PermissionDenied);
            }, Qt::QueuedConnection);
        });
        return;
    }
    openEngine(generation);
}

void AudioLevelMonitor::openEngine(quint64 generation)
{
    if (generation != m_generation) {
        return;
    }
    m_engine.reset(m_createEngine ? m_createEngine() : nullptr);
    if (!m_engine) {
        setState(State::Unavailable);
        return;
    }
    if (!m_deviceId.isEmpty()) {
        bool found = false;
        for (const auto& device : m_engine->availableInputDevices()) {
            found = found || device.id == m_deviceId;
        }
        if (!found) {
            m_engine.reset();
            setState(State::DeviceMissing);
            return;
        }
    }
    if (!m_engine->setAudioSource(IAudioCaptureEngine::AudioSource::Microphone)
        || (!m_deviceId.isEmpty() && !m_engine->setDevice(m_deviceId))) {
        m_engine.reset();
        setState(State::Unavailable);
        return;
    }

    m_peak = std::make_shared<std::atomic<float>>(0.0f);
    const bool pcm16 = m_engine->audioFormat().bitsPerSample == 16;
    // Direct connection: runs on the audio thread and only touches the atomic.
    connect(m_engine.get(), &IAudioCaptureEngine::audioDataReady,
            [peak = m_peak, pcm16](const QByteArray& data, qint64) {
        if (!pcm16) return;
        const float value = peakOf16BitPcm(data);
        float current = peak->load(std::memory_order_relaxed);
        while (value > current
               && !peak->compare_exchange_weak(current, value, std::memory_order_relaxed)) {
        }
    });
    // The microphone stops: some backends say deviceLost, others (macOS,
    // PulseAudio) report that no source is active any more. Either way the
    // meter must not keep inviting the user to speak. Queued, so the engine
    // is not destroyed inside its own signal.
    const auto fail = [this, generation](State state) {
        if (generation != m_generation || !m_engine) return;
        m_engine.reset();
        m_peak.reset();
        m_pollTimer.stop();
        setLevel(0.0f);
        setState(state);
    };
    connect(m_engine.get(), &IAudioCaptureEngine::deviceLost, this,
            [fail](IAudioCaptureEngine::AudioSource) { fail(State::DeviceMissing); }, Qt::QueuedConnection);
    connect(m_engine.get(), &IAudioCaptureEngine::activeSourceChanged, this,
            [fail](IAudioCaptureEngine::AudioSource active) {
                if (active != IAudioCaptureEngine::AudioSource::Microphone
                    && active != IAudioCaptureEngine::AudioSource::Both) {
                    fail(State::Unavailable);
                }
            },
            Qt::QueuedConnection);

    if (!m_engine->start()) {
        m_engine.reset();
        m_peak.reset();
        setState(State::Unavailable);
        return;
    }
    setState(State::Listening);
    m_pollTimer.start();
}

void AudioLevelMonitor::pollPeak()
{
    if (!m_peak) {
        return;
    }
    setLevel(m_peak->exchange(0.0f, std::memory_order_relaxed));
}

void AudioLevelMonitor::stop()
{
    ++m_generation;
    m_pollTimer.stop();
    // Disposal disconnects the callback and stops capture before deletion.
    m_engine.reset();
    m_peak.reset();
    setLevel(0.0f);
    setState(State::Idle);
}

void AudioLevelMonitor::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void AudioLevelMonitor::setLevel(float level)
{
    if (qAbs(m_level - level) < kLevelChangeEpsilon && !(level == 0.0f && m_level != 0.0f)) {
        return;
    }
    m_level = level;
    emit levelChanged();
}

} // namespace SnapTray
