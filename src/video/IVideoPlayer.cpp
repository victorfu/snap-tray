#include "video/IVideoPlayer.h"
#include <QDebug>

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

    qWarning() << "IVideoPlayer: No video player implementation available";
    return nullptr;
}

bool IVideoPlayer::isAvailable()
{
#ifdef Q_OS_MAC
    return isAVFoundationAvailable();
#endif

#ifdef Q_OS_WIN
    return isMediaFoundationAvailable();
#endif

    return false;
}
