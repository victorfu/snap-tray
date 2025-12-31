#ifndef AUTOSCROLLCONTROLLER_H
#define AUTOSCROLLCONTROLLER_H

#include <QObject>
#include <QRect>
#include <memory>

/**
 * @brief Controls automated scrolling for perfect capture conditions
 *
 * This class transforms the scrolling capture from a passive observation
 * to an active, closed-loop control system. It injects scroll events,
 * waits for screen stability, then signals for frame capture.
 *
 * Benefits:
 * - Precise overlap control (30-50% configurable)
 * - No motion blur (capture during stillness)
 * - Automatic end-of-content detection
 * - One-click operation for users
 *
 * Closed-loop control flow:
 *   Inject Scroll -> Wait 50ms -> Signal Capture -> Repeat
 */
class AutoScrollController : public QObject
{
    Q_OBJECT

public:
    enum class ScrollDirection {
        Down,
        Up,
        Left,
        Right
    };
    Q_ENUM(ScrollDirection)

    enum class State {
        Idle,
        Scrolling,       // Scroll event injected, waiting for motion
        Stabilizing,     // Waiting for screen to settle
        Capturing,       // Ready for frame capture
        EndReached,      // Bottom/end of content detected
        Error
    };
    Q_ENUM(State)

    struct Config {
        int scrollDeltaPixels = 500;         // Pixels per scroll step
        int stabilizationDelayMs = 50;       // Wait after scroll before capture
        double targetOverlapRatio = 0.35;    // 35% overlap between frames
        int maxScrollAttempts = 200;         // Safety limit
        int endDetectionThreshold = 3;       // Consecutive identical frames
        bool useAccessibilityAPI = true;     // Use AX API for precise control
    };

    explicit AutoScrollController(QObject *parent = nullptr);
    ~AutoScrollController() override;

    // Configuration
    void setConfig(const Config &config);
    Config config() const;

    void setCaptureRegion(const QRect &region);
    QRect captureRegion() const;

    void setDirection(ScrollDirection direction);
    ScrollDirection direction() const;

    // Control
    bool start();
    void stop();
    void pause();
    void resume();

    // State
    State state() const;
    int scrollCount() const;
    int estimatedProgress() const; // 0-100, -1 if unknown

    // Manual trigger (for hybrid mode or after frame processed)
    void triggerNextScroll();

    // Notify that a frame was captured and processed
    // Used for end-of-content detection via frame hash comparison
    void notifyFrameCaptured(uint64_t frameHash);

    // Platform support
    static bool isSupported();
    static bool hasAccessibilityPermission();
    static void requestAccessibilityPermission();

signals:
    // Emitted when ready to capture a frame
    void readyForCapture();

    // Emitted when end of content is detected
    void endOfContentReached();

    // State changes
    void stateChanged(State newState);
    void scrollPerformed(int scrollIndex, int deltaPixels);

    // Errors
    void error(const QString &message);

    // Progress updates
    void progressUpdated(int scrollCount, int estimatedTotal);

private slots:
    void onStabilizationTimeout();

private:
    class Private;
    std::unique_ptr<Private> d;

    void performScroll();
    bool detectEndOfContent();
    void setState(State newState);
};

#endif // AUTOSCROLLCONTROLLER_H
