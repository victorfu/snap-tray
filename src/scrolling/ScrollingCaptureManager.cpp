#include "scrolling/ScrollingCaptureManager.h"
#include "scrolling/ScrollingCaptureOverlay.h"
#include "scrolling/ScrollingCaptureToolbar.h"
#include "scrolling/ScrollingCaptureThumbnail.h"
#include "scrolling/ScrollingCaptureOnboarding.h"
#include "scrolling/ImageStitcher.h"
#include "scrolling/FixedElementDetector.h"
#include "scrolling/StitchWorker.h"
#include "PinWindowManager.h"

#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QClipboard>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>

ScrollingCaptureManager::ScrollingCaptureManager(PinWindowManager *pinManager, QObject *parent)
    : QObject(parent)
    , m_pinManager(pinManager)
    , m_stitcher(new ImageStitcher(this))
    , m_fixedDetector(new FixedElementDetector(this))
    , m_stitchWorker(new StitchWorker(this))
    , m_captureTimer(new QTimer(this))
    , m_timeoutTimer(new QTimer(this))
{
    connect(m_captureTimer, &QTimer::timeout, this, &ScrollingCaptureManager::captureFrame);

    // Timeout timer - single shot
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "ScrollingCaptureManager: Capture timeout reached";
        finishCapture();
    });

    // Connect sync stitcher signals (used when m_useAsyncStitching is false)
    connect(m_stitcher, &ImageStitcher::progressUpdated, this, [this](int frames, const QSize &size) {
        if (!m_useAsyncStitching) {
            if (m_toolbar) {
                qreal dpr = m_targetScreen ? m_targetScreen->devicePixelRatio() : 1.0;
                m_toolbar->updateSize(size.width() / dpr, size.height() / dpr);
            }
            if (m_thumbnail) {
                // Legacy thumbnail calls removed
            }
        }
    });

    // Connect async StitchWorker signals
    connect(m_stitchWorker, &StitchWorker::frameProcessed,
            this, &ScrollingCaptureManager::onStitchFrameProcessed);
    connect(m_stitchWorker, &StitchWorker::fixedElementsDetected,
            this, &ScrollingCaptureManager::onStitchFixedElementsDetected);
    connect(m_stitchWorker, &StitchWorker::queueNearFull,
            this, &ScrollingCaptureManager::onStitchQueueNearFull);
    connect(m_stitchWorker, &StitchWorker::queueLow,
            this, &ScrollingCaptureManager::onStitchQueueLow);
    connect(m_stitchWorker, &StitchWorker::error,
            this, &ScrollingCaptureManager::onStitchError);
}

ScrollingCaptureManager::~ScrollingCaptureManager()
{
    stop();
}

bool ScrollingCaptureManager::isActive() const
{
    return m_state != State::Idle;
}

void ScrollingCaptureManager::start()
{
    if (isActive()) {
        qDebug() << "ScrollingCaptureManager: Already active";
        return;
    }

    // Detect screen under cursor
    QPoint cursorPos = QCursor::pos();
    m_targetScreen = QGuiApplication::screenAt(cursorPos);
    if (!m_targetScreen) {
        m_targetScreen = QGuiApplication::primaryScreen();
    }

    createComponents();
    setState(State::Selecting);

    emit captureStarted();
}

void ScrollingCaptureManager::startWithRegion(const QRect &region, QScreen *screen)
{
    if (isActive()) {
        qDebug() << "ScrollingCaptureManager: Already active";
        return;
    }

    m_targetScreen = screen ? screen : QGuiApplication::primaryScreen();
    m_captureRegion = region;

    qDebug() << "ScrollingCaptureManager: Starting with pre-selected region:" << region;

    createComponentsWithRegion();
    setState(State::WaitingToStart);

    // Show and position toolbar immediately
    if (m_toolbar) {
        m_toolbar->show();
        m_toolbar->updateSize(region.width(), region.height());
        updateUIPositions();
    }

    emit captureStarted();
}

void ScrollingCaptureManager::stop()
{
    stopFrameCapture();

    // Reset worker to clear any pending frames and release resources
    if (m_stitchWorker) {
        m_stitchWorker->reset();
    }

    destroyComponents();
    setState(State::Idle);
}

void ScrollingCaptureManager::setState(State newState)
{
    if (m_state == newState) {
        return;
    }

    State oldState = m_state;
    m_state = newState;

    qDebug() << "ScrollingCaptureManager: State changed from" << static_cast<int>(oldState)
             << "to" << static_cast<int>(newState);

    // Update UI based on state
    if (m_overlay) {
        switch (newState) {
        case State::Selecting:
            m_overlay->setBorderState(ScrollingCaptureOverlay::BorderState::Selecting);
            m_overlay->setRegionLocked(false);
            break;
        case State::WaitingToStart:
            m_overlay->setBorderState(ScrollingCaptureOverlay::BorderState::Adjusting);
            m_overlay->setRegionLocked(false);
            break;
        case State::Capturing:
            m_overlay->setBorderState(ScrollingCaptureOverlay::BorderState::Capturing);
            m_overlay->setRegionLocked(true);
            break;
        case State::MatchFailed:
            m_overlay->setBorderState(ScrollingCaptureOverlay::BorderState::MatchFailed);
            m_overlay->setRegionLocked(true);
            break;
        case State::Completed:
            m_overlay->setBorderState(ScrollingCaptureOverlay::BorderState::Capturing);
            m_overlay->setRegionLocked(true);
            break;
        default:
            break;
        }
    }

    if (m_toolbar) {
        switch (newState) {
        case State::Selecting:
        case State::WaitingToStart:
            m_toolbar->setMode(ScrollingCaptureToolbar::Mode::Adjusting);
            break;
        case State::Capturing:
        case State::MatchFailed:
            m_toolbar->setMode(ScrollingCaptureToolbar::Mode::Capturing);
            break;
        case State::Completed:
            m_toolbar->setMode(ScrollingCaptureToolbar::Mode::Finished);
            break;
        default:
            break;
        }
    }

    emit stateChanged(newState);
}

