#include "scrolling/ImageStitcher.h"
#include "ImageStitcherImpl.h"

#include <cmath>
#include <utility>
#include <QDebug>
#include <QElapsedTimer>

#include <opencv2/core.hpp>

ImageStitcher::ImageStitcher(QObject *parent)
    : QObject(parent)
{
}

ImageStitcher::~ImageStitcher()
{
}

void ImageStitcher::setStitchConfig(const StitchConfig &config)
{
    m_stitchConfig = config;
    // Clamp confidence threshold to valid range
    m_stitchConfig.confidenceThreshold = qBound(0.0, config.confidenceThreshold, 1.0);
}

void ImageStitcher::setCaptureMode(CaptureMode mode)
{
    m_captureMode = mode;
}

void ImageStitcher::reset()
{
    m_stitchedResult = QImage();
    m_lastFrame = QImage();
    m_currentViewportRect = QRect();
    m_frameCount = 0;
    m_validHeight = 0;
    m_validWidth = 0;
    m_lastSuccessfulDirection = ScrollDirection::Down;
    if (m_lastFrameCache) {
        clearFrameCache(*m_lastFrameCache);
    }
    if (m_currentFrameCache) {
        clearFrameCache(*m_currentFrameCache);
    }
}

ImageStitcher::StitchResult ImageStitcher::addFrame(const QImage &frame)
{
    StitchResult result;

    // Performance timing for Phase 0 profiling
    QElapsedTimer perfTimer;
    perfTimer.start();

    if (frame.isNull()) {
        result.failureReason = "Empty frame";
        result.failureCode = FailureCode::InvalidState;
        return result;
    }

    // First frame - just store it
    if (m_frameCount == 0) {
        m_stitchedResult = frame.convertToFormat(QImage::Format_RGB32);
        m_validHeight = m_stitchedResult.height();
        m_validWidth = m_stitchedResult.width();
        m_lastFrame = m_stitchedResult;
        m_frameCount = 1;
        m_currentViewportRect = QRect(0, 0, frame.width(), frame.height());

        result.success = true;
        result.confidence = 1.0;
        result.overlapPixels = 0;
        result.usedAlgorithm = Algorithm::TemplateMatching;

        emit progressUpdated(m_frameCount, getCurrentSize());
        qDebug() << "ImageStitcher::addFrame perf - first frame:" << perfTimer.elapsed() << "ms";
        return result;
    }

    // Frame size consistency check
    if (!m_lastFrame.isNull() && frame.size() != m_lastFrame.size()) {
        result.success = false;
        result.failureReason = QString("Frame size mismatch: expected %1x%2, got %3x%4")
            .arg(m_lastFrame.width()).arg(m_lastFrame.height())
            .arg(frame.width()).arg(frame.height());
        result.failureCode = FailureCode::InvalidState;
        return result;
    }

    // Prepare frame caches before algorithm execution (avoid redundant QImage→Mat conversions)
    if (!m_currentFrameCache) {
        m_currentFrameCache = std::make_unique<FrameCacheImpl>();
    }
    prepareFrameCache(frame, *m_currentFrameCache);

    if (!m_lastFrameCache) {
        m_lastFrameCache = std::make_unique<FrameCacheImpl>();
    }
    if (!m_lastFrameCache->valid && !m_lastFrame.isNull()) {
        prepareFrameCache(m_lastFrame, *m_lastFrameCache);
    }

    qint64 rowProjMs = 0, templateMs = 0;

    // RSSA: Try matching algorithms based on settings
    if (m_algorithm == Algorithm::Auto) {
        // Use Row Projection as primary algorithm - it's faster and more robust for text documents
        // Row Projection uses 1D cross-correlation which is optimal for vertical/horizontal scrolling
        perfTimer.restart();
        result = tryRowProjectionMatch(frame);
        rowProjMs = perfTimer.elapsed();

        if (result.success && result.confidence >= m_stitchConfig.confidenceThreshold) {
            // Apply the stitch
            StitchResult stitchRes = performStitch(frame, result.overlapPixels, result.direction);
            
            if (stitchRes.success) {
                // Merge stitch result (offset) into the match result
                result.offset = stitchRes.offset;
                
                m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
                std::swap(m_lastFrameCache, m_currentFrameCache);
                m_frameCount++;
                emit progressUpdated(m_frameCount, getCurrentSize());
                qDebug() << "ImageStitcher::addFrame perf - rowProj:" << rowProjMs << "ms (success)";
                return result;
            } else {
                // Stitch failed (e.g. duplicate detected or size limit)
                result = stitchRes; // Propagate failure reason
            }
        }

        // Try template matching as fallback (good for non-text content)
        perfTimer.restart();
        result = tryTemplateMatch(frame);
        templateMs = perfTimer.elapsed();

        if (result.success && result.confidence >= m_stitchConfig.confidenceThreshold) {
            // Apply the stitch
            StitchResult stitchRes = performStitch(frame, result.overlapPixels, result.direction);
            
            if (stitchRes.success) {
                result.offset = stitchRes.offset;
                
                m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
                std::swap(m_lastFrameCache, m_currentFrameCache);
                m_frameCount++;
                emit progressUpdated(m_frameCount, getCurrentSize());
                qDebug() << "ImageStitcher::addFrame perf - rowProj:" << rowProjMs << "ms, template:" << templateMs << "ms (fallback success)";
                return result;
            } else {
                result = stitchRes;
            }
        }

        // All failed. Preserve the last result's failure code if it's more specific than None
        FailureCode finalCode = result.failureCode;
        if (finalCode == FailureCode::None) {
            finalCode = FailureCode::NoAlgorithmSucceeded;
        }
        
        result.success = false;
        result.failureCode = finalCode;
        if (result.failureReason.isEmpty()) {
            result.failureReason = "No matching algorithm succeeded";
        }

        if (m_stitchConfig.usePhaseCorrelation) {
            perfTimer.restart();
            StitchResult phaseResult = tryPhaseCorrelation(frame);
            qint64 phaseMs = perfTimer.elapsed();

            if (phaseResult.success) {
                // Apply the stitch
                StitchResult stitchRes = performStitch(frame, phaseResult.overlapPixels, phaseResult.direction);
                
                if (stitchRes.success) {
                    result = phaseResult; // Use phase result as base
                    result.offset = stitchRes.offset;
                    
                    m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
                    std::swap(m_lastFrameCache, m_currentFrameCache);
                    m_frameCount++;
                    emit progressUpdated(m_frameCount, getCurrentSize());
                    qDebug() << "ImageStitcher::addFrame perf - rowProj:" << rowProjMs << "ms, template:" << templateMs
                             << "ms, phase:" << phaseMs << "ms (fallback success)";
                    return result;
                } else {
                    result = stitchRes; 
                }
            }
        }

        qDebug() << "ImageStitcher::addFrame perf - rowProj:" << rowProjMs << "ms, template:" << templateMs << "ms (all failed)";
        return result;
    }
    else if (m_algorithm == Algorithm::RowProjection) {
        perfTimer.restart();
        result = tryRowProjectionMatch(frame);
        rowProjMs = perfTimer.elapsed();
    }
    else if (m_algorithm == Algorithm::TemplateMatching) {
        perfTimer.restart();
        result = tryTemplateMatch(frame);
        templateMs = perfTimer.elapsed();
    }
    else {
        perfTimer.restart();
        result = tryRowProjectionMatch(frame);
        rowProjMs = perfTimer.elapsed();
    }

    if (result.success) {
        // Apply the stitch for single-algorithm mode
        StitchResult stitchRes = performStitch(frame, result.overlapPixels, result.direction);
        
        if (stitchRes.success) {
            result.offset = stitchRes.offset;
            m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
            std::swap(m_lastFrameCache, m_currentFrameCache);
            m_frameCount++;
            emit progressUpdated(m_frameCount, getCurrentSize());
        } else {
            result = stitchRes;
        }
    }

    qDebug() << "ImageStitcher::addFrame perf - rowProj:" << rowProjMs << "ms, template:" << templateMs << "ms";
    return result;
}

