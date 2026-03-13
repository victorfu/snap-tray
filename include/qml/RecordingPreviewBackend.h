#pragma once

#include "encoding/EncoderFactory.h"
#include <QObject>
#include <QString>

class QQuickView;
class QEvent;
class VideoTrimmer;

/**
 * @brief C++ backend for the QML Recording Preview window.
 *
 * Manages the QQuickView lifecycle, exposes video/trim/format state
 * to QML, and handles save/discard/trim/format-conversion logic.
 *
 * The signal interface preserves the same flow as the old preview window:
 * notify caller to save/discard, then report closed(saved).
 *
 * Usage:
 *   auto* backend = new RecordingPreviewBackend(videoPath, this);
 *   backend->setDefaultOutputFormat(OutputFormat::MP4);
 *   connect(backend, &RecordingPreviewBackend::saveRequested, ...);
 *   connect(backend, &RecordingPreviewBackend::discardRequested, ...);
 *   connect(backend, &RecordingPreviewBackend::closed, ...);
 *   backend->show();
 */
class RecordingPreviewBackend : public QObject
{
    Q_OBJECT

    // Video state
    Q_PROPERTY(QString videoPath READ videoPath CONSTANT)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY stateChanged)

    // Trim state
    Q_PROPERTY(qint64 trimStart READ trimStart WRITE setTrimStart NOTIFY trimRangeChanged)
    Q_PROPERTY(qint64 trimEnd READ trimEnd WRITE setTrimEnd NOTIFY trimRangeChanged)
    Q_PROPERTY(bool hasTrim READ hasTrim NOTIFY trimRangeChanged)
    Q_PROPERTY(qint64 trimmedDuration READ trimmedDuration NOTIFY trimRangeChanged)

    // Format
    Q_PROPERTY(int selectedFormat READ selectedFormat WRITE setSelectedFormat NOTIFY formatChanged)

    // Processing progress
    Q_PROPERTY(bool isProcessing READ isProcessing NOTIFY processingChanged)
    Q_PROPERTY(int processProgress READ processProgress NOTIFY processProgressChanged)
    Q_PROPERTY(QString processStatus READ processStatus NOTIFY processStatusChanged)

    // Error state
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum OutputFormat {
        MP4  = 0,
        GIF  = 1,
        WebP = 2
    };
    Q_ENUM(OutputFormat)

    explicit RecordingPreviewBackend(const QString &videoPath, QObject *parent = nullptr);
    ~RecordingPreviewBackend() override;

    // Window management
    void show();
    void close();
    void setDefaultOutputFormat(int formatInt);

    // Property getters
    QString videoPath() const { return m_videoPath; }
    qint64 duration() const { return m_duration; }
    qint64 position() const { return m_position; }
    bool isPlaying() const { return m_isPlaying; }

    qint64 trimStart() const { return m_trimStart; }
    qint64 trimEnd() const;
    bool hasTrim() const;
    qint64 trimmedDuration() const;

    int selectedFormat() const { return m_selectedFormat; }

    bool isProcessing() const { return m_isProcessing; }
    int processProgress() const { return m_processProgress; }
    QString processStatus() const { return m_processStatus; }

    QString errorMessage() const { return m_errorMessage; }

    // Property setters
    void setTrimStart(qint64 ms);
    void setTrimEnd(qint64 ms);
    void setSelectedFormat(int format);

    // QML actions
    Q_INVOKABLE void save();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void toggleTrim();
    Q_INVOKABLE QString formatTime(qint64 ms) const;
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void reportPlaybackError(const QString &message);

    // Called by QML VideoPlaybackItem position/duration updates
    Q_INVOKABLE void updatePosition(qint64 ms);
    Q_INVOKABLE void updateDuration(qint64 ms);
    Q_INVOKABLE void updatePlayingState(bool playing);

signals:
    // External interface consumed by MainApplication.
    void saveRequested(const QString &videoPath);
    void discardRequested(const QString &videoPath);
    void closed(bool saved);

    // Property change notifications
    void durationChanged();
    void positionChanged();
    void stateChanged();
    void trimRangeChanged();
    void formatChanged();
    void processingChanged();
    void processProgressChanged();
    void processStatusChanged();
    void errorMessageChanged();

private:
    bool eventFilter(QObject* watched, QEvent* event) override;

    void ensureView();
    void applyPlatformWindowFlags();
    void syncCursorSurface();
    void performFormatConversion(OutputFormat format);
    void performTrim();

    void setErrorMessage(const QString &msg);

    // Trim callbacks
    void onTrimProgress(int percent);
    void onTrimFinished(bool success, const QString &outputPath);

    // View
    QQuickView *m_view = nullptr;

    // Video
    QString m_videoPath;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    bool m_isPlaying = false;

    // Trim
    qint64 m_trimStart = 0;
    qint64 m_trimEnd = -1;  // -1 means end of video
    VideoTrimmer *m_trimmer = nullptr;

    // Format
    OutputFormat m_selectedFormat = MP4;

    // Processing
    bool m_isProcessing = false;
    int m_processProgress = 0;
    QString m_processStatus;

    // Error
    QString m_errorMessage;

    // State
    bool m_saved = false;
    QString m_cursorSurfaceId;
    QString m_cursorOwnerId;
};
