#include "scrolling/ScrollingCaptureManager.h"
#include "scrolling/WindowHighlighter.h"
#include "scrolling/AutoScroller.h"
#include "scrolling/ImageCombiner.h"
#include "settings/ScrollingCaptureSettingsManager.h"
#include "WindowDetector.h"
#include "ui/GlobalToast.h"
#include <QGuiApplication>
#include <QScreen>
#include <QClipboard>

ScrollingCaptureManager::ScrollingCaptureManager(QObject *parent)
    : QObject(parent)
{
    // Load options from settings
    m_options = ScrollingCaptureSettingsManager::instance().loadOptions();

    // Create window detector
    m_windowDetector = std::make_unique<WindowDetector>(nullptr);

    // Create image combiner
    m_imageCombiner = std::make_unique<ImageCombiner>();
    m_imageCombiner->setOptions(m_options.minMatchLines, m_options.autoIgnoreBottomEdge);
}

ScrollingCaptureManager::~ScrollingCaptureManager()
{
    cleanup();
}

bool ScrollingCaptureManager::isActive() const
{
    return m_state != ScrollingCaptureState::Idle;
}

bool ScrollingCaptureManager::isCapturing() const
{
    return m_state == ScrollingCaptureState::Capturing;
}

void ScrollingCaptureManager::setState(ScrollingCaptureState newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(newState);
    }
}

void ScrollingCaptureManager::startWindowSelection()
{
    if (isActive()) {
        qWarning() << "ScrollingCaptureManager: Already active";
        return;
    }

    // Reset state
    m_resultImage = QImage();
    m_resultStatus = CaptureStatus::Failed;
    m_imageCombiner->reset();

    // Determine target screen
    QPoint cursorPos = QCursor::pos();
    m_targetScreen = QGuiApplication::screenAt(cursorPos);
    if (!m_targetScreen) {
        m_targetScreen = QGuiApplication::primaryScreen();
    }

    // Configure window detector and wait for window list to be ready
    m_windowDetector->setScreen(m_targetScreen);
    m_windowDetector->setDetectionFlags(DetectionFlag::Windows);

    // Connect to windowListReady signal before starting refresh
    connect(m_windowDetector.get(), &WindowDetector::windowListReady,
            this, &ScrollingCaptureManager::onWindowListReady,
            Qt::UniqueConnection);

    m_windowDetector->refreshWindowListAsync();

    setState(ScrollingCaptureState::Selecting);
}

void ScrollingCaptureManager::onWindowListReady()
{
    // Only show highlighter if we're in Selecting state
    if (m_state != ScrollingCaptureState::Selecting) {
        return;
    }

    // Create and show window highlighter (with embedded Start/Cancel buttons)
    if (!m_highlighter) {
        m_highlighter = new WindowHighlighter();
        m_highlighter->setWindowDetector(m_windowDetector.get());

        connect(m_highlighter, &WindowHighlighter::startClicked,
                this, &ScrollingCaptureManager::onStartClicked);
        connect(m_highlighter, &WindowHighlighter::cancelled,
                this, &ScrollingCaptureManager::onCancelClicked);
        connect(m_highlighter, &WindowHighlighter::windowHovered,
                this, &ScrollingCaptureManager::onWindowHovered);
    }

    // Start tracking now that window list is ready
    m_highlighter->startTracking();
}

void ScrollingCaptureManager::cancelCapture()
{
    cleanup();
    setState(ScrollingCaptureState::Idle);
    emit captureCancelled();
}

void ScrollingCaptureManager::stopCapture()
{
    if (m_state == ScrollingCaptureState::Capturing) {
        if (m_autoScroller) {
            m_autoScroller->stop(true);
        }
        // Will trigger onScrollingCompleted
    }
}

void ScrollingCaptureManager::cleanup()
{
    // Stop auto scroller
    if (m_autoScroller) {
        m_autoScroller->stop(false);
        m_autoScroller.reset();
    }

    // Hide and delete highlighter
    if (m_highlighter) {
        m_highlighter->stopTracking();
        m_highlighter->deleteLater();
        m_highlighter = nullptr;
    }

    m_targetWindow = 0;
    m_targetRect = QRect();
}

