#include "video/IVideoFrameReader.h"

#ifdef Q_OS_MACOS
std::unique_ptr<IVideoFrameReader> createAVFoundationFrameReader();
#endif

std::unique_ptr<IVideoFrameReader> IVideoFrameReader::create()
{
#ifdef Q_OS_MACOS
    return createAVFoundationFrameReader();
#else
    return nullptr;
#endif
}
