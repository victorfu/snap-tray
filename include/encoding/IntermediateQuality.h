#pragma once

#include "encoding/VideoBitrate.h"
#include "video/IVideoTranscoder.h"

#include <QLatin1String>
#include <QSize>
#include <QtGlobal>

// Phase 3 policy: every number behind "record a high-quality intermediate when
// the preview is on, then save at the user's quality" lives here, as pure
// functions, so RecordingManager, the encoders and the preview backend agree.
namespace SnapTray::IntermediateQuality {

// Quality target handed to encoders that support constant-quality rate
// control (0-100 on both CODECAPI_AVEncCommonQuality and VideoToolbox).
constexpr int kConstantQualityValue = 85;
// Short GOPs keep preview seeking responsive and trims accurate.
constexpr int kKeyFrameIntervalSeconds = 1;
// VBR fallback / bitrate ceiling for the intermediate: the spec's 0.5-0.6 bpp.
constexpr double kIntermediateBitsPerPixel = 0.55;
constexpr int kIntermediateMaxBitrate = 100000000; // ~100 Mbps
// Free space needed at start, in minutes of intermediate recording, before
// falling back to the user's quality.
constexpr int kMinimumRecordingMinutes = 10;
// Used when a probe cannot report a frame rate.
constexpr int kFallbackFrameRate = 30;

constexpr qint64 kBitsPerByte = 8;
constexpr qint64 kMsPerSecond = 1000;
constexpr qint64 kSecondsPerMinute = 60;

inline int intermediateBitrate(const QSize& frameSize, int frameRate)
{
    const double bitrate = double(frameSize.width()) * double(frameSize.height())
                           * double(qMax(1, frameRate)) * kIntermediateBitsPerPixel;
    return static_cast<int>(qBound<double>(VideoBitrate::kMinBitrate, bitrate, kIntermediateMaxBitrate));
}

inline qint64 estimatedBytesPerMinute(int bitrate)
{
    return qint64(bitrate) / kBitsPerByte * kSecondsPerMinute;
}

inline bool hasRoomForIntermediate(qint64 freeBytes, const QSize& frameSize, int frameRate)
{
    if (freeBytes <= 0) {
        return false;
    }
    const qint64 needed = estimatedBytesPerMinute(intermediateBitrate(frameSize, frameRate))
                          * kMinimumRecordingMinutes;
    return freeBytes >= needed;
}

// Total (video + audio + container) average bitrate in bits/s; 0 when the
// inputs cannot support an estimate. Deliberately conservative: it includes
// audio, so a borderline file re-encodes rather than moves.
inline qint64 averageBitrate(qint64 fileBytes, qint64 durationMs)
{
    if (fileBytes <= 0 || durationMs <= 0) {
        return 0;
    }
    return fileBytes * kBitsPerByte * kMsPerSecond / durationMs;
}

inline int effectiveFrameRate(double probedFps)
{
    const int rounded = qRound(probedFps);
    return rounded >= 1 ? rounded : kFallbackFrameRate;
}

// The output quality contract: the H.264 bitrate for the final dimensions at
// the source frame rate and the user's quality (clamped by forQuality).
inline int outputBitrateFor(const QSize& outputSize, double probedFps, int userQuality)
{
    return VideoBitrate::forQuality(outputSize, effectiveFrameRate(probedFps), userQuality);
}

// Smart rule for an unedited save: move the intermediate only when it is
// H.264 (the only codec the bitrate formula describes) and already at or
// below the user's target. Anything unknown re-encodes.
inline bool smartSaveShouldMove(const VideoFileProbe& probe, qint64 fileBytes, int userQuality)
{
    if (!probe.valid || probe.videoCodec != QLatin1String(kVideoCodecH264)) {
        return false;
    }
    const qint64 actual = averageBitrate(fileBytes, probe.durationMs);
    if (actual <= 0) {
        return false;
    }
    return actual <= outputBitrateFor(probe.videoSize, probe.frameRate, userQuality);
}

} // namespace SnapTray::IntermediateQuality
