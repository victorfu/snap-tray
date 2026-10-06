#include "video/IVideoFrameReader.h"

#ifdef Q_OS_MACOS
std::unique_ptr<IVideoFrameReader> createAVFoundationFrameReader();
#endif

#ifdef SNAPTRAY_ENABLE_LINUX_RECORDING
std::unique_ptr<IVideoFrameReader> createFFmpegFrameReader();
#endif

std::unique_ptr<IVideoFrameReader> IVideoFrameReader::create()
{
#ifdef Q_OS_MACOS
    return createAVFoundationFrameReader();
#elif defined(SNAPTRAY_ENABLE_LINUX_RECORDING)
    return createFFmpegFrameReader();
#else
    return nullptr;
#endif
}
