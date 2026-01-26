#include "scrolling/AutoScroller.h"
#include "scrolling/ImageCombiner.h"
#include "scrolling/ScrollSender.h"
#include "capture/ICaptureEngine.h"
#include <QGuiApplication>
#include <QScreen>
#include <QThread>
#include <QCursor>
#include <cstring>

namespace {
struct CaptureLoopGuard {
    explicit CaptureLoopGuard(bool& flagRef) : flag(flagRef) { flag = true; }
    ~CaptureLoopGuard() { flag = false; }
    bool& flag;
};
}

AutoScroller::AutoScroller(QObject *parent)
    : QObject(parent)
{
    m_loopTimer = new QTimer(this);
    m_loopTimer->setSingleShot(true);
    connect(m_loopTimer, &QTimer::timeout, this, &AutoScroller::captureLoop);
}

AutoScroller::~AutoScroller()
{
    stop(false);
}

void AutoScroller::setOptions(const ScrollingCaptureOptions& options)
{
    m_options = options;
}

void AutoScroller::setTarget(WId window, const QRect& rect)
{
    m_targetWindow = window;
    m_targetRect = rect;
}

void AutoScroller::setImageCombiner(ImageCombiner* combiner)
{
    m_imageCombiner = combiner;
}

void AutoScroller::start()
{
    if (m_running) return;

    // Validate
    if (m_targetWindow == 0 || m_targetRect.isNull()) {
        emit error(tr("No target window set"));
        return;
    }

    if (!m_imageCombiner) {
        emit error(tr("No image combiner set"));
        return;
    }

    // Initialize capture engine
    if (!initializeCaptureEngine()) {
        emit error(tr("Failed to initialize capture engine"));
        return;
    }

    // Initialize scroll sender
    m_scrollSender = ScrollSender::create();
    if (!m_scrollSender) {
        emit error(tr("Failed to create scroll sender"));
        return;
    }

    m_scrollSender->setMethod(m_options.scrollMethod);
    m_scrollSender->setAmount(m_options.scrollAmount);

    // Set target position for macOS (scroll events go to cursor position)
    QPoint targetCenter = m_targetRect.center();
    m_scrollSender->setTargetPosition(targetCenter.x(), targetCenter.y());

    // Move cursor to target window center BEFORE first frame capture
    // This ensures consistent hover states across all frames
    moveCursorToTarget();

    // Reset state
    m_running = true;
    m_stopRequested = false;
    m_inCaptureLoop = false;
    m_completionEmitted = false;
    m_emitCompletionOnStop = false;
    m_frameCount = 0;
    m_previousFrame = QImage();
    m_overallStatus = CaptureStatus::Successful;
    m_imageCombiner->reset();

    // Optionally scroll to top first, then wait asynchronously for content to settle
    if (m_options.autoScrollTop) {
        scrollToTop();
        // Wait for content to settle (async)
        QTimer::singleShot(m_options.startDelayMs, this, &AutoScroller::captureLoop);
    } else {
        // Wait for hover states to settle after cursor move (async)
        QTimer::singleShot(150, this, &AutoScroller::captureLoop);
    }
}

void AutoScroller::stop(bool emitCompletion)
{
    m_stopRequested = true;
    m_emitCompletionOnStop = emitCompletion;
    m_running = false;
    m_loopTimer->stop();

    if (!m_inCaptureLoop) {
        cleanupResources();
        if (emitCompletion) {
            emitCompletedOnce();
        }
    }
}

bool AutoScroller::initializeCaptureEngine()
{
    // Create capture engine
    m_captureEngine.reset(ICaptureEngine::createBestEngine(nullptr));
    if (!m_captureEngine) {
        return false;
    }

    // Find the screen containing the target rect
    QScreen* targetScreen = QGuiApplication::screenAt(m_targetRect.center());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }

    // Configure for the target region
    if (!m_captureEngine->setRegion(m_targetRect, targetScreen)) {
        return false;
    }

    return m_captureEngine->start();
}

void AutoScroller::moveCursorToTarget()
{
    // Move cursor to target window center before capturing
    // This ensures consistent hover states across all frames
    QPoint targetCenter = m_targetRect.center();
    QCursor::setPos(targetCenter);
    qDebug() << "AutoScroller: Moved cursor to" << targetCenter;
}

bool AutoScroller::scrollToTop()
{
    if (!m_scrollSender) return false;
    return m_scrollSender->scrollToTop(m_targetWindow);
}

bool AutoScroller::sendScroll()
{
    if (!m_scrollSender) return false;
    return m_scrollSender->sendScroll(m_targetWindow);
}

