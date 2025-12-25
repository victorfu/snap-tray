#ifndef AVFOUNDATIONENCODER_H
#define AVFOUNDATIONENCODER_H

#include "IVideoEncoder.h"

#ifdef Q_OS_MAC

class AVFoundationEncoderPrivate;

/**
 * @brief macOS AVFoundation H.264 encoder
 *
 * Uses Apple's AVFoundation framework to encode MP4 video
 * with hardware acceleration via VideoToolbox.
 */
class AVFoundationEncoder : public IVideoEncoder
{
    Q_OBJECT

public:
    explicit AVFoundationEncoder(QObject *parent = nullptr);
    ~AVFoundationEncoder() override;

    bool isAvailable() const override;
    QString encoderName() const override { return "AVFoundation"; }

    bool start(const QString &outputPath, const QSize &frameSize, int frameRate) override;
    void writeFrame(const QImage &frame, qint64 timestampMs = -1) override;
    void finish() override;
    void abort() override;

    bool isRunning() const override;
    QString lastError() const override;
    qint64 framesWritten() const override;
    QString outputPath() const override;

    void setQuality(int quality) override;

private:
    AVFoundationEncoderPrivate *d;
};

#endif // Q_OS_MAC
#endif // AVFOUNDATIONENCODER_H
