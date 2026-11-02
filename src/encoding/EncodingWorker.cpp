#include "encoding/EncodingWorker.h"
#include "recording/CapturedFrame.h"

#include <optional>
#include "IVideoEncoder.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"

#include <QDebug>
#include <QThread>
#include <QPointer>
#include <QMetaObject>
#include <exception>

EncodingWorker::EncodingWorker(QObject *parent)
    : QObject(parent)
{
}

EncodingWorker::~EncodingWorker()
{
}

void EncodingWorker::setVideoEncoder(IVideoEncoder* encoder)
{
    m_videoEncoder.reset(encoder);
}

void EncodingWorker::setGifEncoder(NativeGifEncoder* encoder)
{
    m_gifEncoder.reset(encoder);
}

void EncodingWorker::setWebPEncoder(WebPAnimationEncoder* encoder)
{
    m_webpEncoder.reset(encoder);
}

void EncodingWorker::setEncoderType(EncoderType type)
{
    m_encoderType = type;
}

void EncodingWorker::moveOwnedEncodersToThread(QThread* targetThread)
{
    if (!targetThread) {
        return;
    }

    if (QThread::currentThread() == thread()) {
        if (m_videoEncoder) {
            m_videoEncoder->moveToThread(targetThread);
        }
        if (m_gifEncoder) {
            m_gifEncoder->moveToThread(targetThread);
        }
        if (m_webpEncoder) {
            m_webpEncoder->moveToThread(targetThread);
        }
        return;
    }

    const bool invoked = QMetaObject::invokeMethod(this, [this, targetThread]() {
        if (m_videoEncoder) {
            m_videoEncoder->moveToThread(targetThread);
        }
        if (m_gifEncoder) {
            m_gifEncoder->moveToThread(targetThread);
        }
        if (m_webpEncoder) {
            m_webpEncoder->moveToThread(targetThread);
        }
    }, Qt::BlockingQueuedConnection);

    if (!invoked) {
        qWarning() << "EncodingWorker: Failed to move encoders to target thread";
    }
}

void EncodingWorker::setWatermarkSettings(const WatermarkRenderer::Settings& settings)
{
    if (QThread::currentThread() == thread()) {
        m_watermarkSettings = settings;
        m_cachedWatermarkImage = QImage();  // Clear cache, will be regenerated
        return;
    }

    const WatermarkRenderer::Settings copiedSettings = settings;
    QMetaObject::invokeMethod(this, [this, copiedSettings]() {
        m_watermarkSettings = copiedSettings;
        m_cachedWatermarkImage = QImage();  // Clear cache, will be regenerated
    }, Qt::BlockingQueuedConnection);
}

bool EncodingWorker::start()
{
    if (QThread::currentThread() == thread()) {
        return startOnWorkerThread();
    }

    bool started = false;
    const bool invoked = QMetaObject::invokeMethod(this, [this, &started]() {
        started = startOnWorkerThread();
    }, Qt::BlockingQueuedConnection);

    if (!invoked) {
        qWarning() << "EncodingWorker: Failed to invoke start() on worker thread";
        return false;
    }

    return started;
}

bool EncodingWorker::startOnWorkerThread()
{
    Q_ASSERT(QThread::currentThread() == thread());

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
    if (m_encoderType == EncoderType::WebP && !m_webpEncoder) {
        qWarning() << "EncodingWorker: No WebP encoder set";
        return false;
    }

    m_running = true;
    m_acceptingFrames = true;
    m_finishRequested = false;
    m_finishCalled = false;
    m_isProcessing = false;
    m_framesWritten = 0;
    m_wasNearFull = false;

    // Clear any stale data from previous session
    {
        QMutexLocker locker(&m_queueMutex);
        clearQueuesLocked();
    }

    return true;
}

void EncodingWorker::clearQueuesLocked()
{
    // The byte count belongs to the queued frames: dropping them drops it.
    m_frameQueue.clear();
    m_audioQueue.clear();
    m_queuedBytes = 0;
}

void EncodingWorker::stop()
{
    m_acceptingFrames = false;
    m_finishRequested = false;
    m_running = false;

    if (QThread::currentThread() == thread()) {
        stopOnWorkerThread();
        return;
    }

    const bool invoked = QMetaObject::invokeMethod(this, [this]() {
        stopOnWorkerThread();
    }, Qt::BlockingQueuedConnection);

    if (!invoked) {
        qWarning() << "EncodingWorker: Failed to invoke stop() on worker thread";
    }
}

