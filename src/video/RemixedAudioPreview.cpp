#include "video/RemixedAudioPreview.h"
#include <QDebug>
#include <QMutexLocker>
#include <QtEndian>
#include <chrono>
#include <cmath>

namespace SnapTray {
namespace {
qint64 nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
RemixedAudioPreview::RemixedAudioPreview() = default;
RemixedAudioPreview::~RemixedAudioPreview()
{
    { QMutexLocker lock(&m_mutex); m_stopping = true; }
    wait();
}
bool RemixedAudioPreview::setSource(IVideoPlayer::AudioSource source)
{
    const bool needed = bool(source);
    {
        QMutexLocker lock(&m_mutex);
        m_source = std::move(source);
        ++m_generation;
    }
    if (needed && !isRunning()) start();
    return true;
}
bool RemixedAudioPreview::hasSource() const { QMutexLocker lock(&m_mutex); return bool(m_source); }
void RemixedAudioPreview::setVolume(float volume, bool muted)
{
    QMutexLocker lock(&m_mutex);
    m_volume = qBound(0.0f, volume, 1.0f); m_muted = muted;
}
void RemixedAudioPreview::synchronize(qint64 ms, bool playing, float rate, bool discontinuity)
{
    QMutexLocker lock(&m_mutex);
    if (discontinuity || playing != m_playing || !qFuzzyCompare(rate, m_rate)) ++m_generation;
    m_positionMs = ms; m_clockNs = nowNs(); m_playing = playing; m_rate = rate;
}
void RemixedAudioPreview::run()
{
    auto device = createPreviewPcmOutput();
    bool opened = false;
    quint64 generation = 0;
    qint64 nextFrame = 0;
    while (true) {
        IVideoPlayer::AudioSource source;
        bool active, muted, stop;
        float volume;
        quint64 currentGeneration;
        qint64 positionFrame;
        {
            QMutexLocker lock(&m_mutex);
            stop = m_stopping;
            source = m_source;
            active = source && m_playing && qFuzzyCompare(m_rate, 1.0f);
            muted = m_muted; volume = m_volume; currentGeneration = m_generation;
            positionFrame = qMax<qint64>(0, m_positionMs * 48
                + (active ? (nowNs() - m_clockNs) * 48000 / 1000000000 : 0));
        }
        if (stop) break;
        if (generation != currentGeneration) {
            if (opened) device->reset();
            generation = currentGeneration;
            nextFrame = positionFrame;
        }
        if (!active) { msleep(5); continue; }
        if (!opened) {
            if (!device || !device->open()) {
                qWarning() << "Cannot open native audio output for the recording mix";
                // Retry only after a new play/seek/source change.
                do { msleep(20); QMutexLocker lock(&m_mutex);
                    if (m_stopping) return;
                    if (m_generation != generation) break;
                } while (true);
                continue;
            }
            opened = true; nextFrame = positionFrame;
        }
        const int queued = device->queuedFrames();
        constexpr qint64 kResyncFrames = 4800; // 100 ms; tolerate native clock quantization
        if (qAbs(nextFrame - queued - positionFrame) > kResyncFrames) {
            device->reset(); nextFrame = positionFrame;
        }
        if (device->queuedFrames() >= PreviewPcmOutput::kChunkFrames * PreviewPcmOutput::kBufferCount) {
            msleep(5); continue;
        }
        QByteArray pcm = source(nextFrame, PreviewPcmOutput::kChunkFrames);
        if (pcm.size() != PreviewPcmOutput::kChunkFrames * 4) pcm = QByteArray(PreviewPcmOutput::kChunkFrames * 4, '\0');
        if (muted) pcm.fill('\0');
        else if (volume < 1.0f) {
            auto* data = reinterpret_cast<uchar*>(pcm.data());
            for (qsizetype i = 0; i < pcm.size(); i += 2)
                qToLittleEndian<qint16>(qint16(std::lround(qFromLittleEndian<qint16>(data + i) * volume)), data + i);
        }
        // A source read may overlap a seek. Never enqueue its obsolete samples.
        {
            QMutexLocker lock(&m_mutex);
            if (m_stopping) break;
            if (m_generation != generation) continue;
            if (!device->write(pcm)) {
                qWarning() << "Native recording mix output stopped";
                device->reset(); opened = false;
            } else nextFrame += PreviewPcmOutput::kChunkFrames;
        }
    }
    if (opened) device->reset();
}
} // namespace SnapTray
