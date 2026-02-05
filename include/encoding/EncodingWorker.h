#ifndef ENCODINGWORKER_H
#define ENCODINGWORKER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QQueue>
#include <QFuture>
#include <QSize>
#include <atomic>
#include <memory>

#include "WatermarkRenderer.h"

class IVideoEncoder;
class NativeGifEncoder;

/**
 * @brief Worker class for offloading heavy encoding operations from UI thread
 *
 * This class manages a queue of frames to be processed and runs the heavy
 * encoding operations (watermark compositing, format conversion, encoder writes)
 * in a worker thread using QtConcurrent.
 *
 * The pattern (matching StitchWorker):
 * 1. UI thread calls enqueueFrame() with captured frame (fast, non-blocking)
 * 2. Worker thread processes frames sequentially via processNextFrame()
 * 3. Results are emitted via signals back to UI thread
 *
 * Thread safety:
 * - Frame queue is protected by mutex
 * - Encoders are owned and only accessed by the worker thread
 * - Audio samples pass through directly (encoder handles backpressure)
 */
class EncodingWorker : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Frame data passed to the encoding queue
     */
    struct FrameData {
        QImage frame;
        qint64 timestampMs;
    };

    /**
     * @brief Audio data passed to the encoding queue
     */
    struct AudioData {
        QByteArray data;
        qint64 timestampMs;
    };

    /**
     * @brief Encoder type selection
     */
    enum class EncoderType {
        Video,  // IVideoEncoder (MP4)
        Gif     // NativeGifEncoder
    };

    explicit EncodingWorker(QObject *parent = nullptr);
    ~EncodingWorker();

    // ========== Configuration (call before start) ==========

    /**
     * @brief Set the video encoder (MP4)
     * @param encoder Takes ownership
     */
    void setVideoEncoder(IVideoEncoder* encoder);

    /**
     * @brief Set the GIF encoder
     * @param encoder Takes ownership
     */
    void setGifEncoder(NativeGifEncoder* encoder);

    /**
     * @brief Set active encoder type
     */
    void setEncoderType(EncoderType type);

    /**
     * @brief Configure watermark settings
     */
    void setWatermarkSettings(const WatermarkRenderer::Settings& settings);

    // ========== Lifecycle ==========

    /**
     * @brief Start the encoding worker
     * @return true if started successfully
     */
    bool start();

    /**
     * @brief Stop immediately, abort encoding
     */
    void stop();

    /**
     * @brief Request finish: process remaining queue then finish encoder
     *
     * Non-blocking. Emits finished() when complete.
     */
    void requestFinish();

    // ========== Frame/Audio Submission ==========

    /**
     * @brief Enqueue a frame for encoding (called from UI thread, non-blocking)
     * @param frame Frame data with image and timestamp
     * @return true if enqueued, false if queue is full
     */
    bool enqueueFrame(const FrameData& frame);

    /**
     * @brief Write audio samples to encoder
     * @param data PCM audio data
     * @param timestampMs Timestamp in milliseconds
     *
     * Thread-safe: Audio is queued and processed by the worker thread.
     * Can be called from any thread (e.g., audio capture thread via DirectConnection).
     */
    void writeAudioSamples(const QByteArray& data, qint64 timestampMs);

    // ========== State Queries ==========

    int queueDepth() const;
    bool isProcessing() const;
    bool isRunning() const { return m_running.load(); }
    qint64 framesWritten() const { return m_framesWritten.load(); }

signals:
    /**
     * @brief Emitted when encoding finishes (after requestFinish)
     */
    void finished(bool success, const QString& outputPath);

    /**
     * @brief Emitted on encoding error
     */
    void error(const QString& message);

    /**
     * @brief Emitted periodically with frame count
     */
    void progress(qint64 framesWritten);

    /**
     * @brief Emitted when queue pressure is high
     */
    void queuePressure(int currentDepth, int maxDepth);

    /**
     * @brief Emitted when queue pressure is relieved
     */
    void queueLow(int currentDepth, int maxDepth);

private:
    void processNextFrame();
    void doProcessFrame(const FrameData& frameData);
    void applyWatermark(QImage& frame);
    void finishEncoder();

    // Encoders (owned, accessed only by worker thread)
    std::unique_ptr<IVideoEncoder> m_videoEncoder;
    std::unique_ptr<NativeGifEncoder> m_gifEncoder;
    EncoderType m_encoderType = EncoderType::Video;

    // Frame queue (thread-safe)
    mutable QMutex m_queueMutex;
    QQueue<FrameData> m_frameQueue;
    static constexpr int MAX_QUEUE_SIZE = 30;  // ~1 second at 30fps

    // Audio queue (thread-safe, shared mutex with frame queue)
    QQueue<AudioData> m_audioQueue;
    static constexpr int MAX_AUDIO_QUEUE_SIZE = 100;  // ~1 second buffer

    // Processing state
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_isProcessing{false};
    std::atomic<bool> m_acceptingFrames{true};
    std::atomic<bool> m_finishRequested{false};
    std::atomic<qint64> m_framesWritten{0};
    std::atomic<bool> m_finishCalled{false};
    QFuture<void> m_processingFuture;
    bool m_wasNearFull = false;

    // Configuration
    WatermarkRenderer::Settings m_watermarkSettings;
    QImage m_cachedWatermarkImage;  // Use QImage for thread safety (QPixmap is not thread-safe)

    // Queue thresholds
    static constexpr int QUEUE_NEAR_FULL_THRESHOLD = 24;  // 80% of max
    static constexpr int QUEUE_LOW_THRESHOLD = 6;         // 20% of max
};

#endif // ENCODINGWORKER_H
