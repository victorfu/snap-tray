#include "encoding/EncodingWorker.h"
#include "IVideoEncoder.h"
#include "encoding/NativeGifEncoder.h"

#include <QDebug>
#include <QPainter>
#include <QtConcurrent>
#include <QMetaObject>

EncodingWorker::EncodingWorker(QObject *parent)
    : QObject(parent)
{
}

EncodingWorker::~EncodingWorker()
{
    // Wait for any pending processing to complete
    if (m_processingFuture.isRunning()) {
        m_acceptingFrames = false;
        m_processingFuture.waitForFinished();
    }
}

void EncodingWorker::setVideoEncoder(IVideoEncoder* encoder)
{
    m_videoEncoder.reset(encoder);
}

void EncodingWorker::setGifEncoder(NativeGifEncoder* encoder)
{
    m_gifEncoder.reset(encoder);
}

void EncodingWorker::setEncoderType(EncoderType type)
{
    m_encoderType = type;
}

void EncodingWorker::setWatermarkSettings(const WatermarkRenderer::Settings& settings)
{
    m_watermarkSettings = settings;
    m_cachedWatermark = QPixmap();  // Clear cache, will be regenerated
}

void EncodingWorker::setFrameSize(const QSize& size)
{
    m_frameSize = size;
}

bool EncodingWorker::start()
{
    if (m_running.load()) {
        return false;
    }

    // Validate encoder is set
    if (m_encoderType == EncoderType::Video && !m_videoEncoder) {
        qWarning() << "EncodingWorker: No video encoder set";
        return false;
    }
    if (m_encoderType == EncoderType::Gif && !m_gifEncoder) {
        qWarning() << "EncodingWorker: No GIF encoder set";
        return false;
    }

    m_running = true;
    m_acceptingFrames = true;
    m_finishRequested = false;
    m_framesWritten = 0;
    m_wasNearFull = false;

    // Clear any stale frames from previous session
    {
        QMutexLocker locker(&m_queueMutex);
        m_frameQueue.clear();
    }

    return true;
}

void EncodingWorker::stop()
{
    m_acceptingFrames = false;
    m_finishRequested = false;
    m_running = false;

    // Clear queue
    {
        QMutexLocker locker(&m_queueMutex);
        m_frameQueue.clear();
    }

    // Abort encoders
    if (m_videoEncoder) {
        m_videoEncoder->abort();
    }
    if (m_gifEncoder) {
        m_gifEncoder->abort();
    }

    // Wait for processing to finish
    if (m_processingFuture.isRunning()) {
        m_processingFuture.waitForFinished();
    }
}

void EncodingWorker::requestFinish()
{
    m_acceptingFrames = false;
    m_finishRequested = true;

    // If not currently processing, finish immediately
    if (!m_isProcessing.load()) {
        finishEncoder();
    }
    // Otherwise, processNextFrame() will call finishEncoder() when queue empties
}

bool EncodingWorker::enqueueFrame(const FrameData& frame)
{
    if (frame.frame.isNull()) {
        return false;
    }

    if (!m_acceptingFrames.load() || !m_running.load()) {
        return false;
    }

    int depth;
    {
        QMutexLocker locker(&m_queueMutex);

        if (m_frameQueue.size() >= MAX_QUEUE_SIZE) {
            return false;
        }

        // QImage uses implicit sharing (COW), no deep copy here
        m_frameQueue.enqueue(frame);
        depth = m_frameQueue.size();
    }

    // Emit queue pressure signals outside lock
    if (depth >= QUEUE_NEAR_FULL_THRESHOLD && !m_wasNearFull) {
        emit queuePressure(depth, MAX_QUEUE_SIZE);
        m_wasNearFull = true;
    }

    // Kick off processing if not already running
    if (!m_isProcessing.exchange(true)) {
        m_processingFuture = QtConcurrent::run([this]() {
            processNextFrame();
        });
    }

    return true;
}

void EncodingWorker::writeAudioSamples(const QByteArray& data, qint64 timestampMs)
{
    // Audio passes through directly to encoder
    // AVAssetWriter handles thread safety and backpressure
    if (m_running.load() && m_encoderType == EncoderType::Video && m_videoEncoder) {
        m_videoEncoder->writeAudioSamples(data, timestampMs);
    }
}

