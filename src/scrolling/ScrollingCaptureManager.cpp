#include "scrolling/ScrollingCaptureManager.h"
#include "scrolling/ScrollingCaptureOverlay.h"
#include "scrolling/ScrollingCaptureToolbar.h"
#include "scrolling/ScrollingCaptureThumbnail.h"
#include "scrolling/ImageStitcher.h"
#include "scrolling/FixedElementDetector.h"
#include "PinWindowManager.h"

#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <QClipboard>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDateTime>
#include <QDebug>

ScrollingCaptureManager::ScrollingCaptureManager(PinWindowManager *pinManager, QObject *parent)
    : QObject(parent)
    , m_pinManager(pinManager)
    , m_stitcher(new ImageStitcher(this))
    , m_fixedDetector(new FixedElementDetector(this))
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

    connect(m_stitcher, &ImageStitcher::progressUpdated, this, [this](int frames, const QSize &size) {
        if (m_toolbar) {
            qreal dpr = m_targetScreen ? m_targetScreen->devicePixelRatio() : 1.0;
            m_toolbar->updateSize(size.width() / dpr, size.height() / dpr);
        }
        if (m_thumbnail) {
            m_thumbnail->setStitchedImage(m_stitcher->getStitchedImage());
            m_thumbnail->setViewportRect(m_stitcher->currentViewportRect());
        }
    });
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
    if (m_state == State::WaitingToStart) {
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
    finishCapture();
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
    stop();
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
    stop();
}

void ScrollingCaptureManager::onCancelClicked()
{
    stop();
    emit captureCancelled();
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
    m_captureTimer->start(CAPTURE_INTERVAL_MS);
    m_timeoutTimer->start(MAX_CAPTURE_TIMEOUT_MS);
    m_totalFrameCount = 0;
    m_isProcessingFrame = false;
    m_pendingFrames.clear();
}

void ScrollingCaptureManager::stopFrameCapture()
{
    m_captureTimer->stop();
    m_timeoutTimer->stop();
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
    if (frame.isNull()) {
        qDebug() << "ScrollingCaptureManager: Failed to grab capture region";
        m_isProcessingFrame = false;
        return;
    }

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

        auto detection = m_fixedDetector->detect();
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
    ImageStitcher::StitchResult result = m_stitcher->addFrame(frameToStitch);

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

    m_stitchedResult = m_stitcher->getStitchedImage();

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
        m_thumbnail->setStitchedImage(m_stitchedResult);
    }
}
