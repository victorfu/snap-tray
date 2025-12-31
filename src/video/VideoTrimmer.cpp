#include "video/VideoTrimmer.h"
#include "video/IVideoPlayer.h"
#include "IVideoEncoder.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include "encoding/EncoderFactory.h"

#include <QDebug>
#include <QTimer>
#include <QFileInfo>

VideoTrimmer::VideoTrimmer(QObject *parent)
    : QObject(parent)
{
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

    // Estimate total frames (assuming 30fps)
    m_totalFrames = static_cast<int>((m_trimEnd - m_trimStart) * 30 / 1000);
    if (m_totalFrames <= 0) m_totalFrames = 1;

    // Create video player to extract frames
    m_player = IVideoPlayer::create(this);
    if (!m_player) {
        emit error(tr("Failed to create video player"));
        cleanup();
        m_running = false;
        return;
    }

    // Get video dimensions from input
    if (!m_player->load(m_inputPath)) {
        emit error(tr("Failed to load input video"));
        cleanup();
        m_running = false;
        return;
    }

    // Wait for video to load and get dimensions
    // For now, use a reasonable default; the first frame will give us actual size
    QSize videoSize(1920, 1080);  // Will be updated on first frame

    // Create encoder based on format using EncoderFactory
    EncoderFactory::EncoderConfig config;
    config.format = m_format;
    config.frameSize = videoSize;
    config.frameRate = 30;
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

    // Connect player frame signal
    connect(m_player, &IVideoPlayer::frameReady,
            this, &VideoTrimmer::onFrameReady);

    // Initialize seek position to start - we'll extract first frame at m_trimStart
    m_seekPosition = m_trimStart;
    m_currentPosition = m_trimStart;

    // Seek to start position and begin extraction
    m_player->seek(m_trimStart);

    // Wait for first frame via frameReady signal (no processNextFrame call here)
    // The frame will arrive via onFrameReady, which will then trigger processNextFrame
}

void VideoTrimmer::cancel()
{
    if (m_running) {
        qDebug() << "VideoTrimmer: Cancelled";
        m_cancelled = true;
        m_running = false;

        // Abort encoders
        if (m_videoEncoder) {
            m_videoEncoder->abort();
        }
        if (m_gifEncoder) {
            m_gifEncoder->abort();
        }
        if (m_webpEncoder) {
            m_webpEncoder->abort();
        }

        cleanup();
    }
}

void VideoTrimmer::onFrameReady(const QImage &frame)
{
    if (m_cancelled || !m_running) {
        return;
    }

    // Calculate adjusted timestamp using m_seekPosition (the position we requested)
    // This ensures correct timestamps since m_seekPosition hasn't been incremented yet
    qint64 adjustedTime = m_seekPosition - m_trimStart;

    // Write frame to encoder
    if (m_videoEncoder) {
        m_videoEncoder->writeFrame(frame, adjustedTime);
    } else if (m_gifEncoder) {
        m_gifEncoder->writeFrame(frame, adjustedTime);
    } else if (m_webpEncoder) {
        m_webpEncoder->writeFrame(frame, adjustedTime);
    }

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

    // Advance position (33ms = ~30fps) for next frame
    m_currentPosition += 33;

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
        m_player->seek(m_currentPosition);
        // Frame will be delivered via frameReady signal
    }
}

void VideoTrimmer::onEncodingFinished(bool success, const QString &path)
{
    qDebug() << "VideoTrimmer: Encoding finished, success:" << success;

    m_running = false;

    if (success) {
        emit progress(100);
        emit finished(true, path);
    } else {
        emit error(tr("Encoding failed"));
        emit finished(false, QString());
    }

    cleanup();
}

void VideoTrimmer::cleanup()
{
    if (m_player) {
        m_player->stop();
        delete m_player;
        m_player = nullptr;
    }

    // Encoders will be deleted by parent (we don't delete here as finish may still be running)
    m_videoEncoder = nullptr;
    m_gifEncoder = nullptr;
    m_webpEncoder = nullptr;
}
