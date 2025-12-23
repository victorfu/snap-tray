#ifndef FRAMEPROCESSORTHREAD_H
#define FRAMEPROCESSORTHREAD_H

#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QImage>
#include <atomic>

class ICaptureEngine;
class FFmpegEncoder;

/**
 * @brief Worker thread for non-blocking frame capture and encoding
 *
 * Uses a producer-consumer pattern where the main thread signals capture
 * requests and this thread performs the actual capture and encoding.
 * This keeps the UI responsive during screen recording.
 */
class FrameProcessorThread : public QThread
{
    Q_OBJECT

public:
    explicit FrameProcessorThread(QObject *parent = nullptr);
    ~FrameProcessorThread() override;

    /**
     * @brief Initialize with capture engine and encoder (call before start)
     * Ownership remains with caller - objects must outlive this thread.
     */
    void initialize(ICaptureEngine *captureEngine, FFmpegEncoder *encoder);

    /**
     * @brief Request a frame capture (non-blocking, called from main thread)
     * @return true if request was queued, false if queue is full (frame skip)
     */
    bool requestCapture();

    /**
     * @brief Get current pending capture count (for backpressure monitoring)
     */
    int pendingCaptureCount() const;

    /**
     * @brief Request graceful shutdown
     */
    void requestStop();

    /**
     * @brief Check if thread is actively processing
     */
    bool isProcessing() const { return m_processing.load(); }

    /**
     * @brief Get total frames processed
     */
    qint64 frameCount() const { return m_frameCount.load(); }

    /**
     * @brief Get total frames skipped due to backpressure
     */
    qint64 skippedCount() const { return m_skippedCount.load(); }

signals:
    /**
     * @brief Emitted when a frame was successfully encoded
     */
    void frameProcessed(qint64 frameNumber);

    /**
     * @brief Emitted when a frame was skipped due to backpressure
     */
    void frameSkipped();

    /**
     * @brief Emitted when an error occurs during capture or encoding
     */
    void processingError(const QString &message);

protected:
    void run() override;

private:
    void processNextCapture();

    ICaptureEngine *m_captureEngine = nullptr;
    FFmpegEncoder *m_encoder = nullptr;

    // Thread synchronization
    mutable QMutex m_mutex;
    QWaitCondition m_condition;

    // Capture request queue (bounded by counter)
    int m_pendingCaptures = 0;
    static constexpr int MAX_PENDING_CAPTURES = 3;  // Limit queue depth

    // State (atomic for lock-free access)
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_processing{false};
    std::atomic<qint64> m_frameCount{0};
    std::atomic<qint64> m_skippedCount{0};
};

#endif // FRAMEPROCESSORTHREAD_H