void ScrollingCaptureManager::createComponents()
{
    // Create overlay
    m_overlay = new ScrollingCaptureOverlay();
    connect(m_overlay, &ScrollingCaptureOverlay::regionSelected, this, &ScrollingCaptureManager::onRegionSelected);
    connect(m_overlay, &ScrollingCaptureOverlay::regionChanged, this, &ScrollingCaptureManager::onRegionChanged);
    connect(m_overlay, &ScrollingCaptureOverlay::selectionConfirmed, this, &ScrollingCaptureManager::onSelectionConfirmed);
    connect(m_overlay, &ScrollingCaptureOverlay::stopRequested, this, &ScrollingCaptureManager::onStopRequested);
    connect(m_overlay, &ScrollingCaptureOverlay::cancelled, this, &ScrollingCaptureManager::onOverlayCancelled);
    m_overlay->initializeForScreen(m_targetScreen);

    // Create toolbar
    m_toolbar = new ScrollingCaptureToolbar();
    connect(m_toolbar, &ScrollingCaptureToolbar::startClicked, this, &ScrollingCaptureManager::onStartClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::stopClicked, this, &ScrollingCaptureManager::onStopClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::pinClicked, this, &ScrollingCaptureManager::onPinClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::saveClicked, this, &ScrollingCaptureManager::onSaveClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::copyClicked, this, &ScrollingCaptureManager::onCopyClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::closeClicked, this, &ScrollingCaptureManager::onCloseClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::cancelClicked, this, &ScrollingCaptureManager::onCancelClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::directionToggled, this, &ScrollingCaptureManager::onDirectionToggled);
    m_toolbar->hide();

    // Create thumbnail
    m_thumbnail = new ScrollingCaptureThumbnail();
    m_thumbnail->hide();

    // Create onboarding
    m_onboarding = new ScrollingCaptureOnboarding(m_overlay);
    m_onboarding->hide();

    // Reset stitcher and detector
    m_stitcher->reset();
    m_fixedDetector->reset();
}

void ScrollingCaptureManager::createComponentsWithRegion()
{
    // Create overlay with pre-selected region (skips selection phase)
    m_overlay = new ScrollingCaptureOverlay();
    // Don't connect regionSelected since we already have the region
    connect(m_overlay, &ScrollingCaptureOverlay::regionChanged, this, &ScrollingCaptureManager::onRegionChanged);
    connect(m_overlay, &ScrollingCaptureOverlay::selectionConfirmed, this, &ScrollingCaptureManager::onSelectionConfirmed);
    connect(m_overlay, &ScrollingCaptureOverlay::stopRequested, this, &ScrollingCaptureManager::onStopRequested);
    connect(m_overlay, &ScrollingCaptureOverlay::cancelled, this, &ScrollingCaptureManager::onOverlayCancelled);
    m_overlay->initializeForScreenWithRegion(m_targetScreen, m_captureRegion);

    // Create toolbar (same as createComponents)
    m_toolbar = new ScrollingCaptureToolbar();
    connect(m_toolbar, &ScrollingCaptureToolbar::startClicked, this, &ScrollingCaptureManager::onStartClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::stopClicked, this, &ScrollingCaptureManager::onStopClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::pinClicked, this, &ScrollingCaptureManager::onPinClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::saveClicked, this, &ScrollingCaptureManager::onSaveClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::copyClicked, this, &ScrollingCaptureManager::onCopyClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::closeClicked, this, &ScrollingCaptureManager::onCloseClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::cancelClicked, this, &ScrollingCaptureManager::onCancelClicked);
    connect(m_toolbar, &ScrollingCaptureToolbar::directionToggled, this, &ScrollingCaptureManager::onDirectionToggled);
    m_toolbar->hide();

    // Create thumbnail
    m_thumbnail = new ScrollingCaptureThumbnail();
    m_thumbnail->hide();

    // Create onboarding
    m_onboarding = new ScrollingCaptureOnboarding(m_overlay);
    m_onboarding->hide();

    // Reset stitcher and detector
    m_stitcher->reset();
    m_fixedDetector->reset();
}

void ScrollingCaptureManager::destroyComponents()
{
    if (m_overlay) {
        m_overlay->close();
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }

    if (m_toolbar) {
        m_toolbar->close();
        m_toolbar->deleteLater();
        m_toolbar = nullptr;
    }

    if (m_thumbnail) {
        m_thumbnail->close();
        m_thumbnail->deleteLater();
        m_thumbnail = nullptr;
    }

    if (m_onboarding) {
        m_onboarding->close();
        m_onboarding->deleteLater();
        m_onboarding = nullptr;
    }

    // Reset all state
    m_captureRegion = QRect();
    m_targetScreen = nullptr;
    m_lastFrame = QImage();
    m_stitchedResult = QImage();
    m_totalFrameCount = 0;
    m_fixedElementsDetected = false;
    m_hasSuccessfulStitch = false;
    m_isProcessingFrame = false;
    m_pendingFrames.clear();
}

