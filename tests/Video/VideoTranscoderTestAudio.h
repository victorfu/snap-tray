#pragma once

#include <QPoint>
#include <QRgb>
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

// Decodes at `sampleRate` (Windows: at the decoder's native rate if the
// Source Reader cannot resample; `out->sampleRate` is authoritative). Returns
// false and sets `error` if the file has no decodable audio track or the
// platform has no test decoder yet.
bool decodeAudioTrack(const QString& path, int sampleRate, DecodedAudio* out, QString* error);

#ifdef Q_OS_WIN
// Windows has no IVideoFrameReader (GIF/WebP keep the player fallback), so
// the tests read decoded output frames through this test-only Source Reader
// helper instead: one entry per decoded frame, with the colour of the pixel
// at `point` (video pixels, top-left origin).
struct DecodedVideoPixel {
    double timeMs = 0.0;
    QRgb color = 0;
};

bool decodeVideoPixels(const QString& path, const QPoint& point, std::vector<DecodedVideoPixel>* out,
                       QString* error);
#endif
