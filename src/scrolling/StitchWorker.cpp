#include "scrolling/StitchWorker.h"
#include "scrolling/ImageStitcher.h"
#include "scrolling/FixedElementDetector.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QtConcurrent>

StitchWorker::StitchWorker(QObject *parent)
    : QObject(parent)
    , m_stitcher(new ImageStitcher(nullptr))  // No parent - managed manually
    , m_fixedDetector(new FixedElementDetector(nullptr))
{
    // Note: These objects will be accessed from worker thread
    // They have no parent to avoid Qt's thread affinity issues
}

StitchWorker::~StitchWorker()
{
    // Wait for any pending processing to complete
    if (m_processingFuture.isRunning()) {
        m_processingFuture.waitForFinished();
    }

    delete m_stitcher;
    delete m_fixedDetector;
}

void StitchWorker::setCaptureMode(CaptureMode mode)
{
    m_captureMode = mode;
    m_stitcher->setCaptureMode(mode == CaptureMode::Vertical
        ? ImageStitcher::CaptureMode::Vertical
        : ImageStitcher::CaptureMode::Horizontal);
    m_fixedDetector->setCaptureMode(mode == CaptureMode::Vertical
        ? FixedElementDetector::CaptureMode::Vertical
        : FixedElementDetector::CaptureMode::Horizontal);
}

void StitchWorker::setStitchConfig(double confidenceThreshold, bool detectStaticRegions)
{
    m_confidenceThreshold = confidenceThreshold;
    m_detectStaticRegions = detectStaticRegions;

    StitchConfig config;
    config.confidenceThreshold = confidenceThreshold;
    config.detectStaticRegions = detectStaticRegions;
    m_stitcher->setStitchConfig(config);
    m_fixedDetector->setEnabled(detectStaticRegions);
}

void StitchWorker::reset()
{
    // Wait for any pending processing
    if (m_processingFuture.isRunning()) {
        m_processingFuture.waitForFinished();
    }

    // Clear queue
    {
        QMutexLocker locker(&m_queueMutex);
        m_frameQueue.clear();
    }

    // Reset components
    m_stitcher->reset();
    m_fixedDetector->reset();
    m_fixedElementsFound = false;
    m_lastFrame = QImage();
    m_isProcessing = false;
}

bool StitchWorker::enqueueFrame(const QImage &frame)
{
    if (frame.isNull()) {
        return false;
    }

    {
        QMutexLocker locker(&m_queueMutex);

        if (m_frameQueue.size() >= MAX_QUEUE_SIZE) {
            qDebug() << "StitchWorker: Queue full, dropping frame";
            return false;
        }

        m_frameQueue.enqueue(frame.copy());

        // Warn if queue is getting full
        if (m_frameQueue.size() >= MAX_QUEUE_SIZE - 2) {
            emit queueNearFull();
        }
    }

    // Kick off processing if not already running
    if (!m_isProcessing.exchange(true)) {
        m_processingFuture = QtConcurrent::run([this]() {
            processNextFrame();
        });
    }

    return true;
}

int StitchWorker::queueDepth() const
{
    QMutexLocker locker(&m_queueMutex);
    return m_frameQueue.size();
}

bool StitchWorker::isProcessing() const
{
    return m_isProcessing.load();
}

QImage StitchWorker::getStitchedImage() const
{
    return m_stitcher->getStitchedImage();
}

int StitchWorker::frameCount() const
{
    return m_stitcher->frameCount();
}

void StitchWorker::processNextFrame()
{
    while (true) {
        QImage frame;

        // Get next frame from queue
        {
            QMutexLocker locker(&m_queueMutex);
            if (m_frameQueue.isEmpty()) {
                m_isProcessing = false;
                return;
            }
            frame = m_frameQueue.dequeue();
        }

        // Process the frame
        doProcessFrame(frame);
    }
}

void StitchWorker::doProcessFrame(const QImage &frame)
{
    QElapsedTimer perfTimer;
    qint64 detectMs = 0, stitchMs = 0;

    Result result;
    result.frameCount = m_stitcher->frameCount();

    // Check if frame changed
    bool frameChanged = true;
    if (!m_lastFrame.isNull()) {
        frameChanged = ImageStitcher::isFrameChanged(frame, m_lastFrame);
    }

    if (!frameChanged) {
        result.success = false;
        result.failureReason = "Frame unchanged";
        result.confidence = 1.0;
        emit frameProcessed(result);
        return;
    }

    // Fixed element detection (if not yet detected)
    if (m_detectStaticRegions && !m_fixedElementsFound) {
        m_fixedDetector->addFrame(frame);

        perfTimer.start();
        auto detection = m_fixedDetector->detect();
        detectMs = perfTimer.elapsed();

        if (detection.detected) {
            m_fixedElementsFound = true;
            result.fixedElementsDetected = true;
            result.leadingFixed = detection.leadingFixed;
            result.trailingFixed = detection.trailingFixed;

            emit fixedElementsDetected(detection.leadingFixed, detection.trailingFixed);

            qDebug() << "StitchWorker: Fixed elements detected -"
                     << "leading:" << detection.leadingFixed
                     << "trailing:" << detection.trailingFixed
                     << "(detect took" << detectMs << "ms)";
        }
    }

    // Crop fixed elements if detected
    QImage frameToStitch = frame;
    if (m_fixedElementsFound) {
        QImage cropped = m_fixedDetector->cropFixedRegions(frame);
        if (!cropped.isNull()) {
            frameToStitch = cropped;
        }
    }

    // Stitch
    perfTimer.restart();
    ImageStitcher::StitchResult stitchResult = m_stitcher->addFrame(frameToStitch);
    stitchMs = perfTimer.elapsed();

    // Build result
    result.success = stitchResult.success;
    result.confidence = stitchResult.confidence;
    result.failureReason = stitchResult.failureReason;
    result.overlapPixels = stitchResult.overlapPixels;
    result.frameCount = m_stitcher->frameCount();
    result.currentSize = m_stitcher->getCurrentSize();

    // Calculate last successful position
    if (stitchResult.success) {
        QRect viewport = m_stitcher->currentViewportRect();
        result.lastSuccessfulPosition = (m_captureMode == CaptureMode::Vertical)
            ? viewport.bottom() : viewport.right();
    }

    m_lastFrame = frame;

    qDebug() << "StitchWorker: Frame processed -"
             << "success:" << result.success
             << "confidence:" << result.confidence
             << "detect:" << detectMs << "ms, stitch:" << stitchMs << "ms";

    emit frameProcessed(result);
}
