#include "video/IVideoFrameReader.h"

#ifdef Q_OS_MACOS
std::unique_ptr<IVideoFrameReader> createAVFoundationFrameReader();
#endif
#ifdef Q_OS_WIN
std::unique_ptr<IVideoFrameReader> createMediaFoundationFrameReader();
#endif

std::unique_ptr<IVideoFrameReader> IVideoFrameReader::create()
{
#ifdef Q_OS_MACOS
    return createAVFoundationFrameReader();
#else
    return nullptr;
#endif
}

std::unique_ptr<IVideoFrameReader> IVideoFrameReader::createOffline()
{
#ifdef Q_OS_MACOS
    return createAVFoundationFrameReader();
#elif defined(Q_OS_WIN)
    return createMediaFoundationFrameReader();
#else
    return nullptr;
#endif
}