void EncodingWorker::stopOnWorkerThread()
{
    Q_ASSERT(QThread::currentThread() == thread());

    m_acceptingFrames = false;
    m_finishRequested = false;
    m_running = false;
    m_isProcessing = false;
    m_wasNearFull = false;

    // Clear queues
    {
        QMutexLocker locker(&m_queueMutex);
        clearQueuesLocked();
    }

    // Abort encoders. This runs in the worker affinity thread.
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

void EncodingWorker::requestFinish()
{
    if (QThread::currentThread() == thread()) {
        requestFinishOnWorkerThread();
        return;
    }

    QPointer<EncodingWorker> guard(this);
    const bool invoked = QMetaObject::invokeMethod(this, [guard]() {
        if (guard) {
            guard->requestFinishOnWorkerThread();
        }
    }, Qt::QueuedConnection);

    if (!invoked) {
        qWarning() << "EncodingWorker: Failed to invoke requestFinish() on worker thread";
    }
}

void EncodingWorker::requestFinishOnWorkerThread()
{
    Q_ASSERT(QThread::currentThread() == thread());

    m_acceptingFrames = false;

    // The worker is already in a terminal state (stopped or failed).
    if (!m_running.load() || m_finishCalled.load()) {
        m_finishRequested = false;
        return;
    }

    m_finishRequested = true;

    bool hasPendingWork = false;
    {
        QMutexLocker locker(&m_queueMutex);
        hasPendingWork = !m_audioQueue.isEmpty() || !m_frameQueue.isEmpty();
    }

    if (hasPendingWork) {
        scheduleProcessing();
        return;
    }

    // Avoid inline finalize on the caller path: always queue finish on worker thread.
    QPointer<EncodingWorker> guard(this);
    QMetaObject::invokeMethod(this, [guard]() {
        if (guard) {
            guard->finishEncoder();
        }
    }, Qt::QueuedConnection);
}

void EncodingWorker::setQueueBudget(const QueueBudget& budget)
{
    QMutexLocker locker(&m_queueMutex);
    m_budget = budget;
    m_budget.maxFrames = qMax(1, m_budget.maxFrames);
}

void EncodingWorker::setHealth(std::shared_ptr<SnapTray::RecordingHealth> health)
{
    QMutexLocker locker(&m_queueMutex);
    m_health = std::move(health);
}

qint64 EncodingWorker::encoderFramesWritten() const
{
    if (m_encoderType == EncoderType::Video && m_videoEncoder) return m_videoEncoder->framesWritten();
    if (m_encoderType == EncoderType::Gif && m_gifEncoder) return m_gifEncoder->framesWritten();
    if (m_encoderType == EncoderType::WebP && m_webpEncoder) return m_webpEncoder->framesWritten();
    return 0;
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
    int maxFrames;
    qint64 queuedBytes;
    const qint64 frameBytes = frame.frame.sizeInBytes();
    const qint64 nowNs = SnapTray::captureClockNowNs();
    using Drop = SnapTray::RecordingHealth::FrameDrop;
    std::optional<Drop> drop;
    {
        QMutexLocker locker(&m_queueMutex);
        if (m_health) m_health->frameOffered();
        maxFrames = m_budget.maxFrames;
        if (m_frameQueue.size() >= m_budget.maxFrames) {
            drop = Drop::QueueFull;
        } else if (!m_frameQueue.isEmpty() && m_queuedBytes + frameBytes > m_budget.maxBytes) {
            drop = Drop::QueueBytes;
        } else if (!m_frameQueue.isEmpty()
                   && nowNs - m_frameQueue.head().enqueuedNs > m_budget.maxAgeMs * 1000000) {
            drop = Drop::QueueStale;
        } else {
            // QImage uses implicit sharing (COW), no deep copy here
            FrameData queued = frame;
            queued.enqueuedNs = nowNs;
            m_frameQueue.enqueue(queued);
            m_queuedBytes += frameBytes;
        }
        depth = m_frameQueue.size();
        queuedBytes = m_queuedBytes;
        if (m_health) {
            if (drop) m_health->frameDropped(*drop);
            else m_health->frameQueued(depth, queuedBytes);
        }
    }
    if (drop) {
        return false;
    }

    // Emit queue pressure signals outside lock
    if (depth >= maxFrames * QUEUE_NEAR_FULL_PERCENT / 100 && !m_wasNearFull.exchange(true)) {
        emit queuePressure(depth, maxFrames);
    }

    scheduleProcessing();

    return true;
}

void EncodingWorker::writeAudioSamples(const QByteArray& data, qint64 startFrame)
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
            // Drop rather than block the capture thread. Packets carry their
            // sample position, so the encoder leaves a gap instead of
            // compressing time.
            if (m_health) m_health->audioDropped(data.size());
            return;
        }
        m_audioQueue.enqueue({data, startFrame});
        if (m_health) m_health->audioQueued();
    }

    scheduleProcessing();
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

