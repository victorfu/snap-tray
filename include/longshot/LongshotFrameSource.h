#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

namespace SnapTray::Longshot {

// Sequential, cropped frame access over a trim range of a recording.
// Frames are QImage::Format_RGB32 in crop-local coordinates. Implementations
// decode in media-time order and never hold more than a few frames.
class LongshotFrameSource
{
public:
    virtual ~LongshotFrameSource() = default;

    // startMs inclusive, endMs exclusive (-1 = end of media). `crop` is in
    // video pixels (empty = full frame) and is normalized the same way the
    // preview normalizes its crop (even alignment, minimum side).
    virtual bool open(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop) = 0;
    // Next frame in time order; nullopt at the end of the range or on error
    // (see lastError()). *tMs receives the frame's media time.
    virtual std::optional<QImage> next(qint64* tMs) = 0;
    virtual QSize frameSize() const = 0;        // crop size
    virtual QSize videoSize() const = 0;        // full decoded frame size (for crop normalization keys)
    virtual double frameRate() const = 0;
    virtual int expectedFrameCount() const = 0; // frames in the range at the nominal rate
    virtual QString lastError() const = 0;
};

} // namespace SnapTray::Longshot
