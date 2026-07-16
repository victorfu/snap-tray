#include "video/VideoTrimmer.h"
#include "video/IVideoPlayer.h"
#include "IVideoEncoder.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include "encoding/EncoderFactory.h"

#include <QDebug>
#include <QTimer>
#include <QFileInfo>

namespace {
constexpr int kFrameExtractionTimeoutMs = 5000;
constexpr int kFrameEncodingTimeoutMs = 5000;
constexpr int kEncoderRetryIntervalMs = 10;
}

VideoTrimmer::VideoTrimmer(QObject *parent)
    : QObject(parent)
    , m_frameTimeout(new QTimer(this))
{
    m_frameTimeout->setSingleShot(true);
    connect(m_frameTimeout, &QTimer::timeout, this, [this]() {
        if (m_waitingForEncoder) {
            failTrim(tr("Timed out while waiting for the video encoder"));
        } else if (m_waitingForFrame) {
            failTrim(tr("Timed out while extracting a video frame"));
        }
    });
}

VideoTrimmer::~VideoTrimmer()
{
    cancel();
    cleanup();
}

void VideoTrimmer::setInputPath(const QString &path)
{
    m_inputPath = path;
}

void VideoTrimmer::setTrimRange(qint64 startMs, qint64 endMs)
{
    m_trimStart = startMs;
    m_trimEnd = endMs;
}

void VideoTrimmer::setOutputFormat(EncoderFactory::Format format)
{
    m_format = format;
}

void VideoTrimmer::setOutputPath(const QString &path)
{
    m_outputPath = path;
}

void VideoTrimmer::startTrim()
{
    if (m_running) {
        qWarning() << "VideoTrimmer: Already running";
        return;
    }

    if (m_inputPath.isEmpty() || m_outputPath.isEmpty()) {
        emit error(tr("Input or output path not set"));
        return;
    }

    if (m_trimEnd <= m_trimStart) {
        emit error(tr("Invalid trim range"));
        return;
    }

    qDebug() << "VideoTrimmer: Starting trim"
             << "from" << m_trimStart << "to" << m_trimEnd
             << "format:" << static_cast<int>(m_format);

    m_running = true;
    m_cancelled = false;
    m_currentPosition = m_trimStart;
    m_frameCount = 0;
    m_waitingForFrame = false;
    m_waitingForEncoder = false;
    m_pendingFrame = QImage();
    m_pendingTimestamp = 0;
    m_frameTimeout->stop();

    // Create video player to extract frames
    m_player = IVideoPlayer::create(this);
    if (!m_player) {
        emit error(tr("Failed to create video player"));
        cleanup();
        m_running = false;
        return;
    }

    // Connect to mediaLoaded to continue initialization after player is ready
    connect(m_player, &IVideoPlayer::mediaLoaded,
            this, &VideoTrimmer::onMediaLoaded);
    // Connect before load(): the Windows backend emits its preview frame
    // synchronously. onFrameReady intentionally ignores it until a seek is
    // requested, so extraction never depends on that load-time signal.
    connect(m_player, &IVideoPlayer::frameReady,
            this, &VideoTrimmer::onFrameReady);
    connect(m_player, &IVideoPlayer::error,
            this, [this](const QString &msg) {
        failTrim(tr("Video processing failed: %1").arg(msg));
    });

    // Start loading - onMediaLoaded will be called when ready
    if (!m_player->load(m_inputPath)) {
        emit error(tr("Failed to load input video"));
        cleanup();
        m_running = false;
        return;
    }

    // Wait for mediaLoaded signal before proceeding
}