void EncodingWorker::scheduleProcessing()
{
    if (!m_running.load() || m_finishCalled.load()) {
        return;
    }

    // m_isProcessing guards both "scheduled" and "actively processing".
    if (m_isProcessing.exchange(true)) {
        return;
    }

    const bool invoked = QMetaObject::invokeMethod(this, [this]() {
        processNextFrame();
    }, Qt::QueuedConnection);

    if (!invoked) {
        m_isProcessing = false;
        qWarning() << "EncodingWorker: Failed to schedule processing task";
    }
}

void EncodingWorker::processNextFrame()
{
    Q_ASSERT(QThread::currentThread() == thread());

    while (true) {
        FrameData frameData;
        AudioData audioData;
        bool hasFrame = false;
        bool hasAudio = false;
        int frameDepth = 0;
        bool queueBecameEmpty = false;
        bool shouldFinish = false;

        // Get next item from queues (prioritize audio to prevent buffer buildup)
        {
            QMutexLocker locker(&m_queueMutex);

            if (!m_running.load()) {
                m_isProcessing = false;
                return;
            } else if (!m_audioQueue.isEmpty()) {
                audioData = m_audioQueue.dequeue();
                hasAudio = true;
            } else if (!m_frameQueue.isEmpty()) {
                frameData = m_frameQueue.dequeue();
                m_queuedBytes = qMax<qint64>(0, m_queuedBytes - frameData.frame.sizeInBytes());
                frameDepth = m_frameQueue.size();
                hasFrame = true;
            } else {
                m_isProcessing = false;
                queueBecameEmpty = true;
                shouldFinish = m_finishRequested.exchange(false);
            }
        }

        if (queueBecameEmpty) {
            if (shouldFinish && m_running.load()) {
                finishEncoder();
                return;
            }

            // Race guard: if new work arrived right after we observed an empty queue,
            // reacquire processing ownership and continue.
            bool hasPendingWork = false;
            {
                QMutexLocker locker(&m_queueMutex);
                hasPendingWork = !m_audioQueue.isEmpty() || !m_frameQueue.isEmpty();
            }
            if (hasPendingWork && m_running.load() && !m_finishCalled.load()
                && !m_isProcessing.exchange(true)) {
                continue;
            }
            return;
        } else if (hasAudio) {
            // Write audio directly to encoder (we're on worker thread)
            try {
                if (m_videoEncoder) {
                    m_videoEncoder->writeAudioSamples(audioData.data, audioData.startFrame);
                }
            } catch (const std::exception& e) {
                handleProcessingFailure(QStringLiteral("audio encoding"),
                                        QString::fromLocal8Bit(e.what()));
                return;
            } catch (...) {
                handleProcessingFailure(QStringLiteral("audio encoding"),
                                        QStringLiteral("unknown exception"));
                return;
            }
        } else if (hasFrame) {
            // Emit queue low signal outside lock
            int maxFrames;
            {
                QMutexLocker locker(&m_queueMutex);
                maxFrames = m_budget.maxFrames;
            }
            if (frameDepth <= maxFrames * QUEUE_LOW_PERCENT / 100 && m_wasNearFull.exchange(false)) {
                QPointer<EncodingWorker> guard(this);
                QMetaObject::invokeMethod(this, [guard, frameDepth, maxFrames]() {
                    if (guard) {
                        emit guard->queueLow(frameDepth, maxFrames);
                    }
                }, Qt::QueuedConnection);
            }

            // Process the frame (heavy work)
            doProcessFrame(frameData);
            if (!m_running.load()) {
                m_isProcessing = false;
                return;
            }
        }
    }
}