void ScrollingCaptureManager::onRegionSelected(const QRect &region)
{
    m_captureRegion = region;
    setState(State::WaitingToStart);

    // Show and position toolbar
    if (m_toolbar) {
        m_toolbar->show();
        m_toolbar->updateSize(region.width(), region.height());
        updateUIPositions();
    }
}

void ScrollingCaptureManager::onRegionChanged(const QRect &region)
{
    m_captureRegion = region;
    if (m_toolbar) {
        m_toolbar->updateSize(region.width(), region.height());
    }
    updateUIPositions();
}

void ScrollingCaptureManager::onSelectionConfirmed()
{
    if (m_state != State::WaitingToStart) {
        return;
    }

    if (m_onboarding && !ScrollingCaptureOnboarding::hasSeenTutorial()) {
        m_onboarding->showTutorial();
        // Connect signal to start capture when tutorial is dismissed
        connect(m_onboarding, &ScrollingCaptureOnboarding::tutorialCompleted,
                this, &ScrollingCaptureManager::onStartClicked,
                Qt::UniqueConnection);
    } else {
        if (m_onboarding) {
            m_onboarding->showHint();
        }
        onStartClicked();
    }
}

void ScrollingCaptureManager::onStopRequested()
{
    if (m_state == State::Capturing || m_state == State::MatchFailed) {
        finishCapture();
    }
}

void ScrollingCaptureManager::onOverlayCancelled()
{
    stop();
    emit captureCancelled();
}

void ScrollingCaptureManager::onStartClicked()
{
    if (m_state != State::WaitingToStart) {
        return;
    }

    // Show thumbnail
    if (m_thumbnail) {
        m_thumbnail->show();
        updateUIPositions();
    }

    startFrameCapture();
    setState(State::Capturing);
}

void ScrollingCaptureManager::onStopClicked()
{
    // Defer finishCapture() to allow toolbar's event handler to complete
    // (finishCapture may call stop() which destroys the toolbar)
    QTimer::singleShot(0, this, [this]() {
        finishCapture();
    });
}

void ScrollingCaptureManager::onPinClicked()
{
    if (m_stitchedResult.isNull()) {
        return;
    }

    QPixmap pixmap = QPixmap::fromImage(m_stitchedResult);
    if (m_pinManager) {
        m_pinManager->createPinWindow(pixmap, m_captureRegion.topLeft());
    }

    emit captureCompleted(pixmap);

    // Defer stop() to allow toolbar's event handler to complete
    QTimer::singleShot(0, this, [this]() {
        stop();
    });
}

void ScrollingCaptureManager::onSaveClicked()
{
    if (m_stitchedResult.isNull()) {
        qDebug() << "ScrollingCaptureManager: No image to save";
        return;
    }

    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultName = QString("%1/ScrollCapture_%2.png").arg(picturesPath).arg(timestamp);

    QString filePath = QFileDialog::getSaveFileName(
        nullptr,
        "Save Scrolling Capture",
        defaultName,
        "PNG Image (*.png);;JPEG Image (*.jpg);;All Files (*)"
    );

    if (!filePath.isEmpty()) {
        bool saved = m_stitchedResult.save(filePath);
        if (saved) {
            qDebug() << "ScrollingCaptureManager: Saved to" << filePath;
        } else {
            qDebug() << "ScrollingCaptureManager: Failed to save to" << filePath;
            // Could emit a signal here to notify the UI of the failure
        }
    }
}

void ScrollingCaptureManager::onCopyClicked()
{
    if (m_stitchedResult.isNull()) {
        return;
    }

    QClipboard *clipboard = QGuiApplication::clipboard();
    clipboard->setImage(m_stitchedResult);
    qDebug() << "ScrollingCaptureManager: Copied to clipboard";
}

void ScrollingCaptureManager::onCloseClicked()
{
    // Defer stop() to allow toolbar's event handler to complete
    QTimer::singleShot(0, this, [this]() {
        stop();
    });
}

void ScrollingCaptureManager::onCancelClicked()
{
    // Defer stop() to next event loop iteration to allow the toolbar's
    // mouse event handler to complete first (avoids crash from destroying
    // widget while inside its own event handler)
    QTimer::singleShot(0, this, [this]() {
        stop();
        emit captureCancelled();
    });
}

void ScrollingCaptureManager::onDirectionToggled()
{
    // Toggle between vertical and horizontal
    if (m_captureDirection == CaptureDirection::Vertical) {
        setCaptureDirection(CaptureDirection::Horizontal);
    } else {
        setCaptureDirection(CaptureDirection::Vertical);
    }
}

