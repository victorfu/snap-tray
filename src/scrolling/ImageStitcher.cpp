#include "scrolling/ImageStitcher.h"

#include <cmath>

ImageStitcher::ImageStitcher(QObject *parent)
    : QObject(parent)
{
}

ImageStitcher::~ImageStitcher()
{
}

void ImageStitcher::setAlgorithm(Algorithm algo)
{
    m_algorithm = algo;
}

void ImageStitcher::setDetectFixedElements(bool enabled)
{
    m_detectFixedElements = enabled;
}

void ImageStitcher::setConfidenceThreshold(double threshold)
{
    m_confidenceThreshold = qBound(0.0, threshold, 1.0);
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
}

ImageStitcher::StitchResult ImageStitcher::addFrame(const QImage &frame)
{
    StitchResult result;

    if (frame.isNull()) {
        result.failureReason = "Empty frame";
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
        result.matchedFeatures = 0;
        result.usedAlgorithm = Algorithm::ORB;

        emit progressUpdated(m_frameCount, getCurrentSize());
        return result;
    }

    // Check if frame has changed enough - skip stitching but not a failure
    if (!isFrameChanged(frame, m_lastFrame)) {
        result.success = false;  // Not a successful stitch (no new content)
        result.confidence = 1.0;
        result.failureReason = "Frame unchanged";
        return result;
    }

    // Try matching algorithms based on settings
    if (m_algorithm == Algorithm::Auto) {
        // Use Row Projection as primary algorithm - it's faster and more robust for text documents
        // Row Projection uses 1D cross-correlation which is optimal for vertical/horizontal scrolling
        result = tryRowProjectionMatch(frame);
        if (result.success && result.confidence >= m_confidenceThreshold) {
            m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
            m_frameCount++;
            emit progressUpdated(m_frameCount, getCurrentSize());
            return result;
        }

        // Try template matching as fallback (good for non-text content)
        result = tryTemplateMatch(frame);
        if (result.success && result.confidence >= m_confidenceThreshold) {
            m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
            m_frameCount++;
            emit progressUpdated(m_frameCount, getCurrentSize());
            return result;
        }

        // Try ORB as last resort (works well for images with distinct features)
        result = tryORBMatch(frame);
        if (result.success && result.confidence >= m_confidenceThreshold) {
            m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
            m_frameCount++;
            emit progressUpdated(m_frameCount, getCurrentSize());
            return result;
        }

        // Fallback: match within existing stitched image (useful when user scrolls back)
        result = tryInPlaceMatchInStitched(frame);
        if (result.success) {
            m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
            m_frameCount++;
            emit progressUpdated(m_frameCount, getCurrentSize());
            return result;
        }

        // All failed
        result.success = false;
        result.failureReason = "No matching algorithm succeeded";
        return result;
    }
    else if (m_algorithm == Algorithm::RowProjection) {
        result = tryRowProjectionMatch(frame);
    }
    else if (m_algorithm == Algorithm::ORB) {
        result = tryORBMatch(frame);
    }
    else if (m_algorithm == Algorithm::TemplateMatching) {
        result = tryTemplateMatch(frame);
    }
    else {
        result = tryRowProjectionMatch(frame);
    }

    if (!result.success) {
        StitchResult inPlace = tryInPlaceMatchInStitched(frame);
        if (inPlace.success) {
            result = inPlace;
        }
    }

    if (result.success) {
        m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
        m_frameCount++;
        emit progressUpdated(m_frameCount, getCurrentSize());
    }

    return result;
}

bool ImageStitcher::isFrameChanged(const QImage &frame1, const QImage &frame2)
{
    if (frame1.size() != frame2.size()) {
        return true;
    }

    static constexpr int kSampleSize = 64;
    static constexpr int kPixelDiffThreshold = 20;
    static constexpr double kAverageDiffThreshold = 2.0;
    static constexpr double kChangedRatioThreshold = 0.005;

    QImage small1 = frame1.scaled(kSampleSize, kSampleSize, Qt::IgnoreAspectRatio,
                                  Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    QImage small2 = frame2.scaled(kSampleSize, kSampleSize, Qt::IgnoreAspectRatio,
                                  Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);

    if (small1.size() != small2.size()) {
        return true;
    }

    const int pixelCount = kSampleSize * kSampleSize;
    long long diffSum = 0;
    int changedPixels = 0;

    for (int y = 0; y < kSampleSize; ++y) {
        const QRgb *line1 = reinterpret_cast<const QRgb*>(small1.constScanLine(y));
        const QRgb *line2 = reinterpret_cast<const QRgb*>(small2.constScanLine(y));
        for (int x = 0; x < kSampleSize; ++x) {
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
