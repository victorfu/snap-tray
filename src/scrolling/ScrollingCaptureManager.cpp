#include "scrolling/ScrollingCaptureManager.h"
#include "scrolling/ScrollingCaptureOverlay.h"
#include "scrolling/ScrollingCaptureToolbar.h"
#include "scrolling/ScrollingCaptureThumbnail.h"
#include "scrolling/ScrollingCaptureOnboarding.h"
#include "scrolling/StitchWorker.h"
#include "scrolling/ImageStitcher.h"  // For FailureCode enum
#include "capture/ICaptureEngine.h"
#include "platform/WindowLevel.h"
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

    // Connect StitchWorker signals
    connect(m_stitchWorker, &StitchWorker::frameProcessed,
            this, &ScrollingCaptureManager::onStitchFrameProcessed);
    connect(m_stitchWorker, &StitchWorker::queueNearFull,
            this, &ScrollingCaptureManager::onStitchQueueNearFull);
    connect(m_stitchWorker, &StitchWorker::queueLow,
            this, &ScrollingCaptureManager::onStitchQueueLow);
    connect(m_stitchWorker, &StitchWorker::error,
            this, &ScrollingCaptureManager::onStitchError);
    connect(m_stitchWorker, &StitchWorker::finishCompleted,
            this, &ScrollingCaptureManager::onStitchFinishCompleted);
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
        case State::Completing:
            // Keep capturing state visual while processing
            m_overlay->setBorderState(ScrollingCaptureOverlay::BorderState::Capturing);
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
        case State::Completing:
            // Keep capturing mode while processing (or could add Processing mode)
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
    m_hasSuccessfulStitch = false;
    m_isProcessingFrame = false;
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

    // Initialize capture engine (GPU-accelerated alternative to grabWindow)
    m_captureEngine = ICaptureEngine::createBestEngine(this);
    if (m_captureEngine) {
        m_captureEngine->setRegion(m_captureRegion, m_targetScreen);

        // Exclude UI windows from capture
        QList<WId> excludedWindows;
        if (m_overlay) excludedWindows.append(m_overlay->winId());
        if (m_toolbar) excludedWindows.append(m_toolbar->winId());
        if (m_thumbnail) excludedWindows.append(m_thumbnail->winId());

#ifdef Q_OS_MACOS
        m_captureEngine->setExcludedWindows(excludedWindows);
#else
        // Windows: use SetWindowDisplayAffinity to exclude from capture
        if (m_overlay) setWindowExcludedFromCapture(m_overlay, true);
        if (m_toolbar) setWindowExcludedFromCapture(m_toolbar, true);
        if (m_thumbnail) setWindowExcludedFromCapture(m_thumbnail, true);
#endif

        if (!m_captureEngine->start()) {
            qWarning() << "ScrollingCaptureManager: Capture engine failed to start, using grabWindow fallback";
            delete m_captureEngine;
            m_captureEngine = nullptr;
        } else {
            qDebug() << "ScrollingCaptureManager: Using" << m_captureEngine->engineName();
        }
    }

    // Initialize StitchWorker
    m_stitchWorker->reset();
    m_stitchWorker->setCaptureMode(m_captureDirection == CaptureDirection::Vertical
        ? StitchWorker::CaptureMode::Vertical
        : StitchWorker::CaptureMode::Horizontal);
    m_stitchWorker->setStitchConfig(0.4, true);  // confidence threshold, detect static regions
}

void ScrollingCaptureManager::stopFrameCapture()
{
    m_captureTimer->stop();
    m_timeoutTimer->stop();
    m_currentStatus = ScrollingCaptureThumbnail::CaptureStatus::Idle;

    // Cleanup capture engine
    if (m_captureEngine) {
        m_captureEngine->stop();
        m_captureEngine->deleteLater();
        m_captureEngine = nullptr;
    }

#ifndef Q_OS_MACOS
    // Windows: restore window capture visibility
    if (m_overlay) setWindowExcludedFromCapture(m_overlay, false);
    if (m_toolbar) setWindowExcludedFromCapture(m_toolbar, false);
    if (m_thumbnail) setWindowExcludedFromCapture(m_thumbnail, false);
#endif

    // Note: Do NOT reset worker here - finishCapture() needs to retrieve the stitched result first.
    // The worker will be reset when a new capture starts or when stop() is called.
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

    // Check if we've hit the maximum frame limit BEFORE attempting grab
    // This prevents processing one extra frame beyond the limit
    if (m_totalFrameCount >= MAX_TOTAL_FRAMES) {
        qDebug() << "ScrollingCaptureManager: Maximum frame count reached, finishing capture";
        m_isProcessingFrame = false;
        finishCapture();
        return;
    }

    QElapsedTimer perfTimer;
    perfTimer.start();

    QImage frame = grabCaptureRegion();
    qint64 grabMs = perfTimer.elapsed();

    if (frame.isNull()) {
        qDebug() << "ScrollingCaptureManager: Failed to grab capture region";
        m_isProcessingFrame = false;
        return;
    }

    // Increment frame count only after successful grab
    m_totalFrameCount++;

    // Enqueue frame for async processing
    if (m_thumbnail) {
        m_thumbnail->setViewportImage(frame);
    }
    m_lastFrame = frame;
    m_captureIndex++;
    bool queued = m_stitchWorker->enqueueFrame(frame);
    if (!queued) {
        qDebug() << "ScrollingCaptureManager: Frame dropped (queue full)";
    }
    qDebug() << "ScrollingCaptureManager::captureFrame - grab:" << grabMs << "ms";
    m_isProcessingFrame = false;
}

QImage ScrollingCaptureManager::grabCaptureRegion()
{
    if (!m_targetScreen || m_captureRegion.isEmpty()) {
        return QImage();
    }

    // Try GPU-accelerated capture engine first
    if (m_captureEngine && m_captureEngine->isRunning()) {
        QImage frame = m_captureEngine->captureFrame();
        if (!frame.isNull()) {
            frame.setDevicePixelRatio(1.0);
            return frame;
        }
        // Fall through to grabWindow fallback if engine returned null
        qDebug() << "ScrollingCaptureManager: Capture engine returned null, falling back to grabWindow";
    }

    // Fallback: QScreen::grabWindow (slower but always works)
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

    // Non-blocking: transition to Completing state and wait for signal
    setState(State::Completing);
    m_stitchWorker->requestFinish();
    // Result will be delivered via onStitchFinishCompleted signal
}

void ScrollingCaptureManager::onStitchFinishCompleted(const QImage &result)
{
    if (m_state != State::Completing) {
        qDebug() << "ScrollingCaptureManager: Ignoring finish signal in state" << static_cast<int>(m_state);
        return;
    }

    m_stitchedResult = result;
    completeCapture();
}

void ScrollingCaptureManager::completeCapture()
{
    setState(State::Completed);

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