void ScrollingCaptureManager::onWindowHovered(WId windowId, const QString& title)
{
    Q_UNUSED(title);

    // Update target when we have a valid window
    if (windowId != 0 && m_highlighter) {
        m_targetWindow = windowId;
        m_targetRect = m_highlighter->highlightedRect();
    }
    // When cursor leaves window, keep the last valid target
}

void ScrollingCaptureManager::onStartClicked()
{
    if (m_state != ScrollingCaptureState::Selecting) return;

    // Get current target from highlighter
    if (m_highlighter) {
        m_targetWindow = m_highlighter->highlightedWindow();
        m_targetRect = m_highlighter->highlightedRect();
    }

    if (m_targetWindow == 0 || m_targetRect.isNull()) {
        GlobalToast::instance().showToast(
            GlobalToast::Info,
            tr("No Window Selected"),
            tr("Please hover over a window first"),
            3000);
        return;
    }

    startCapturing();
}

void ScrollingCaptureManager::onCancelClicked()
{
    cancelCapture();
}

void ScrollingCaptureManager::onStopClicked()
{
    stopCapture();
}

void ScrollingCaptureManager::startCapturing()
{
    // Switch highlighter to capture mode (shows animated border instead of selection UI)
    if (m_highlighter) {
        m_highlighter->setCapturing(true);
    }

    // Create and configure auto scroller
    m_autoScroller = std::make_unique<AutoScroller>();
    m_autoScroller->setOptions(m_options);
    m_autoScroller->setTarget(m_targetWindow, m_targetRect);
    m_autoScroller->setImageCombiner(m_imageCombiner.get());

    connect(m_autoScroller.get(), &AutoScroller::frameCaptured,
            this, &ScrollingCaptureManager::onFrameCaptured);
    connect(m_autoScroller.get(), &AutoScroller::completed,
            this, &ScrollingCaptureManager::onScrollingCompleted);
    connect(m_autoScroller.get(), &AutoScroller::error,
            this, &ScrollingCaptureManager::onScrollingError);

    setState(ScrollingCaptureState::Capturing);

    // Start after a short delay to allow overlays to hide
    QTimer::singleShot(m_options.startDelayMs, m_autoScroller.get(), &AutoScroller::start);
}

void ScrollingCaptureManager::onFrameCaptured(int frameIndex)
{
    emit captureProgress(frameIndex);
}

void ScrollingCaptureManager::onScrollingCompleted(CaptureStatus status)
{
    finishCapture(status);
}

void ScrollingCaptureManager::onScrollingError(const QString& error)
{
    cleanup();
    setState(ScrollingCaptureState::Idle);
    emit captureError(error);

    GlobalToast::instance().showToast(
        GlobalToast::Error,
        tr("Capture Failed"),
        error,
        5000);
}

void ScrollingCaptureManager::finishCapture(CaptureStatus status)
{
    m_resultStatus = status;
    m_resultImage = m_imageCombiner->resultImage();

    if (m_resultImage.isNull() || m_imageCombiner->frameCount() < 2) {
        onScrollingError(tr("Not enough frames captured"));
        return;
    }

    setState(ScrollingCaptureState::Processing);

    // For now, directly show result (could add preview window later)
    showResult();
}

void ScrollingCaptureManager::showResult()
{
    cleanup();
    setState(ScrollingCaptureState::Idle);

    // Emit completed with result
    emit captureCompleted(m_resultImage, m_resultStatus);

    // Show success toast
    QString statusText;
    switch (m_resultStatus) {
    case CaptureStatus::Successful:
        statusText = tr("Capture completed successfully");
        break;
    case CaptureStatus::PartiallySuccessful:
        statusText = tr("Capture completed with some estimated matches");
        break;
    default:
        statusText = tr("Capture completed");
        break;
    }

    GlobalToast::instance().showToast(
        GlobalToast::Success,
        tr("Scrolling Capture"),
        tr("%1 frames, %2 x %3 px")
            .arg(m_imageCombiner->frameCount())
            .arg(m_resultImage.width())
            .arg(m_resultImage.height()),
        3000);

    // Copy to clipboard by default
    QGuiApplication::clipboard()->setImage(m_resultImage);
}