void EncodingWorker::doProcessFrame(const FrameData& frameData)
{
    try {
        // Write to encoder. Every encoder counts accepted frames synchronously
        // in writeFrame(), so an unchanged count means it rejected the frame.
        const qint64 writtenBefore = encoderFramesWritten();
        const qint64 encodeStartNs = SnapTray::captureClockNowNs();
        const bool wroteNative = m_encoderType == EncoderType::Video && m_videoEncoder
            && writeNativeFrame(frameData);
        if (!wroteNative) {
            // Make a copy for modification (triggers COW only now)
            QImage frame = frameData.frame;

            // Apply watermark if enabled
            if (m_watermarkSettings.enabled) {
                applyWatermark(frame);
            }

            if (m_encoderType == EncoderType::Video && m_videoEncoder) {
                m_videoEncoder->writeFrame(frame, frameData.timestampMs);
            } else if (m_encoderType == EncoderType::Gif && m_gifEncoder) {
                m_gifEncoder->writeFrame(frame, frameData.timestampMs);
            } else if (m_encoderType == EncoderType::WebP && m_webpEncoder) {
                m_webpEncoder->writeFrame(frame, frameData.timestampMs);
            }
        }
        if (m_health) {
            const qint64 nowNs = SnapTray::captureClockNowNs();
            if (encoderFramesWritten() > writtenBefore) {
                m_health->frameEncoded(encodeStartNs - frameData.enqueuedNs, nowNs - encodeStartNs, nowNs);
            } else {
                m_health->frameDropped(SnapTray::RecordingHealth::FrameDrop::EncoderRejected);
            }
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
    } catch (const std::exception& e) {
        handleProcessingFailure(QStringLiteral("frame encoding"),
                                QString::fromLocal8Bit(e.what()));
    } catch (...) {
        handleProcessingFailure(QStringLiteral("frame encoding"),
                                QStringLiteral("unknown exception"));
    }
}

bool EncodingWorker::writeNativeFrame(const FrameData& frameData)
{
    using Path = SnapTray::RecordingHealth::FramePath;
    std::optional<Path> imagePath;
    if (!frameData.nativeBuffer) {
        imagePath = Path::NoNativeBuffer;
    } else if (m_watermarkSettings.enabled) {
        // The watermark is composited on the CPU image; the surface lacks it.
        imagePath = Path::Watermark;
    } else if (!m_nativeFramesEnabled.load()) {
        imagePath = Path::Disabled;
    } else if (!m_videoEncoder->writeNativeFrame(frameData.nativeBuffer, frameData.timestampMs)) {
        imagePath = Path::Incompatible;
    }
    if (m_health) {
        m_health->framePath(imagePath.value_or(Path::Native));
    }
    return !imagePath;
}

void EncodingWorker::handleProcessingFailure(const QString& context, const QString& details)
{
    const bool wasRunning = m_running.exchange(false);
    m_acceptingFrames = false;
    m_finishRequested = false;
    m_finishCalled = true;
    m_isProcessing = false;
    m_wasNearFull = false;

    {
        QMutexLocker locker(&m_queueMutex);
        clearQueuesLocked();
    }

    // Another path already stopped this worker; avoid duplicate error emissions.
    if (!wasRunning) {
        return;
    }

    const QString message = QStringLiteral("Encoding worker %1 failed: %2")
                                .arg(context, details);
    qWarning() << "EncodingWorker:" << message;

    QPointer<EncodingWorker> guard(this);
    QMetaObject::invokeMethod(this, [guard, message]() {
        if (guard) {
            emit guard->error(message);
        }
    }, Qt::QueuedConnection);
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
    Q_ASSERT(QThread::currentThread() == thread());

    // Worker was stopped/cancelled before finalize task ran.
    if (!m_running.load()) {
        m_finishCalled = true;
        return;
    }

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

    } else if (m_encoderType == EncoderType::WebP && m_webpEncoder) {
        // Connect to encoder's finished signal before calling finish()
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_webpEncoder.get(), &WebPAnimationEncoder::finished,
                        this, [this, conn](bool s, const QString& path) {
            QObject::disconnect(*conn);
            m_running = false;
            emit finished(s, path);
        });

        m_webpEncoder->finish();
    } else {
        // No encoder, emit failure
        m_running = false;
        emit finished(false, QString());
    }
}
