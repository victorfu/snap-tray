#ifndef SCROLLINGCAPTUREMANAGER_H
#define SCROLLINGCAPTUREMANAGER_H

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>

#include "scrolling/StitchWorker.h"
#include "scrolling/ScrollingCaptureThumbnail.h"
#include "scrolling/AutoScrollController.h"

class ScrollingCaptureOverlay;
class ScrollingCaptureToolbar;
class ScrollingCaptureOnboarding;
class PinWindowManager;
class QScreen;
class ICaptureEngine;

class ScrollingCaptureManager : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,
        Selecting,
        WaitingToStart,
        Capturing,
        MatchFailed,
        Completing,  // Waiting for worker thread to finish processing
        Completed
    };
    Q_ENUM(State)

    enum class CaptureDirection {
        Vertical,
        Horizontal
    };
    Q_ENUM(CaptureDirection)

    enum class ScrollMode {
        Manual,     // User scrolls manually
        Auto        // AutoScrollController injects scroll events
    };
    Q_ENUM(ScrollMode)

    explicit ScrollingCaptureManager(PinWindowManager *pinManager, QObject *parent = nullptr);
    ~ScrollingCaptureManager();

    bool isActive() const;
    State state() const { return m_state; }

    CaptureDirection captureDirection() const { return m_captureDirection; }
    void setCaptureDirection(CaptureDirection direction);

    // Scroll mode (manual vs auto-scroll)
    ScrollMode scrollMode() const { return m_scrollMode; }

    // Auto-scroll support
    static bool isAutoScrollSupported();
    static bool hasAutoScrollPermission();
    static void requestAutoScrollPermission();

    // Recovery info for match failure UX
    int lastSuccessfulPosition() const { return m_lastSuccessfulPosition; }
    bool isMatchFailed() const { return m_state == State::MatchFailed; }

public slots:
    void start();
    void startWithRegion(const QRect &region, QScreen *screen);
    void stop();

signals:
    void captureStarted();
    void captureCompleted(const QPixmap &result);
    void captureCancelled();
    void stateChanged(State state);
    void directionChanged(CaptureDirection direction);
    void matchStatusChanged(bool matched, double confidence, int lastSuccessfulPos);
    void scrollModeChanged(ScrollMode mode);
    void autoScrollError(const QString &message);

private slots:
    // Overlay signals
    void onRegionSelected(const QRect &region);
    void onRegionChanged(const QRect &region);
    void onSelectionConfirmed();
    void onStopRequested();
    void onOverlayCancelled();

    // Toolbar signals
    void onStartClicked();
    void onStopClicked();
    void onPinClicked();
    void onSaveClicked();
    void onCopyClicked();
    void onCloseClicked();
    void onCancelClicked();
    void onDirectionToggled();

    // Frame capture
    void captureFrame();

    // AutoScrollController signals
    void onAutoScrollReadyForCapture();
    void onAutoScrollEndOfContent();
    void onAutoScrollStateChanged(AutoScrollController::State state);
    void onAutoScrollError(const QString &message);

    // StitchWorker signals (async processing)
    void onStitchFrameProcessed(const StitchWorker::Result &result);
    void onStitchQueueNearFull(int currentDepth, int maxDepth);
    void onStitchQueueLow(int currentDepth, int maxDepth);
    void onStitchError(const QString &message);
    void onStitchFinishCompleted(const QImage &result);

private:
    void setState(State newState);
    void createComponents(const QRect* presetRegion = nullptr);
    void destroyComponents();
    void startFrameCapture();
    void stopFrameCapture();
    QImage grabCaptureRegion();
    void updateUIPositions();
    void finishCapture();
    void completeCapture();  // Common completion logic after result is ready
    void handleSuccess(const StitchWorker::Result &result);
    void handleFailure(const StitchWorker::Result &result);
    void showWarning(const StitchWorker::Result &result);

    PinWindowManager *m_pinManager;
    State m_state = State::Idle;
    CaptureDirection m_captureDirection = CaptureDirection::Vertical;
    ScrollMode m_scrollMode = ScrollMode::Manual;
    int m_lastSuccessfulPosition = 0;  // Y for vertical, X for horizontal

    // UI Components
    QPointer<ScrollingCaptureOverlay> m_overlay;
    QPointer<ScrollingCaptureToolbar> m_toolbar;
    QPointer<ScrollingCaptureThumbnail> m_thumbnail;
    QPointer<ScrollingCaptureOnboarding> m_onboarding;

    // Processing
    StitchWorker *m_stitchWorker;
    AutoScrollController *m_autoScrollController;

    // Capture engine (GPU-accelerated alternative to grabWindow)
    ICaptureEngine *m_captureEngine = nullptr;

    // Capture state
    QRect m_captureRegion;  // In global coordinates
    QScreen *m_targetScreen = nullptr;
    QTimer *m_captureTimer;
    QTimer *m_timeoutTimer;
    int m_totalFrameCount = 0;
    bool m_hasSuccessfulStitch = false;
    bool m_isProcessingFrame = false;  // Guard against reentrant captureFrame calls

    // Result
    QImage m_stitchedResult;

    // Capture rate control
    int m_captureIntervalMs = CAPTURE_INTERVAL_MS;
    ScrollingCaptureThumbnail::CaptureStatus m_currentStatus = ScrollingCaptureThumbnail::CaptureStatus::Idle;
    QElapsedTimer m_lastIntervalAdjustment;
    int m_consecutiveSuccessCount = 0;
    bool m_queuePressureRelieved = false;

    // Recovery distance tracking
    int m_captureIndex = 0;              // Incremented only for changed frames
    int m_lastSuccessCaptureIndex = 0;   // Index of last successful stitch
    int m_estimatedScrollPerFrame = 0;   // EMA of scroll delta per frame
    bool m_hasScrollEstimate = false;

    void updateRecoveryEstimate(const StitchWorker::Result &result);
    int calculateRecoveryDistance() const;
    bool isWarningFailure(ImageStitcher::FailureCode code) const;
    uint64_t computeFrameHash(const QImage &frame) const;

    // Scrolling capture timing constants
    static constexpr int CAPTURE_INTERVAL_MS = 60;
    static constexpr int MAX_CAPTURE_INTERVAL_MS = 200;
    static constexpr int INTERVAL_INCREMENT_MS = 20;
    static constexpr int INTERVAL_DECREMENT_MS = 10;
    static constexpr int ADJUSTMENT_COOLDOWN_MS = 500;
    static constexpr int SUCCESS_COUNT_FOR_SPEEDUP = 5;
    static constexpr double EMA_ALPHA = 0.3;
    static constexpr int MIN_RECOVERY_PX = 30;
    static constexpr int MAX_RECOVERY_PX = 600;
    static constexpr int DEFAULT_SCROLL_ESTIMATE = 60;
    static constexpr int MAX_CAPTURE_TIMEOUT_MS = 300000;  // 5 minutes
    static constexpr int MAX_TOTAL_FRAMES = 1000;
};

#endif // SCROLLINGCAPTUREMANAGER_H
