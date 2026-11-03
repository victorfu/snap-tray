#pragma once
#include "video/IVideoPlayer.h"
#include <QThread>
#include <QMutex>
#include <memory>

namespace SnapTray {

// A small native output queue. Owned exclusively by the audio worker.
class PreviewPcmOutput {
public:
    virtual ~PreviewPcmOutput() = default;
    virtual bool open() = 0;
    virtual void reset() = 0;
    virtual int queuedFrames() const = 0;
    virtual bool write(const QByteArray& pcm) = 0;
    static constexpr int kChunkFrames = 960; // 20 ms, 48 kHz stereo PCM16
    static constexpr int kBufferCount = 3;
};
std::unique_ptr<PreviewPcmOutput> createPreviewPcmOutput();

// The video player supplies its source clock. This worker reads the sidecars
// off the GUI thread and drops all queued audio on seeks/pause/source changes.
// Non-unit playback rates are silent, matching edited export's speed policy.
class RemixedAudioPreview final : private QThread {
public:
    RemixedAudioPreview();
    ~RemixedAudioPreview() override;
    bool setSource(IVideoPlayer::AudioSource source);
    bool hasSource() const;
    void synchronize(qint64 positionMs, bool playing, float rate, bool discontinuity = false);
    void setVolume(float volume, bool muted);
private:
    void run() override;
    mutable QMutex m_mutex;
    IVideoPlayer::AudioSource m_source;
    qint64 m_positionMs = 0, m_clockNs = 0;
    quint64 m_generation = 0;
    bool m_playing = false, m_muted = false, m_stopping = false;
    float m_rate = 1.0f, m_volume = 1.0f;
};
} // namespace SnapTray
