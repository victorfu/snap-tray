#pragma once

#include <QRect>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>

// Quality used for preview edits; matches the previous VideoTrimmer export.
constexpr int kDefaultTranscodeQuality = 80;

struct VideoTranscodeRequest {
    QString inputPath;
    QString outputPath;
    qint64 startMs = 0;
    qint64 endMs = -1;      // -1 = end of input
    QRect cropRect;         // video pixels, even-aligned; empty = full frame
    int videoBitrate = 0;   // bits/s; 0 = VideoBitrate::forQuality(output size, fps, kDefaultTranscodeQuality)
};

struct VideoTranscodeResult {
    bool success = false;
    bool audioCopied = false; // Source audio preserved, including verified re-encode fallback.
    QString errorMessage;
};

struct VideoFileProbe {
    bool valid = false;
    QSize videoSize;
    qint64 durationMs = 0;
    bool hasAudio = false;
};

// Offline MP4 trim + crop + H.264 re-encode with AAC passthrough.
class IVideoTranscoder
{
public:
    // Return false to cancel. Values below 100 may come from an internal
    // worker thread (AVFoundation) or from the thread that called
    // transcode() (Media Foundation). 100 is reported once, on the calling
    // thread, only after the output has been validated; returning false
    // there still cancels and the output is removed. The callback must not
    // block on the thread that called transcode().
    using ProgressCallback = std::function<bool(int percent)>;

    virtual ~IVideoTranscoder() = default;

    // Blocking. Run off the GUI thread in production code. On failure or
    // cancellation the output file is removed; the input is never removed here.
    // An input with audio may succeed only if its audio is preserved in the output.
    virtual VideoTranscodeResult transcode(const VideoTranscodeRequest& request,
                                           const ProgressCallback& progress) = 0;
    virtual VideoFileProbe probe(const QString& filePath) = 0;

    // nullptr on platforms without a native implementation (Linux beta).
    static std::unique_ptr<IVideoTranscoder> create();
};
