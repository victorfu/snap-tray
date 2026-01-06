#include "scrolling/AutoScrollController.h"
#include "scrolling/FrameStabilityDetector.h"

#include <QTimer>
#include <QDebug>

// Forward declaration of platform-specific scroll injection
namespace AutoScrollPlatform {
bool injectScrollEvent(int deltaPixels, AutoScrollController::ScrollDirection dir, const QRect &region);
}

class AutoScrollController::Private
{
public:
    Config config;
    State state = State::Idle;
    ScrollDirection direction = ScrollDirection::Down;

    QRect captureRegion;

    int scrollCount = 0;
    int consecutiveIdenticalFrames = 0;
    uint64_t lastFrameHash = 0;

    QTimer *stabilizationTimer = nullptr;
    FrameStabilityDetector *stabilityDetector = nullptr;
    double stabilityThreshold = 0.98;
};

// === Public Implementation ===

AutoScrollController::AutoScrollController(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->stabilizationTimer = new QTimer(this);
    d->stabilizationTimer->setSingleShot(true);
    connect(d->stabilizationTimer, &QTimer::timeout,
            this, &AutoScrollController::onStabilizationTimeout);

    // Create stability detector for closed-loop control
    d->stabilityDetector = new FrameStabilityDetector(this);
    FrameStabilityDetector::Config stabConfig;
    stabConfig.stabilityThreshold = d->stabilityThreshold;
    stabConfig.consecutiveStableRequired = 2;
    stabConfig.maxWaitFrames = 30;
    d->stabilityDetector->setConfig(stabConfig);
}

AutoScrollController::~AutoScrollController() = default;

void AutoScrollController::setConfig(const Config &config)
{
    d->config = config;
}

AutoScrollController::Config AutoScrollController::config() const
{
    return d->config;
}

void AutoScrollController::setCaptureRegion(const QRect &region)
{
    d->captureRegion = region;
}

QRect AutoScrollController::captureRegion() const
{
    return d->captureRegion;
}

void AutoScrollController::setDirection(ScrollDirection direction)
{
    d->direction = direction;
}

AutoScrollController::ScrollDirection AutoScrollController::direction() const
{
    return d->direction;
}

bool AutoScrollController::start()
{
    if (d->state != State::Idle) {
        qWarning() << "AutoScrollController: Already running";
        return false;
    }

    if (!isSupported()) {
        emit error(QStringLiteral("Auto-scroll not supported on this platform"));
        return false;
    }

    if (!hasAccessibilityPermission()) {
        emit error(QStringLiteral("Accessibility permission required for auto-scroll"));
        requestAccessibilityPermission();
        return false;
    }

    if (d->captureRegion.isEmpty()) {
        emit error(QStringLiteral("Capture region not set"));
        return false;
    }

    d->scrollCount = 0;
    d->consecutiveIdenticalFrames = 0;
    d->lastFrameHash = 0;

    // Start in Capturing state - first frame doesn't need scroll
    setState(State::Capturing);

    // Signal ready for first frame capture
    emit readyForCapture();

    return true;
}

void AutoScrollController::stop()
{
    d->stabilizationTimer->stop();
    setState(State::Idle);
}

void AutoScrollController::pause()
{
    if (d->state == State::Stabilizing) {
        d->stabilizationTimer->stop();
    }
    // Keep current state for resume
}

void AutoScrollController::resume()
{
    if (d->state == State::Stabilizing) {
        d->stabilizationTimer->start(d->config.stabilizationDelayMs);
    }
}

AutoScrollController::State AutoScrollController::state() const
{
    return d->state;
}

int AutoScrollController::scrollCount() const
{
    return d->scrollCount;
}

int AutoScrollController::estimatedProgress() const
{
    // Progress is unknown without page height info
    // Could be estimated from scroll position if we had access to page metrics
    return -1;
}

void AutoScrollController::triggerNextScroll()
{
    if (d->state != State::Capturing) {
        qDebug() << "AutoScrollController: triggerNextScroll called in wrong state:" << static_cast<int>(d->state);
        return;
    }

    // Check if we've reached the end
    if (detectEndOfContent()) {
        setState(State::EndReached);
        emit endOfContentReached();
        return;
    }

    // Check safety limit
    if (d->scrollCount >= d->config.maxScrollAttempts) {
        qWarning() << "AutoScrollController: Max scroll attempts reached";
        setState(State::EndReached);
        emit endOfContentReached();
        return;
    }

    // Perform scroll
    performScroll();
}