void ScrollingCaptureManager::setCaptureDirection(CaptureDirection direction)
{
    if (m_captureDirection == direction) {
        return;
    }

    m_captureDirection = direction;

    // Update stitcher capture mode
    if (m_stitcher) {
        m_stitcher->setCaptureMode(direction == CaptureDirection::Vertical
            ? ImageStitcher::CaptureMode::Vertical
            : ImageStitcher::CaptureMode::Horizontal);
    }

    // Update fixed element detector capture mode
    if (m_fixedDetector) {
        m_fixedDetector->setCaptureMode(direction == CaptureDirection::Vertical
            ? FixedElementDetector::CaptureMode::Vertical
            : FixedElementDetector::CaptureMode::Horizontal);
    }

    // Update toolbar direction display
    if (m_toolbar) {
        m_toolbar->setDirection(direction == CaptureDirection::Vertical
            ? ScrollingCaptureToolbar::Direction::Vertical
            : ScrollingCaptureToolbar::Direction::Horizontal);
    }

    // Update thumbnail direction
    if (m_thumbnail) {
        m_thumbnail->setDirection(direction == CaptureDirection::Vertical
            ? ScrollingCaptureThumbnail::Direction::Vertical
            : ScrollingCaptureThumbnail::Direction::Horizontal);
    }

    // Update overlay direction for constrained move during capture
    if (m_overlay) {
        m_overlay->setCaptureDirection(direction == CaptureDirection::Vertical
            ? ScrollingCaptureOverlay::CaptureDirection::Vertical
            : ScrollingCaptureOverlay::CaptureDirection::Horizontal);
    }

    emit directionChanged(direction);
}

void ScrollingCaptureManager::startFrameCapture()
{
    m_captureIntervalMs = CAPTURE_INTERVAL_MS;
    m_captureTimer->setInterval(m_captureIntervalMs);
    m_captureTimer->start(m_captureIntervalMs);
    m_timeoutTimer->start(MAX_CAPTURE_TIMEOUT_MS);
    m_totalFrameCount = 0;
    m_consecutiveSuccessCount = 0;
    m_queuePressureRelieved = false;
    m_currentStatus = ScrollingCaptureThumbnail::CaptureStatus::Capturing;
    m_lastIntervalAdjustment.invalidate();
    m_captureIndex = 0;
    m_lastSuccessCaptureIndex = 0;
    m_estimatedScrollPerFrame = 0;
    m_hasScrollEstimate = false;
    m_isProcessingFrame = false;
    m_pendingFrames.clear();

    // Initialize StitchWorker if using async mode
    if (m_useAsyncStitching) {
        m_stitchWorker->reset();
        m_stitchWorker->setCaptureMode(m_captureDirection == CaptureDirection::Vertical
            ? StitchWorker::CaptureMode::Vertical
            : StitchWorker::CaptureMode::Horizontal);
        m_stitchWorker->setStitchConfig(0.4, true);  // confidence threshold, detect static regions
    }
}

void ScrollingCaptureManager::stopFrameCapture()
{
    m_captureTimer->stop();
    m_timeoutTimer->stop();
    m_currentStatus = ScrollingCaptureThumbnail::CaptureStatus::Idle;

    // Note: Do NOT reset worker here - finishCapture() needs to retrieve the stitched result first.
    // The worker will be reset when a new capture starts or when stop() is called.
}

bool ScrollingCaptureManager::restitchWithFixedElements()
{
    if (m_pendingFrames.empty()) {
        // No pending frames means we haven't captured anything yet
        // This is not an error, just nothing to restitch
        qDebug() << "ScrollingCaptureManager: No pending frames to restitch";
        return false;  // Return false to indicate nothing was restitched
    }

    // Save current stitcher state in case restitch fails completely
    QImage previousResult = m_stitcher->getStitchedImage();
    int previousFrameCount = m_stitcher->frameCount();
    QImage previousLastFrame = m_lastFrame;
    bool previousHasSuccessfulStitch = m_hasSuccessfulStitch;

    m_stitcher->reset();

    bool anySucceeded = false;
    int successCount = 0;

    QImage lastRestitchedFrame;

    for (const QImage &rawFrame : m_pendingFrames) {
        QImage frameToStitch = m_fixedDetector->cropFixedRegions(rawFrame);
        if (frameToStitch.isNull()) {
            qDebug() << "ScrollingCaptureManager: cropFixedRegions returned null frame";
            continue;
        }

        lastRestitchedFrame = rawFrame;

        ImageStitcher::StitchResult result = m_stitcher->addFrame(frameToStitch);
        if (result.success) {
            anySucceeded = true;
            successCount++;
        } else if (result.failureReason != "Frame unchanged") {
            qDebug() << "ScrollingCaptureManager: Restitch failed -" << result.failureReason;
            // Continue trying remaining frames to salvage what we can
        }
    }

    m_pendingFrames.clear();

    if (anySucceeded) {
        if (!lastRestitchedFrame.isNull()) {
            m_lastFrame = lastRestitchedFrame;
        }

        m_hasSuccessfulStitch = m_stitcher->frameCount() > 1;

        QRect viewport = m_stitcher->currentViewportRect();
        m_lastSuccessfulPosition = (m_captureDirection == CaptureDirection::Vertical)
            ? viewport.bottom()
            : viewport.right();
    }

    // If restitch completely failed, try to restore previous state
    if (!anySucceeded && !previousResult.isNull() && previousFrameCount > 0) {
        qDebug() << "ScrollingCaptureManager: Restitch failed, restoring previous state";
        // Restore stitcher by re-adding the previous result as initial frame
        m_stitcher->reset();
        // Re-initialize with previous result - add it as the first frame
        // This isn't perfect but preserves the work done so far
        m_stitcher->addFrame(previousResult);
        m_lastFrame = previousLastFrame;
        m_hasSuccessfulStitch = previousHasSuccessfulStitch;
        return false;
    }

    qDebug() << "ScrollingCaptureManager: Restitch completed -" << successCount << "frames succeeded";
    return anySucceeded;
}

