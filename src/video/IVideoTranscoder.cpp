#include "video/IVideoTranscoder.h"

#ifdef Q_OS_MACOS
std::unique_ptr<IVideoTranscoder> createAVFoundationTranscoder();
#endif
#ifdef Q_OS_WIN
std::unique_ptr<IVideoTranscoder> createMediaFoundationTranscoder();
#endif

std::unique_ptr<IVideoTranscoder> IVideoTranscoder::create()
{
#if defined(Q_OS_MACOS)
    return createAVFoundationTranscoder();
#elif defined(Q_OS_WIN)
    return createMediaFoundationTranscoder();
#else
    return nullptr;
#endif
}
