#include "VideoTranscoderTestAudio.h"

// Linux beta has no native transcoder and skips tst_VideoTranscoder in init().
// macOS (_mac.mm) and Windows (_win.cpp) have real decoders. A platform that
// gains a transcoder must replace this stub with a real decoder, because the
// audio contract tests fail rather than skip without one.
bool decodeAudioTrack(const QString&, int, DecodedAudio*, QString* error)
{
    if (error) {
        *error = QStringLiteral("No test audio decoder on this platform");
    }
    return false;
}
