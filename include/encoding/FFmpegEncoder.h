#pragma once

#include "IVideoEncoder.h"
#include <memory>

// Linux H.264/AAC encoding through the distribution's FFmpeg libraries.
// Calls must be serialized on the owning encoding thread, like EncodingWorker.
class FFmpegEncoder final : public IVideoEncoder
{
public:
    explicit FFmpegEncoder(QObject* parent = nullptr);
    ~FFmpegEncoder() override;

    bool isAvailable() const override;
    QString encoderName() const override;
    bool start(const QString& path, const QSize& size, int frameRate) override;
    void writeFrame(const QImage& frame, qint64 timestampMs = -1) override;
    void finish() override;
    void abort() override;
    bool isRunning() const override;
    QString lastError() const override;
    qint64 framesWritten() const override;
    QString outputPath() const override;
    void setQuality(int quality) override;
    void setKeyFrameIntervalSeconds(int seconds) override;
    void setRateControl(SnapTray::VideoRateControl mode, int quality) override;
    SnapTray::VideoRateControl effectiveRateControl() const override;
    void setBitrate(int bitrate) { m_bitrate = qMax(0, bitrate); }
    void setAudioFormat(int sampleRate, int channels, int bitsPerSample) override;
    bool isAudioSupported() const override;
    bool isAudioEnabled() const override;
    void writeAudioSamples(const QByteArray& pcm, qint64 startFrame) override;

private:
    struct State;
    std::unique_ptr<State> m_state;
    bool drainPackets(bool audio = false);
    bool encodeAudio(bool flushPartial);
    void fail(const QString& message);
    void release();
    QString m_error;
    QString m_outputPath;
    qint64 m_framesWritten = 0;
    int m_quality = 80;
    int m_bitrate = 0;
    int m_audioRate = 0;
    int m_audioChannels = 0;
    SnapTray::VideoRateControl m_rateControl = SnapTray::VideoRateControl::Bitrate;
    QString m_encoderName;
    int m_keyframeSeconds = 2;
};