void ScrollingCaptureManager::captureFrame()
{
    // Guard against reentrant calls (timer callback during finishCapture)
    if (m_isProcessingFrame) {
        return;
    }

    if (m_state != State::Capturing && m_state != State::MatchFailed) {
        return;
    }

    m_isProcessingFrame = true;

    // Performance timing for Phase 0 profiling
    QElapsedTimer perfTimer;
    qint64 grabMs = 0, detectMs = 0, stitchMs = 0, totalMs = 0;
    perfTimer.start();

    // Check if we've hit the maximum frame limit BEFORE incrementing
    // This prevents processing one extra frame beyond the limit
    if (m_totalFrameCount >= MAX_TOTAL_FRAMES) {
        qDebug() << "ScrollingCaptureManager: Maximum frame count reached, finishing capture";
        m_isProcessingFrame = false;
        finishCapture();
        return;
    }

    m_totalFrameCount++;

    QImage frame = grabCaptureRegion();
    grabMs = perfTimer.elapsed();

    if (frame.isNull()) {
        qDebug() << "ScrollingCaptureManager: Failed to grab capture region";
        m_isProcessingFrame = false;
        return;
    }

    // Async mode: just enqueue frame and return quickly
    if (m_useAsyncStitching) {
        if (m_thumbnail) {
            m_thumbnail->setViewportImage(frame);
        }
        m_lastFrame = frame;
        m_captureIndex++;
        bool queued = m_stitchWorker->enqueueFrame(frame);
        if (!queued) {
            qDebug() << "ScrollingCaptureManager: Frame dropped (queue full)";
        }
        qDebug() << "ScrollingCaptureManager::captureFrame (async) - grab:" << grabMs << "ms";
        m_isProcessingFrame = false;
        return;
    }

    // Sync mode: process frame directly (original logic)
    // Check if frame changed relative to previous capture
    bool frameChanged = true;
    if (!m_lastFrame.isNull()) {
        frameChanged = ImageStitcher::isFrameChanged(frame, m_lastFrame);
    }

    bool restitched = false;

    // Add frame to fixed element detector (use detector's own frame count)
    // Only add if frame has changed to avoid polluting detector with static frames
    if (frameChanged && !m_fixedElementsDetected) {
        m_fixedDetector->addFrame(frame);

        // Store frames until fixed elements are detected, with a limit
        if (static_cast<int>(m_pendingFrames.size()) < MAX_PENDING_FRAMES) {
            m_pendingFrames.push_back(frame);
        } else {
            // If we hit the limit, give up on fixed element detection
            // and clear pending frames to free memory
            qDebug() << "ScrollingCaptureManager: Max pending frames reached, disabling fixed element detection";
            m_fixedElementsDetected = true;  // Disable further detection attempts
            m_pendingFrames.clear();
            emit fixedElementDetectionDisabled();
        }

        perfTimer.restart();
        auto detection = m_fixedDetector->detect();
        detectMs = perfTimer.elapsed();

        if (detection.detected) {
            m_fixedElementsDetected = true;
            qDebug() << "ScrollingCaptureManager: Fixed elements detected -"
                     << "leading:" << detection.leadingFixed
                     << "trailing:" << detection.trailingFixed;
            bool restitchSuccess = restitchWithFixedElements();
            restitched = true;

            if (!restitchSuccess) {
                // Restitch failed or had no frames, continue with normal stitching
                qDebug() << "ScrollingCaptureManager: Restitch had issues, continuing normally";
            }

            // Recover to Capturing state after restitch attempt
            if (m_state == State::MatchFailed) {
                setState(State::Capturing);
            }
        }
    }

    if (restitched) {
        // Always store the raw frame for frame change detection
        // This ensures consistent comparison (raw vs raw) to avoid false positives
        // after fixed element cropping. The cropping is applied separately during stitching.
        m_lastFrame = frame;
        // Emit UI update signal after successful restitch
        if (m_stitcher->frameCount() > 0) {
            emit matchStatusChanged(true, 0.8, m_lastSuccessfulPosition);
        }
        totalMs = grabMs + detectMs;
        qDebug() << "ScrollingCaptureManager::captureFrame perf (restitch) - grab:" << grabMs
                 << "ms, detect:" << detectMs << "ms, total:" << totalMs << "ms";
        m_isProcessingFrame = false;
        return;
    }

    // Crop fixed elements if detected
    QImage frameToStitch = frame;
    if (m_fixedElementsDetected) {
        frameToStitch = m_fixedDetector->cropFixedRegions(frame);
        if (frameToStitch.isNull()) {
            qDebug() << "ScrollingCaptureManager: cropFixedRegions returned null, using original frame";
            frameToStitch = frame;
        }
    }

    // Try to stitch
    perfTimer.restart();
    ImageStitcher::StitchResult result = m_stitcher->addFrame(frameToStitch);
    stitchMs = perfTimer.elapsed();

    qDebug() << "ScrollingCaptureManager::captureFrame -"
             << "success:" << result.success
             << "frameCount:" << m_stitcher->frameCount()
             << "hasSuccessfulStitch:" << m_hasSuccessfulStitch
             << "reason:" << result.failureReason;

    if (result.success) {
        // Mark that scrolling has started only after actual stitching (not first frame)
        // frameCount > 1 means at least 2 frames have been processed (actual stitch occurred)
        if (m_stitcher->frameCount() > 1) {
            m_hasSuccessfulStitch = true;
        }

        // Track last successful position for recovery UX
        QRect viewport = m_stitcher->currentViewportRect();
        m_lastSuccessfulPosition = (m_captureDirection == CaptureDirection::Vertical)
            ? viewport.bottom() : viewport.right();

        // Update UI
        if (m_toolbar) {
            m_toolbar->setMatchStatus(true, result.confidence);
            m_toolbar->setMatchRecoveryInfo(m_lastSuccessfulPosition, false);
        }
        if (m_thumbnail) {
            ScrollingCaptureThumbnail::MatchStatus status;
            if (result.confidence >= 0.70) {
                status = ScrollingCaptureThumbnail::MatchStatus::Good;
            } else if (result.confidence >= 0.50) {
                status = ScrollingCaptureThumbnail::MatchStatus::Warning;
            } else {
                status = ScrollingCaptureThumbnail::MatchStatus::Failed;
            }
            m_thumbnail->setMatchStatus(status, result.confidence);
            m_thumbnail->setLastSuccessfulPosition(m_lastSuccessfulPosition);
            m_thumbnail->setShowRecoveryHint(false);
        }

        // If we were in MatchFailed, recover to Capturing
        if (m_state == State::MatchFailed) {
            setState(State::Capturing);
            if (m_overlay) {
                m_overlay->clearMatchFailedMessage();
            }
        }
        emit matchStatusChanged(true, result.confidence, m_lastSuccessfulPosition);
    } else {
        // Check if maximum size was reached (height for vertical, width for horizontal)
        if (result.failureReason.contains("Maximum height reached") ||
            result.failureReason.contains("Maximum width reached")) {
            qDebug() << "ScrollingCaptureManager: Maximum size reached, finishing capture";
            m_isProcessingFrame = false;
            finishCapture();
            return;
        }

        // Frame unchanged - just skip, don't count or auto-finish
        if (result.failureReason == "Frame unchanged") {
            m_isProcessingFrame = false;
            return;
        }

        // Match failed
        if (m_state != State::MatchFailed) {
            setState(State::MatchFailed);
        }

        if (m_toolbar) {
            m_toolbar->setMatchStatus(false, result.confidence);
            m_toolbar->setMatchRecoveryInfo(m_lastSuccessfulPosition, true);
        }
        if (m_thumbnail) {
            m_thumbnail->setMatchStatus(ScrollingCaptureThumbnail::MatchStatus::Failed, result.confidence);
            m_thumbnail->setShowRecoveryHint(true);
        }
        if (m_overlay) {
            QString hint = (m_captureDirection == CaptureDirection::Vertical)
                ? tr("Match failed - scroll back up slowly")
                : tr("Match failed - scroll back left slowly");
            m_overlay->setMatchFailedMessage(hint);
        }

        emit matchStatusChanged(false, result.confidence, m_lastSuccessfulPosition);
        qDebug() << "ScrollingCaptureManager: Match failed -" << result.failureReason;
    }

    m_lastFrame = frame;

    // Log performance metrics
    totalMs = grabMs + detectMs + stitchMs;
    qDebug() << "ScrollingCaptureManager::captureFrame perf - grab:" << grabMs
             << "ms, detect:" << detectMs << "ms, stitch:" << stitchMs
             << "ms, total:" << totalMs << "ms";

    m_isProcessingFrame = false;
}