void AutoScrollController::notifyFrameCaptured(uint64_t frameHash)
{
    // End-of-content detection via frame hash comparison
    if (frameHash == d->lastFrameHash) {
        d->consecutiveIdenticalFrames++;
        qDebug() << "AutoScrollController: Identical frame detected, count:" << d->consecutiveIdenticalFrames;
    } else {
        d->consecutiveIdenticalFrames = 0;
    }

    d->lastFrameHash = frameHash;
}

bool AutoScrollController::checkFrameStability(const QImage &frame)
{
    if (!d->stabilityDetector) {
        return true;  // No detector, assume stable
    }

    if (d->state != State::Stabilizing) {
        return false;  // Only check during stabilization phase
    }

    auto result = d->stabilityDetector->addFrame(frame);

    if (result.timedOut) {
        // Stability detection timed out - proceed anyway but log warning
        qWarning() << "AutoScrollController: Stability timeout after" << result.totalFramesChecked << "frames";
        d->stabilizationTimer->stop();  // Cancel timer if running
        setState(State::Capturing);
        emit readyForCapture();
        return true;
    }

    if (result.stable) {
        qDebug() << "AutoScrollController: Frame stable after" << result.totalFramesChecked
                 << "frames, score:" << result.stabilityScore;
        d->stabilizationTimer->stop();  // Cancel timer if running
        setState(State::Capturing);
        emit readyForCapture();
        return true;
    }

    return false;
}

void AutoScrollController::setStabilityThreshold(double threshold)
{
    d->stabilityThreshold = qBound(0.0, threshold, 1.0);
    if (d->stabilityDetector) {
        auto config = d->stabilityDetector->config();
        config.stabilityThreshold = d->stabilityThreshold;
        d->stabilityDetector->setConfig(config);
    }
}

void AutoScrollController::performScroll()
{
    setState(State::Scrolling);

    // Calculate scroll delta based on overlap ratio
    // scrollDelta = frameHeight * (1 - overlapRatio)
    int frameHeight = (d->direction == ScrollDirection::Down || d->direction == ScrollDirection::Up)
                          ? d->captureRegion.height()
                          : d->captureRegion.width();

    int scrollDelta = static_cast<int>(frameHeight * (1.0 - d->config.targetOverlapRatio));
    scrollDelta = qMin(scrollDelta, d->config.scrollDeltaPixels);
    scrollDelta = qMax(scrollDelta, 50); // Minimum scroll

    if (AutoScrollPlatform::injectScrollEvent(scrollDelta, d->direction, d->captureRegion)) {
        d->scrollCount++;
        emit scrollPerformed(d->scrollCount, scrollDelta);

        // Reset stability detector for closed-loop control
        if (d->stabilityDetector) {
            d->stabilityDetector->reset();
        }

        // Wait for stabilization
        // The timer serves as a fallback; closed-loop checkFrameStability() is preferred
        setState(State::Stabilizing);
        d->stabilizationTimer->start(d->config.stabilizationDelayMs);
    } else {
        setState(State::Error);
        emit error(QStringLiteral("Failed to inject scroll event"));
    }
}

void AutoScrollController::onStabilizationTimeout()
{
    // Screen should be stable now, ready for capture
    setState(State::Capturing);
    emit readyForCapture();
}

bool AutoScrollController::detectEndOfContent()
{
    // End detection based on consecutive identical frame hashes
    return d->consecutiveIdenticalFrames >= d->config.endDetectionThreshold;
}

void AutoScrollController::setState(State newState)
{
    if (d->state != newState) {
        d->state = newState;
        emit stateChanged(newState);
    }
}

// Platform-specific stubs for unsupported platforms
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)

namespace AutoScrollPlatform {
bool injectScrollEvent(int /*deltaPixels*/, AutoScrollController::ScrollDirection /*dir*/, const QRect & /*region*/)
{
    qWarning() << "AutoScrollController: Scroll injection not implemented for this platform";
    return false;
}
}

bool AutoScrollController::isSupported()
{
    return false;
}

bool AutoScrollController::hasAccessibilityPermission()
{
    return true;
}

void AutoScrollController::requestAccessibilityPermission()
{
    // No-op on unsupported platforms
}

#endif
