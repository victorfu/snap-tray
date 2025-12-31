#include "stitching/StaticSceneAnalyzer.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QDebug>

class StaticSceneAnalyzer::Private
{
public:
    Config config;

    int frameCount = 0;
    cv::Size frameSize;

    // Welford's algorithm state (per-pixel online variance)
    cv::Mat mean;       // Running mean (CV_32FC3)
    cv::Mat M2;         // Running sum of squared differences (CV_32FC3)

    // Cached results
    cv::Mat variance;   // Computed variance map
    cv::Mat staticMask; // Binary mask (CV_8UC1)
    bool maskValid = false;

    void updateWelford(const cv::Mat &frame);
    void computeVariance();
    void generateMask();
    void classifyRegions(std::vector<StaticSceneAnalyzer::StaticRegion> &regions) const;
};

void StaticSceneAnalyzer::Private::updateWelford(const cv::Mat &frame)
{
    // Convert to float (0-1 range) for numerical stability
    cv::Mat floatFrame;
    frame.convertTo(floatFrame, CV_32FC3, 1.0 / 255.0);

    if (frameCount == 0) {
        // First frame: initialize mean and M2
        frameSize = frame.size();
        mean = floatFrame.clone();
        M2 = cv::Mat::zeros(frameSize, CV_32FC3);
        frameCount = 1;
        return;
    }

    frameCount++;

    // Welford's online algorithm for variance:
    // For each new value x:
    //   delta = x - mean
    //   mean = mean + delta / n
    //   delta2 = x - mean  (using updated mean)
    //   M2 = M2 + delta * delta2
    // Variance = M2 / (n - 1)

    cv::Mat delta;
    cv::subtract(floatFrame, mean, delta);

    // Update mean: mean += delta / n
    cv::Mat scaledDelta;
    delta.convertTo(scaledDelta, CV_32FC3, 1.0 / static_cast<double>(frameCount));
    cv::add(mean, scaledDelta, mean);

    // Compute delta2 = x - updated_mean
    cv::Mat delta2;
    cv::subtract(floatFrame, mean, delta2);

    // Update M2: M2 += delta * delta2 (element-wise)
    cv::Mat m2Update;
    cv::multiply(delta, delta2, m2Update);
    cv::add(M2, m2Update, M2);

    maskValid = false;
}

void StaticSceneAnalyzer::Private::computeVariance()
{
    if (frameCount < 2) {
        variance = cv::Mat::zeros(frameSize, CV_32FC3);
        return;
    }

    // Variance = M2 / (n - 1)
    M2.convertTo(variance, CV_32FC3, 1.0 / static_cast<double>(frameCount - 1));
}

void StaticSceneAnalyzer::Private::generateMask()
{
    computeVariance();

    // Convert to single-channel variance (max across RGB channels)
    // This ensures we detect static regions even if one channel varies
    std::vector<cv::Mat> channels;
    cv::split(variance, channels);

    cv::Mat maxVariance;
    cv::max(channels[0], channels[1], maxVariance);
    cv::max(maxVariance, channels[2], maxVariance);

    // Threshold: low variance = static
    cv::Mat rawMask;
    cv::threshold(maxVariance, rawMask, config.varianceThreshold, 1.0, cv::THRESH_BINARY_INV);
    rawMask.convertTo(rawMask, CV_8UC1, 255.0);

    // Morphological cleanup
    cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_RECT,
        cv::Size(config.morphKernelSize, config.morphKernelSize));

    // Close: fill small holes in static regions
    cv::morphologyEx(rawMask, staticMask, cv::MORPH_CLOSE, kernel);

    // Open: remove small noise
    cv::morphologyEx(staticMask, staticMask, cv::MORPH_OPEN, kernel);

    maskValid = true;
}

