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

void ImageStitcher::reset()
{
    m_stitchedResult = QImage();
    m_lastFrame = QImage();
    m_currentViewportRect = QRect();
    m_frameCount = 0;
    m_validHeight = 0;
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
        // Try ORB first
        result = tryORBMatch(frame);
        if (result.success && result.confidence >= m_confidenceThreshold) {
            m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
            m_frameCount++;
            emit progressUpdated(m_frameCount, getCurrentSize());
            return result;
        }

        // Try template matching as fallback
        result = tryTemplateMatch(frame);
        if (result.success && result.confidence >= m_confidenceThreshold) {
            m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
            m_frameCount++;
            emit progressUpdated(m_frameCount, getCurrentSize());
            return result;
        }

        // Both failed
        result.success = false;
        result.failureReason = "No matching algorithm succeeded";
        return result;
    }
    else if (m_algorithm == Algorithm::ORB) {
        result = tryORBMatch(frame);
    }
    else if (m_algorithm == Algorithm::TemplateMatching) {
        result = tryTemplateMatch(frame);
    }
    else {
        result = tryORBMatch(frame);
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
    if (m_stitchedResult.isNull() || m_validHeight == 0) {
        return QImage();
    }
    if (m_validHeight == m_stitchedResult.height()) {
        return m_stitchedResult;
    }
    return m_stitchedResult.copy(0, 0, m_stitchedResult.width(), m_validHeight);
}

QSize ImageStitcher::getCurrentSize() const
{
    if (m_stitchedResult.isNull()) {
        return QSize(0, 0);
    }
    return QSize(m_stitchedResult.width(), m_validHeight);
}
