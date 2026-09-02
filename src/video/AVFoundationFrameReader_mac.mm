#include "video/IVideoFrameReader.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <QTransform>

#include <cmath>
#include <limits>

namespace {

class AVFoundationFrameReader final : public IVideoFrameReader
{
public:
    ~AVFoundationFrameReader() override { reset(); }

    bool load(const QString& filePath) override
    {
        @autoreleasepool {
            reset();
            m_error.clear();
            NSURL* url = filePath.startsWith(QStringLiteral("file://"))
                ? [NSURL URLWithString:filePath.toNSString()]
                : [NSURL fileURLWithPath:filePath.toNSString()];
            AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
            AVAssetTrack* track = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
            const double durationSeconds = CMTimeGetSeconds(asset.duration);
            if (!track || !std::isfinite(durationSeconds) || durationSeconds <= 0) {
                m_error = QStringLiteral("No readable video track");
                return false;
            }

            const CGAffineTransform transform = track.preferredTransform;
            m_transform = QTransform(transform.a, transform.b, transform.c,
                                     transform.d, transform.tx, transform.ty);
            m_size = m_transform.mapRect(QRectF(0, 0, track.naturalSize.width,
                                                track.naturalSize.height)).size().toSize();
            m_duration = static_cast<qint64>(std::llround(durationSeconds * 1000.0));
            m_frameRate = track.nominalFrameRate > 0 ? track.nominalFrameRate : 30.0;

            // AVAssetReader synthesizes a black image for an empty leading edit.
            // Begin at real media instead, retaining its target timeline so
            // frameAt() can extend the first image across the empty lead-in.
            CMTime contentStart = kCMTimeInvalid;
            for (AVAssetTrackSegment* segment in track.segments) {
                const CMTimeRange target = segment.timeMapping.target;
                if (!segment.isEmpty && CMTIME_IS_NUMERIC(target.start)
                    && CMTIME_IS_NUMERIC(target.duration)
                    && CMTimeCompare(target.duration, kCMTimeZero) > 0
                    && (!CMTIME_IS_NUMERIC(contentStart)
                        || CMTimeCompare(target.start, contentStart) < 0)) {
                    contentStart = target.start;
                }
            }
            if (track.segments.count == 0) {
                contentStart = track.timeRange.start;
            }
            if (!CMTIME_IS_NUMERIC(contentStart)
                || CMTimeCompare(contentStart, asset.duration) >= 0) {
                m_error = QStringLiteral("Video track contains no readable media segments");
                return false;
            }

            NSError* error = nil;
            m_reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
            if (!m_reader) {
                setError(error, QStringLiteral("Failed to create video reader"));
                return false;
            }
            m_output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:@{
                (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
            }];
            m_output.alwaysCopiesSampleData = NO;
            if (![m_reader canAddOutput:m_output]) {
                m_error = QStringLiteral("Failed to configure video reader output");
                return false;
            }
            [m_reader addOutput:m_output];
            m_reader.timeRange = CMTimeRangeFromTimeToTime(contentStart, asset.duration);
            if (![m_reader startReading]) {
                setError(m_reader.error, QStringLiteral("Failed to start video reader"));
                return false;
            }
            return !m_size.isEmpty();
        }
    }

    QImage frameAt(qint64 positionMs) override
    {
        @autoreleasepool {
            if (!m_reader || !m_error.isEmpty() || positionMs < m_lastPosition) {
                if (m_error.isEmpty()) {
                    m_error = QStringLiteral("Video reader requires ascending timestamps");
                }
                return {};
            }
            m_lastPosition = positionMs;
            const CMTime requestedTime = CMTimeMake(qMax<qint64>(0, positionMs), 1000);

            // Keep one decoded frame and one look-ahead sample. The first frame
            // also covers a recording's empty lead-in before its first sample.
            while (readNextSample()) {
                const CMTime sampleTime = CMSampleBufferGetPresentationTimeStamp(m_nextSample);
                if (!CMTIME_IS_NUMERIC(sampleTime)) {
                    m_error = QStringLiteral("Video frame has an invalid timestamp");
                    return {};
                }
                if (!m_currentFrame.isNull() && CMTimeCompare(sampleTime, requestedTime) > 0) {
                    break;
                }
                m_currentFrame = copyFrame(m_nextSample);
                CFRelease(m_nextSample);
                m_nextSample = nullptr;
                if (m_currentFrame.isNull()) {
                    return {};
                }
            }

            if (!m_error.isEmpty()) {
                return {};
            }
            if (m_currentFrame.isNull()) {
                m_error = QStringLiteral("Video track contains no decoded frames");
            }
            return m_currentFrame;
        }
    }

    QSize videoSize() const override { return m_size; }
    qint64 duration() const override { return m_duration; }
    double frameRate() const override { return m_frameRate; }
    QString lastError() const override { return m_error; }

private:
    void reset()
    {
        if (m_nextSample) {
            CFRelease(m_nextSample);
            m_nextSample = nullptr;
        }
        if (m_reader.status == AVAssetReaderStatusReading) {
            [m_reader cancelReading];
        }
        m_reader = nil;
        m_output = nil;
        m_currentFrame = {};
        m_size = {};
        m_duration = 0;
        m_frameRate = 0;
        m_lastPosition = -1;
        m_atEnd = false;
    }

    void setError(NSError* error, const QString& fallback)
    {
        m_error = error ? QString::fromNSString(error.localizedDescription) : fallback;
    }

    bool readNextSample()
    {
        if (m_nextSample) {
            return true;
        }
        if (m_atEnd) {
            return false;
        }
        m_nextSample = [m_output copyNextSampleBuffer];
        if (!m_nextSample) {
            m_atEnd = true;
            if (m_reader.status == AVAssetReaderStatusFailed
                || m_reader.status == AVAssetReaderStatusCancelled) {
                setError(m_reader.error, QStringLiteral("Failed to decode video frame"));
            }
        }
        return m_nextSample != nullptr;
    }

    QImage copyFrame(CMSampleBufferRef sample)
    {
        CVPixelBufferRef buffer = CMSampleBufferGetImageBuffer(sample);
        if (!buffer || CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly)
                           != kCVReturnSuccess) {
            m_error = QStringLiteral("Failed to access decoded video pixels");
            return {};
        }
        const size_t width = CVPixelBufferGetWidth(buffer);
        const size_t height = CVPixelBufferGetHeight(buffer);
        const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
        const auto* pixels = static_cast<const uchar*>(CVPixelBufferGetBaseAddress(buffer));
        QImage frame;
        if (pixels && width <= std::numeric_limits<int>::max()
            && height <= std::numeric_limits<int>::max()
            && stride <= std::numeric_limits<int>::max()) {
            frame = QImage(pixels, static_cast<int>(width), static_cast<int>(height),
                           static_cast<int>(stride), QImage::Format_ARGB32).copy();
        }
        CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
        if (frame.isNull()) {
            m_error = QStringLiteral("Failed to copy decoded video frame");
            return {};
        }
        return m_transform.isIdentity() ? frame : frame.transformed(m_transform);
    }

    AVAssetReader* m_reader = nil;
    AVAssetReaderTrackOutput* m_output = nil;
    CMSampleBufferRef m_nextSample = nullptr;
    QImage m_currentFrame;
    QTransform m_transform;
    QSize m_size;
    qint64 m_duration = 0;
    qint64 m_lastPosition = -1;
    double m_frameRate = 0;
    bool m_atEnd = false;
    QString m_error;
};

} // namespace

std::unique_ptr<IVideoFrameReader> createAVFoundationFrameReader()
{
    return std::make_unique<AVFoundationFrameReader>();
}