// Constants for frame change detection
namespace {
    constexpr int kSampleSize = 64;
    constexpr int kPixelDiffThreshold = 20;
    constexpr double kAverageDiffThreshold = 2.0;
    constexpr double kChangedRatioThreshold = 0.005;
}

QImage ImageStitcher::downsampleForComparison(const QImage &frame)
{
    return frame.scaled(kSampleSize, kSampleSize, Qt::IgnoreAspectRatio,
                        Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
}

bool ImageStitcher::compareDownsampled(const QImage &small1, const QImage &small2)
{
    if (small1.size() != small2.size()) {
        return true;  // Consider changed if sizes differ
    }

    const int width = small1.width();
    const int height = small1.height();
    const int pixelCount = width * height;
    long long diffSum = 0;
    int changedPixels = 0;

    for (int y = 0; y < height; ++y) {
        const QRgb *line1 = reinterpret_cast<const QRgb*>(small1.constScanLine(y));
        const QRgb *line2 = reinterpret_cast<const QRgb*>(small2.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            int diff = std::abs(qRed(line1[x]) - qRed(line2[x])) +
                       std::abs(qGreen(line1[x]) - qGreen(line2[x])) +
                       std::abs(qBlue(line1[x]) - qBlue(line2[x]));
            diffSum += diff;
            if (diff > kPixelDiffThreshold) {
                changedPixels++;
            }
        }
    }

    double averageDiff = static_cast<double>(diffSum) / (pixelCount * 3);
    double changedRatio = static_cast<double>(changedPixels) / pixelCount;

    return averageDiff > kAverageDiffThreshold || changedRatio > kChangedRatioThreshold;
}

bool ImageStitcher::isFrameChanged(const QImage &frame1, const QImage &frame2)
{
    if (frame1.size() != frame2.size()) {
        return true;
    }

    // Use the helper methods - downsamples both frames
    QImage small1 = downsampleForComparison(frame1);
    QImage small2 = downsampleForComparison(frame2);

    return compareDownsampled(small1, small2);
}

QImage ImageStitcher::getStitchedImage() const
{
    if (m_stitchedResult.isNull()) {
        return QImage();
    }

    if (m_captureMode == CaptureMode::Horizontal) {
        // Horizontal mode: crop by width
        if (m_validWidth == 0) {
            return QImage();
        }
        if (m_validWidth == m_stitchedResult.width()) {
            return m_stitchedResult;
        }
        return m_stitchedResult.copy(0, 0, m_validWidth, m_stitchedResult.height());
    } else {
        // Vertical mode: crop by height
        if (m_validHeight == 0) {
            return QImage();
        }
        if (m_validHeight == m_stitchedResult.height()) {
            return m_stitchedResult;
        }
        return m_stitchedResult.copy(0, 0, m_stitchedResult.width(), m_validHeight);
    }
}

QSize ImageStitcher::getCurrentSize() const
{
    if (m_stitchedResult.isNull()) {
        return QSize(0, 0);
    }

    if (m_captureMode == CaptureMode::Horizontal) {
        return QSize(m_validWidth, m_stitchedResult.height());
    } else {
        return QSize(m_stitchedResult.width(), m_validHeight);
    }
}