QImage ScrollingCaptureManager::grabCaptureRegion()
{
    if (!m_targetScreen || m_captureRegion.isEmpty()) {
        return QImage();
    }

    QRect screenGeom = m_targetScreen->geometry();

    // Validate capture region is within screen bounds
    QRect validRegion = m_captureRegion.intersected(screenGeom);
    if (validRegion.isEmpty() || validRegion.width() < 10 || validRegion.height() < 10) {
        qDebug() << "ScrollingCaptureManager: Capture region outside screen bounds or too small";
        return QImage();
    }

    int relX = validRegion.x() - screenGeom.x();
    int relY = validRegion.y() - screenGeom.y();

    // Ensure relative coordinates are non-negative
    if (relX < 0 || relY < 0) {
        qDebug() << "ScrollingCaptureManager: Invalid relative coordinates";
        return QImage();
    }

    QPixmap pixmap = m_targetScreen->grabWindow(0, relX, relY,
        validRegion.width(), validRegion.height());

    if (pixmap.isNull()) {
        qDebug() << "ScrollingCaptureManager: grabWindow returned null pixmap";
        return QImage();
    }

    // Convert to image and normalize to physical pixels (DPR = 1.0)
    // This ensures consistent dimensions for stitching regardless of screen DPI
    QImage image = pixmap.toImage();
    image.setDevicePixelRatio(1.0);

    return image;
}

void ScrollingCaptureManager::updateUIPositions()
{
    if (m_toolbar) {
        m_toolbar->positionNear(m_captureRegion);
        if (m_toolbar->isVisible()) {
            m_toolbar->raise();
        }
    }

    if (m_thumbnail) {
        m_thumbnail->positionNear(m_captureRegion);
        if (m_thumbnail->isVisible()) {
            m_thumbnail->raise();
        }
    }
}

void ScrollingCaptureManager::finishCapture()
{
    // Stop timer first to prevent any more captureFrame() calls
    stopFrameCapture();

    // Change state immediately after stopping timer to prevent race conditions
    // with any pending timer callbacks in the event queue
    setState(State::Completed);

    // Get stitched result from appropriate source
    if (m_useAsyncStitching) {
        m_stitchedResult = m_stitchWorker->getStitchedImage();
    } else {
        m_stitchedResult = m_stitcher->getStitchedImage();
    }

    if (m_stitchedResult.isNull()) {
        qDebug() << "ScrollingCaptureManager: No stitched result";
        stop();
        emit captureCancelled();
        return;
    }

    // Update toolbar with final size (convert to logical pixels)
    if (m_toolbar) {
        qreal dpr = m_targetScreen ? m_targetScreen->devicePixelRatio() : 1.0;
        m_toolbar->updateSize(m_stitchedResult.width() / dpr, m_stitchedResult.height() / dpr);
    }

    // Update thumbnail with final image
    if (m_thumbnail) {
        // Legacy thumbnail call removed
    }
}

