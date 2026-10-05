#include "video/IVideoTranscoder.h"

#ifdef Q_OS_MACOS
std::unique_ptr<IVideoTranscoder> createAVFoundationTranscoder();
#endif
#ifdef Q_OS_WIN
std::unique_ptr<IVideoTranscoder> createMediaFoundationTranscoder();
#endif

#ifdef SNAPTRAY_ENABLE_LINUX_RECORDING
std::unique_ptr<IVideoTranscoder> createFFmpegTranscoder();
#endif

std::unique_ptr<IVideoTranscoder> IVideoTranscoder::create()
{
#if defined(Q_OS_MACOS)
    return createAVFoundationTranscoder();
#elif defined(Q_OS_WIN)
    return createMediaFoundationTranscoder();
#elif defined(SNAPTRAY_ENABLE_LINUX_RECORDING)
    return createFFmpegTranscoder();
#else
    return nullptr;
#endif
}
