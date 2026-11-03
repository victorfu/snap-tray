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
#include <QSharedPointer>
#include <memory>
#include <optional>
#include <atomic>
#include <functional>
#include <QStringList>
#include "capture/IAudioCaptureEngine.h"
#include "recording/RecordingAudioStatus.h"
#include "recording/SessionClock.h"
#include "recording/FramePacer.h"
#include "recording/RecordingHealth.h"
#include "recording/DiskSpaceGuard.h"
#include "recording/RecordingSession.h"
#include "recording/RecordingInterruption.h"

#include "WatermarkRenderer.h"
#include "recording/WindowTimelineRecorder.h"
#include "recording/CursorTimelineRecorder.h"
#include "recording/KeyTimelineRecorder.h"
#include "utils/ResourceCleanupHelper.h"

class RecordingInitTask;
namespace SnapTray {
class QmlRecordingControlBar;
}
class QmlCountdownOverlay;
class NativeGifEncoder;
class IVideoEncoder;
class ICaptureEngine;
class IAudioCaptureEngine;
class EncodingWorker;
class QScreen;
class QThread;

class RecordingManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Recording state enumeration
     */
    enum class State {
        Idle,       // No recording activity
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
    bool isActive() const;          // Recording or post-record workflow in progress
    bool blocksCapture() const;     // A recording is being prepared, captured or encoded; an open preview does not block captures
    bool isRecording() const;       // Actively recording frames
    bool isPaused() const;          // Recording is paused
    bool isPreviewing() const;      // Preview window is open
    QString activeScreenSummary() const;
    // Per-source audio outcome of the current recording; Off/Off when idle.
    SnapTray::RecordingAudioStatus audioStatus() const { return m_audioStatus; }
    // True while frames are being lost (sustained, not a single hiccup).
    bool isDroppingFrames() const { return m_healthPolicy.isWarning(); }
    // True while the recording's disk is low on space (warning or stopped).
    bool isLowOnDiskSpace() const { return m_lowDiskSpace; }
    // Recordings an earlier run left behind, found by scanForRecoveries().
    // Opens one in the preview, like a recording that just finished; false
    // when a recording or preview is already open.
    bool openRecoveredRecording(const QString& videoPath);
    // Scans the temp folder off the GUI thread; emits recoveriesFound.
    void scanForRecoveries();
    static bool isSessionActive(const QString& videoPath);

    // Plain-text summary of the last recording's capture and encoding
    // counters, for the user to copy. Empty until a recording has finished.
    QString lastDiagnostics() const { return m_lastDiagnostics; }
    State state() const { return m_state; }

public slots:
    void startScreenRecording(QScreen* screen = nullptr);  // Record full screen using screen-first semantics
    void stopRecording();           // Stop and save recording
    void cancelRecording();         // Cancel without saving
    void pauseRecording();          // Pause recording
    void resumeRecording();         // Resume recording
    void togglePause();             // Toggle pause state
    // Brings the floating control bar back into view on the recorded screen.
    // Returns false when there is no bar to show, including while recording on
    // platforms that keep active controls in the tray.
    bool showRecordingControls();
    void onPreviewClosed(bool saved);  // Handle preview window close
public:
    enum class SaveOutcome { Saved, Cancelled, Failed };
    // Saves a file exported by the preview (auto-save or the save dialog).
    // `outputSize` (pixels) replaces the recording region in the filename's
    // {w}x{h} for a cropped export; empty = region. Unlike the no-preview
    // flow, a cancelled dialog or failed save leaves the file where it is:
    // the preview still owns it and the recording it came from.
    // `savedPath` (optional) receives where the recording was saved.
    SaveOutcome triggerSaveDialog(const QString &videoPath, const QSize &outputSize = QSize(),
                                  QString *savedPath = nullptr);
public slots:

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
    void audioStatusChanged();
    void droppingFramesChanged(bool dropping);
    void lowDiskSpaceChanged(bool low);
    void recoveriesFound(const QList<SnapTray::RecordingSession::Recoverable>& recoveries);
    // A recording ended for a reason other than Stop; emitted once per recording.
    void recordingInterrupted(SnapTray::RecordingStopReason reason);
    // A recovered recording that was not saved (cancelled or failed): it can
    // still be recovered later.
    void recoveryKept(const QString& videoPath);
    void diagnosticsAvailable();
    // recordedAsIntermediate: the recording was captured as the high-quality
    // intermediate rather than at the selected quality.
    void previewRequested(const QString &tempVideoPath, int defaultOutputFormat, bool recordedAsIntermediate);

private slots:
    void captureFrame();
    void onEncodingFinished(bool success, const QString &outputPath);
    void onEncodingError(const QString &error);
    void updateDuration();

private:
    // Grant targeted access to unit tests without altering access-specifier mangling.
    friend class TestRecordingManagerStateMachine;
    friend class TestRecordingManagerLifecycle;
    friend class TestRecordingStartup;
    friend class TestLinuxRecordingPrototype;
    friend class tst_MainApplicationTrayMenu;

    void startFrameCapture();
    void initializeStartState();
    void prepareAudioPermission();
    void finishAudioPermission(quint64 generation, bool granted);
    void addStartupAudioWarning(const QString& warning);
    void flushStartupAudioWarnings();
    void reportAudioWarning(const QString& warning);
    void applyEncoderAudioResult(bool audioEnabled, const QString& warning);
    void setAudioStatus(const SnapTray::RecordingAudioStatus& status);
    // Moves every source currently in `from` to `to` (e.g. Starting -> Active).
    void transitionAudioSources(SnapTray::AudioSourceState from, SnapTray::AudioSourceState to);
    void configureAudioCapture();
    void beginAsyncInitialization();   // Start async initialization
    void onInitializationComplete(const QSharedPointer<RecordingInitTask> &task,
                                  quint64 generation);   // Handle async init completion
    void startCaptureTimers();         // Start capture and duration timers
    void stopFrameCapture();
    void cleanupRecording();
    void cleanupAudio();               // Clean up audio capture resources
    void cleanupStaleTempFiles();      // Clean up old temp files on startup
    QString generateOutputPath() const;
    void setState(State newState);
    // `keepOnCancel`: the preview flow keeps the file (and its window
    // timeline) when the dialog is cancelled or the save fails.
    SaveOutcome showSaveDialog(const QString &tempOutputPath, const QSize &outputSize = QSize(),
                               bool keepOnCancel = false, QString *savedPath = nullptr);
    void loadAndValidateFrameRate();
    void resetPauseTracking();
    bool shouldUseDedicatedEncodingThread(bool hasNativeEncoder) const;
    void teardownEncodingWorker(bool abortEncoding);

    // Window timeline for preview snapping. Only recorded when the preview
    // will open (the sidecar is its input); written next to the temp MP4.
    void startWindowTimeline();
    void finishWindowTimeline(const QString& outputPath, bool success);
    // The pointer sidecar (#159), next to the recording while it is previewed.
    qint64 mediaTimeMs();
    void startCursorTimeline();
    void dropCursorTimeline(bool removeFile);
public:
    // True while this recording writes a key sidecar (shown in the tray status).
    bool isRecordingKeys() const;
private:

    // Countdown methods
    void startCountdown();                    // Begin countdown overlay
    void startRecordingAfterCountdown();      // Called when countdown finishes
    void onCountdownCancelled();              // Handle countdown cancellation

private:
    // Recording state
    QPointer<SnapTray::QmlRecordingControlBar> m_controlBar;
    QString m_tempVideoPath;
    std::unique_ptr<EncodingWorker> m_encodingWorker;
    // GIF/WebP keep the whole animation in memory until finish(); the disk
    // guard counts it as recorded data (#171 review).
    std::shared_ptr<std::atomic<qint64>> m_bufferedOutputBytes;  // GIF/WebP held in memory until the end
    std::shared_ptr<std::atomic<qint64>> m_writtenOutputBytes;   // MP4 written to a temporary file (Linux)
    // Whether a kept file can be played (the recovery scan's probe); a seam for tests.
    std::function<bool(const QString&)> m_isRecordingPlayable;
    std::unique_ptr<QThread> m_encodingThread;         // Dedicated encoding thread for encoding workload
    quint64 m_encodingGeneration = 0;                  // Guards stale queued encoding callbacks
    bool m_usingNativeEncoder;          // True if using native encoder (vs GIF)
    bool m_encoderAcceptsNativeFrames = false;  // The MP4 encoder takes GPU frames (#155)
    CaptureEnginePtr m_captureEngine;   // Screen capture engine (stop + disconnect + deleteLater)

    // Capture state
    std::unique_ptr<QTimer> m_captureTimer;
    std::unique_ptr<QTimer> m_durationTimer;
    QElapsedTimer m_elapsedTimer;
    QRect m_recordingRegion;
    QPointer<QScreen> m_targetScreen;
    State m_state;
    int m_frameRate;
    std::atomic<qint64> m_frameCount;
    bool m_acceptAsyncFrames = false; // guarded by m_durationMutex

    // Recording clock for video frames: start gate, pause removal and
    // ordering (SessionClock), then rate limiting (FramePacer). The pacer is
    // guarded by m_durationMutex, like m_acceptAsyncFrames.
    SnapTray::SessionClock m_sessionClock;
    SnapTray::FramePacer m_framePacer;
    // With m_durationMutex held: true and the encoder time when a frame
    // captured at `sourceTimeNs` belongs in the video. `paced` applies the
    // frame pacer (backends that capture on their own thread).
    bool admitFrameLocked(qint64 sourceTimeNs, bool paced, qint64* encoderTimeMs);

    // Recording health: counters shared with the encoding worker, checked
    // once a second for sustained frame loss.
    std::shared_ptr<SnapTray::RecordingHealth> m_health;
    SnapTray::HealthWarningPolicy m_healthPolicy;
    qint64 m_healthOfferedSeen = 0;
    qint64 m_healthDroppedSeen = 0;
    int m_healthTicks = 0;
    QString m_lastDiagnostics;
    void checkHealth(qint64 nowMs);

    // Disk space of the volume the recording is written to, probed off the
    // GUI thread every kDiskCheckIntervalMs while recording or paused.
    SnapTray::DiskSpaceGuard m_diskGuard;
    SnapTray::WriteRateEstimator m_writeRate;
    QString m_recordingFilePath;
    std::unique_ptr<QTimer> m_diskTimer;
    bool m_diskProbeRunning = false;
    bool m_diskUnknownReported = false;
    bool m_lowDiskSpace = false;
    void startDiskMonitor();
    void stopDiskMonitor();
    void checkDiskSpace();
    // pendingBytes: output held in memory that still has to be written (GIF/WebP).
    void onDiskSample(quint64 generation, qint64 freeBytes, qint64 fileBytes, qint64 sampleMs,
                      qint64 pendingBytes = 0);
    void setLowDiskSpace(bool low);

    // One way out of a recording for every cause (#158): idempotent, keeps
    // what was recorded, tells the user why. Before anything is recorded
    // (preparing, countdown) it cancels instead.
    void interruptRecording(SnapTray::RecordingStopReason reason, const QString& detail = QString());
    std::optional<SnapTray::RecordingStopReason> m_stopReason;
    QList<QMetaObject::Connection> m_screenWatch;
    void watchTargetScreen();
    void unwatchTargetScreen();

    // Session manifest and lock for crash recovery (#157).
    SnapTray::RecordingSessionInfo m_session;
    std::unique_ptr<class QLockFile> m_sessionLock;
    int m_sessionHeartbeat = 0;
    void beginSession(const QString& videoPath, const QSize& frameSize, bool audioRequired);
    void updateSession(SnapTray::RecordingSessionInfo::State state);
    void endSession(bool removeManifest);
    SnapTray::RecordingHealth::ClockCounters clockCountersLocked() const;

    // Pause tracking
    qint64 m_pausedDuration;     // Total time spent paused
    qint64 m_pauseStartTime;     // When current pause began
    bool m_mediaClockPaused = false; // media time holds at m_pauseStartTime until resume
    mutable QMutex m_durationMutex;  // Protects pause duration variables

    // Audio capture
    AudioEnginePtr m_audioEngine;       // Audio engine with callback-draining deferred disposal
    bool m_audioEnabled;
    SnapTray::RecordingAudioStatus m_audioStatus;
    int m_audioSource;  // 0=Microphone, 1=SystemAudio, 2=Both
    QString m_audioDevice;
    struct StartSettings {
        int outputFormat = 0;
        bool showPreview = true;
        int quality = 55;
        bool audioEnabled = false;
        int audioSource = 0;
        QString audioDevice;
        bool countdownEnabled = true;
        int countdownSeconds = 3;
        bool windowsGraphicsCapture = false;
        bool nativeFrameEncoding = true;
        int keyRecordingMode = 0; // KeyRecordingMode; off unless switched on (#187)
    } m_startSettings;
    quint64 m_startGeneration = 0;
    bool m_permissionPending = false;
    bool m_captureExclusionWarningShown = false;
    std::function<bool()> m_captureControlsMayBeVisible;
    std::function<bool()> m_requiresDirectMp4Recording;
    // Free bytes on the volume holding `path`; replaced by tests.
    std::function<qint64(const QString&)> m_freeBytesForPath;
    bool chooseIntermediateQuality(const QString& outputDirectory, const QSize& frameSize);
    // Whether this start records the high-quality intermediate; also stored in
    // m_recordedAsIntermediate. Without preview the disk is never queried.
    bool decideIntermediateQuality(bool showPreview, const QString& outputDirectory, const QSize& frameSize);
    bool m_recordedAsIntermediate = false;
    void warnAboutVisibleCaptureControls();
    QStringList m_startupAudioWarnings;
    QStringList m_reportedStartupAudioWarnings;
    bool m_collectStartupAudioWarnings = false;
    std::function<IAudioCaptureEngine*()> m_createAudioEngine = [] {
        return IAudioCaptureEngine::createBestEngine(nullptr);
    };
    std::function<IAudioCaptureEngine::MicrophonePermission()> m_checkMicrophonePermission =
        &IAudioCaptureEngine::checkMicrophonePermission;
    std::function<void(std::function<void(bool)>)> m_requestMicrophonePermission =
        &IAudioCaptureEngine::requestMicrophonePermission;
    std::function<void()> m_initializeRecording = [this] { beginAsyncInitialization(); };

    // Async initialization
    QSharedPointer<RecordingInitTask> m_initTask;
    QFuture<void> m_initFuture;
    quint64 m_initGeneration = 0;

    // Countdown
    QmlCountdownOverlay* m_countdownOverlay = nullptr;
    bool m_countdownEnabled;
    int m_countdownSeconds;

    // Final output format selection used by direct-save and preview flows.
    int m_defaultOutputFormat = 0;  // 0=MP4, 1=GIF, 2=WebP

    // Watermark for recording (settings stored, rendering done by EncodingWorker)
    WatermarkRenderer::Settings m_watermarkSettings;
    std::unique_ptr<SnapTray::WindowTimelineRecorder> m_windowTimelineRecorder;
    std::unique_ptr<SnapTray::CursorTimelineRecorder> m_cursorRecorder;
    // Keys for shortcut hints (#187), only when switched on before recording.
    std::unique_ptr<SnapTray::KeyTimelineRecorder> m_keyRecorder;
    std::function<std::unique_ptr<SnapTray::KeyProbe>()> m_createKeyProbe = &SnapTray::createKeyProbe;
    void startKeyTimeline();
    QString m_cursorSidecarVideo;
    QString m_cursorDiagnostics;
    std::function<std::unique_ptr<SnapTray::CursorProbe>()> m_createCursorProbe = &SnapTray::createCursorProbe;
    SnapTray::WindowFrameMapping m_windowFrameMapping;
    std::function<SnapTray::WindowTimelineRecorder::Enumerator(QScreen*)> m_createWindowEnumerator =
        &SnapTray::WindowTimelineRecorder::detectorEnumerator;
};

#endif // RECORDINGMANAGER_H
