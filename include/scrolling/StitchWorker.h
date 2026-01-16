#ifndef STITCHWORKER_H
#define STITCHWORKER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QQueue>
#include <QFuture>
#include <atomic>
#include "scrolling/ImageStitcher.h"

class FixedElementDetector;

/**
 * @brief Worker class for offloading heavy stitching operations from UI thread
 *
 * This class manages a queue of frames to be processed and runs the heavy
 * computations (fixed element detection, image stitching) in a worker thread
 * using QtConcurrent.
 *
 * The pattern:
 * 1. UI thread calls enqueueFrame() with captured frame (fast, non-blocking)
 * 2. Worker thread processes frames sequentially via processNextFrame()
 * 3. Results are emitted via signals back to UI thread
 *
 * Thread safety: The frame queue is protected by mutex. The ImageStitcher
 * and FixedElementDetector are owned and only accessed by the worker thread.
 */
class StitchWorker : public QObject
{
    Q_OBJECT

public:
    explicit StitchWorker(QObject *parent = nullptr);
    ~StitchWorker();

    /**
     * @brief Stitch result returned to UI thread
     */
    struct Result {
        bool success = false;
        ImageStitcher::FailureCode failureCode = ImageStitcher::FailureCode::None;
        double confidence = 0.0;
        QString failureReason;
        int overlapPixels = 0;
        int frameCount = 0;
        QSize currentSize;
        int lastSuccessfulPosition = 0;
        bool fixedElementsDetected = false;
        int leadingFixed = 0;
        int trailingFixed = 0;
        QSize frameSize;
    };

    /**
     * @brief Capture mode for scrolling direction
     */
    enum class CaptureMode {
        Vertical,
        Horizontal
    };

    /**
     * @brief Set the capture mode (vertical or horizontal scrolling)
     */
    void setCaptureMode(CaptureMode mode);

    /**
     * @brief Set stitch configuration
     */
    void setStitchConfig(double confidenceThreshold, bool detectStaticRegions);

    /**
     * @brief Reset the worker state (clears queue, resets stitcher)
     * @note This is a blocking call - use resetAsync() from UI thread
     */
    void reset();

    /**
     * @brief Non-blocking reset for UI thread
     *
     * Immediately stops accepting frames and clears the queue.
     * If processing is in progress, waits asynchronously and emits
     * resetCompleted() when done.
     */
    void resetAsync();

    /**
     * @brief Enqueue a frame for processing (called from UI thread, non-blocking)
     * @param frame The captured frame
     * @return true if frame was queued, false if queue is full
     */
    bool enqueueFrame(const QImage &frame);

    /**
     * @brief Get current queue depth
     */
    int queueDepth() const;

    /**
     * @brief Check if processing is in progress
     */
    bool isProcessing() const;

    /**
     * @brief Get the final stitched result
     * @return The fully stitched image (call after stopping)
     * @note This method is NOT thread-safe. Use requestFinish() instead
     *       when the worker thread may still be processing.
     */
    QImage getStitchedImage() const;

    /**
     * @brief Get current frame count
     */
    int frameCount() const;

    /**
     * @brief Request finish processing and emit finishCompleted signal when done
     *
     * Non-blocking alternative to finishAndGetResult(). Stops accepting new frames
     * and emits finishCompleted(QImage) when all queued frames are processed.
     */
    void requestFinish();

signals:
    /**
     * @brief Emitted when a frame has been processed
     * @param result The stitch result
     */
    void frameProcessed(const StitchWorker::Result &result);

    /**
     * @brief Emitted when fixed elements are detected
     * @param leading Leading fixed element size (top/left)
     * @param trailing Trailing fixed element size (bottom/right)
     */
    void fixedElementsDetected(int leading, int trailing);

    /**
     * @brief Emitted when queue is getting full
     */
    void queueNearFull(int currentDepth, int maxDepth);
    
    /**
     * @brief Emitted when queue pressure is relieved
     */
    void queueLow(int currentDepth, int maxDepth);

    /**
     * @brief Emitted when an error occurs
     */
    void error(const QString &message);

    /**
     * @brief Emitted when finish processing is complete (non-blocking finish)
     * @param result The final stitched image
     */
    void finishCompleted(const QImage &result);

    /**
     * @brief Emitted when async reset is complete
     */
    void resetCompleted();

private slots:
    void processNextFrame();

private:
    void doProcessFrame(const QImage &frame);
    void doResetCleanup();

    // Processing components (owned, accessed only by worker)
    ImageStitcher *m_stitcher;
    FixedElementDetector *m_fixedDetector;

    // Frame queue (thread-safe)
    mutable QMutex m_queueMutex;
    QQueue<QImage> m_frameQueue;
    static constexpr int MAX_QUEUE_SIZE = 10;

    // Processing state
    std::atomic<bool> m_isProcessing{false};
    std::atomic<bool> m_acceptingFrames{true};  // Set to false to stop accepting new frames
    std::atomic<bool> m_finishRequested{false}; // Set to true when requestFinish() is called
    QFuture<void> m_processingFuture;
    bool m_wasNearFull = false;

    // Fixed element detection buffering
    QVector<QImage> m_pendingRawFrames;
    qint64 m_pendingMemoryUsage = 0;
    bool m_fixedDetected = false;           // fixed elements detection succeeded
    bool m_detectionDisabled = false;       // stop pending/detect when budget exceeded
    int m_minFramesForDetection = 6;
    QImage m_lastRawSmall;                  // cached downsampled raw (64x64)
    QImage m_lastProcessedSmall;            // cached downsampled processed (64x64)
    static constexpr int MAX_PENDING_FRAMES = 30;
    static constexpr qint64 MAX_PENDING_MEMORY_BYTES = 300 * 1024 * 1024;  // 300MB

    // Configuration
    CaptureMode m_captureMode = CaptureMode::Vertical;
    bool m_detectStaticRegions = true;
};

#endif // STITCHWORKER_H
