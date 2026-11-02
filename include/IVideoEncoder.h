#ifndef IVIDEOENCODER_H
#define IVIDEOENCODER_H

#include <QObject>
#include <QSize>
#include <QImage>
#include <QString>
#include "encoding/VideoRateControl.h"

#include <atomic>
#include <memory>

/**
 * @brief Abstract interface for video encoders (MP4 only)
 *
 * Platform implementations:
 * - macOS: AVFoundationEncoder
 * - Windows: MediaFoundationEncoder
 * - Linux: FFmpegEncoder (system libraries)
 *
 * This interface provides a common API for platform video encoding.
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

    /**
     * @brief Encode a capture backend's own surface without a CPU copy.
     * @param nativeBuffer CapturedFrame::nativeBuffer (a CVPixelBuffer on macOS)
     * @return true when the encoder took charge of the frame (written, or
     * dropped the way writeFrame() drops under backpressure); false when it
     * cannot use this buffer as is (pixel format or size). The caller then
     * writes the frame's image instead, so no frame is lost.
     */
    virtual bool writeNativeFrame(const std::shared_ptr<void> &nativeBuffer, qint64 timestampMs)
    {
        Q_UNUSED(nativeBuffer);
        Q_UNUSED(timestampMs);
        return false;
    }

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

    /**
     * @brief Choose how bits are spent. ConstantQuality asks the platform
     * encoder for @p qualityValue (0-100) with a bitrate ceiling; encoders
     * that cannot honour it stay in Bitrate mode (see effectiveRateControl()).
     * Must be called before start().
     */
    virtual void setRateControl(SnapTray::VideoRateControl mode, int qualityValue)
    {
        Q_UNUSED(mode);
        Q_UNUSED(qualityValue);
    }

    /**
     * @brief Keyframe (GOP) interval in seconds; 0 keeps the encoder default.
     * Must be called before start().
     */
    virtual void setKeyFrameIntervalSeconds(int seconds) { Q_UNUSED(seconds); }

    // Writes the MP4 as self-contained fragments, so a crash or power loss
    // leaves a file that plays up to its last complete fragment. Call before
    // start(). Returns false when the encoder cannot (the file then needs a
    // normal finish to be playable).
    virtual bool setFragmentedOutput(bool enabled) { Q_UNUSED(enabled); return false; }
    virtual bool isFragmentedOutput() const { return false; }

    /** @brief The rate control actually in effect after start(). */
    virtual SnapTray::VideoRateControl effectiveRateControl() const
    {
        return SnapTray::VideoRateControl::Bitrate;
    }

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
     * @brief Check if audio is actually enabled after start()
     * @return true if audio was successfully configured and enabled
     *
     * This may return false even if setAudioFormat() was called, if the
     * audio stream configuration failed during start(). Use this to check
     * actual audio status after recording begins.
     */
    virtual bool isAudioEnabled() const { return false; }

    /**
     * @brief Write audio samples to the encoder
     * @param pcmData Raw PCM audio data
     * @param startFrame First frame on the configured audio sample-rate timeline
     */
    virtual void writeAudioSamples(const QByteArray &pcmData, qint64 startFrame) {
        Q_UNUSED(pcmData); Q_UNUSED(startFrame);
    }

    // Bytes this encoder has written to disk so far, readable from any thread;
    // null when the output file at outputPath() shows that itself. Encoders
    // that write elsewhere until finish() (a temporary file) report it here
    // so the disk guard sees the real size during recording.
    using ByteCounter = std::shared_ptr<std::atomic<qint64>>;
    virtual ByteCounter writtenBytesCounter() const { return nullptr; }

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
