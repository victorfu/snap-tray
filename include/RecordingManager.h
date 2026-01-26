#ifndef RECORDINGMANAGER_H
#define RECORDINGMANAGER_H

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QFuture>
#include <memory>

#include "WatermarkRenderer.h"
#include "utils/ResourceCleanupHelper.h"

class RecordingRegionSelector;
class RecordingControlBar;
class RecordingBoundaryOverlay;
class RecordingInitTask;
class InPlacePreviewOverlay;
class CountdownOverlay;
class NativeGifEncoder;
class IVideoEncoder;
class ICaptureEngine;
class IAudioCaptureEngine;
class EncodingWorker;
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
        Preparing,  // Initializing capture/encoder (async)
        Countdown,  // Countdown before recording
        Recording,  // Actively capturing frames
        Paused,     // Recording paused
        Encoding,   // Encoding after stop
        Previewing  // Preview window open
    };
    Q_ENUM(State)

    explicit RecordingManager(QObject *parent = nullptr);
    ~RecordingManager();

    // State queries
    bool isActive() const;          // Region selection or recording in progress
    bool isRecording() const;       // Actively recording frames
    bool isSelectingRegion() const; // In region selection mode
    bool isPaused() const;          // Recording is paused
    bool isPreviewing() const;      // Preview window is open
    State state() const { return m_state; }

public slots:
    void startRegionSelectionWithPreset(const QRect &region, QScreen *screen);  // Use preset region
    void startFullScreenRecording(QScreen* screen = nullptr);  // Record entire screen (cursor position or specified)
    void stopRecording();           // Stop and save recording
    void cancelRecording();         // Cancel without saving
    void pauseRecording();          // Pause recording
    void resumeRecording();         // Resume recording
    void togglePause();             // Toggle pause state
    void onPreviewClosed(bool saved);  // Handle preview window close
    void triggerSaveDialog(const QString &videoPath);  // Show save dialog for video

signals:
    void preparationStarted();
    void preparationProgress(const QString &step);
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
    void previewRequested(const QString &tempVideoPath);
    void annotatePreviewRequested(const QString &tempVideoPath);

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
    void beginAsyncInitialization();   // Start async initialization
    void onInitializationComplete();   // Handle async init completion
    void startCaptureTimers();         // Start capture and duration timers
    void stopFrameCapture();
    void cleanupRecording();
    void cleanupAudio();               // Clean up audio capture resources
    void cleanupStaleTempFiles();      // Clean up old temp files on startup
    QString generateOutputPath() const;
    void setState(State newState);
    void showSaveDialog(const QString &tempOutputPath);
    RecordingRegionSelector* createRegionSelector();

    // Countdown methods
    void startCountdown();                    // Begin countdown overlay
    void startRecordingAfterCountdown();      // Called when countdown finishes
    void onCountdownCancelled();              // Handle countdown cancellation

    // In-place preview methods
    void cleanupPreviewMode();
    void connectPreviewSignals();

private slots:
    // Preview mode handlers
    void onPreviewPlayRequested();
    void onPreviewPauseRequested();
    void onPreviewSeekRequested(qint64 positionMs);
    void onPreviewVolumeToggled();
    void onPreviewAnnotateRequested();
    void onPreviewSaveRequested();
    void onPreviewDiscardRequested();
    void onPreviewPositionChanged(qint64 positionMs);
    void onPreviewDurationChanged(qint64 durationMs);
    void onPreviewStateChanged(int state);

private:
    // Region selection
    QPointer<RecordingRegionSelector> m_regionSelector;

    // Recording state
    QPointer<RecordingControlBar> m_controlBar;
    QPointer<RecordingBoundaryOverlay> m_boundaryOverlay;
    QPointer<InPlacePreviewOverlay> m_previewOverlay;
    QString m_tempVideoPath;
    std::unique_ptr<EncodingWorker> m_encodingWorker;  // Offloads encoding to worker thread
    bool m_usingNativeEncoder;          // True if using native encoder (vs GIF)
    CaptureEnginePtr m_captureEngine;   // Screen capture engine (stop + disconnect + deleteLater)

    // Capture state
    std::unique_ptr<QTimer> m_captureTimer;
    std::unique_ptr<QTimer> m_durationTimer;
    QElapsedTimer m_elapsedTimer;
    QRect m_recordingRegion;
    QScreen *m_targetScreen;
    State m_state;
    int m_frameRate;
    qint64 m_frameCount;

    // Pause tracking
    qint64 m_pausedDuration;     // Total time spent paused
    qint64 m_pauseStartTime;     // When current pause began
    mutable QMutex m_durationMutex;  // Protects pause duration variables

    // Audio capture
    AudioEnginePtr m_audioEngine;       // Audio capture engine (stop + disconnect + deleteLater)
    bool m_audioEnabled;
    int m_audioSource;  // 0=Microphone, 1=SystemAudio, 2=Both
    QString m_audioDevice;
    QString m_tempAudioPath;

    // Async initialization
    std::unique_ptr<RecordingInitTask> m_initTask;
    QFuture<void> m_initFuture;

    // Countdown
    QPointer<CountdownOverlay> m_countdownOverlay;
    bool m_countdownEnabled;
    int m_countdownSeconds;

    // Watermark for recording (settings stored, rendering done by EncodingWorker)
    WatermarkRenderer::Settings m_watermarkSettings;
};

#endif // RECORDINGMANAGER_H
