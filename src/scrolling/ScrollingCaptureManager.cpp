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
{
    connect(m_captureTimer, &QTimer::timeout, this, &ScrollingCaptureManager::captureFrame);

    connect(m_stitcher, &ImageStitcher::progressUpdated, this, [this](int frames, const QSize &size) {
        if (m_toolbar) {
            m_toolbar->updateSize(size.width(), size.height());
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

    m_captureRegion = QRect();
    m_lastFrame = QImage();
    m_stitchedResult = QImage();
    m_unchangedFrameCount = 0;
    m_fixedElementsDetected = false;
    m_hasSuccessfulStitch = false;
    m_pendingFrames.clear();
}

void ScrollingCaptureManager::onRegionSelected(const QRect &region)
{
    m_captureRegion = region;
    setState(State::WaitingToStart);

    // Show and position toolbar
    if (m_toolbar) {
        m_toolbar->show();
        updateUIPositions();
    }
}

void ScrollingCaptureManager::onRegionChanged(const QRect &region)
{
    m_captureRegion = region;
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
        m_stitchedResult.save(filePath);
        qDebug() << "ScrollingCaptureManager: Saved to" << filePath;
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

void ScrollingCaptureManager::startFrameCapture()
{
    m_captureTimer->start(CAPTURE_INTERVAL_MS);
    m_unchangedFrameCount = 0;
    m_pendingFrames.clear();
}

void ScrollingCaptureManager::stopFrameCapture()
{
    m_captureTimer->stop();
}

bool ScrollingCaptureManager::restitchWithFixedElements()
{
    if (m_pendingFrames.empty()) {
        return true;
    }

    m_stitcher->reset();
    m_unchangedFrameCount = 0;

    bool allSucceeded = true;
    for (const QImage &rawFrame : m_pendingFrames) {
        QImage frameToStitch = m_fixedDetector->cropFixedRegions(rawFrame);
        ImageStitcher::StitchResult result = m_stitcher->addFrame(frameToStitch);
        if (!result.success && result.failureReason != "Frame unchanged") {
            qDebug() << "ScrollingCaptureManager: Restitch failed -" << result.failureReason;
            allSucceeded = false;
            // Continue trying remaining frames to salvage what we can
        }
    }

    m_pendingFrames.clear();
    return allSucceeded;
}

void ScrollingCaptureManager::captureFrame()
{
    if (m_state != State::Capturing && m_state != State::MatchFailed) {
        return;
    }

    QImage frame = grabCaptureRegion();
    if (frame.isNull()) {
        return;
    }

    bool restitched = false;

    // Add frame to fixed element detector (use detector's own frame count)
    if (!m_fixedElementsDetected) {
        m_fixedDetector->addFrame(frame);

        // Store all frames until fixed elements are detected to avoid data loss
        // during restitching. Memory is freed after detection (in restitchWithFixedElements).
        m_pendingFrames.push_back(frame);

        auto detection = m_fixedDetector->detect();
        if (detection.detected) {
            m_fixedElementsDetected = true;
            qDebug() << "ScrollingCaptureManager: Fixed elements detected -"
                     << "leading:" << detection.leadingFixed
                     << "trailing:" << detection.trailingFixed;
            bool restitchSuccess = restitchWithFixedElements();
            restitched = true;

            if (!restitchSuccess) {
                // Some frames failed during restitch, show warning but continue
                if (m_state != State::MatchFailed) {
                    setState(State::MatchFailed);
                }
            }
        }
    }

    if (restitched) {
        return;
    }

    // Crop fixed elements if detected
    QImage frameToStitch = frame;
    if (m_fixedElementsDetected) {
        frameToStitch = m_fixedDetector->cropFixedRegions(frame);
    }

    // Try to stitch
    ImageStitcher::StitchResult result = m_stitcher->addFrame(frameToStitch);

    qDebug() << "ScrollingCaptureManager::captureFrame -"
             << "success:" << result.success
             << "frameCount:" << m_stitcher->frameCount()
             << "hasSuccessfulStitch:" << m_hasSuccessfulStitch
             << "unchangedCount:" << m_unchangedFrameCount
             << "reason:" << result.failureReason;

    if (result.success) {
        // Mark that scrolling has started only after actual stitching (not first frame)
        // frameCount > 1 means at least 2 frames have been processed (actual stitch occurred)
        if (m_stitcher->frameCount() > 1) {
            m_hasSuccessfulStitch = true;
        }

        // Update UI
        if (m_toolbar) {
            m_toolbar->setMatchStatus(true, result.confidence);
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
        }

        // If we were in MatchFailed, recover to Capturing
        if (m_state == State::MatchFailed) {
            setState(State::Capturing);
        }

        m_unchangedFrameCount = 0;
    } else {
        // Check if frame is just unchanged
        if (result.failureReason == "Frame unchanged") {
            // Only count unchanged frames after scrolling has started
            // This gives users time to begin scrolling before the timeout kicks in
            if (m_hasSuccessfulStitch) {
                m_unchangedFrameCount++;
                if (m_unchangedFrameCount >= UNCHANGED_FRAME_THRESHOLD) {
                    qDebug() << "ScrollingCaptureManager: No motion detected, finishing capture";
                    finishCapture();
                }
            }
            return;
        }

        // Match failed
        if (m_state != State::MatchFailed) {
            setState(State::MatchFailed);
        }

        if (m_toolbar) {
            m_toolbar->setMatchStatus(false, result.confidence);
        }
        if (m_thumbnail) {
            m_thumbnail->setMatchStatus(ScrollingCaptureThumbnail::MatchStatus::Failed, result.confidence);
        }

        qDebug() << "ScrollingCaptureManager: Match failed -" << result.failureReason;
    }

    m_lastFrame = frame;
}

QImage ScrollingCaptureManager::grabCaptureRegion()
{
    if (!m_targetScreen || m_captureRegion.isEmpty()) {
        return QImage();
    }

    QRect screenGeom = m_targetScreen->geometry();
    int relX = m_captureRegion.x() - screenGeom.x();
    int relY = m_captureRegion.y() - screenGeom.y();

    QPixmap pixmap = m_targetScreen->grabWindow(0, relX, relY,
        m_captureRegion.width(), m_captureRegion.height());

    if (pixmap.isNull()) {
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
    }

    if (m_thumbnail) {
        m_thumbnail->positionNear(m_captureRegion);
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

    // Update toolbar with final size
    if (m_toolbar) {
        m_toolbar->updateSize(m_stitchedResult.width(), m_stitchedResult.height());
    }

    // Update thumbnail with final image
    if (m_thumbnail) {
        m_thumbnail->setStitchedImage(m_stitchedResult);
    }
}
