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
    m_regionWriteIndex = 0;
}

void FixedElementDetector::addFrame(const QImage &frame)
{
    if (!m_enabled || frame.isNull()) {
        return;
    }

    m_frameCount++;

    // Calculate region height with bounds validation
    int regionHeight = std::min(ANALYSIS_REGION_SIZE, frame.height() / 3);
    if (regionHeight < MIN_ANALYSIS_REGION_SIZE) {
        // Frame is too small for meaningful analysis
        qDebug() << "FixedElementDetector: Frame too small for analysis";
        return;
    }

    // Validate region bounds before extraction
    int trailingY = frame.height() - regionHeight;
    if (trailingY < 0) {
        trailingY = 0;
        regionHeight = frame.height();
    }

    // Extract leading (top) and trailing (bottom) regions for vertical scrolling
    QImage leadingRegion = frame.copy(0, 0, frame.width(), regionHeight);
    QImage trailingRegion = frame.copy(0, trailingY, frame.width(), regionHeight);

    if (leadingRegion.isNull() || trailingRegion.isNull()) {
        qDebug() << "FixedElementDetector: Failed to extract regions";
        return;
    }

    // Use circular buffer for efficient storage (no vector erase from front)
    if (static_cast<int>(m_leadingRegions.size()) < MAX_FRAMES_TO_STORE) {
        // Still filling the buffer
        m_leadingRegions.push_back(leadingRegion.convertToFormat(QImage::Format_RGB32));
        m_trailingRegions.push_back(trailingRegion.convertToFormat(QImage::Format_RGB32));
    } else {
        // Buffer full - overwrite oldest entry (circular buffer)
        m_leadingRegions[m_regionWriteIndex] = leadingRegion.convertToFormat(QImage::Format_RGB32);
        m_trailingRegions[m_regionWriteIndex] = trailingRegion.convertToFormat(QImage::Format_RGB32);
        m_regionWriteIndex = (m_regionWriteIndex + 1) % MAX_FRAMES_TO_STORE;
    }
}

FixedElementDetector::DetectionResult FixedElementDetector::detect()
{
    DetectionResult result;

    if (!m_enabled) {
        return result;
    }

    // Need at least MIN_FRAMES_FOR_DETECTION valid regions
    if (static_cast<int>(m_leadingRegions.size()) < MIN_FRAMES_FOR_DETECTION) {
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
    if (m_leadingRegions.empty()) {
        m_leadingCropSize = 0;
        return;
    }

    // Validate all regions have consistent dimensions
    int height = m_leadingRegions[0].height();
    int width = m_leadingRegions[0].width();

    for (size_t i = 1; i < m_leadingRegions.size(); ++i) {
        if (m_leadingRegions[i].height() != height || m_leadingRegions[i].width() != width) {
            qDebug() << "FixedElementDetector: Inconsistent region dimensions";
            m_leadingCropSize = 0;
            return;
        }
    }

    if (height <= 0 || width <= 0) {
        m_leadingCropSize = 0;
        return;
    }

    int consistentRows = 0;

    // Check row by row from top
    for (int y = 0; y < height; ++y) {
        bool rowIsFixed = true;

        // Compare this row across all stored frames
        for (size_t i = 1; i < m_leadingRegions.size(); ++i) {
            if (!compareRows(m_leadingRegions[i-1], m_leadingRegions[i], y)) {
                rowIsFixed = false;
                break;
            }
        }

        if (rowIsFixed) {
            consistentRows++;
        } else {
            // Found a changing row, stop (assume header is contiguous)
            break;
        }
    }

    m_leadingCropSize = consistentRows;
}

void FixedElementDetector::analyzeTrailingRegion()
{
    if (m_trailingRegions.empty()) {
        m_trailingCropSize = 0;
        return;
    }

    // Validate all regions have consistent dimensions
    int height = m_trailingRegions[0].height();
    int width = m_trailingRegions[0].width();

    for (size_t i = 1; i < m_trailingRegions.size(); ++i) {
        if (m_trailingRegions[i].height() != height || m_trailingRegions[i].width() != width) {
            qDebug() << "FixedElementDetector: Inconsistent trailing region dimensions";
            m_trailingCropSize = 0;
            return;
        }
    }

    if (height <= 0 || width <= 0) {
        m_trailingCropSize = 0;
        return;
    }

    int consistentRows = 0;

    // Check row by row from bottom
    for (int y = height - 1; y >= 0; --y) {
        bool rowIsFixed = true;

        // Compare this row across all stored frames
        for (size_t i = 1; i < m_trailingRegions.size(); ++i) {
            if (!compareRows(m_trailingRegions[i-1], m_trailingRegions[i], y)) {
                rowIsFixed = false;
                break;
            }
        }

        if (rowIsFixed) {
            consistentRows++;
        } else {
            // Found a changing row, stop
            break;
        }
    }

    m_trailingCropSize = consistentRows;
}

bool FixedElementDetector::compareRows(const QImage &img1, const QImage &img2, int y) const
{
    if (img1.width() != img2.width() || y < 0 || y >= img1.height() || y >= img2.height()) {
        return false;
    }

    const QRgb *line1 = reinterpret_cast<const QRgb*>(img1.constScanLine(y));
    const QRgb *line2 = reinterpret_cast<const QRgb*>(img2.constScanLine(y));
    
    int diffCount = 0;
    int threshold = img1.width() * 0.1; // Allow 10% pixels to differ (noise/compression artifacts)

    for (int x = 0; x < img1.width(); ++x) {
        int diff = std::abs(qRed(line1[x]) - qRed(line2[x])) +
                   std::abs(qGreen(line1[x]) - qGreen(line2[x])) +
                   std::abs(qBlue(line1[x]) - qBlue(line2[x]));

        if (diff > 30) {
            diffCount++;
            if (diffCount > threshold) {
                return false;
            }
        }
    }

    return true;
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