QImage AutoScroller::captureCurrentFrame()
{
    if (!m_captureEngine) return QImage();

    // ScreenCaptureKit is asynchronous - may need to wait for first frame
    // Retry a few times with small delays
    for (int retry = 0; retry < 10; retry++) {
        QImage frame = m_captureEngine->captureFrame();
        if (!frame.isNull()) {
            // SCKCaptureEngine may return full display instead of cropped region
            // Crop to target rect if frame is larger than expected
            QScreen* screen = QGuiApplication::screenAt(m_targetRect.center());
            qreal dpr = screen ? screen->devicePixelRatio() : 1.0;

            // Calculate expected size in physical pixels
            int expectedWidth = static_cast<int>(m_targetRect.width() * dpr);
            int expectedHeight = static_cast<int>(m_targetRect.height() * dpr);

            qDebug() << "AutoScroller: Frame size:" << frame.size()
                     << "Target rect:" << m_targetRect
                     << "DPR:" << dpr
                     << "Expected size:" << expectedWidth << "x" << expectedHeight;

            if (frame.width() > expectedWidth || frame.height() > expectedHeight) {
                // Frame is full display - crop to target rect
                QPoint screenPos = screen ? screen->geometry().topLeft() : QPoint(0, 0);
                int cropX = static_cast<int>((m_targetRect.x() - screenPos.x()) * dpr);
                int cropY = static_cast<int>((m_targetRect.y() - screenPos.y()) * dpr);

                // Ensure we don't go out of bounds
                cropX = qMax(0, qMin(cropX, frame.width() - expectedWidth));
                cropY = qMax(0, qMin(cropY, frame.height() - expectedHeight));

                qDebug() << "AutoScroller: Cropping from" << frame.size()
                         << "to" << expectedWidth << "x" << expectedHeight
                         << "at offset" << cropX << "," << cropY;

                frame = frame.copy(cropX, cropY, expectedWidth, expectedHeight);

                qDebug() << "AutoScroller: Cropped frame size:" << frame.size();
            }

            return frame;
        }
        // Wait a bit for frame to arrive
        QThread::msleep(50);
    }

    return QImage();
}

void AutoScroller::captureLoop()
{
    CaptureLoopGuard guard(m_inCaptureLoop);

    if (m_stopRequested || !m_running) {
        cleanupResources();
        if (m_emitCompletionOnStop) {
            emitCompletedOnce();
        }
        return;
    }

    // Safety limits
    if (m_frameCount >= m_options.maxFrames) {
        qDebug() << "AutoScroller: Max frames reached";
        m_running = false;
        cleanupResources();
        emitCompletedOnce();
        return;
    }

    if (m_imageCombiner->totalHeight() >= m_options.maxHeightPx) {
        qDebug() << "AutoScroller: Max height reached";
        m_running = false;
        cleanupResources();
        emitCompletedOnce();
        return;
    }

    // Capture current frame
    QImage currentFrame = captureCurrentFrame();
    if (currentFrame.isNull()) {
        emit error(tr("Failed to capture frame"));
        return;
    }

    // Convert to consistent format for comparison
    if (currentFrame.format() != QImage::Format_ARGB32) {
        currentFrame = currentFrame.convertToFormat(QImage::Format_ARGB32);
    }

    // Check if reached bottom (compare with previous frame)
    if (m_frameCount > 0 && isScrollReachedBottom(m_previousFrame, currentFrame)) {
        qDebug() << "AutoScroller: Reached bottom at frame" << m_frameCount;
        m_running = false;
        cleanupResources();
        emitCompletedOnce();
        return;
    }

    // Combine with result
    CombineResult result;
    if (m_frameCount == 0) {
        m_imageCombiner->setFirstFrame(currentFrame);
        result.status = CaptureStatus::Successful;
    } else {
        result = m_imageCombiner->addFrame(currentFrame);
    }

    // Update overall status
    if (result.status == CaptureStatus::Failed) {
        // Complete failure - stop
        qDebug() << "AutoScroller: Image combining failed";
        m_running = false;
        m_overallStatus = CaptureStatus::Failed;
        cleanupResources();
        emitCompletedOnce();
        return;
    } else if (result.status == CaptureStatus::PartiallySuccessful) {
        // Fallback used - mark overall as partial
        m_overallStatus = CaptureStatus::PartiallySuccessful;
    }

    m_frameCount++;
    emit frameCaptured(m_frameCount);
    emit progressUpdated(m_frameCount, m_imageCombiner->totalHeight());

    // Store for next comparison
    m_previousFrame = currentFrame;

    if (m_stopRequested || !m_running) {
        cleanupResources();
        if (m_emitCompletionOnStop) {
            emitCompletedOnce();
        }
        return;
    }

    // Send scroll event
    if (!sendScroll()) {
        qWarning() << "AutoScroller: Failed to send scroll";
    }

    // Schedule next capture after delay
    m_loopTimer->start(m_options.scrollDelayMs);
}

bool AutoScroller::isScrollReachedBottom(const QImage& prevFrame, const QImage& currFrame)
{
    // First try platform API (Windows only)
    if (m_scrollSender && m_scrollSender->isAtBottom(m_targetWindow)) {
        return true;
    }

    // Fallback: Compare images
    // If they're identical, scrolling had no effect = at bottom
    return compareImages(prevFrame, currFrame);
}

bool AutoScroller::compareImages(const QImage& a, const QImage& b)
{
    if (a.size() != b.size()) return false;
    if (a.format() != b.format()) return false;

    // Line-by-line memcmp comparison
    for (int y = 0; y < a.height(); y++) {
        const uchar* lineA = a.constScanLine(y);
        const uchar* lineB = b.constScanLine(y);
        if (std::memcmp(lineA, lineB, a.bytesPerLine()) != 0) {
            return false;
        }
    }

    return true;
}

void AutoScroller::cleanupResources()
{
    if (m_captureEngine) {
        m_captureEngine->stop();
        m_captureEngine.reset();
    }
    m_scrollSender.reset();
}

void AutoScroller::emitCompletedOnce()
{
    if (m_completionEmitted) {
        return;
    }
    m_completionEmitted = true;
    emit completed(m_overallStatus);
}
