#include "encoding/EncodingWorker.h"
#include "IVideoEncoder.h"
#include "encoding/NativeGifEncoder.h"

#include <QDebug>
#include <QPainter>
#include <QPointer>
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
    m_cachedWatermarkImage = QImage();  // Clear cache, will be regenerated
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
    m_finishCalled = false;
    m_framesWritten = 0;
    m_wasNearFull = false;

    // Clear any stale data from previous session
    {
        QMutexLocker locker(&m_queueMutex);
        m_frameQueue.clear();
        m_audioQueue.clear();
    }

    return true;
}

void EncodingWorker::stop()
{
    m_acceptingFrames = false;
    m_finishRequested = false;
    m_running = false;

    // Clear queues
    {
        QMutexLocker locker(&m_queueMutex);
        m_frameQueue.clear();
        m_audioQueue.clear();
    }

    // Wait for processing to finish BEFORE aborting encoders
    // This prevents race condition where abort() is called while
    // worker thread is in writeFrame()
    if (m_processingFuture.isRunning()) {
        m_processingFuture.waitForFinished();
    }

    // Now safe to abort encoders (no concurrent access)
    if (m_videoEncoder) {
        m_videoEncoder->abort();
    }
    if (m_gifEncoder) {
        m_gifEncoder->abort();
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
    if (!m_acceptingFrames.load() || !m_running.load()) {
        return;
    }

    if (m_encoderType != EncoderType::Video || !m_videoEncoder) {
        return;  // Audio only supported for video encoding
    }

    {
        QMutexLocker locker(&m_queueMutex);
        if (m_audioQueue.size() >= MAX_AUDIO_QUEUE_SIZE) {
            return;  // Drop audio if queue full (prevents blocking capture thread)
        }
        m_audioQueue.enqueue({data, timestampMs});
    }

    // Kick off processing if not already running
    if (!m_isProcessing.exchange(true)) {
        m_processingFuture = QtConcurrent::run([this]() {
            processNextFrame();
        });
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
        AudioData audioData;
        bool hasFrame = false;
        bool hasAudio = false;
        int frameDepth = 0;

        // Get next item from queues (prioritize audio to prevent buffer buildup)
        {
            QMutexLocker locker(&m_queueMutex);

            if (!m_audioQueue.isEmpty()) {
                audioData = m_audioQueue.dequeue();
                hasAudio = true;
            } else if (!m_frameQueue.isEmpty()) {
                frameData = m_frameQueue.dequeue();
                frameDepth = m_frameQueue.size();
                hasFrame = true;
            } else {
                m_isProcessing = false;

                // Check if finish was requested
                if (m_finishRequested.exchange(false)) {
                    QPointer<EncodingWorker> guard(this);
                    QMetaObject::invokeMethod(this, [guard]() {
                        if (guard) {
                            guard->finishEncoder();
                        }
                    }, Qt::QueuedConnection);
                }
                return;
            }
        }

        if (hasAudio) {
            // Write audio directly to encoder (we're on worker thread)
            if (m_videoEncoder) {
                m_videoEncoder->writeAudioSamples(audioData.data, audioData.timestampMs);
            }
        } else if (hasFrame) {
            // Emit queue low signal outside lock
            if (frameDepth <= QUEUE_LOW_THRESHOLD && m_wasNearFull) {
                QPointer<EncodingWorker> guard(this);
                QMetaObject::invokeMethod(this, [guard, frameDepth]() {
                    if (guard) {
                        emit guard->queueLow(frameDepth, MAX_QUEUE_SIZE);
                    }
                }, Qt::QueuedConnection);
                m_wasNearFull = false;
            }

            // Process the frame (heavy work)
            doProcessFrame(frameData);
        }
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
        QPointer<EncodingWorker> guard(this);
        QMetaObject::invokeMethod(this, [guard, count]() {
            if (guard) {
                emit guard->progress(count);
            }
        }, Qt::QueuedConnection);
    }
}

void EncodingWorker::applyWatermark(QImage& frame)
{
    if (!m_watermarkSettings.enabled) {
        return;
    }

    // Create watermark image if not cached (only happens once per recording)
    // Use QImage instead of QPixmap for thread safety (QPixmap is not thread-safe)
    if (m_cachedWatermarkImage.isNull() && !m_watermarkSettings.imagePath.isEmpty()) {
        m_cachedWatermarkImage = QImage(m_watermarkSettings.imagePath);
        if (!m_cachedWatermarkImage.isNull() && m_watermarkSettings.imageScale != 100) {
            QSize newSize = m_cachedWatermarkImage.size() * m_watermarkSettings.imageScale / 100;
            m_cachedWatermarkImage = m_cachedWatermarkImage.scaled(newSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }

    if (m_cachedWatermarkImage.isNull()) {
        return;
    }

    // Use thread-safe QImage-based rendering
    frame = WatermarkRenderer::applyToImageWithCache(frame, m_cachedWatermarkImage, m_watermarkSettings);
}

void EncodingWorker::finishEncoder()
{
    // Ensure finishEncoder() only executes once (race condition guard)
    if (m_finishCalled.exchange(true)) {
        return;
    }

    if (m_encoderType == EncoderType::Video && m_videoEncoder) {
        // Connect to encoder's finished signal before calling finish()
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_videoEncoder.get(), &IVideoEncoder::finished,
                        this, [this, conn](bool s, const QString& path) {
            QObject::disconnect(*conn);
            m_running = false;
            emit finished(s, path);
        });

        m_videoEncoder->finish();

    } else if (m_encoderType == EncoderType::Gif && m_gifEncoder) {
        // Connect to encoder's finished signal before calling finish()
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_gifEncoder.get(), &NativeGifEncoder::finished,
                        this, [this, conn](bool s, const QString& path) {
            QObject::disconnect(*conn);
            m_running = false;
            emit finished(s, path);
        });

        m_gifEncoder->finish();
    } else {
        // No encoder, emit failure
        m_running = false;
        emit finished(false, QString());
    }
}