// ============================================================================
// StitchWorker signal handlers (async processing)
// ============================================================================

void ScrollingCaptureManager::updateRecoveryEstimate(const StitchWorker::Result &result)
{
    if (!result.success || result.failureCode != ImageStitcher::FailureCode::None) {
        return;
    }
    
    // Calculate actual scroll delta
    bool isVertical = (m_captureDirection == CaptureDirection::Vertical);
    int frameDim = isVertical ? result.frameSize.height() : result.frameSize.width();
    int deltaPx = frameDim - result.overlapPixels;
    
    if (deltaPx <= 0) return;  // Invalid
    
    // Update EMA
    if (!m_hasScrollEstimate) {
        m_estimatedScrollPerFrame = deltaPx;
        m_hasScrollEstimate = true;
    } else {
        m_estimatedScrollPerFrame = qRound(
            (1.0 - EMA_ALPHA) * m_estimatedScrollPerFrame + EMA_ALPHA * deltaPx
        );
    }
    
    m_lastSuccessCaptureIndex = m_captureIndex;
}

int ScrollingCaptureManager::calculateRecoveryDistance() const
{
    int failedFrames = m_captureIndex - m_lastSuccessCaptureIndex;
    
    int estimate = m_hasScrollEstimate ? m_estimatedScrollPerFrame : DEFAULT_SCROLL_ESTIMATE;
    estimate = qMax(estimate, 30);  // Floor
    
    int recoveryPx = failedFrames * estimate;
    
    return qBound(MIN_RECOVERY_PX, recoveryPx, MAX_RECOVERY_PX);
}

bool ScrollingCaptureManager::isWarningFailure(ImageStitcher::FailureCode code) const
{
    switch (code) {
        case ImageStitcher::FailureCode::AmbiguousMatch:
        case ImageStitcher::FailureCode::LowConfidence:
        case ImageStitcher::FailureCode::DuplicateDetected:
            return true;
        default:
            return false;
    }
}

void ScrollingCaptureManager::onStitchFrameProcessed(const StitchWorker::Result &result)
{
    if (m_state != State::Capturing && m_state != State::MatchFailed) {
        qDebug() << "ScrollingCaptureManager: Ignoring frame processed signal in state" << static_cast<int>(m_state);
        return;
    }

    qDebug() << "ScrollingCaptureManager::onStitchFrameProcessed -"
             << "success:" << result.success
             << "confidence:" << result.confidence
             << "frameCount:" << result.frameCount
             << "code:" << static_cast<int>(result.failureCode);

    using FailureCode = ImageStitcher::FailureCode;

    switch (result.failureCode) {
        case FailureCode::None:
            handleSuccess(result);
            break;

        case FailureCode::FrameUnchanged:
        case FailureCode::ScrollTooSmall:
            // Keep Capturing, do not show error
            break;

        case FailureCode::MaxSizeReached:
            qDebug() << "ScrollingCaptureManager: Max size reached, finishing capture";
            finishCapture();
            break;

        case FailureCode::AmbiguousMatch:
        case FailureCode::LowConfidence:
        case FailureCode::DuplicateDetected:
            showWarning(result);
            break;

        case FailureCode::OverlapMismatch:
        case FailureCode::ViewportMismatch:
        case FailureCode::InvalidState:
        case FailureCode::NoAlgorithmSucceeded:
        case FailureCode::Timeout:
        case FailureCode::OverlapTooSmall:
        default:
            handleFailure(result);
            break;
    }
}

void ScrollingCaptureManager::handleSuccess(const StitchWorker::Result &result)
{
    m_consecutiveSuccessCount++;

    // Try to restore speed if pressure was relieved
    if (m_queuePressureRelieved &&
        m_consecutiveSuccessCount >= SUCCESS_COUNT_FOR_SPEEDUP &&
        m_captureIntervalMs > CAPTURE_INTERVAL_MS) {
        
        if (!m_lastIntervalAdjustment.isValid() ||
            m_lastIntervalAdjustment.elapsed() >= ADJUSTMENT_COOLDOWN_MS) {
            
            m_captureIntervalMs = qMax(m_captureIntervalMs - INTERVAL_DECREMENT_MS,
                                       CAPTURE_INTERVAL_MS);
            m_captureTimer->setInterval(m_captureIntervalMs);
            m_lastIntervalAdjustment.restart();
            m_consecutiveSuccessCount = 0;
            
            qDebug() << "ScrollingCaptureManager: Capture rate restored to" << m_captureIntervalMs << "ms";
        }
    }

    // Clear warning when back to default speed
    if (m_captureIntervalMs == CAPTURE_INTERVAL_MS && m_currentStatus == ScrollingCaptureThumbnail::CaptureStatus::Warning) {
        if (m_thumbnail) {
            m_thumbnail->setStatus(ScrollingCaptureThumbnail::CaptureStatus::Capturing);
        }
        m_queuePressureRelieved = false;
    }

    if (result.frameCount > 1) {
        m_hasSuccessfulStitch = true;
    }

    m_lastSuccessfulPosition = result.lastSuccessfulPosition;
    updateRecoveryEstimate(result);

    // Update UI
    if (m_toolbar) {
        m_toolbar->setMatchStatus(true, result.confidence);
        m_toolbar->setMatchRecoveryInfo(m_lastSuccessfulPosition, false);
        qreal dpr = m_targetScreen ? m_targetScreen->devicePixelRatio() : 1.0;
        m_toolbar->updateSize(result.currentSize.width() / dpr, result.currentSize.height() / dpr);
    }
    if (m_thumbnail) {
        m_thumbnail->setStats(result.frameCount, result.currentSize);
        m_currentStatus = ScrollingCaptureThumbnail::CaptureStatus::Capturing;
        m_thumbnail->setStatus(m_currentStatus);
        m_thumbnail->clearError();
    }

    // If we were in MatchFailed, recover to Capturing
    if (m_state == State::MatchFailed) {
        setState(State::Capturing);
        if (m_overlay) {
            m_overlay->clearMatchFailedMessage();
        }
    }
    emit matchStatusChanged(true, result.confidence, m_lastSuccessfulPosition);
}

