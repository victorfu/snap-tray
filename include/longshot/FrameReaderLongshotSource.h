#pragma once

#include "longshot/LongshotFrameSource.h"

#include <memory>

class IVideoFrameReader;

namespace SnapTray::Longshot {

// Adapts the platform IVideoFrameReader (ascending-only frameAt) to the
// longshot contract: iterates media times at the nominal frame rate from
// startMs to endMs and crops each frame.
class FrameReaderLongshotSource final : public LongshotFrameSource
{
public:
    explicit FrameReaderLongshotSource(std::unique_ptr<IVideoFrameReader> reader);
    ~FrameReaderLongshotSource() override;

    // Uses IVideoFrameReader::createOffline(); nullptr where no reader exists (Linux).
    static std::unique_ptr<LongshotFrameSource> createNative();

    bool open(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop) override;
    std::optional<QImage> next(qint64* tMs) override;
    QSize frameSize() const override { return m_crop.size(); }
    QSize videoSize() const override { return m_videoSize; }
    double frameRate() const override { return m_frameRate; }
    int expectedFrameCount() const override { return m_frameCount; }
    QString lastError() const override { return m_lastError; }

private:
    std::unique_ptr<IVideoFrameReader> m_reader;
    QRect m_crop;
    QSize m_videoSize;
    double m_frameRate = 0.0;
    qint64 m_startMs = 0;
    qint64 m_endMs = 0;
    int m_frameCount = 0;
    int m_nextIndex = 0;
    QString m_lastError;
};

} // namespace SnapTray::Longshot
