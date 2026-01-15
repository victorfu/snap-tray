#ifndef VIDEOTRIMMER_H
#define VIDEOTRIMMER_H

#include <QObject>
#include <QString>
#include "encoding/EncoderFactory.h"

class IVideoPlayer;
class IVideoEncoder;
class NativeGifEncoder;
class WebPAnimationEncoder;

/**
 * @brief Async video trimming by re-encoding.
 *
 * This class extracts frames from the specified trim range of a video
 * and re-encodes them into a new video file. The operation runs
 * asynchronously to avoid blocking the UI.
 */
class VideoTrimmer : public QObject
{
    Q_OBJECT

public:
    explicit VideoTrimmer(QObject *parent = nullptr);
    ~VideoTrimmer() override;

    /**
     * @brief Set the input video file path.
     */
    void setInputPath(const QString &path);

    /**
     * @brief Set the trim range.
     * @param startMs Start position in milliseconds
     * @param endMs End position in milliseconds
     */
    void setTrimRange(qint64 startMs, qint64 endMs);

    /**
     * @brief Set the output format.
     */
    void setOutputFormat(EncoderFactory::Format format);

    /**
     * @brief Set the output file path.
     */
    void setOutputPath(const QString &path);

    /**
     * @brief Start the trim operation (async).
     */
    void startTrim();

    /**
     * @brief Cancel the ongoing trim operation.
     */
    void cancel();

    /**
     * @brief Check if trim is in progress.
     */
    bool isRunning() const { return m_running; }

signals:
    /**
     * @brief Emitted to report progress.
     * @param percent Progress percentage (0-100)
     */
    void progress(int percent);

    /**
     * @brief Emitted when trimming completes.
     * @param success True if successful
     * @param outputPath Path to the output file
     */
    void finished(bool success, const QString &outputPath);

    /**
     * @brief Emitted on error.
     * @param message Error description
     */
    void error(const QString &message);

private slots:
    void onFrameReady(const QImage &frame);
    void processNextFrame();
    void onEncodingFinished(bool success, const QString &path);
    void onMediaLoaded();

private:
    void cleanup();

    QString m_inputPath;
    QString m_outputPath;
    qint64 m_trimStart = 0;
    qint64 m_trimEnd = 0;
    EncoderFactory::Format m_format = EncoderFactory::Format::MP4;

    IVideoPlayer *m_player = nullptr;
    IVideoEncoder *m_videoEncoder = nullptr;
    NativeGifEncoder *m_gifEncoder = nullptr;
    WebPAnimationEncoder *m_webpEncoder = nullptr;

    bool m_running = false;
    bool m_cancelled = false;
    qint64 m_currentPosition = 0;
    qint64 m_seekPosition = 0;    // Position we requested frame from (for timestamp calculation)
    int m_frameCount = 0;
    int m_totalFrames = 0;
};

#endif // VIDEOTRIMMER_H
