#ifndef MOCKVIDEOENCODER_H
#define MOCKVIDEOENCODER_H

#include "IVideoEncoder.h"
#include <QList>
#include <QPair>

/**
 * @brief Mock implementation of IVideoEncoder for testing
 *
 * Allows controlling encoder behavior and tracking method calls
 * without actual video encoding.
 */
class MockVideoEncoder : public IVideoEncoder
{
    Q_OBJECT

public:
    explicit MockVideoEncoder(QObject *parent = nullptr);
    ~MockVideoEncoder() override = default;

    // IVideoEncoder interface implementation
    bool isAvailable() const override { return m_available; }
    QString encoderName() const override { return QStringLiteral("MockVideoEncoder"); }

    bool start(const QString &outputPath, const QSize &frameSize, int frameRate) override;
    void writeFrame(const QImage &frame, qint64 timestampMs = -1) override;
    void finish() override;
    void abort() override;

    bool isRunning() const override { return m_running; }
    QString lastError() const override { return m_lastError; }
    qint64 framesWritten() const override { return m_framesWritten; }
    QString outputPath() const override { return m_outputPath; }

    void setQuality(int quality) override;

    // Audio support
    void setAudioFormat(int sampleRate, int channels, int bitsPerSample) override;
    bool isAudioSupported() const override { return m_audioSupported; }
    void writeAudioSamples(const QByteArray &pcmData, qint64 timestampMs) override;

    // ========== Mock Control Methods ==========

    /**
     * @brief Control encoder availability
     */
    void setAvailable(bool available);

    /**
     * @brief Control whether start() succeeds
     */
    void setStartSucceeds(bool succeeds);

    /**
     * @brief Control whether finish() succeeds
     */
    void setFinishSucceeds(bool succeeds);

    /**
     * @brief Control audio support
     */
    void setAudioSupported(bool supported);

    /**
     * @brief Emit finished signal
     */
    void simulateFinished(bool success);

    /**
     * @brief Emit error signal
     */
    void simulateError(const QString &message);

    /**
     * @brief Emit progress signal
     */
    void simulateProgress(qint64 frames);

    // ========== Spy Methods ==========

    int startCallCount() const { return m_startCalls; }
    int finishCallCount() const { return m_finishCalls; }
    int abortCallCount() const { return m_abortCalls; }
    int writeFrameCallCount() const { return m_writeFrameCalls; }
    int writeAudioCallCount() const { return m_writeAudioCalls; }

    // Get recorded frames and audio for verification
    QList<QImage> writtenFrames() const { return m_writtenFrames; }
    QList<qint64> writtenFrameTimestamps() const { return m_writtenFrameTimestamps; }
    QList<QPair<QByteArray, qint64>> writtenAudioSamples() const { return m_writtenAudioSamples; }

    // Start parameters
    QString startOutputPath() const { return m_startOutputPath; }
    QSize startFrameSize() const { return m_startFrameSize; }
    int startFrameRate() const { return m_startFrameRate; }
    int quality() const { return m_quality; }

    // Audio format parameters
    int audioSampleRate() const { return m_audioSampleRate; }
    int audioChannels() const { return m_audioChannels; }
    int audioBitsPerSample() const { return m_audioBitsPerSample; }

    /**
     * @brief Reset all call counters and recorded data
     */
    void resetCounters();

private:
    bool m_available = true;
    bool m_running = false;
    bool m_startSucceeds = true;
    bool m_finishSucceeds = true;
    bool m_audioSupported = true;

    QString m_outputPath;
    QString m_lastError;
    qint64 m_framesWritten = 0;
    int m_quality = 55;

    // Audio format
    int m_audioSampleRate = 48000;
    int m_audioChannels = 2;
    int m_audioBitsPerSample = 16;

    // Call counters
    int m_startCalls = 0;
    int m_finishCalls = 0;
    int m_abortCalls = 0;
    int m_writeFrameCalls = 0;
    int m_writeAudioCalls = 0;

    // Start parameters
    QString m_startOutputPath;
    QSize m_startFrameSize;
    int m_startFrameRate = 30;

    // Recorded data
    QList<QImage> m_writtenFrames;
    QList<qint64> m_writtenFrameTimestamps;
    QList<QPair<QByteArray, qint64>> m_writtenAudioSamples;
};

#endif // MOCKVIDEOENCODER_H
