#ifndef NATIVEGIFENCODER_H
#define NATIVEGIFENCODER_H

#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>

/**
 * @brief Native GIF encoder using msf_gif library
 *
 * Advantages over FFmpeg-based GIF encoding:
 * - Zero external dependencies (single header library)
 * - Per-frame variable delay (fixes audio/video sync issues)
 * - Automatic palette optimization
 * - Incremental encoding with good memory efficiency
 */
class NativeGifEncoder : public QObject
{
    Q_OBJECT

public:
    explicit NativeGifEncoder(QObject *parent = nullptr);
    ~NativeGifEncoder();

    /**
     * @brief Start encoding
     * @param outputPath Output file path
     * @param frameSize Frame size (automatically adjusted to even dimensions)
     * @param frameRate Base frame rate (used to calculate default delay)
     * @return true if encoding started successfully
     */
    bool start(const QString &outputPath, const QSize &frameSize, int frameRate);

    /**
     * @brief Write a frame
     * @param frame Frame image (any format, will be converted to RGBA)
     * @param timestampMs Timestamp from recording start (milliseconds)
     *
     * Uses timestamp to calculate per-frame delay, fixing sync issues
     * that occurred with FFmpeg's fixed frame rate approach.
     */
    void writeFrame(const QImage &frame, qint64 timestampMs = -1);

    /**
     * @brief Finish encoding and write to file
     */
    void finish();

    /**
     * @brief Abort encoding and clean up
     */
    void abort();

    // State queries
    bool isRunning() const { return m_running; }
    QString lastError() const { return m_lastError; }
    qint64 framesWritten() const { return m_framesWritten; }
    QString outputPath() const { return m_outputPath; }

    /**
     * @brief Set maximum bit depth for color quantization
     * @param depth 1-16, higher values produce better colors but larger files
     */
    void setMaxBitDepth(int depth);
    int maxBitDepth() const { return m_maxBitDepth; }

    /**
     * @brief Set maximum consecutive frame failures before stopping
     * @param max Maximum failures (default: 10)
     */
    void setMaxConsecutiveFailures(int max);
    int maxConsecutiveFailures() const { return m_maxConsecutiveFailures; }

    /// Default value for maximum consecutive failures
    static constexpr int DEFAULT_MAX_CONSECUTIVE_FAILURES = 10;

signals:
    void finished(bool success, const QString &outputPath);
    void error(const QString &message);
    void progress(qint64 framesWritten);

private:
    void cleanup();
    int calculateCentiseconds(qint64 timestampMs);

    void *m_gifState;  // Opaque pointer to MsfGifState
    QString m_outputPath;
    QSize m_frameSize;
    int m_frameRate;
    int m_maxBitDepth;
    qint64 m_framesWritten;
    int m_consecutiveFailures;  // Track consecutive frame encoding failures
    int m_maxConsecutiveFailures;  // Configurable threshold
    qint64 m_lastTimestampMs;
    QString m_lastError;
    bool m_running;
    bool m_aborted;
};

#endif // NATIVEGIFENCODER_H
