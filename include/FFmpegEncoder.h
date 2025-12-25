#ifndef FFMPEGENCODER_H
#define FFMPEGENCODER_H

#include <QObject>
#include <QProcess>
#include <QSize>
#include <QImage>

class FFmpegEncoder : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Output format enumeration
     */
    enum class OutputFormat {
        MP4 = 0,    // H.264 encoded MP4 (default)
        GIF = 1     // Palette-optimized GIF
    };
    Q_ENUM(OutputFormat)

    explicit FFmpegEncoder(QObject *parent = nullptr);
    ~FFmpegEncoder();

    /**
     * @brief Set output format before calling start()
     */
    void setOutputFormat(OutputFormat format) { m_outputFormat = format; }
    OutputFormat outputFormat() const { return m_outputFormat; }

    /**
     * @brief Set CRF (Constant Rate Factor) for quality control
     * @param crf Value 0-51, lower = better quality (18-28 recommended)
     */
    void setCrf(int crf) { m_crf = qBound(0, crf, 51); }
    int crf() const { return m_crf; }

    /**
     * @brief Set encoding preset for speed/quality balance
     * @param preset e.g. "ultrafast", "fast", "medium", "slow"
     */
    void setPreset(const QString &preset) { m_preset = preset; }
    QString preset() const { return m_preset; }

    /**
     * @brief Set audio file path for muxing with video
     * @param path Path to WAV file containing audio data
     *
     * Must be called before start(). If set, audio will be
     * encoded to AAC and muxed with the video.
     */
    void setAudioFilePath(const QString &path) { m_audioFilePath = path; }
    QString audioFilePath() const { return m_audioFilePath; }

    /**
     * @brief Check if audio is enabled for encoding
     */
    bool hasAudio() const { return !m_audioFilePath.isEmpty(); }

    bool start(const QString &outputPath, const QSize &frameSize, int frameRate);
    void writeFrame(const QImage &frame);
    void finish();
    void abort();

    bool isRunning() const;
    QString lastError() const;
    qint64 framesWritten() const;
    QString outputPath() const { return m_outputPath; }

    static bool isFFmpegAvailable();
    static QString ffmpegPath();

signals:
    void finished(bool success, const QString &outputPath);
    void error(const QString &message);
    void progress(qint64 framesWritten);

private slots:
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onReadyReadStandardError();

private:
    QStringList buildArguments(const QString &outputPath,
                               const QSize &frameSize, int frameRate) const;
    QStringList buildMp4Arguments(const QString &outputPath,
                                  const QSize &frameSize, int frameRate) const;
    QStringList buildMp4WithAudioArguments(const QString &outputPath,
                                           const QSize &frameSize, int frameRate) const;
    QStringList buildGifArguments(const QString &outputPath,
                                  const QSize &frameSize, int frameRate) const;

    QProcess *m_process;
    QString m_outputPath;
    QSize m_frameSize;
    int m_frameRate;
    qint64 m_framesWritten;
    QString m_lastError;
    bool m_finishing;
    bool m_aborting;  // True when abort() is called to suppress error signals
    OutputFormat m_outputFormat;
    int m_crf;
    QString m_preset;
    QString m_audioFilePath;  // Path to WAV audio file for muxing
};

#endif // FFMPEGENCODER_H