void StaticSceneAnalyzer::Private::classifyRegions(
    std::vector<StaticSceneAnalyzer::StaticRegion> &regions) const
{
    if (!maskValid)
        return;

    // Find contours of static regions
    std::vector<std::vector<cv::Point>> contours;
    cv::Mat maskCopy = staticMask.clone();
    cv::findContours(maskCopy, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    for (const auto &contour : contours) {
        cv::Rect bounds = cv::boundingRect(contour);

        // Skip tiny regions
        if (bounds.area() < config.minRegionArea)
            continue;

        StaticSceneAnalyzer::StaticRegion region;
        region.bounds = QRect(bounds.x, bounds.y, bounds.width, bounds.height);

        // Compute staticness as ratio of static pixels in bounding rect
        cv::Mat roi = staticMask(bounds);
        region.staticness = cv::countNonZero(roi) / static_cast<double>(bounds.area());

        // Classify by position relative to frame
        double relativeTop = bounds.y / static_cast<double>(frameSize.height);
        double relativeBottom = (bounds.y + bounds.height) / static_cast<double>(frameSize.height);
        double relativeLeft = bounds.x / static_cast<double>(frameSize.width);
        double relativeRight = (bounds.x + bounds.width) / static_cast<double>(frameSize.width);
        double relativeWidth = bounds.width / static_cast<double>(frameSize.width);
        double relativeHeight = bounds.height / static_cast<double>(frameSize.height);

        // Classification logic
        if (relativeTop < 0.15 && relativeWidth > 0.5) {
            // Top 15% and spans >50% width = header
            region.type = QStringLiteral("header");
        } else if (relativeBottom > 0.85 && relativeWidth > 0.5) {
            // Bottom 15% and spans >50% width = footer
            region.type = QStringLiteral("footer");
        } else if (relativeLeft < 0.15 && relativeHeight > 0.3) {
            // Left 15% and tall = left sidebar
            region.type = QStringLiteral("sidebar-left");
        } else if (relativeRight > 0.85 && relativeHeight > 0.3) {
            // Right 15% and tall = right sidebar
            region.type = QStringLiteral("sidebar-right");
        } else {
            // Anything else = floating element
            region.type = QStringLiteral("floating");
        }

        regions.push_back(region);
    }
}

// === Public Implementation ===

StaticSceneAnalyzer::StaticSceneAnalyzer(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

StaticSceneAnalyzer::~StaticSceneAnalyzer() = default;

void StaticSceneAnalyzer::setConfig(const Config &config)
{
    d->config = config;
}

StaticSceneAnalyzer::Config StaticSceneAnalyzer::config() const
{
    return d->config;
}

void StaticSceneAnalyzer::addFrame(const cv::Mat &frame)
{
    if (frame.empty())
        return;

    // Ensure consistent size
    if (d->frameCount > 0 && frame.size() != d->frameSize) {
        qWarning() << "StaticSceneAnalyzer: Frame size mismatch, resetting";
        reset();
    }

    // Handle different input formats
    cv::Mat bgrFrame;
    if (frame.channels() == 4) {
        cv::cvtColor(frame, bgrFrame, cv::COLOR_BGRA2BGR);
    } else if (frame.channels() == 1) {
        cv::cvtColor(frame, bgrFrame, cv::COLOR_GRAY2BGR);
    } else {
        bgrFrame = frame;
    }

    d->updateWelford(bgrFrame);

    // Check if ready for first analysis
    if (d->frameCount == d->config.minFramesForAnalysis) {
        d->generateMask();

        // Emit signals for detected regions
        auto regions = detectRegions();
        for (const auto &region : regions) {
            emit staticRegionDetected(region);
        }

        emit analysisReady();
    } else if (d->frameCount > d->config.minFramesForAnalysis) {
        // Update mask periodically (every 5 frames)
        if (d->frameCount % 5 == 0) {
            d->generateMask();
        }
    }
}

cv::Mat StaticSceneAnalyzer::getStaticMask() const
{
    if (!d->maskValid && d->frameCount >= d->config.minFramesForAnalysis) {
        // Force generation if needed
        const_cast<Private *>(d.get())->generateMask();
    }
    return d->staticMask.clone();
}

cv::Mat StaticSceneAnalyzer::getVarianceMap() const
{
    const_cast<Private *>(d.get())->computeVariance();

    if (d->variance.empty())
        return cv::Mat();

    // Convert to grayscale and normalize for visualization
    std::vector<cv::Mat> channels;
    cv::split(d->variance, channels);

    cv::Mat maxVariance;
    cv::max(channels[0], channels[1], maxVariance);
    cv::max(maxVariance, channels[2], maxVariance);

    cv::Mat normalized;
    cv::normalize(maxVariance, normalized, 0, 255, cv::NORM_MINMAX);
    normalized.convertTo(normalized, CV_8UC1);

    return normalized;
}

cv::Mat StaticSceneAnalyzer::applyMask(const cv::Mat &frame) const
{
    cv::Mat mask = getStaticMask();
    if (mask.empty())
        return frame.clone();

    cv::Mat result = frame.clone();

    // Ensure mask matches frame channels
    cv::Mat invertedMask;
    cv::bitwise_not(mask, invertedMask);

    // Zero out static regions (set to black)
    if (frame.channels() == 1) {
        result.setTo(cv::Scalar(0), mask);
    } else if (frame.channels() == 3) {
        result.setTo(cv::Scalar(0, 0, 0), mask);
    } else if (frame.channels() == 4) {
        result.setTo(cv::Scalar(0, 0, 0, 255), mask);
    }

    return result;
}

std::vector<StaticSceneAnalyzer::StaticRegion> StaticSceneAnalyzer::detectRegions() const
{
    std::vector<StaticRegion> regions;

    if (d->frameCount >= d->config.minFramesForAnalysis) {
        if (!d->maskValid) {
            const_cast<Private *>(d.get())->generateMask();
        }
        d->classifyRegions(regions);
    }

    return regions;
}

void StaticSceneAnalyzer::getCropValues(int &topCrop, int &bottomCrop) const
{
    topCrop = 0;
    bottomCrop = 0;

    auto regions = detectRegions();
    for (const auto &region : regions) {
        if (region.type == QStringLiteral("header")) {
            topCrop = std::max(topCrop, region.bounds.bottom());
        } else if (region.type == QStringLiteral("footer")) {
            int fromBottom = d->frameSize.height - region.bounds.top();
            bottomCrop = std::max(bottomCrop, fromBottom);
        }
    }
}

bool StaticSceneAnalyzer::isReady() const
{
    return d->frameCount >= d->config.minFramesForAnalysis && d->maskValid;
}

void StaticSceneAnalyzer::reset()
{
    d->frameCount = 0;
    d->mean = cv::Mat();
    d->M2 = cv::Mat();
    d->variance = cv::Mat();
    d->staticMask = cv::Mat();
    d->maskValid = false;
}

int StaticSceneAnalyzer::frameCount() const
{
    return d->frameCount;
}
