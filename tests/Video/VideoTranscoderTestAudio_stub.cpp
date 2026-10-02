#include "VideoTranscoderTestAudio.h"

// Platforms without a native transcoder skip tst_VideoTranscoder in init().
// A platform that gains a transcoder must replace this stub with a real
// decoder (Windows: Media Foundation Source Reader with PCM output), because
// the audio contract tests fail rather than skip without one.
bool decodeAudioTrack(const QString&, int, DecodedAudio*, QString* error)
{
    if (error) {
        *error = QStringLiteral("No test audio decoder on this platform");
    }
    return false;
}
