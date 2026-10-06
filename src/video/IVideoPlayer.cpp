#include "video/IVideoPlayer.h"
#include <QDebug>

void IVideoPlayer::stepForward()
{
    pause();
    seek(qMin(duration(), position() + frameIntervalMs()));
}

#ifdef Q_OS_MAC
class AVFoundationPlayer;
IVideoPlayer* createAVFoundationPlayer(QObject *parent);
bool isAVFoundationAvailable();
#endif

#ifdef Q_OS_WIN
class MediaFoundationPlayer;
IVideoPlayer* createMediaFoundationPlayer(QObject *parent);
bool isMediaFoundationAvailable();
#endif

#ifdef SNAPTRAY_ENABLE_LINUX_RECORDING
IVideoPlayer* createFFmpegPlayer(QObject* parent);
#endif

IVideoPlayer* IVideoPlayer::create(QObject *parent)
{
#ifdef Q_OS_MAC
    if (isAVFoundationAvailable()) {
        qDebug() << "IVideoPlayer: Using AVFoundation player";
        return createAVFoundationPlayer(parent);
    }
    qWarning() << "IVideoPlayer: AVFoundation not available";
#endif

#ifdef Q_OS_WIN
    if (isMediaFoundationAvailable()) {
        qDebug() << "IVideoPlayer: Using Media Foundation player";
        return createMediaFoundationPlayer(parent);
    }
    qWarning() << "IVideoPlayer: Media Foundation not available";
#endif

#ifdef SNAPTRAY_ENABLE_LINUX_RECORDING
    return createFFmpegPlayer(parent);
#endif
    qWarning() << "IVideoPlayer: No video player implementation available";
    return nullptr;
}
