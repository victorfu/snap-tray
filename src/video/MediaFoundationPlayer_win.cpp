#include "video/IVideoPlayer.h"

#ifdef Q_OS_WIN

#include <QDebug>

// Stub implementation for MediaFoundation video player
// TODO: Implement full MediaFoundation-based video playback

IVideoPlayer* createMediaFoundationPlayer(QObject *parent)
{
    Q_UNUSED(parent)
    qWarning() << "MediaFoundationPlayer: Not yet implemented";
    return nullptr;
}

bool isMediaFoundationAvailable()
{
    // Return false until implementation is complete
    return false;
}

#endif // Q_OS_WIN