int EncodingWorker::queueDepth() const
{
    QMutexLocker locker(&m_queueMutex);
    return m_frameQueue.size();
}

bool EncodingWorker::isProcessing() const
{
    return m_isProcessing.load();
}

void EncodingWorker::processNextFrame()
{
    while (true) {
        FrameData frameData;
        int depth;

        // Get next frame from queue
        {
            QMutexLocker locker(&m_queueMutex);
            if (m_frameQueue.isEmpty()) {
                m_isProcessing = false;

                // Check if finish was requested
                if (m_finishRequested.exchange(false)) {
                    QMetaObject::invokeMethod(this, [this]() {
                        finishEncoder();
                    }, Qt::QueuedConnection);
                }
                return;
            }
            frameData = m_frameQueue.dequeue();
            depth = m_frameQueue.size();
        }

        // Emit queue low signal outside lock
        if (depth <= QUEUE_LOW_THRESHOLD && m_wasNearFull) {
            QMetaObject::invokeMethod(this, [this, depth]() {
                emit queueLow(depth, MAX_QUEUE_SIZE);
            }, Qt::QueuedConnection);
            m_wasNearFull = false;
        }

        // Process the frame (heavy work)
        doProcessFrame(frameData);
    }
}

void EncodingWorker::doProcessFrame(const FrameData& frameData)
{
    // Make a copy for modification (triggers COW only now)
    QImage frame = frameData.frame;

    // Apply watermark if enabled
    if (m_watermarkSettings.enabled) {
        applyWatermark(frame);
    }

    // Write to encoder
    if (m_encoderType == EncoderType::Video && m_videoEncoder) {
        m_videoEncoder->writeFrame(frame, frameData.timestampMs);
    } else if (m_encoderType == EncoderType::Gif && m_gifEncoder) {
        m_gifEncoder->writeFrame(frame, frameData.timestampMs);
    }

    // Update frame count
    qint64 count = ++m_framesWritten;

    // Emit progress periodically (every 30 frames = ~1 second at 30fps)
    if (count % 30 == 0) {
        QMetaObject::invokeMethod(this, [this, count]() {
            emit progress(count);
        }, Qt::QueuedConnection);
    }
}

void EncodingWorker::applyWatermark(QImage& frame)
{
    if (!m_watermarkSettings.enabled) {
        return;
    }

    // Create watermark pixmap if not cached
    if (m_cachedWatermark.isNull() && !m_watermarkSettings.imagePath.isEmpty()) {
        m_cachedWatermark = QPixmap(m_watermarkSettings.imagePath);
        if (!m_cachedWatermark.isNull() && m_watermarkSettings.imageScale != 100) {
            QSize newSize = m_cachedWatermark.size() * m_watermarkSettings.imageScale / 100;
            m_cachedWatermark = m_cachedWatermark.scaled(newSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }

    // Use WatermarkRenderer to apply
    QPixmap pixmap = QPixmap::fromImage(frame);
    pixmap = WatermarkRenderer::applyToPixmap(pixmap, m_watermarkSettings);
    frame = pixmap.toImage();
}

void EncodingWorker::finishEncoder()
{
    m_running = false;

    QString outputPath;
    bool success = false;

    if (m_encoderType == EncoderType::Video && m_videoEncoder) {
        outputPath = m_videoEncoder->outputPath();

        // Connect to encoder's finished signal before calling finish()
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_videoEncoder.get(), &IVideoEncoder::finished,
                        this, [this, conn](bool s, const QString& path) {
            QObject::disconnect(*conn);
            emit finished(s, path);
        });

        m_videoEncoder->finish();

    } else if (m_encoderType == EncoderType::Gif && m_gifEncoder) {
        outputPath = m_gifEncoder->outputPath();

        // Connect to encoder's finished signal before calling finish()
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_gifEncoder.get(), &NativeGifEncoder::finished,
                        this, [this, conn](bool s, const QString& path) {
            QObject::disconnect(*conn);
            emit finished(s, path);
        });

        m_gifEncoder->finish();
    } else {
        // No encoder, emit failure
        emit finished(false, QString());
    }
}
