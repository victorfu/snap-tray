#include "scrolling/StitchWorker.h"
#include "scrolling/ImageStitcher.h"
#include "scrolling/FixedElementDetector.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
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
    m_acceptingFrames = true;  // Re-enable accepting frames for next capture

    // Reset buffering state
    m_pendingRawFrames.clear();
    m_pendingRawFrames.squeeze();
    m_pendingMemoryUsage = 0;
    m_fixedDetected = false;
    m_detectionDisabled = false;
    m_lastRawFrameForChange = QImage();
    m_lastProcessedFrameForChange = QImage();
    m_lastRawSmall = QImage();
    m_lastProcessedSmall = QImage();
}

namespace {
    qint64 calculateFrameBytes(const QImage& frame) {
        qint64 bytes = frame.sizeInBytes();
        if (bytes == 0) {
            bytes = static_cast<qint64>(frame.bytesPerLine()) * frame.height();
        }
        return bytes;
    }
}

bool StitchWorker::enqueueFrame(const QImage &frame)
{
    if (frame.isNull()) {
        return false;
    }

    // Check if we're still accepting frames
    if (!m_acceptingFrames.load()) {
        qDebug() << "StitchWorker: Not accepting frames, dropping frame";
        return false;
    }

    // Normalize frame format - avoid double copy by using implicit sharing
    // when already RGB32, otherwise convertToFormat does the necessary copy
    QImage normalized = (frame.format() == QImage::Format_RGB32)
        ? frame  // implicit sharing, no deep copy
        : frame.convertToFormat(QImage::Format_RGB32);

    int depth;
    {
        QMutexLocker locker(&m_queueMutex);

        if (m_frameQueue.size() >= MAX_QUEUE_SIZE) {
            qDebug() << "StitchWorker: Queue full, dropping frame";
            return false;
        }

        m_frameQueue.enqueue(normalized);
        depth = m_frameQueue.size();
    }

    // Emit outside lock
    if (depth >= 8) { // QUEUE_NEAR_FULL_THRESHOLD
        emit queueNearFull(depth, MAX_QUEUE_SIZE);
        m_wasNearFull = true;
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

QImage StitchWorker::finishAndGetResult(int timeoutMs)
{
    // 1. Stop accepting new frames
    m_acceptingFrames = false;

    // 2. Wait for queue to drain and processing to complete
    if (m_processingFuture.isRunning()) {
        if (timeoutMs > 0) {
            // Wait with timeout
            QElapsedTimer timer;
            timer.start();
            while (m_processingFuture.isRunning() && timer.elapsed() < timeoutMs) {
                // Brief sleep to avoid busy-waiting
                QThread::msleep(10);
            }
            if (m_processingFuture.isRunning()) {
                qWarning() << "StitchWorker::finishAndGetResult: Timeout waiting for processing to complete";
            }
        } else {
            m_processingFuture.waitForFinished();
        }
    }

    // 3. Return deep copy of stitched result (thread-safe)
    return m_stitcher->getStitchedImage().copy();
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
        int depth;
        {
            QMutexLocker locker(&m_queueMutex);
            if (m_frameQueue.isEmpty()) {
                m_isProcessing = false;
                return;
            }
            frame = m_frameQueue.dequeue();
            depth = m_frameQueue.size();
        }

        if (depth <= 3 && m_wasNearFull) { // QUEUE_LOW_THRESHOLD
            emit queueLow(depth, MAX_QUEUE_SIZE);
            m_wasNearFull = false;
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

    QImage rawFrame = frame;

    // 1. Frame Change Detection (using cached downsampled images for efficiency)
    if (!m_fixedDetected) {
        // Compare raw vs raw using cached downsampled images
        QImage rawSmall = ImageStitcher::downsampleForComparison(rawFrame);
        if (!m_lastRawSmall.isNull() && !ImageStitcher::compareDownsampled(rawSmall, m_lastRawSmall)) {
            result.success = false;
            result.failureReason = "Frame unchanged";
            result.confidence = 1.0;
            result.failureCode = ImageStitcher::FailureCode::FrameUnchanged;
            emit frameProcessed(result);
            return;
        }
        m_lastRawFrameForChange = rawFrame;
        m_lastRawSmall = rawSmall;  // Cache the downsampled version
    } else {
        // Compare processed vs processed using cached downsampled images
        QImage processed = m_fixedDetector->cropFixedRegions(rawFrame);
        QImage processedSmall = ImageStitcher::downsampleForComparison(processed);
        if (!m_lastProcessedSmall.isNull() &&
            !ImageStitcher::compareDownsampled(processedSmall, m_lastProcessedSmall)) {
            result.success = false;
            result.failureReason = "Frame unchanged";
            result.confidence = 1.0;
            result.failureCode = ImageStitcher::FailureCode::FrameUnchanged;
            emit frameProcessed(result);
            return;
        }
        m_lastProcessedFrameForChange = processed;
        m_lastProcessedSmall = processedSmall;  // Cache the downsampled version
    }

    bool justRestitched = false;

    // 2. Pending Buffer & Detection Logic
    if (m_detectStaticRegions && !m_fixedDetected && !m_detectionDisabled) {
        qint64 frameBytes = calculateFrameBytes(rawFrame);

        if (m_pendingRawFrames.size() >= MAX_PENDING_FRAMES ||
            m_pendingMemoryUsage + frameBytes > MAX_PENDING_MEMORY_BYTES) {
            // Exceeded budget: disable detection, clear pending, continue raw stitching
            m_detectionDisabled = true;
            m_pendingRawFrames.clear();
            m_pendingRawFrames.squeeze();
            m_pendingMemoryUsage = 0;
            qWarning() << "StitchWorker: Fixed detection budget exceeded, disabling detection.";
        } else {
            // Use implicit sharing - rawFrame is already normalized (RGB32) from queue
            // and won't be modified, so deep copy is unnecessary
            m_pendingRawFrames.append(rawFrame);
            m_pendingMemoryUsage += frameBytes;
            m_fixedDetector->addFrame(rawFrame);

            if (m_pendingRawFrames.size() >= m_minFramesForDetection) {
                perfTimer.start();
                auto detection = m_fixedDetector->detect();
                detectMs = perfTimer.elapsed();

                if (detection.detected) {
                    m_fixedDetected = true;
                    m_fixedElementsFound = true; // Sync legacy flag
                    result.fixedElementsDetected = true;
                    result.leadingFixed = detection.leadingFixed;
                    result.trailingFixed = detection.trailingFixed;

                    emit fixedElementsDetected(detection.leadingFixed, detection.trailingFixed);

                    qDebug() << "StitchWorker: Fixed elements detected -"
                             << "leading:" << detection.leadingFixed
                             << "trailing:" << detection.trailingFixed
                             << "(detect took" << detectMs << "ms)";

                    // Restitch all pending frames with crop
                    m_stitcher->reset();

                    for (const QImage& raw : m_pendingRawFrames) {
                        QImage cropped = m_fixedDetector->cropFixedRegions(raw);
                        if (!cropped.isNull()) {
                            // During restitch, we process all buffered frames
                            ImageStitcher::StitchResult stitchResult = m_stitcher->addFrame(cropped);
                            
                            // Capture the result of the last frame (which corresponds to current processing)
                            // or basically just update the result struct with the latest state
                            result.success = stitchResult.success;
                            result.confidence = stitchResult.confidence;
                            result.failureReason = stitchResult.failureReason;
                            result.overlapPixels = stitchResult.overlapPixels;
                            result.failureCode = stitchResult.failureCode;
                            result.frameSize = cropped.size();
                            result.currentSize = m_stitcher->getCurrentSize();
                            if (stitchResult.success) {
                                QRect viewport = m_stitcher->currentViewportRect();
                                result.lastSuccessfulPosition = (m_captureMode == CaptureMode::Vertical)
                                    ? viewport.bottom() : viewport.right();
                            }
                        }
                    }

                    // Update processed baseline to last valid cropped frame
                    if (!m_pendingRawFrames.isEmpty()) {
                        m_lastProcessedFrameForChange = m_fixedDetector->cropFixedRegions(m_pendingRawFrames.last());
                        m_lastProcessedSmall = ImageStitcher::downsampleForComparison(m_lastProcessedFrameForChange);
                    }

                    m_pendingRawFrames.clear();
                    m_pendingRawFrames.squeeze();
                    m_pendingMemoryUsage = 0;
                    justRestitched = true;
                }
            }
        }
    }

    // 3. Regular Stitching (if not just restitched)
    if (!justRestitched) {
        QImage frameToStitch = rawFrame;
        if (m_fixedDetected) {
            QImage cropped = m_fixedDetector->cropFixedRegions(rawFrame);
            if (!cropped.isNull()) {
                frameToStitch = cropped;
            }
        }

        perfTimer.restart();
        ImageStitcher::StitchResult stitchResult = m_stitcher->addFrame(frameToStitch);
        stitchMs = perfTimer.elapsed();

        result.success = stitchResult.success;
        result.confidence = stitchResult.confidence;
        result.failureReason = stitchResult.failureReason;
        result.failureCode = stitchResult.failureCode;
        result.overlapPixels = stitchResult.overlapPixels;
        result.frameSize = frameToStitch.size();
        result.currentSize = m_stitcher->getCurrentSize();
        if (stitchResult.success) {
            QRect viewport = m_stitcher->currentViewportRect();
            result.lastSuccessfulPosition = (m_captureMode == CaptureMode::Vertical)
                ? viewport.bottom() : viewport.right();
        }
    }

    result.frameCount = m_stitcher->frameCount();
    m_lastFrame = frame; // Update legacy tracking just in case

    qDebug() << "StitchWorker: Frame processed -"
             << "success:" << result.success
             << "confidence:" << result.confidence
             << "detect:" << detectMs << "ms, stitch:" << stitchMs << "ms";

    emit frameProcessed(result);
}
