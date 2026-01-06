#include "scrolling/FrameStabilityDetector.h"

#include <QDebug>
#include <cmath>

FrameStabilityDetector::FrameStabilityDetector(QObject *parent)
    : QObject(parent)
{
}

FrameStabilityDetector::~FrameStabilityDetector() = default;

void FrameStabilityDetector::setConfig(const Config &config)
{
    m_config = config;
}

FrameStabilityDetector::Config FrameStabilityDetector::config() const
{
    return m_config;
}

void FrameStabilityDetector::setFixedRegions(const QRect &header, const QRect &footer)
{
    m_config.fixedHeaderRect = header;
    m_config.fixedFooterRect = footer;
}

void FrameStabilityDetector::reset()
{
    m_result = StabilityResult();
    m_lastFrame = QImage();
    m_lastROI = QImage();
}

FrameStabilityDetector::StabilityResult FrameStabilityDetector::addFrame(const QImage &frame)
{
    if (frame.isNull()) {
        return m_result;
    }

    m_result.totalFramesChecked++;

    // Check for timeout
    if (m_result.totalFramesChecked > m_config.maxWaitFrames) {
        m_result.timedOut = true;
        m_result.stable = false;
        qDebug() << "FrameStabilityDetector: Timeout after" << m_result.totalFramesChecked << "frames";
        return m_result;
    }

    // Extract and downsample ROI
    QImage currentROI = extractROI(frame);
    currentROI = downsample(currentROI);

    if (currentROI.isNull()) {
        m_lastFrame = frame;
        m_lastROI = currentROI;
        return m_result;
    }

    // First frame - no comparison possible
    if (m_lastROI.isNull()) {
        m_lastFrame = frame;
        m_lastROI = currentROI;
        return m_result;
    }

    // Compute NCC between current and previous ROI
    double ncc = computeNCC(m_lastROI, currentROI);
    m_result.stabilityScore = ncc;

    // Check if stable
    bool isStable = (ncc >= m_config.stabilityThreshold);

    if (isStable) {
        m_result.consecutiveStableCount++;
        if (m_result.consecutiveStableCount >= m_config.consecutiveStableRequired) {
            m_result.stable = true;
            qDebug() << "FrameStabilityDetector: Stable after" << m_result.totalFramesChecked
                     << "frames, score:" << ncc;
        }
    } else {
        // Reset consecutive count on any instability
        m_result.consecutiveStableCount = 0;
        m_result.stable = false;
    }

    m_lastFrame = frame;
    m_lastROI = currentROI;

    return m_result;
}

FrameStabilityDetector::StabilityResult FrameStabilityDetector::currentResult() const
{
    return m_result;
}

QImage FrameStabilityDetector::extractROI(const QImage &frame) const
{
    if (frame.isNull()) {
        return QImage();
    }

    int width = frame.width();
    int height = frame.height();

    // Calculate margin from edges
    int marginX = (width * m_config.roiMarginPercent) / 100;
    int marginY = (height * m_config.roiMarginPercent) / 100;

    // Start with center region excluding margins
    int roiX = marginX;
    int roiY = marginY;
    int roiWidth = width - (2 * marginX);
    int roiHeight = height - (2 * marginY);

    // Adjust for fixed header (exclude more from top)
    if (m_config.fixedHeaderRect.isValid() && !m_config.fixedHeaderRect.isEmpty()) {
        int headerBottom = m_config.fixedHeaderRect.bottom();
        if (headerBottom > roiY) {
            int adjustment = headerBottom - roiY + 10; // 10px safety margin
            roiY += adjustment;
            roiHeight -= adjustment;
        }
    }

    // Adjust for fixed footer (exclude more from bottom)
    if (m_config.fixedFooterRect.isValid() && !m_config.fixedFooterRect.isEmpty()) {
        int footerTop = m_config.fixedFooterRect.top();
        int currentBottom = roiY + roiHeight;
        if (currentBottom > footerTop) {
            int adjustment = currentBottom - footerTop + 10; // 10px safety margin
            roiHeight -= adjustment;
        }
    }

    // Validate ROI dimensions
    if (roiWidth < 32 || roiHeight < 32) {
        // ROI too small, use center 50% of frame
        roiX = width / 4;
        roiY = height / 4;
        roiWidth = width / 2;
        roiHeight = height / 2;
    }

    return frame.copy(roiX, roiY, roiWidth, roiHeight);
}