void VideoTrimmer::onMediaLoaded()
{
    if (m_cancelled || !m_running) {
        return;
    }

    qDebug() << "VideoTrimmer: Media loaded, continuing initialization";

    if (m_format == EncoderFactory::Format::MP4 && m_player->hasAudio()) {
        failTrim(tr("MP4 trimming cannot preserve this video's audio; the original file was kept"));
        return;
    }

    // Now we can get accurate video properties
    QSize videoSize = m_player->videoSize();
    if (videoSize.isEmpty()) {
        videoSize = QSize(1920, 1080);  // Fallback
    }

    int frameIntervalMs = qMax(1, m_player->frameIntervalMs());
    m_totalFrames = static_cast<int>((m_trimEnd - m_trimStart) / frameIntervalMs);
    if (m_totalFrames <= 0) m_totalFrames = 1;

    qDebug() << "VideoTrimmer: Video size:" << videoSize
             << "frame rate:" << m_player->frameRate()
             << "total frames:" << m_totalFrames;

    // Create encoder based on format using EncoderFactory
    EncoderFactory::EncoderConfig config;
    config.format = m_format;
    config.frameSize = videoSize;
    config.frameRate = static_cast<int>(m_player->frameRate());
    config.outputPath = m_outputPath;
    config.quality = 80;

    auto result = EncoderFactory::create(config, this);

    if (!result.success) {
        emit error(tr("Failed to create encoder: %1").arg(result.errorMessage));
        cleanup();
        m_running = false;
        return;
    }

    m_videoEncoder = result.nativeEncoder;
    m_gifEncoder = result.gifEncoder;
    m_webpEncoder = result.webpEncoder;

    // Connect finished signals
    if (m_videoEncoder) {
        connect(m_videoEncoder, &IVideoEncoder::finished,
                this, &VideoTrimmer::onEncodingFinished);
    } else if (m_gifEncoder) {
        connect(m_gifEncoder, &NativeGifEncoder::finished,
                this, &VideoTrimmer::onEncodingFinished);
    } else if (m_webpEncoder) {
        connect(m_webpEncoder, &WebPAnimationEncoder::finished,
                this, &VideoTrimmer::onEncodingFinished);
    }

    // Initialize seek position to start - we'll extract first frame at m_trimStart
    m_seekPosition = m_trimStart;
    m_currentPosition = m_trimStart;

    // Seek to start position and begin extraction
    requestFrame(m_trimStart);
}

void VideoTrimmer::cancel()
{
    if (m_running) {
        qDebug() << "VideoTrimmer: Cancelled";
        m_cancelled = true;
        m_running = false;
        m_waitingForFrame = false;
        m_waitingForEncoder = false;
        m_pendingFrame = QImage();
        m_frameTimeout->stop();
        abortEncoders();
        cleanup();
    }
}

void VideoTrimmer::onFrameReady(const QImage &frame)
{
    if (m_cancelled || !m_running || !m_waitingForFrame) {
        return;
    }

    m_waitingForFrame = false;
    m_frameTimeout->stop();

    if (frame.isNull()) {
        failTrim(tr("Failed to extract a video frame"));
        return;
    }

    // Calculate adjusted timestamp using m_seekPosition (the position we requested)
    // This ensures correct timestamps since m_seekPosition hasn't been incremented yet
    qint64 adjustedTime = m_seekPosition - m_trimStart;

    submitFrame(frame, adjustedTime);
}

void VideoTrimmer::submitFrame(const QImage &frame, qint64 timestampMs)
{
    const qint64 framesBefore = encoderFramesWritten();
    if (m_videoEncoder) {
        m_videoEncoder->writeFrame(frame, timestampMs);
    } else if (m_gifEncoder) {
        m_gifEncoder->writeFrame(frame, timestampMs);
    } else if (m_webpEncoder) {
        m_webpEncoder->writeFrame(frame, timestampMs);
    }

    if (encoderFramesWritten() > framesBefore) {
        completeCurrentFrame();
        return;
    }

    // Native video encoders can apply temporary backpressure. Preserve the
    // exact frame and timestamp and retry from the event loop until accepted
    // or until the encoding timeout expires. GIF/WebP writes are synchronous,
    // so a missing frame count increment is a permanent failure for them.
    if (m_videoEncoder && m_videoEncoder->isRunning()) {
        m_pendingFrame = frame;
        m_pendingTimestamp = timestampMs;
        m_waitingForEncoder = true;
        m_frameTimeout->start(kFrameEncodingTimeoutMs);
        QTimer::singleShot(kEncoderRetryIntervalMs,
                           this, &VideoTrimmer::retryPendingFrame);
        return;
    }

    failTrim(tr("Failed to encode a video frame"));
}

