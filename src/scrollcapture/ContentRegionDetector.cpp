#include "scrollcapture/ContentRegionDetector.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
struct Span {
    int start = -1;
    int end = -1;

    bool isValid() const { return start >= 0 && end >= start; }
    int length() const { return isValid() ? (end - start + 1) : 0; }
};

cv::Mat toGrayMat(const QImage& image)
{
    if (image.isNull()) {
        return {};
    }
    QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat mat(gray.height(), gray.width(), CV_8UC1,
                const_cast<uchar*>(gray.constBits()), gray.bytesPerLine());
    return mat.clone();
}

Span findLargestActiveSpan(const cv::Mat& projection, double threshold, int minLength)
{
    Span best;
    int currentStart = -1;
    const int length = std::max(projection.rows, projection.cols);

    for (int i = 0; i < length; ++i) {
        const double value = (projection.rows == 1)
            ? projection.at<float>(0, i)
            : projection.at<float>(i, 0);

        if (value >= threshold) {
            if (currentStart < 0) {
                currentStart = i;
            }
            continue;
        }

        if (currentStart >= 0) {
            const int currentEnd = i - 1;
            Span candidate{currentStart, currentEnd};
            if (candidate.length() >= minLength && candidate.length() > best.length()) {
                best = candidate;
            }
            currentStart = -1;
        }
    }

    if (currentStart >= 0) {
        Span candidate{currentStart, length - 1};
        if (candidate.length() >= minLength && candidate.length() > best.length()) {
            best = candidate;
        }
    }

    return best;
}

ContentRegionDetector::DetectionResult fallbackResult(const QRect& windowRect, const QString& warning)
{
    ContentRegionDetector::DetectionResult result;
    result.success = true;
    result.fallbackUsed = true;
    result.contentRect = windowRect;
    result.headerIgnore = 0;
    result.footerIgnore = 0;
    result.confidence = 0.1;
    result.warning = warning;
    return result;
}
} // namespace

ContentRegionDetector::DetectionResult ContentRegionDetector::detect(const QImage& before,
                                                                     const QImage& after,
                                                                     const QRect& windowRect) const
{
    if (before.isNull() || after.isNull() || !windowRect.isValid()) {
        return fallbackResult(windowRect, QStringLiteral("Invalid frames for content detection."));
    }

    cv::Mat beforeGray = toGrayMat(before);
    cv::Mat afterGray = toGrayMat(after);
    if (beforeGray.empty() || afterGray.empty() || beforeGray.size() != afterGray.size()) {
        return fallbackResult(windowRect, QStringLiteral("Unable to build grayscale frames."));
    }

    const int originalW = beforeGray.cols;
    const int originalH = beforeGray.rows;

    constexpr int kTargetWidth = 700;
    const double scale = (originalW > kTargetWidth)
        ? (static_cast<double>(kTargetWidth) / static_cast<double>(originalW))
        : 1.0;

    cv::Mat beforeScaled = beforeGray;
    cv::Mat afterScaled = afterGray;
    if (scale < 1.0) {
        cv::resize(beforeGray, beforeScaled, cv::Size(), scale, scale, cv::INTER_AREA);
        cv::resize(afterGray, afterScaled, cv::Size(), scale, scale, cv::INTER_AREA);
    }

    cv::Mat diff;
    cv::absdiff(beforeScaled, afterScaled, diff);
    cv::GaussianBlur(diff, diff, cv::Size(3, 3), 0.0);

    cv::Mat rowProjection;
    cv::Mat colProjection;
    cv::reduce(diff, rowProjection, 1, cv::REDUCE_AVG, CV_32F);
    cv::reduce(diff, colProjection, 0, cv::REDUCE_AVG, CV_32F);

    double rowMin = 0.0;
    double rowMax = 0.0;
    double colMin = 0.0;
    double colMax = 0.0;
    cv::minMaxLoc(rowProjection, &rowMin, &rowMax);
    cv::minMaxLoc(colProjection, &colMin, &colMax);

    const double rowThreshold = std::max(4.0, rowMax * 0.25);
    const double colThreshold = std::max(4.0, colMax * 0.25);

    const int minRowLength = std::max(40, static_cast<int>(beforeScaled.rows * 0.28));
    const int minColLength = std::max(60, static_cast<int>(beforeScaled.cols * 0.38));

    Span rowSpan = findLargestActiveSpan(rowProjection, rowThreshold, minRowLength);
    Span colSpan = findLargestActiveSpan(colProjection, colThreshold, minColLength);

    if (!rowSpan.isValid() || !colSpan.isValid()) {
        return fallbackResult(windowRect, QStringLiteral("Dynamic region is too small; fallback to window bounds."));
    }

    int x0 = static_cast<int>(std::round(colSpan.start / scale));
    int x1 = static_cast<int>(std::round((colSpan.end + 1) / scale));
    int y0 = static_cast<int>(std::round(rowSpan.start / scale));
    int y1 = static_cast<int>(std::round((rowSpan.end + 1) / scale));

    x0 = std::clamp(x0, 0, originalW - 1);
    y0 = std::clamp(y0, 0, originalH - 1);
    x1 = std::clamp(x1, x0 + 1, originalW);
    y1 = std::clamp(y1, y0 + 1, originalH);

    const double logicalScaleX = static_cast<double>(windowRect.width()) / static_cast<double>(originalW);
    const double logicalScaleY = static_cast<double>(windowRect.height()) / static_cast<double>(originalH);

    QRect detectedRect(
        windowRect.x() + static_cast<int>(std::round(x0 * logicalScaleX)),
        windowRect.y() + static_cast<int>(std::round(y0 * logicalScaleY)),
        static_cast<int>(std::round((x1 - x0) * logicalScaleX)),
        static_cast<int>(std::round((y1 - y0) * logicalScaleY))
    );

    detectedRect = detectedRect.intersected(windowRect);
    if (detectedRect.width() < std::max(80, windowRect.width() / 4)
        || detectedRect.height() < std::max(80, windowRect.height() / 4)) {
        return fallbackResult(windowRect, QStringLiteral("Detected content region is too small."));
    }

    DetectionResult result;
    result.success = true;
    result.contentRect = detectedRect;
    result.headerIgnore = std::max(0, detectedRect.top() - windowRect.top());
    result.footerIgnore = std::max(0, windowRect.bottom() - detectedRect.bottom());
    result.confidence = std::clamp((rowMax + colMax) / 200.0, 0.0, 1.0);
    return result;
}
