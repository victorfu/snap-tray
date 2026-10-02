#pragma once

#include <QSize>
#include <QtGlobal>

// H.264 bitrate used by the recording encoders and the preview transcoder.
namespace SnapTray::VideoBitrate {

constexpr int kMinBitrate = 1000000;
constexpr int kMaxBitrate = 50000000;
constexpr double kMinBitsPerPixel = 0.1;
constexpr double kBitsPerPixelRange = 0.2;
constexpr int kMaxQuality = 100;

inline int forQuality(const QSize& frameSize, int frameRate, int quality)
{
    const double bitsPerPixel = kMinBitsPerPixel
        + (qBound(0, quality, kMaxQuality) / static_cast<double>(kMaxQuality)) * kBitsPerPixelRange;
    const double bitrate = static_cast<double>(frameSize.width()) * frameSize.height() * frameRate * bitsPerPixel;
    return static_cast<int>(qBound(static_cast<double>(kMinBitrate), bitrate, static_cast<double>(kMaxBitrate)));
}

} // namespace SnapTray::VideoBitrate
