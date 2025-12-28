#ifndef WEBPANIMENCODER_H
#define WEBPANIMENCODER_H

#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>

// Forward declarations for libwebp types
struct WebPAnimEncoder;
struct WebPAnimEncoderOptions;
struct WebPConfig;

/**
 * @brief WebP animation encoder using libwebp
 *
 * Advantages over GIF:
 * - Much smaller file sizes (typically 50-70% smaller than GIF)
 * - Better color support (24-bit color, not limited to 256 colors)
 * - Alpha channel support
 * - Configurable quality/size tradeoff
 */
class WebPAnimationEncoder : public QObject
{
    Q_OBJECT

public:
    explicit WebPAnimationEncoder(QObject *parent = nullptr);
    ~WebPAnimationEncoder();

    /**
     * @brief Start encoding
     * @param outputPath Output file path
     * @param frameSize Frame size (will be used as-is)
     * @param frameRate Base frame rate (used to calculate default delay)
     * @return true if encoding started successfully
     */
    bool start(const QString &outputPath, const QSize &frameSize, int frameRate);

    /**
     * @brief Write a frame
     * @param frame Frame image (any format, will be converted to RGBA)
     * @param timestampMs Timestamp from recording start (milliseconds)
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
     * @brief Set encoding quality
     * @param quality 0-100, higher values produce better quality but larger files
     */
    void setQuality(int quality);
    int quality() const { return m_quality; }

    /**
     * @brief Set whether the animation should loop
     * @param loop true for infinite loop, false for play once
     */
    void setLooping(bool loop);
    bool isLooping() const { return m_looping; }

signals:
    void finished(bool success, const QString &outputPath);
    void error(const QString &message);
    void progress(qint64 framesWritten);

private:
    void cleanup();

    struct WebPEncoderData;
    WebPEncoderData *m_data;  // Opaque pointer to internal data

    QString m_outputPath;
    QSize m_frameSize;
    int m_frameRate;
    int m_quality;
    bool m_looping;
    qint64 m_framesWritten;
    qint64 m_lastTimestampMs;
    QString m_lastError;
    bool m_running;
    bool m_aborted;
};

#endif // WEBPANIMENCODER_H
