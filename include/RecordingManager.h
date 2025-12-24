#ifndef RECORDINGMANAGER_H
#define RECORDINGMANAGER_H

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>

class RecordingRegionSelector;
class RecordingControlBar;
class RecordingBoundaryOverlay;
class FFmpegEncoder;
class ICaptureEngine;
class QScreen;

class RecordingManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Recording state enumeration
     */
    enum class State {
        Idle,       // No recording activity
        Selecting,  // Region selection in progress
        Recording,  // Actively capturing frames
        Paused,     // Recording paused
        Encoding    // FFmpeg encoding after stop
    };
    Q_ENUM(State)

    explicit RecordingManager(QObject *parent = nullptr);
    ~RecordingManager();

    // State queries
    bool isActive() const;          // Region selection or recording in progress
    bool isRecording() const;       // Actively recording frames
    bool isSelectingRegion() const; // In region selection mode
    bool isPaused() const;          // Recording is paused
    State state() const { return m_state; }

public slots:
    void startRegionSelection();    // Begin region selection flow
    void startRegionSelectionWithPreset(const QRect &region, QScreen *screen);  // Use preset region
    void stopRecording();           // Stop and save recording
    void cancelRecording();         // Cancel without saving
    void pauseRecording();          // Pause recording
    void resumeRecording();         // Resume recording
    void togglePause();             // Toggle pause state

signals:
    void recordingStarted();
    void recordingStopped(const QString &outputPath);
    void recordingCancelled();
    void recordingError(const QString &error);
    void recordingWarning(const QString &warning);
    void durationChanged(qint64 elapsedMs);
    void recordingPaused();
    void recordingResumed();
    void stateChanged(State state);
    void selectionCancelledWithRegion(const QRect &region, QScreen *screen);

private slots:
    void onRegionSelected(const QRect &region, QScreen *screen);
    void onRegionCancelledWithRegion(const QRect &region, QScreen *screen);
    void onRegionCancelled();  // Handle cancel without valid region
    void captureFrame();
    void onEncodingFinished(bool success, const QString &outputPath);
    void onEncodingError(const QString &error);
    void updateDuration();

private:
    void startFrameCapture();
    void stopFrameCapture();
    void cleanupRecording();
    void cleanupStaleTempFiles();      // Clean up old temp files on startup
    QString generateOutputPath() const;
    void setState(State newState);
    void showSaveDialog(const QString &tempOutputPath);

    // Region selection
    QPointer<RecordingRegionSelector> m_regionSelector;

    // Recording state
    QPointer<RecordingControlBar> m_controlBar;
    QPointer<RecordingBoundaryOverlay> m_boundaryOverlay;
    FFmpegEncoder *m_encoder;
    ICaptureEngine *m_captureEngine;

    // Capture state
    QTimer *m_captureTimer;
    QTimer *m_durationTimer;
    QElapsedTimer m_elapsedTimer;
    QRect m_recordingRegion;
    QScreen *m_targetScreen;
    State m_state;
    int m_frameRate;
    qint64 m_frameCount;

    // Pause tracking
    qint64 m_pausedDuration;     // Total time spent paused
    qint64 m_pauseStartTime;     // When current pause began
};

#endif // RECORDINGMANAGER_H
