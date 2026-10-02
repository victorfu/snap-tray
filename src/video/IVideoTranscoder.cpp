#include "video/IVideoTranscoder.h"

#ifdef Q_OS_MACOS
std::unique_ptr<IVideoTranscoder> createAVFoundationTranscoder();
#endif

std::unique_ptr<IVideoTranscoder> IVideoTranscoder::create()
{
#if defined(Q_OS_MACOS)
    return createAVFoundationTranscoder();
#else
    return nullptr;
#endif
}
