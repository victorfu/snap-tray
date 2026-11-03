#pragma once

class QImage;
#include <QRect>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>
#include <vector>

#include "recording/AudioTracks.h"
#include "recording/TimelineEdit.h"

// Quality used for preview edits; the quality the preview has always exported at.
constexpr int kDefaultTranscodeQuality = 80;

// Output audio may fall short of the source audio's coverage of the selected
// range by this much before it counts as lost audio. Edge packets are copied
// or dropped whole (a 1024-sample AAC packet is 21.3 ms at 48 kHz, 23.2 ms at
// 44.1 kHz), so a range the source audio only grazes may legitimately export
// without audio. Shared by the native transcoders and the preview backend so
// all of them judge "lost audio" the same way.
constexpr qint64 kAudioCoverageToleranceMs = 50;

// Probe codec identifiers. The smart-save rule only trusts the H.264 bitrate
// formula, so anything else re-encodes.
constexpr auto kVideoCodecH264 = "h264";
constexpr auto kVideoCodecHevc = "hevc";

struct VideoTranscodeRequest {
    QString inputPath;
    QString outputPath;
    qint64 startMs = 0;
    qint64 endMs = -1;      // -1 = end of input
    QRect cropRect;         // video pixels, even-aligned; empty = full frame
    int videoBitrate = 0;   // bits/s; 0 = VideoBitrate::forQuality(output size, fps, kDefaultTranscodeQuality)
    // #190 output options; only for transcoders whose supportsOutputOptions()
    // is true. The frame is cropped, then filtered (frameFilter may already
    // scale it), then scaled to outputSize.
    QSize outputSize;       // even sides, at most the filtered size; empty = the filtered size
    int frameRate = 0;      // at most the source rate; 0 = the source rate
    QString videoCodec;     // kVideoCodecH264 or kVideoCodecHevc; empty = H.264
    // #185: the kept parts in source order, each at its speed, joined in the
    // output; only for transcoders whose supportsSegments() is true. Empty =
    // startMs..endMs at normal speed. Audio is kept for parts at normal speed
    // and silent for the others; the result stays in sync at every seam.
    std::vector<SnapTray::TimelineSegment> segments;
    // #189: mix the separate tracks recorded next to this video at these
    // settings, instead of copying its mixed track. Empty = as recorded.
    // Only for transcoders whose supportsAudioMix() is true.
    QString audioTracksVideoPath;
    SnapTray::AudioMixSettings audioMix;
    // Draws on each output frame (already cropped, RGB32) before it is
    // encoded; sourceMs is the frame's time in the input. Empty = none.
    // Only for transcoders whose supportsFrameFilter() is true.
    std::function<void(QImage& frame, qint64 sourceMs)> frameFilter;
    // The size of the frames frameFilter returns when it changes it (the
    // frame around an export, #184); empty = the crop size. Every transcoder
    // that supports frameFilter must accept it.
    QSize filterOutputSize;
};

struct VideoTranscodeResult {
    bool success = false;
    // The output carries source audio. False when the source has none, or when
    // the selected range lies outside the source audio (video-only export).
    bool audioCopied = false;
    // Source time the output starts at. The requested start where the writer
    // keeps per-frame timing (AVFoundation); where the container is constant
    // frame rate (Media Foundation) it is the start of the frame shown at the
    // requested start, so at most one frame earlier. Audio shares this origin.
    qint64 startMs = 0;
    QString errorMessage;
};

struct VideoFileProbe {
    bool valid = false;
    QSize videoSize;
    qint64 durationMs = 0;
    bool hasAudio = false;
    // Presentation range of the first audio track; -1/-1 without audio.
    qint64 audioStartMs = -1;
    qint64 audioEndMs = -1;
    double frameRate = 0.0;   // nominal fps of the first video track; 0 when unknown
    QString videoCodec;       // "h264", "hevc", or empty when unknown
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
    // An input with audio may succeed only if the audio inside the selected
    // range is preserved in the output; a range the source audio does not
    // reach (see kAudioCoverageToleranceMs) exports video only, as the source
    // is there.
    virtual VideoTranscodeResult transcode(const VideoTranscodeRequest& request,
                                           const ProgressCallback& progress) = 0;
    virtual VideoFileProbe probe(const QString& filePath) = 0;
    // True when transcode() applies VideoTranscodeRequest::frameFilter.
    virtual bool supportsFrameFilter() const { return false; }
    // True when transcode() applies outputSize and frameRate (#190).
    virtual bool supportsOutputOptions() const { return false; }
    // True when transcode() joins VideoTranscodeRequest::segments (#185).
    virtual bool supportsSegments() const { return false; }
    // True when transcode() mixes VideoTranscodeRequest::audioTracksVideoPath (#189).
    virtual bool supportsAudioMix() const { return false; }
    // True when transcode() can write videoCodec (H.264 is always supported).
    virtual bool supportsVideoCodec(const QString& codec) const
    {
        return codec.isEmpty() || codec == QLatin1String(kVideoCodecH264);
    }

    // nullptr on platforms/builds without a recording backend.
    static std::unique_ptr<IVideoTranscoder> create();
};