void ScrollingCaptureManager::handleFailure(const StitchWorker::Result &result)
{
    m_consecutiveSuccessCount = 0;
    
    using CStatus = ScrollingCaptureThumbnail::CaptureStatus;
    CStatus status = isWarningFailure(result.failureCode)
        ? CStatus::Warning
        : CStatus::Failed;
    
    // Match failed
    if (m_state != State::MatchFailed && status == CStatus::Failed) {
        setState(State::MatchFailed);
    }

    if (m_toolbar) {
        m_toolbar->setMatchStatus(false, result.confidence);
        m_toolbar->setMatchRecoveryInfo(m_lastSuccessfulPosition, true);
    }
    if (m_thumbnail) {
        m_currentStatus = status;
        m_thumbnail->setStatus(m_currentStatus);
        int recoveryPx = calculateRecoveryDistance();
        m_thumbnail->setErrorInfo(result.failureCode, result.failureReason, recoveryPx);
    }
    if (m_overlay && status == CStatus::Failed) {
        QString hint = (m_captureDirection == CaptureDirection::Vertical)
            ? tr("Match failed - scroll back up slowly")
            : tr("Match failed - scroll back left slowly");
        m_overlay->setMatchFailedMessage(hint);
    }

    emit matchStatusChanged(false, result.confidence, m_lastSuccessfulPosition);
    qDebug() << "ScrollingCaptureManager: Match failed -" << result.failureReason;
}

void ScrollingCaptureManager::showWarning(const StitchWorker::Result &result)
{
    m_consecutiveSuccessCount = 0;
    if (m_thumbnail) {
        m_currentStatus = ScrollingCaptureThumbnail::CaptureStatus::Warning;
        
        QString msg;
        using FailureCode = ImageStitcher::FailureCode;
        switch (result.failureCode) {
            case FailureCode::AmbiguousMatch:    msg = "Ambiguous"; break;
            case FailureCode::LowConfidence:     msg = "Unsure"; break;
            case FailureCode::DuplicateDetected: msg = "Duplicate"; break;
            default:                             msg = "Warning"; break;
        }
        
        m_thumbnail->setStatus(m_currentStatus, msg);
    }
    qDebug() << "ScrollingCaptureManager: Warning -" << result.failureReason;
}

void ScrollingCaptureManager::onStitchFixedElementsDetected(int leading, int trailing)
{
    if (m_state != State::Capturing && m_state != State::MatchFailed) {
        return;
    }
    qDebug() << "ScrollingCaptureManager::onStitchFixedElementsDetected -"
             << "leading:" << leading << "trailing:" << trailing;
    m_fixedElementsDetected = true;
}

void ScrollingCaptureManager::onStitchQueueNearFull(int currentDepth, int maxDepth)
{
    if (m_state != State::Capturing && m_state != State::MatchFailed) {
        return;
    }
    // Cooldown check
    if (m_lastIntervalAdjustment.isValid() &&
        m_lastIntervalAdjustment.elapsed() < ADJUSTMENT_COOLDOWN_MS) {
        return;
    }
    
    // Slow down capture rate
    m_captureIntervalMs = qMin(m_captureIntervalMs + INTERVAL_INCREMENT_MS, MAX_CAPTURE_INTERVAL_MS);
    m_captureTimer->setInterval(m_captureIntervalMs);
    m_lastIntervalAdjustment.restart();
    m_consecutiveSuccessCount = 0;
    
    // Show warning
    if (m_thumbnail) {
        m_currentStatus = ScrollingCaptureThumbnail::CaptureStatus::Warning;
        m_thumbnail->setStatus(m_currentStatus,
            QString("Lag (%1/%2)").arg(currentDepth).arg(maxDepth));
    }
    
    qDebug() << "ScrollingCaptureManager: Capture rate slowed to" << m_captureIntervalMs << "ms";
}

void ScrollingCaptureManager::onStitchQueueLow(int currentDepth, int maxDepth)
{
    m_queuePressureRelieved = true;
    qDebug() << "ScrollingCaptureManager: Queue pressure relieved (" << currentDepth << "/" << maxDepth << ")";
}

void ScrollingCaptureManager::onStitchError(const QString &message)
{
    qWarning() << "ScrollingCaptureManager::onStitchError -" << message;
}
