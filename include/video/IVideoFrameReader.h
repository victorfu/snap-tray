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
    virtual QSize videoSize() const = 0;
    virtual qint64 duration() const = 0;
    virtual double frameRate() const = 0;
    virtual QString lastError() const = 0;

    // Platforms without an offline reader retain their existing player path.
    static std::unique_ptr<IVideoFrameReader> create();
};