QImage FrameStabilityDetector::downsample(const QImage &image) const
{
    if (image.isNull()) {
        return QImage();
    }

    int maxDim = m_config.downsampleMaxDim;
    if (maxDim <= 0) {
        maxDim = 128;
    }

    int width = image.width();
    int height = image.height();

    // Only downsample if needed
    if (width <= maxDim && height <= maxDim) {
        return image.convertToFormat(QImage::Format_Grayscale8);
    }

    // Calculate scale factor
    double scale = static_cast<double>(maxDim) / std::max(width, height);
    int newWidth = static_cast<int>(width * scale);
    int newHeight = static_cast<int>(height * scale);

    // Scale and convert to grayscale
    return image.scaled(newWidth, newHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                .convertToFormat(QImage::Format_Grayscale8);
}

double FrameStabilityDetector::computeNCC(const QImage &img1, const QImage &img2) const
{
    if (img1.isNull() || img2.isNull()) {
        return 0.0;
    }

    // Ensure same size
    if (img1.size() != img2.size()) {
        return 0.0;
    }

    // Ensure grayscale format
    QImage gray1 = img1.format() == QImage::Format_Grayscale8
                   ? img1 : img1.convertToFormat(QImage::Format_Grayscale8);
    QImage gray2 = img2.format() == QImage::Format_Grayscale8
                   ? img2 : img2.convertToFormat(QImage::Format_Grayscale8);

    int width = gray1.width();
    int height = gray1.height();
    int pixelCount = width * height;

    if (pixelCount == 0) {
        return 0.0;
    }

    // Compute means
    double sum1 = 0.0, sum2 = 0.0;
    for (int y = 0; y < height; ++y) {
        const uchar *line1 = gray1.constScanLine(y);
        const uchar *line2 = gray2.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            sum1 += line1[x];
            sum2 += line2[x];
        }
    }
    double mean1 = sum1 / pixelCount;
    double mean2 = sum2 / pixelCount;

    // Compute NCC
    double sumProduct = 0.0;
    double sumSq1 = 0.0;
    double sumSq2 = 0.0;

    for (int y = 0; y < height; ++y) {
        const uchar *line1 = gray1.constScanLine(y);
        const uchar *line2 = gray2.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            double v1 = line1[x] - mean1;
            double v2 = line2[x] - mean2;
            sumProduct += v1 * v2;
            sumSq1 += v1 * v1;
            sumSq2 += v2 * v2;
        }
    }

    double denominator = std::sqrt(sumSq1 * sumSq2);
    if (denominator < 1e-10) {
        // Both images are constant (uniform color) - consider them identical
        return 1.0;
    }

    double ncc = sumProduct / denominator;

    // Clamp to [-1, 1] range (numerical precision)
    return std::clamp(ncc, -1.0, 1.0);
}

double FrameStabilityDetector::compareFrames(const QImage &frame1, const QImage &frame2,
                                              const QRect &roi, int maxDim)
{
    FrameStabilityDetector detector;
    Config config;
    config.downsampleMaxDim = maxDim;
    detector.setConfig(config);

    QImage img1 = frame1;
    QImage img2 = frame2;

    // Apply ROI if specified
    if (roi.isValid() && !roi.isEmpty()) {
        img1 = frame1.copy(roi);
        img2 = frame2.copy(roi);
    }

    // Downsample
    img1 = detector.downsample(img1);
    img2 = detector.downsample(img2);

    return detector.computeNCC(img1, img2);
}
