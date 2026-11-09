#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

// Offline frame extraction for export. Requests must be in ascending timestamp
// order. Empty lead-in time uses the first video frame; gaps hold the last frame.
class IVideoFrameReader
{
public:
    virtual ~IVideoFrameReader() = default;

    virtual bool load(const QString& filePath) = 0;
    virtual QImage frameAt(qint64 positionMs) = 0;
    // Native edited MP4 export samples fractional-fps sources without first
    // rounding the requested time down to a millisecond.
    virtual QImage frameAtMicroseconds(qint64 positionUs) { return frameAt(positionUs / 1000); }
    // Next presentation time after the last request, or -1 at EOF. Edited
    // export uses it to retain source changes around VFR gaps and cut ends.
    virtual qint64 nextFrameTimeMicroseconds() const { return -1; }
    virtual QSize videoSize() const = 0;
    virtual qint64 duration() const = 0;
    virtual double frameRate() const = 0;
    virtual QString lastError() const = 0;

    // Offline export readers: AVFoundation, Media Foundation, or FFmpeg.
    static std::unique_ptr<IVideoFrameReader> create();

};
