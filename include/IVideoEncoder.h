#ifndef IVIDEOENCODER_H
#define IVIDEOENCODER_H

#include <QObject>
#include <QSize>
#include <QImage>
#include <QString>

/**
 * @brief Abstract interface for video encoders (MP4 only)
 *
 * Platform implementations:
 * - macOS: AVFoundationEncoder
 * - Windows: MediaFoundationEncoder
 *
 * This interface provides a common API for native video encoding
 * without requiring external dependencies like FFmpeg.
 */
class IVideoEncoder : public QObject
{
    Q_OBJECT

public:
    explicit IVideoEncoder(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IVideoEncoder() = default;

    // Encoder info
    virtual bool isAvailable() const = 0;
    virtual QString encoderName() const = 0;

    // Encoding lifecycle
    virtual bool start(const QString &outputPath, const QSize &frameSize, int frameRate) = 0;
    virtual void writeFrame(const QImage &frame, qint64 timestampMs = -1) = 0;
    virtual void finish() = 0;
    virtual void abort() = 0;

    // State queries
    virtual bool isRunning() const = 0;
    virtual QString lastError() const = 0;
    virtual qint64 framesWritten() const = 0;
    virtual QString outputPath() const = 0;

    /**
     * @brief Set encoding quality
     * @param quality 0-100 (higher = better quality, larger file)
     */
    virtual void setQuality(int quality) { Q_UNUSED(quality); }

    // ========== Audio Support ==========

    /**
     * @brief Configure audio format (call before start())
     * @param sampleRate Sample rate in Hz (e.g., 48000)
     * @param channels Number of audio channels (1=mono, 2=stereo)
     * @param bitsPerSample Bits per sample (typically 16)
     */
    virtual void setAudioFormat(int sampleRate, int channels, int bitsPerSample) {
        Q_UNUSED(sampleRate); Q_UNUSED(channels); Q_UNUSED(bitsPerSample);
    }

    /**
     * @brief Check if this encoder supports audio
     * @return true if audio encoding is supported
     */
    virtual bool isAudioSupported() const { return false; }

    /**
     * @brief Write audio samples to the encoder
     * @param pcmData Raw PCM audio data
     * @param timestampMs Timestamp in milliseconds since recording start
     */
    virtual void writeAudioSamples(const QByteArray &pcmData, qint64 timestampMs) {
        Q_UNUSED(pcmData); Q_UNUSED(timestampMs);
    }

    /**
     * @brief Factory: create best available native encoder for MP4
     * @return Native encoder or nullptr if none available
     */
    static IVideoEncoder* createNativeEncoder(QObject *parent = nullptr);

signals:
    void finished(bool success, const QString &outputPath);
    void error(const QString &message);
    void progress(qint64 framesWritten);
};

#endif // IVIDEOENCODER_H
