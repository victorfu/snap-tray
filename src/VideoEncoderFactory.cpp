#include "IVideoEncoder.h"
#include <QDebug>

#ifdef Q_OS_MAC
#include "AVFoundationEncoder.h"
#endif

#ifdef Q_OS_WIN
#include "MediaFoundationEncoder.h"
#endif

IVideoEncoder* IVideoEncoder::createNativeEncoder(QObject *parent)
{
#ifdef Q_OS_MAC
    auto encoder = new AVFoundationEncoder(parent);
    if (encoder->isAvailable()) {
        qDebug() << "VideoEncoderFactory: Using AVFoundation encoder";
        return encoder;
    }
    qDebug() << "VideoEncoderFactory: AVFoundation encoder not available";
    delete encoder;
#endif

#ifdef Q_OS_WIN
    auto encoder = new MediaFoundationEncoder(parent);
    if (encoder->isAvailable()) {
        qDebug() << "VideoEncoderFactory: Using Media Foundation encoder";
        return encoder;
    }
    qDebug() << "VideoEncoderFactory: Media Foundation encoder not available";
    delete encoder;
#endif

    qWarning() << "VideoEncoderFactory: No native encoder available on this platform";
    return nullptr;
}
