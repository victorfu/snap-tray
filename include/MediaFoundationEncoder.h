#ifndef MEDIAFOUNDATIONENCODER_H
#define MEDIAFOUNDATIONENCODER_H

#include "IVideoEncoder.h"

#ifdef Q_OS_WIN

class MediaFoundationEncoderPrivate;

/**
 * @brief Windows Media Foundation H.264 encoder
 *
 * Uses Windows Media Foundation APIs to encode MP4 video
 * with hardware acceleration when available.
 */
class MediaFoundationEncoder : public IVideoEncoder
{
    Q_OBJECT

public:
    explicit MediaFoundationEncoder(QObject *parent = nullptr);
    ~MediaFoundationEncoder() override;

    bool isAvailable() const override;
    QString encoderName() const override { return "Media Foundation"; }

    bool start(const QString &outputPath, const QSize &frameSize, int frameRate) override;
    void writeFrame(const QImage &frame, qint64 timestampMs = -1) override;
    void finish() override;
    void abort() override;

    bool isRunning() const override;
    QString lastError() const override;
    qint64 framesWritten() const override;
    QString outputPath() const override;

    void setQuality(int quality) override;

    // Audio support
    void setAudioFormat(int sampleRate, int channels, int bitsPerSample) override;
    bool isAudioSupported() const override;
    bool isAudioEnabled() const override;
    void writeAudioSamples(const QByteArray &pcmData, qint64 timestampMs) override;

private:
    MediaFoundationEncoderPrivate *d;
};

#endif // Q_OS_WIN
#endif // MEDIAFOUNDATIONENCODER_H
