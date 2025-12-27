#ifndef SCROLLINGCAPTUREMANAGER_H
#define SCROLLINGCAPTUREMANAGER_H

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <vector>

class ScrollingCaptureOverlay;
class ScrollingCaptureToolbar;
class ScrollingCaptureThumbnail;
class ImageStitcher;
class FixedElementDetector;
class PinWindowManager;
class QScreen;

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
        Completed
    };
    Q_ENUM(State)

    explicit ScrollingCaptureManager(PinWindowManager *pinManager, QObject *parent = nullptr);
    ~ScrollingCaptureManager();

    bool isActive() const;
    State state() const { return m_state; }

public slots:
    void start();
    void startWithRegion(const QRect &region, QScreen *screen);
    void stop();

signals:
    void captureStarted();
    void captureCompleted(const QPixmap &result);
    void captureCancelled();
    void stateChanged(State state);

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

    // Frame capture
    void captureFrame();

private:
    void setState(State newState);
    void createComponents();
    void createComponentsWithRegion();
    void destroyComponents();
    void startFrameCapture();
    void stopFrameCapture();
    QImage grabCaptureRegion();
    void updateUIPositions();
    void finishCapture();
    bool restitchWithFixedElements();

    PinWindowManager *m_pinManager;
    State m_state = State::Idle;

    // UI Components
    QPointer<ScrollingCaptureOverlay> m_overlay;
    QPointer<ScrollingCaptureToolbar> m_toolbar;
    QPointer<ScrollingCaptureThumbnail> m_thumbnail;

    // Processing
    ImageStitcher *m_stitcher;
    FixedElementDetector *m_fixedDetector;

    // Capture state
    QRect m_captureRegion;  // In global coordinates
    QScreen *m_targetScreen = nullptr;
    QTimer *m_captureTimer;
    QImage m_lastFrame;
    int m_unchangedFrameCount = 0;
    bool m_fixedElementsDetected = false;
    bool m_hasSuccessfulStitch = false;
    std::vector<QImage> m_pendingFrames;

    // Result
    QImage m_stitchedResult;

    static constexpr int CAPTURE_INTERVAL_MS = 60;  // ~16 FPS
    static constexpr int UNCHANGED_FRAME_THRESHOLD = 5;
};

#endif // SCROLLINGCAPTUREMANAGER_H
