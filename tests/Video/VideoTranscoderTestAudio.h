#pragma once

#include <QString>
#include <QtGlobal>

#include <vector>

// Test-only decoder for tst_VideoTranscoder: decodes the first audio track of
// a media file to PCM so tests can verify audio content, not just presence.
struct DecodedAudio {
    int sampleRate = 0;
    // Presentation time of mono[0], in frames at sampleRate.
    qint64 firstFrame = 0;
    // First channel, normalized to [-1, 1], placed by presentation time.
    std::vector<float> mono;
};

// Decodes at `sampleRate`. Returns false and sets `error` if the file has no
// decodable audio track or the platform has no test decoder yet.
bool decodeAudioTrack(const QString& path, int sampleRate, DecodedAudio* out, QString* error);
