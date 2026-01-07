#ifndef AUDIOFILEWRITER_H
#define AUDIOFILEWRITER_H

#include <QObject>
#include <QFile>
#include <QString>
#include <QMutex>

/**
 * @brief Simple WAV file writer for audio recording
 *
 * Writes raw PCM audio data to a WAV file format.
 * Used to temporarily store audio data during recording,
 * which is then muxed with video by FFmpeg.
 */
class AudioFileWriter : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Audio format specification
     */
    struct AudioFormat {
        int sampleRate = 48000;     // Sample rate in Hz
        int channels = 2;            // Number of channels
        int bitsPerSample = 16;      // Bits per sample
    };

    explicit AudioFileWriter(QObject *parent = nullptr);
    ~AudioFileWriter();

    /**
     * @brief Start writing to a new WAV file
     * @param filePath Path to the output file
     * @param format Audio format specification
     * @return true if file was opened successfully
     */
    bool start(const QString &filePath, const AudioFormat &format);

    /**
     * @brief Write audio data to the file
     * @param data Raw PCM audio data
     *
     * Thread-safe: can be called from audio capture thread
     */
    void writeAudioData(const QByteArray &data);

    /**
     * @brief Finish writing and close the file
     *
     * Updates the WAV header with final sizes.
     * Must be called before using the file.
     * @return true if header was updated successfully, false otherwise
     */
    bool finish();

    /**
     * @brief Check if the writer is currently active
     */
    bool isWriting() const { return m_file.isOpen(); }

    /**
     * @brief Get the output file path
     */
    QString filePath() const { return m_filePath; }

    /**
     * @brief Get the number of bytes written
     */
    qint64 bytesWritten() const { return m_dataSize; }

    /**
     * @brief Get the duration of audio written in milliseconds
     */
    qint64 durationMs() const;

signals:
    /**
     * @brief Emitted when an error occurs
     */
    void error(const QString &message);

    /**
     * @brief Emitted when a non-fatal warning occurs
     */
    void warning(const QString &message);

private:
    /**
     * @brief Write the WAV file header
     */
    void writeHeader();

    /**
     * @brief Update the WAV header with final sizes
     * @return true if header was updated successfully
     */
    bool updateHeader();

    QFile m_file;
    QString m_filePath;
    AudioFormat m_format;
    qint64 m_dataSize = 0;
    mutable QMutex m_mutex;
};

#endif // AUDIOFILEWRITER_H
