#ifndef FFMPEGENCODER_H
#define FFMPEGENCODER_H

#include <QObject>
#include <QProcess>
#include <QSize>
#include <QPixmap>
#include <functional>

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

    bool start(const QString &outputPath, const QSize &frameSize, int frameRate);
    void writeFrame(const QPixmap &frame);
    void finish();
    void abort();

    bool isRunning() const;
    QString lastError() const;
    qint64 framesWritten() const;

    static bool isFFmpegAvailable();
    static QString ffmpegPath();

    /**
     * @brief Check FFmpeg availability asynchronously (non-blocking)
     * @param callback Called on main thread with result
     */
    static void checkAvailabilityAsync(std::function<void(bool available)> callback);

    /**
     * @brief Start encoder asynchronously (non-blocking)
     * Emits startCompleted(success) when done instead of blocking.
     */
    void startAsync(const QString &outputPath, const QSize &frameSize, int frameRate);

signals:
    /**
     * @brief Emitted when async startup completes
     */
    void startCompleted(bool success);
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
    QStringList buildGifArguments(const QString &outputPath,
                                  const QSize &frameSize, int frameRate) const;

    QProcess *m_process;
    QString m_outputPath;
    QSize m_frameSize;
    int m_frameRate;
    qint64 m_framesWritten;
    QString m_lastError;
    bool m_finishing;
    bool m_startPending = false;  // For async startup tracking
    OutputFormat m_outputFormat;
};

#endif // FFMPEGENCODER_H