void VideoTrimmer::retryPendingFrame()
{
    if (!m_running || m_cancelled || !m_waitingForEncoder) {
        return;
    }

    if (!m_videoEncoder || !m_videoEncoder->isRunning()) {
        failTrim(tr("Failed to encode a video frame"));
        return;
    }

    const qint64 framesBefore = m_videoEncoder->framesWritten();
    m_videoEncoder->writeFrame(m_pendingFrame, m_pendingTimestamp);
    if (m_videoEncoder->framesWritten() > framesBefore) {
        completeCurrentFrame();
        return;
    }

    QTimer::singleShot(kEncoderRetryIntervalMs,
                       this, &VideoTrimmer::retryPendingFrame);
}

void VideoTrimmer::completeCurrentFrame()
{
    m_waitingForEncoder = false;
    m_pendingFrame = QImage();
    m_pendingTimestamp = 0;
    m_frameTimeout->stop();
    m_frameCount++;

    // Update progress
    int percent = (m_frameCount * 100) / m_totalFrames;
    percent = qBound(0, percent, 99);  // Never show 100% until finished
    emit progress(percent);

    // Process next frame
    QTimer::singleShot(0, this, &VideoTrimmer::processNextFrame);
}

void VideoTrimmer::processNextFrame()
{
    if (m_cancelled || !m_running) {
        return;
    }

    // Advance position by one frame interval
    m_currentPosition += qMax(1, m_player->frameIntervalMs());

    if (m_currentPosition >= m_trimEnd) {
        // Finished extracting frames
        qDebug() << "VideoTrimmer: Frame extraction complete, finalizing...";

        if (m_videoEncoder) {
            m_videoEncoder->finish();
        } else if (m_gifEncoder) {
            m_gifEncoder->finish();
        } else if (m_webpEncoder) {
            m_webpEncoder->finish();
        }
        return;
    }

    // Update seek position BEFORE seeking so onFrameReady knows the correct timestamp
    m_seekPosition = m_currentPosition;

    // Seek to next position and request frame
    if (m_player) {
        requestFrame(m_currentPosition);
    }
}

void VideoTrimmer::onEncodingFinished(bool success, const QString &path)
{
    qDebug() << "VideoTrimmer: Encoding finished, success:" << success;

    m_running = false;

    QFileInfo outputInfo(path);
    const bool validOutput = success && m_frameCount > 0
        && outputInfo.exists() && outputInfo.size() > 0;

    if (validOutput) {
        emit progress(100);
        emit finished(true, path);
    } else {
        emit error(tr("Encoding failed"));
        emit finished(false, QString());
    }

    cleanup();
}

void VideoTrimmer::requestFrame(qint64 positionMs)
{
    if (!m_player || !m_running || m_cancelled) {
        return;
    }

    m_seekPosition = positionMs;
    m_waitingForFrame = true;
    m_frameTimeout->start(kFrameExtractionTimeoutMs);
    m_player->seek(positionMs);
}

void VideoTrimmer::failTrim(const QString &message)
{
    if (!m_running) {
        return;
    }

    qWarning() << "VideoTrimmer:" << message;
    m_running = false;
    m_waitingForFrame = false;
    m_waitingForEncoder = false;
    m_pendingFrame = QImage();
    m_pendingTimestamp = 0;
    m_frameTimeout->stop();
    abortEncoders();
    cleanup();
    emit error(message);
}

void VideoTrimmer::abortEncoders()
{
    if (m_videoEncoder) {
        m_videoEncoder->abort();
    }
    if (m_gifEncoder) {
        m_gifEncoder->abort();
    }
    if (m_webpEncoder) {
        m_webpEncoder->abort();
    }
}

qint64 VideoTrimmer::encoderFramesWritten() const
{
    if (m_videoEncoder) {
        return m_videoEncoder->framesWritten();
    }
    if (m_gifEncoder) {
        return m_gifEncoder->framesWritten();
    }
    if (m_webpEncoder) {
        return m_webpEncoder->framesWritten();
    }
    return 0;
}

void VideoTrimmer::cleanup()
{
    m_waitingForFrame = false;
    m_waitingForEncoder = false;
    m_pendingFrame = QImage();
    m_pendingTimestamp = 0;
    m_frameTimeout->stop();
    if (m_player) {
        m_player->stop();
        m_player->deleteLater();
        m_player = nullptr;
    }

    // Encoders will be deleted by parent (we don't delete here as finish may still be running)
    m_videoEncoder = nullptr;
    m_gifEncoder = nullptr;
    m_webpEncoder = nullptr;
}
