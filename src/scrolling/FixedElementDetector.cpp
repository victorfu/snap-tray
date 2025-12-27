#include "scrolling/FixedElementDetector.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QDebug>
#include <cmath>

FixedElementDetector::FixedElementDetector(QObject *parent)
    : QObject(parent)
{
}

FixedElementDetector::~FixedElementDetector()
{
}

void FixedElementDetector::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void FixedElementDetector::reset()
{
    m_leadingRegions.clear();
    m_trailingRegions.clear();
    m_leadingCropSize = 0;
    m_trailingCropSize = 0;
    m_detected = false;
    m_frameCount = 0;
}

void FixedElementDetector::addFrame(const QImage &frame)
{
    if (!m_enabled || frame.isNull()) {
        return;
    }

    // Limit stored regions (we only need regions, not full frames)
    if (m_leadingRegions.size() >= MAX_FRAMES_TO_STORE) {
        m_leadingRegions.erase(m_leadingRegions.begin());
        m_trailingRegions.erase(m_trailingRegions.begin());
    }

    m_frameCount++;

    // Extract leading (top) and trailing (bottom) regions for vertical scrolling
    int regionHeight = std::min(ANALYSIS_REGION_SIZE, frame.height() / 3);
    QImage leadingRegion = frame.copy(0, 0, frame.width(), regionHeight);
    QImage trailingRegion = frame.copy(0, frame.height() - regionHeight, frame.width(), regionHeight);

    m_leadingRegions.push_back(leadingRegion.convertToFormat(QImage::Format_RGB32));
    m_trailingRegions.push_back(trailingRegion.convertToFormat(QImage::Format_RGB32));
}

FixedElementDetector::DetectionResult FixedElementDetector::detect()
{
    DetectionResult result;

    if (!m_enabled || m_frameCount < MIN_FRAMES_FOR_DETECTION) {
        return result;
    }

    if (m_leadingRegions.size() < MIN_FRAMES_FOR_DETECTION) {
        return result;
    }

    // Verify that scrolling is actually happening by checking if first and last
    // regions differ (prevents false positives on static content or slow scrolling)
    double firstLastLeadingSimilarity = calculateRegionSimilarity(
        m_leadingRegions.front(), m_leadingRegions.back());
    double firstLastTrailingSimilarity = calculateRegionSimilarity(
        m_trailingRegions.front(), m_trailingRegions.back());

    // If both leading and trailing are nearly identical between first and last frame,
    // there's no scrolling happening - don't detect fixed elements
    if (firstLastLeadingSimilarity >= SIMILARITY_THRESHOLD &&
        firstLastTrailingSimilarity >= SIMILARITY_THRESHOLD) {
        // Content appears static, not scrolling
        return result;
    }

    // Analyze leading region (header/left sidebar)
    analyzeLeadingRegion();

    // Analyze trailing region (footer/right sidebar)
    analyzeTrailingRegion();

    result.detected = (m_leadingCropSize > 0 || m_trailingCropSize > 0);
    result.leadingFixed = m_leadingCropSize;
    result.trailingFixed = m_trailingCropSize;
    result.confidence = result.detected ? 0.9 : 0.0;

    m_detected = result.detected;

    if (result.detected) {
        qDebug() << "FixedElementDetector: Detected fixed elements -"
                 << "leading:" << m_leadingCropSize
                 << "trailing:" << m_trailingCropSize;
    }

    return result;
}

void FixedElementDetector::analyzeLeadingRegion()
{
    if (m_leadingRegions.size() < MIN_FRAMES_FOR_DETECTION) {
        return;
    }

    // Compare consecutive frames to find consistent leading region
    int consecutiveMatches = 0;
    int bestMatchHeight = 0;

    for (size_t i = 1; i < m_leadingRegions.size(); ++i) {
        double similarity = calculateRegionSimilarity(m_leadingRegions[i - 1], m_leadingRegions[i]);

        if (similarity >= SIMILARITY_THRESHOLD) {
            consecutiveMatches++;
            if (consecutiveMatches >= MIN_FRAMES_FOR_DETECTION - 1) {
                // Found consistent fixed region (vertical: height)
                bestMatchHeight = m_leadingRegions[0].height();
            }
        } else {
            consecutiveMatches = 0;
        }
    }

    if (bestMatchHeight > 0) {
        m_leadingCropSize = bestMatchHeight;
    }
}

void FixedElementDetector::analyzeTrailingRegion()
{
    if (m_trailingRegions.size() < MIN_FRAMES_FOR_DETECTION) {
        return;
    }

    // Compare consecutive frames to find consistent trailing region
    int consecutiveMatches = 0;
    int bestMatchSize = 0;

    for (size_t i = 1; i < m_trailingRegions.size(); ++i) {
        double similarity = calculateRegionSimilarity(m_trailingRegions[i - 1], m_trailingRegions[i]);

        if (similarity >= SIMILARITY_THRESHOLD) {
            consecutiveMatches++;
            if (consecutiveMatches >= MIN_FRAMES_FOR_DETECTION - 1) {
                // Found consistent fixed region (vertical: height)
                bestMatchSize = m_trailingRegions[0].height();
            }
        } else {
            consecutiveMatches = 0;
        }
    }

    if (bestMatchSize > 0) {
        m_trailingCropSize = bestMatchSize;
    }
}

double FixedElementDetector::calculateRegionSimilarity(const QImage &region1, const QImage &region2) const
{
    if (region1.size() != region2.size() || region1.isNull() || region2.isNull()) {
        return 0.0;
    }

    // Simple pixel-by-pixel comparison
    int totalPixels = region1.width() * region1.height();
    int matchingPixels = 0;

    for (int y = 0; y < region1.height(); ++y) {
        const QRgb *line1 = reinterpret_cast<const QRgb*>(region1.constScanLine(y));
        const QRgb *line2 = reinterpret_cast<const QRgb*>(region2.constScanLine(y));

        for (int x = 0; x < region1.width(); ++x) {
            int diff = std::abs(qRed(line1[x]) - qRed(line2[x])) +
                       std::abs(qGreen(line1[x]) - qGreen(line2[x])) +
                       std::abs(qBlue(line1[x]) - qBlue(line2[x]));

            if (diff < 30) {  // threshold for pixel match
                matchingPixels++;
            }
        }
    }

    return static_cast<double>(matchingPixels) / totalPixels;
}

bool FixedElementDetector::hasDetectedElements() const
{
    return m_detected && (m_leadingCropSize > 0 || m_trailingCropSize > 0);
}

QImage FixedElementDetector::cropFixedRegions(const QImage &frame) const
{
    if (!hasDetectedElements() || frame.isNull()) {
        return frame;
    }

    // Vertical scrolling: crop top (leading) and bottom (trailing) fixed regions
    int newHeight = frame.height() - m_leadingCropSize - m_trailingCropSize;
    if (newHeight <= 0) {
        return frame;
    }
    return frame.copy(0, m_leadingCropSize, frame.width(), newHeight);
}
