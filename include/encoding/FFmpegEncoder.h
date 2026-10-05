#pragma once

#include "IVideoEncoder.h"
#include <memory>

// Linux prototype: software H.264 through the distribution's FFmpeg libraries.
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

private:
    struct State;
    std::unique_ptr<State> m_state;
    bool drainPackets();
    void fail(const QString& message);
    void release();
    QString m_error;
    QString m_outputPath;
    qint64 m_framesWritten = 0;
    int m_quality = 80;
    int m_keyframeSeconds = 2;
};
