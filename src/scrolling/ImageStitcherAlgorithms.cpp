#include "scrolling/ImageStitcher.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
// RSSA: Removed opencv2/features2d.hpp - no longer using ORB/SIFT feature detection

#include <QPainter>
#include <QDebug>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
double normalizedVariance(const cv::Mat &mat)
{
    if (mat.empty()) {
        return 0.0;
    }

    cv::Scalar mean, stddev;
    cv::meanStdDev(mat, mean, stddev);
    return (stddev[0] * stddev[0]) / 255.0;
}

cv::Mat qImageToCvMatLocal(const QImage &image)
{
    QImage rgb = image.convertToFormat(QImage::Format_RGB32);

    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));

    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGBA2BGR);
    return bgr.clone();
}

constexpr double kMaxOverlapDiff = 0.10;
constexpr int kOverlapDiffMaxDim = 240;

// Sub-pixel refinement using parabolic interpolation
// Returns refined position with sub-pixel accuracy
double subPixelRefine1D(const cv::Mat &matchResult, int bestPos, bool isVertical)
{
    if (matchResult.empty()) {
        return static_cast<double>(bestPos);
    }

    int maxPos = isVertical ? (matchResult.rows - 1) : (matchResult.cols - 1);
    if (bestPos <= 0 || bestPos >= maxPos) {
        return static_cast<double>(bestPos);
    }

    double y0, y1, y2;
    if (isVertical) {
        y0 = matchResult.at<float>(bestPos - 1, 0);
        y1 = matchResult.at<float>(bestPos, 0);
        y2 = matchResult.at<float>(bestPos + 1, 0);
    } else {
        y0 = matchResult.at<float>(0, bestPos - 1);
        y1 = matchResult.at<float>(0, bestPos);
        y2 = matchResult.at<float>(0, bestPos + 1);
    }

    // Parabolic interpolation: find vertex of parabola through (bestPos-1, y0), (bestPos, y1), (bestPos+1, y2)
    double denominator = y0 - 2.0 * y1 + y2;
    if (std::abs(denominator) < 1e-10) {
        return static_cast<double>(bestPos);
    }

    double subPixelOffset = 0.5 * (y0 - y2) / denominator;
    // Clamp to reasonable range [-0.5, 0.5]
    subPixelOffset = std::clamp(subPixelOffset, -0.5, 0.5);

    return static_cast<double>(bestPos) + subPixelOffset;
}

// Sub-pixel refinement for 2D match result (template matching)
cv::Point2d subPixelRefine2D(const cv::Mat &matchResult, const cv::Point &bestLoc)
{
    if (matchResult.empty()) {
        return cv::Point2d(bestLoc.x, bestLoc.y);
    }

    double refinedX = static_cast<double>(bestLoc.x);
    double refinedY = static_cast<double>(bestLoc.y);

    // Refine Y (vertical) position
    if (bestLoc.y > 0 && bestLoc.y < matchResult.rows - 1) {
        float y0 = matchResult.at<float>(bestLoc.y - 1, bestLoc.x);
        float y1 = matchResult.at<float>(bestLoc.y, bestLoc.x);
        float y2 = matchResult.at<float>(bestLoc.y + 1, bestLoc.x);
        double denom = y0 - 2.0 * y1 + y2;
        if (std::abs(denom) > 1e-10) {
            double offset = 0.5 * (y0 - y2) / denom;
            refinedY += std::clamp(offset, -0.5, 0.5);
        }
    }

    // Refine X (horizontal) position
    if (bestLoc.x > 0 && bestLoc.x < matchResult.cols - 1) {
        float x0 = matchResult.at<float>(bestLoc.y, bestLoc.x - 1);
        float x1 = matchResult.at<float>(bestLoc.y, bestLoc.x);
        float x2 = matchResult.at<float>(bestLoc.y, bestLoc.x + 1);
        double denom = x0 - 2.0 * x1 + x2;
        if (std::abs(denom) > 1e-10) {
            double offset = 0.5 * (x0 - x2) / denom;
            refinedX += std::clamp(offset, -0.5, 0.5);
        }
    }

    return cv::Point2d(refinedX, refinedY);
}

// Seam verification threshold (stricter than general overlap diff)
constexpr double kSeamVerifyThreshold = 0.06;

// Verify seam alignment at full resolution for a given overlap value
// Returns the normalized difference at the seam (lower is better)
double verifySeamAtOverlap(const cv::Mat &lastGray, const cv::Mat &newGray,
                           ImageStitcher::ScrollDirection direction,
                           bool isHorizontal, int overlap)
{
    if (overlap <= 0 || lastGray.empty() || newGray.empty()) {
        return 1.0;
    }

    cv::Mat lastOverlap, newOverlap;

    if (isHorizontal) {
        int height = std::min(lastGray.rows, newGray.rows);
        int width = std::min({overlap, lastGray.cols, newGray.cols});
        if (width <= 0 || height <= 0) {
            return 1.0;
        }

        if (direction == ImageStitcher::ScrollDirection::Right) {
            lastOverlap = lastGray(cv::Rect(lastGray.cols - width, 0, width, height));
            newOverlap = newGray(cv::Rect(0, 0, width, height));
        } else {
            lastOverlap = lastGray(cv::Rect(0, 0, width, height));
            newOverlap = newGray(cv::Rect(newGray.cols - width, 0, width, height));
        }
    } else {
        int width = std::min(lastGray.cols, newGray.cols);
        int height = std::min({overlap, lastGray.rows, newGray.rows});
        if (width <= 0 || height <= 0) {
            return 1.0;
        }

        if (direction == ImageStitcher::ScrollDirection::Down) {
            lastOverlap = lastGray(cv::Rect(0, lastGray.rows - height, width, height));
            newOverlap = newGray(cv::Rect(0, 0, width, height));
        } else {
            lastOverlap = lastGray(cv::Rect(0, 0, width, height));
            newOverlap = newGray(cv::Rect(0, newGray.rows - height, width, height));
        }
    }

    cv::Mat diff;
    cv::absdiff(lastOverlap, newOverlap, diff);
    cv::Scalar meanDiff = cv::mean(diff);
    return meanDiff[0] / 255.0;
}

// Try to find best overlap with ±1 pixel search around the detected overlap
// Returns the best overlap value (may be original, +1, or -1)
int refinedOverlapWithFallback(const cv::Mat &lastGray, const cv::Mat &newGray,
                                ImageStitcher::ScrollDirection direction,
                                bool isHorizontal, int detectedOverlap,
                                int minOverlap, int maxOverlap)
{
    double bestDiff = verifySeamAtOverlap(lastGray, newGray, direction, isHorizontal, detectedOverlap);
    int bestOverlap = detectedOverlap;

    // Try -1 pixel
    if (detectedOverlap - 1 >= minOverlap) {
        double diffMinus1 = verifySeamAtOverlap(lastGray, newGray, direction, isHorizontal, detectedOverlap - 1);
        if (diffMinus1 < bestDiff) {
            bestDiff = diffMinus1;
            bestOverlap = detectedOverlap - 1;
        }
    }

    // Try +1 pixel
    if (detectedOverlap + 1 <= maxOverlap) {
        double diffPlus1 = verifySeamAtOverlap(lastGray, newGray, direction, isHorizontal, detectedOverlap + 1);
        if (diffPlus1 < bestDiff) {
            bestDiff = diffPlus1;
            bestOverlap = detectedOverlap + 1;
        }
    }

    return bestOverlap;
}

constexpr double kMinCorrelation = 0.25;
constexpr double kPeakGapScale = 0.15;
constexpr double kInPlaceMaxAvgDiff = 0.05;
constexpr double kInPlaceMaxChangedRatio = 0.03;
constexpr double kDefaultMaxOverlapRatio = 0.90;

int maxOverlapForFrame(int frameDimension, int minOverlap, double maxOverlapRatio)
{
    if (frameDimension <= minOverlap) {
        return 0;
    }

    int maxByRatio = static_cast<int>(std::floor(frameDimension * maxOverlapRatio));
    maxByRatio = qBound(minOverlap, maxByRatio, frameDimension - 1);
    return maxByRatio;
}

double maxOverlapRatioForFrame(int frameDimension)
{
    if (frameDimension <= 140) {
        return 0.99;
    }
    if (frameDimension <= 200) {
        return 0.97;
    }
    if (frameDimension <= 240) {
        return 0.95;
    }
    if (frameDimension <= 360) {
        return 0.93;
    }
    return kDefaultMaxOverlapRatio;
}

double normalizedCrossCorrelation(const std::vector<double> &a,
                                  size_t startA,
                                  const std::vector<double> &b,
                                  size_t startB,
                                  size_t length)
{
    if (length == 0 || startA + length > a.size() || startB + length > b.size()) {
        return -1.0;
    }

    double sumA = 0.0;
    double sumB = 0.0;
    double sumAA = 0.0;
    double sumBB = 0.0;
    double sumAB = 0.0;

    for (size_t i = 0; i < length; ++i) {
        double va = a[startA + i];
        double vb = b[startB + i];
        sumA += va;
        sumB += vb;
        sumAA += va * va;
        sumBB += vb * vb;
        sumAB += va * vb;
    }

    double meanA = sumA / static_cast<double>(length);
    double meanB = sumB / static_cast<double>(length);
    double varA = sumAA - static_cast<double>(length) * meanA * meanA;
    double varB = sumBB - static_cast<double>(length) * meanB * meanB;

    if (varA <= 1e-8 || varB <= 1e-8) {
        return -1.0;
    }

    double numerator = sumAB - static_cast<double>(length) * meanA * meanB;
    double denominator = std::sqrt(varA * varB);
    if (denominator <= 1e-8) {
        return -1.0;
    }

    return numerator / denominator;
}

struct FrameDiffMetrics {
    double averageDiff = 1.0;
    double changedRatio = 1.0;
};

FrameDiffMetrics computeFrameDiff(const QImage &a, const QImage &b)
{
    FrameDiffMetrics metrics;
    if (a.isNull() || b.isNull()) {
        return metrics;
    }

    static constexpr int kSampleSize = 64;
    static constexpr int kPixelDiffThreshold = 20;

    QImage smallA = a.scaled(kSampleSize, kSampleSize, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    QImage smallB = b.scaled(kSampleSize, kSampleSize, Qt::IgnoreAspectRatio,
                             Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);

    if (smallA.size() != smallB.size()) {
        return metrics;
    }

    const int pixelCount = kSampleSize * kSampleSize;
    long long diffSum = 0;
    int changedPixels = 0;

    for (int y = 0; y < kSampleSize; ++y) {
        const QRgb *lineA = reinterpret_cast<const QRgb*>(smallA.constScanLine(y));
        const QRgb *lineB = reinterpret_cast<const QRgb*>(smallB.constScanLine(y));
        for (int x = 0; x < kSampleSize; ++x) {
            int diff = std::abs(qRed(lineA[x]) - qRed(lineB[x])) +
                       std::abs(qGreen(lineA[x]) - qGreen(lineB[x])) +
                       std::abs(qBlue(lineA[x]) - qBlue(lineB[x]));
            diffSum += diff;
            if (diff > kPixelDiffThreshold) {
                changedPixels++;
            }
        }
    }

    metrics.averageDiff = static_cast<double>(diffSum) / (pixelCount * 3.0 * 255.0);
    metrics.changedRatio = static_cast<double>(changedPixels) / pixelCount;
    return metrics;
}

struct InPlaceMatch {
    bool found = false;
    QRect rect;
    FrameDiffMetrics diff;
};

int searchRadiusForFrame(int frameDimension)
{
    int radius = frameDimension / 6;
    return qBound(16, radius, 160);
}

std::vector<double> gradientProfile(const cv::Mat &gray, bool isHorizontal)
{
    if (gray.empty()) {
        return {};
    }

    int dx = isHorizontal ? 1 : 0;
    int dy = isHorizontal ? 0 : 1;
    cv::Mat grad;
    cv::Sobel(gray, grad, CV_32F, dx, dy, 3);

    cv::Mat absGrad;
    cv::absdiff(grad, cv::Scalar::all(0), absGrad);

    cv::Mat profile;
    if (isHorizontal) {
        cv::reduce(absGrad, profile, 0, cv::REDUCE_SUM, CV_64F);
    } else {
        cv::reduce(absGrad, profile, 1, cv::REDUCE_SUM, CV_64F);
    }

    std::vector<double> vec;
    if (isHorizontal) {
        vec.assign(profile.ptr<double>(0),
                   profile.ptr<double>(0) + profile.cols);
    } else {
        vec.reserve(static_cast<size_t>(profile.rows));
        for (int i = 0; i < profile.rows; ++i) {
            vec.push_back(profile.at<double>(i, 0));
        }
    }
    return vec;
}

InPlaceMatch findInPlaceMatch(const QImage &stitched,
                              const QRect &lastViewport,
                              const QImage &frame,
                              bool isHorizontal,
                              int overlapPixels,
                              ImageStitcher::ScrollDirection direction,
                              int validWidth,
                              int validHeight)
{
    InPlaceMatch best;
    if (stitched.isNull() || frame.isNull() || lastViewport.isNull()) {
        return best;
    }

    int frameWidth = frame.width();
    int frameHeight = frame.height();

    QRect bounds(0, 0, validWidth, validHeight);
    if (!bounds.isValid()) {
        return best;
    }

    int maxPos = isHorizontal ? (bounds.width() - frameWidth) : (bounds.height() - frameHeight);
    if (maxPos < 0) {
        return best;
    }

    bool isForward = (direction == ImageStitcher::ScrollDirection::Down ||
                      direction == ImageStitcher::ScrollDirection::Right);
    int lastPos = isHorizontal ? lastViewport.x() : lastViewport.y();
    int delta = (isHorizontal ? frameWidth : frameHeight) - overlapPixels;
    if (delta < 1) {
        delta = 1;
    }

    int predictedPos = lastPos + (isForward ? delta : -delta);
    int minPos = 0;
    int searchRadius = searchRadiusForFrame(isHorizontal ? frameWidth : frameHeight);
    int step = (searchRadius > 80) ? 2 : 1;

    auto tryPosition = [&](int pos) {
        QRect rect = isHorizontal
            ? QRect(pos, 0, frameWidth, frameHeight)
            : QRect(0, pos, frameWidth, frameHeight);
        if (!bounds.contains(rect)) {
            return;
        }
        QImage existing = stitched.copy(rect);
        if (existing.isNull()) {
            return;
        }
        FrameDiffMetrics diff = computeFrameDiff(existing, frame);
        double score = diff.averageDiff + diff.changedRatio * 0.5;
        double bestScore = best.diff.averageDiff + best.diff.changedRatio * 0.5;
        if (!best.found || score < bestScore) {
            best.found = true;
            best.rect = rect;
            best.diff = diff;
        }
    };

    auto searchAround = [&](int center) {
        center = qBound(minPos, center, maxPos);
        int start = qMax(minPos, center - searchRadius);
        int end = qMin(maxPos, center + searchRadius);
        for (int pos = start; pos <= end; pos += step) {
            tryPosition(pos);
        }
    };

    searchAround(predictedPos);
    if (std::abs(lastPos - predictedPos) > searchRadius / 2) {
        searchAround(lastPos);
    }

    if (!best.found) {
        return best;
    }

    if (best.diff.averageDiff <= kInPlaceMaxAvgDiff &&
        best.diff.changedRatio <= kInPlaceMaxChangedRatio) {
        return best;
    }

    best.found = false;
    return best;
}

InPlaceMatch findGlobalInPlaceMatch(const QImage &stitched,
                                    int validWidth,
                                    int validHeight,
                                    const QImage &frame,
                                    bool isHorizontal)
{
    InPlaceMatch match;
    if (stitched.isNull() || frame.isNull()) {
        return match;
    }

    if (validWidth <= 0 || validHeight <= 0) {
        return match;
    }

    int frameWidth = frame.width();
    int frameHeight = frame.height();
    if (frameWidth > validWidth || frameHeight > validHeight) {
        return match;
    }

    QImage stitchedCropImage = stitched.copy(0, 0, validWidth, validHeight);
    cv::Mat stitchedMat = qImageToCvMatLocal(stitchedCropImage);
    cv::Mat frameMat = qImageToCvMatLocal(frame);
    if (stitchedMat.empty() || frameMat.empty()) {
        return match;
    }

    cv::Mat stitchedGray, frameGray;
    cv::cvtColor(stitchedMat, stitchedGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(frameMat, frameGray, cv::COLOR_BGR2GRAY);

    cv::Rect validRect(0, 0, validWidth, validHeight);
    if (validRect.width <= 0 || validRect.height <= 0 ||
        validRect.x + validRect.width > stitchedGray.cols ||
        validRect.y + validRect.height > stitchedGray.rows) {
        return match;
    }

    cv::Mat stitchedCrop = stitchedGray(validRect);
    std::vector<double> stitchedProfile = gradientProfile(stitchedCrop, isHorizontal);
    std::vector<double> frameProfile = gradientProfile(frameGray, isHorizontal);

    if (stitchedProfile.empty() || frameProfile.empty()) {
        return match;
    }

    int totalLength = static_cast<int>(stitchedProfile.size());
    int window = static_cast<int>(frameProfile.size());
    if (window <= 0 || totalLength < window) {
        return match;
    }

    int maxPos = totalLength - window;
    int step = (totalLength > 5000) ? 2 : 1;
    double bestCorr = -1.0;
    double secondCorr = -1.0;
    int bestPos = 0;

    for (int pos = 0; pos <= maxPos; pos += step) {
        double corr = normalizedCrossCorrelation(
            stitchedProfile, static_cast<size_t>(pos),
            frameProfile, 0, static_cast<size_t>(window));
        if (corr > bestCorr) {
            secondCorr = bestCorr;
            bestCorr = corr;
            bestPos = pos;
        } else if (corr > secondCorr) {
            secondCorr = corr;
        }
    }

    int refineStart = qMax(0, bestPos - step * 3);
    int refineEnd = qMin(maxPos, bestPos + step * 3);
    for (int pos = refineStart; pos <= refineEnd; ++pos) {
        double corr = normalizedCrossCorrelation(
            stitchedProfile, static_cast<size_t>(pos),
            frameProfile, 0, static_cast<size_t>(window));
        if (corr > bestCorr) {
            secondCorr = bestCorr;
            bestCorr = corr;
            bestPos = pos;
        } else if (corr > secondCorr) {
            secondCorr = corr;
        }
    }

    if (bestCorr < 0.18 || (secondCorr > -0.5 && (bestCorr - secondCorr) < 0.05)) {
        return match;
    }

    QRect rect = isHorizontal
        ? QRect(bestPos, 0, frameWidth, frameHeight)
        : QRect(0, bestPos, frameWidth, frameHeight);
    if (!QRect(0, 0, validWidth, validHeight).contains(rect)) {
        return match;
    }

    QImage existing = stitched.copy(rect);
    if (existing.isNull()) {
        return match;
    }

    FrameDiffMetrics diff = computeFrameDiff(existing, frame);
    if (diff.averageDiff <= kInPlaceMaxAvgDiff &&
        diff.changedRatio <= kInPlaceMaxChangedRatio) {
        match.found = true;
        match.rect = rect;
        match.diff = diff;
    }

    return match;
}

cv::Mat downsampleForDiff(const cv::Mat &src, int maxDim)
{
    if (src.empty() || maxDim <= 0) {
        return cv::Mat();
    }

    int longest = std::max(src.cols, src.rows);
    if (longest <= maxDim) {
        return src;
    }

    double scale = static_cast<double>(maxDim) / static_cast<double>(longest);
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(), scale, scale, cv::INTER_AREA);
    return resized;
}

double normalizedOverlapDifference(const cv::Mat &lastGray,
                                   const cv::Mat &newGray,
                                   ImageStitcher::ScrollDirection direction,
                                   bool isHorizontal,
                                   int overlap)
{
    if (overlap <= 0 || lastGray.empty() || newGray.empty()) {
        return 1.0;
    }

    cv::Rect lastRect;
    cv::Rect newRect;

    if (isHorizontal) {
        int height = std::min(lastGray.rows, newGray.rows);
        int width = std::min({overlap, lastGray.cols, newGray.cols});
        if (width <= 0 || height <= 0) {
            return 1.0;
        }

        if (direction == ImageStitcher::ScrollDirection::Right) {
            lastRect = cv::Rect(lastGray.cols - width, 0, width, height);
            newRect = cv::Rect(0, 0, width, height);
        } else {
            lastRect = cv::Rect(0, 0, width, height);
            newRect = cv::Rect(newGray.cols - width, 0, width, height);
        }
    } else {
        int width = std::min(lastGray.cols, newGray.cols);
        int height = std::min({overlap, lastGray.rows, newGray.rows});
        if (width <= 0 || height <= 0) {
            return 1.0;
        }

        if (direction == ImageStitcher::ScrollDirection::Down) {
            lastRect = cv::Rect(0, lastGray.rows - height, width, height);
            newRect = cv::Rect(0, 0, width, height);
        } else {
            lastRect = cv::Rect(0, 0, width, height);
            newRect = cv::Rect(0, newGray.rows - height, width, height);
        }
    }

    cv::Mat lastPatch = lastGray(lastRect);
    cv::Mat newPatch = newGray(newRect);

    cv::Mat lastSample = downsampleForDiff(lastPatch, kOverlapDiffMaxDim);
    cv::Mat newSample = downsampleForDiff(newPatch, kOverlapDiffMaxDim);

    if (lastSample.empty() || newSample.empty() || lastSample.size() != newSample.size()) {
        return 1.0;
    }

    // Direct comparison without blur to preserve edge alignment precision
    cv::Mat diff;
    cv::absdiff(lastSample, newSample, diff);
    cv::Scalar meanDiff = cv::mean(diff);
    return meanDiff[0] / 255.0;
}
}

// RSSA: Static region detection (header/footer/scrollbar)
// Detects UI elements that remain static between frames to exclude them from matching
StaticRegions ImageStitcher::detectStaticRegions(const QImage &img1, const QImage &img2)
{
    StaticRegions result;

    if (img1.isNull() || img2.isNull() || img1.size() != img2.size()) {
        return result;
    }

    cv::Mat mat1 = qImageToCvMat(img1);
    cv::Mat mat2 = qImageToCvMat(img2);

    // Convert to grayscale for comparison
    cv::Mat gray1, gray2;
    cv::cvtColor(mat1, gray1, cv::COLOR_BGR2GRAY);
    cv::cvtColor(mat2, gray2, cv::COLOR_BGR2GRAY);

    const int height = gray1.rows;
    const int width = gray1.cols;

    // === TOP SCAN: Detect Sticky Header ===
    // Scan from y=0 downward, find contiguous rows with near-zero difference
    for (int y = 0; y < height / 3; ++y) {  // Limit to top 1/3
        cv::Mat row1 = gray1.row(y);
        cv::Mat row2 = gray2.row(y);
        double diff = calculateRowDifference(row1, row2);

        if (diff < m_stitchConfig.staticRowThreshold) {
            result.headerHeight = y + 1;
        } else {
            break;  // First non-static row ends the header
        }
    }

    // === BOTTOM SCAN: Detect Sticky Footer ===
    for (int y = height - 1; y > height * 2 / 3; --y) {  // Limit to bottom 1/3
        cv::Mat row1 = gray1.row(y);
        cv::Mat row2 = gray2.row(y);
        double diff = calculateRowDifference(row1, row2);

        if (diff < m_stitchConfig.staticRowThreshold) {
            result.footerHeight = height - y;
        } else {
            break;
        }
    }

    // === RIGHT SCAN: Detect Scrollbar ===
    // Scrollbar track is typically static, thumb moves
    // Check rightmost 30 pixels
    const int scrollbarCheckWidth = 30;
    int staticColumns = 0;
    for (int x = width - 1; x > width - scrollbarCheckWidth && x >= 0; --x) {
        cv::Mat col1 = gray1.col(x);
        cv::Mat col2 = gray2.col(x);
        double diff = cv::norm(col1, col2, cv::NORM_L1) / height;

        if (diff < m_stitchConfig.staticRowThreshold * 2) {
            staticColumns++;
        }
    }
    if (staticColumns > scrollbarCheckWidth * 0.7) {
        result.scrollbarWidth = scrollbarCheckWidth;
    }

    result.detected = (result.headerHeight > 0 || result.footerHeight > 0);

    if (result.detected) {
        qDebug() << "ImageStitcher: Detected static regions - Header:" << result.headerHeight
                 << "Footer:" << result.footerHeight
                 << "Scrollbar:" << result.scrollbarWidth;
    }

    return result;
}

double ImageStitcher::calculateRowDifference(const cv::Mat &row1, const cv::Mat &row2) const
{
    // Calculate mean absolute difference per pixel
    cv::Mat diff;
    cv::absdiff(row1, row2, diff);
    return cv::mean(diff)[0];
}

ImageStitcher::StitchResult ImageStitcher::tryTemplateMatch(const QImage &newFrame)
{
    // Choose directions based on capture mode
    ScrollDirection primaryDir, secondaryDir;
    if (m_captureMode == CaptureMode::Horizontal) {
        primaryDir = ScrollDirection::Right;
        secondaryDir = ScrollDirection::Left;
    } else {
        primaryDir = ScrollDirection::Down;
        secondaryDir = ScrollDirection::Up;
    }

    auto primaryCandidate = computeTemplateMatchCandidate(newFrame, primaryDir);
    auto secondaryCandidate = computeTemplateMatchCandidate(newFrame, secondaryDir);

    if (primaryCandidate.success && secondaryCandidate.success) {
        // Direction hysteresis to reduce jitter when both directions match
        // Higher value (0.15) makes direction switches harder, preventing accidental reversals
        constexpr double DIRECTION_HYSTERESIS = 0.15;

        bool preferPrimary = true;
        if (secondaryCandidate.direction == m_lastSuccessfulDirection) {
            preferPrimary = primaryCandidate.confidence >
                            secondaryCandidate.confidence + DIRECTION_HYSTERESIS;
        } else if (primaryCandidate.direction == m_lastSuccessfulDirection) {
            preferPrimary = primaryCandidate.confidence + DIRECTION_HYSTERESIS >=
                            secondaryCandidate.confidence;
        } else {
            preferPrimary = primaryCandidate.confidence >= secondaryCandidate.confidence;
        }

        const auto &best = preferPrimary ? primaryCandidate : secondaryCandidate;
        m_lastSuccessfulDirection = best.direction;
        return applyCandidate(newFrame, best, Algorithm::TemplateMatching);
    }

    if (primaryCandidate.success) {
        m_lastSuccessfulDirection = primaryCandidate.direction;
        return applyCandidate(newFrame, primaryCandidate, Algorithm::TemplateMatching);
    }

    if (secondaryCandidate.success) {
        m_lastSuccessfulDirection = secondaryCandidate.direction;
        return applyCandidate(newFrame, secondaryCandidate, Algorithm::TemplateMatching);
    }

    StitchResult result;
    result.usedAlgorithm = Algorithm::TemplateMatching;
    result.confidence = std::max(primaryCandidate.confidence, secondaryCandidate.confidence);
    QString dirNames = (m_captureMode == CaptureMode::Horizontal) ? "Right: %1; Left: %2" : "Down: %1; Up: %2";
    result.failureReason = dirNames
        .arg(primaryCandidate.failureReason.isEmpty() ? "No match" : primaryCandidate.failureReason,
             secondaryCandidate.failureReason.isEmpty() ? "No match" : secondaryCandidate.failureReason);
    return result;
}

ImageStitcher::MatchCandidate ImageStitcher::computeTemplateMatchCandidate(const QImage &newFrame,
                                                                            ScrollDirection direction)
{
    MatchCandidate candidate;
    candidate.direction = direction;

    cv::Mat lastMat = qImageToCvMat(m_lastFrame);
    cv::Mat newMat = qImageToCvMat(newFrame);

    if (lastMat.empty() || newMat.empty()) {
        candidate.failureReason = "Failed to convert images";
        return candidate;
    }

    // Convert to grayscale and enhance contrast to strengthen edges
    cv::Mat lastGray, newGray;
    cv::cvtColor(lastMat, lastGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(newMat, newGray, cv::COLOR_BGR2GRAY);

    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
    clahe->apply(lastGray, lastGray);
    clahe->apply(newGray, newGray);

    bool isHorizontal = (direction == ScrollDirection::Left || direction == ScrollDirection::Right);
    int frameDimension = isHorizontal ? lastGray.cols : lastGray.rows;
    double overlapRatio = maxOverlapRatioForFrame(frameDimension);
    int maxOverlap = maxOverlapForFrame(frameDimension, MIN_OVERLAP, overlapRatio);
    if (maxOverlap == 0) {
        candidate.failureReason = "Frame too small for overlap search";
        return candidate;
    }

    struct TemplateResult {
        double confidence = 0.0;
        double offset = 0.0;
        double varianceWeight = 0.0;
    };

    std::vector<TemplateResult> results;

    if (isHorizontal) {
        // Horizontal mode: use template widths instead of heights
        const int baseTemplateWidth = std::min(180, lastGray.cols / 2);
        std::vector<int> templateWidths = {
            baseTemplateWidth,
            std::max(24, baseTemplateWidth / 2),
            std::max(24, static_cast<int>(baseTemplateWidth * 0.75))
        };

        for (int templateWidth : templateWidths) {
            if (templateWidth < 10 || templateWidth > lastGray.cols) {
                continue;
            }

            int searchMaxExtent = std::min(maxOverlap + templateWidth, newGray.cols);
            if (searchMaxExtent <= templateWidth) {
                continue;
            }

            cv::Mat templateImg;
            cv::Mat searchRegion;
            int templateLeft = 0;
            int searchLeft = 0;

            if (direction == ScrollDirection::Right) {
                templateLeft = lastGray.cols - templateWidth;
                templateImg = lastGray(cv::Rect(templateLeft, 0, templateWidth, lastGray.rows));
                searchRegion = newGray(cv::Rect(0, 0, searchMaxExtent, newGray.rows));
                searchLeft = 0;
            } else {
                templateLeft = 0;
                templateImg = lastGray(cv::Rect(0, 0, templateWidth, lastGray.rows));
                searchLeft = newGray.cols - searchMaxExtent;
                searchRegion = newGray(cv::Rect(searchLeft, 0, searchMaxExtent, newGray.rows));
            }

            double varianceScore = normalizedVariance(templateImg);
            if (varianceScore < 0.5) {
                continue;
            }

            if (templateImg.cols > searchRegion.cols || templateImg.rows > searchRegion.rows) {
                continue;
            }

            cv::Mat matchResult;
            cv::matchTemplate(searchRegion, templateImg, matchResult, cv::TM_CCOEFF_NORMED);

            double minVal, maxVal;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(matchResult, &minVal, &maxVal, &minLoc, &maxLoc);

            // Apply sub-pixel refinement for more accurate positioning
            cv::Point2d refinedLoc = subPixelRefine2D(matchResult, maxLoc);

            double fullOffset = templateLeft - (searchLeft + refinedLoc.x);

            if (direction == ScrollDirection::Right && fullOffset <= 0.0) {
                continue;
            }
            if (direction == ScrollDirection::Left && fullOffset >= 0.0) {
                continue;
            }

            results.push_back({maxVal, fullOffset, varianceScore});
        }
    } else {
        // Vertical mode: original height-based logic
        const int baseTemplateHeight = std::min(180, lastGray.rows / 2);
        std::vector<int> templateHeights = {
            baseTemplateHeight,
            std::max(24, baseTemplateHeight / 2),
            std::max(24, static_cast<int>(baseTemplateHeight * 0.75))
        };

        for (int templateHeight : templateHeights) {
            if (templateHeight < 10 || templateHeight > lastGray.rows) {
                continue;
            }

            int searchMaxExtent = std::min(maxOverlap + templateHeight, newGray.rows);
            if (searchMaxExtent <= templateHeight) {
                continue;
            }

            cv::Mat templateImg;
            cv::Mat searchRegion;
            int templateTop = 0;
            int searchTop = 0;

            if (direction == ScrollDirection::Down) {
                templateTop = lastGray.rows - templateHeight;
                templateImg = lastGray(cv::Rect(0, templateTop, lastGray.cols, templateHeight));
                searchRegion = newGray(cv::Rect(0, 0, newGray.cols, searchMaxExtent));
                searchTop = 0;
            } else {
                templateTop = 0;
                templateImg = lastGray(cv::Rect(0, 0, lastGray.cols, templateHeight));
                searchTop = newGray.rows - searchMaxExtent;
                searchRegion = newGray(cv::Rect(0, searchTop, newGray.cols, searchMaxExtent));
            }

            double varianceScore = normalizedVariance(templateImg);
            if (varianceScore < 0.5) {
                continue;
            }

            if (templateImg.cols > searchRegion.cols || templateImg.rows > searchRegion.rows) {
                continue;
            }

            cv::Mat matchResult;
            cv::matchTemplate(searchRegion, templateImg, matchResult, cv::TM_CCOEFF_NORMED);

            double minVal, maxVal;
            cv::Point minLoc, maxLoc;
            cv::minMaxLoc(matchResult, &minVal, &maxVal, &minLoc, &maxLoc);

            // Apply sub-pixel refinement for more accurate positioning
            cv::Point2d refinedLoc = subPixelRefine2D(matchResult, maxLoc);

            double fullOffset = templateTop - (searchTop + refinedLoc.y);

            if (direction == ScrollDirection::Down && fullOffset <= 0.0) {
                continue;
            }
            if (direction == ScrollDirection::Up && fullOffset >= 0.0) {
                continue;
            }

            results.push_back({maxVal, fullOffset, varianceScore});
        }
    }

    if (results.empty()) {
        candidate.failureReason = "No reliable template region found";
        return candidate;
    }

    std::vector<double> offsets;
    offsets.reserve(results.size());
    std::vector<double> confidences;
    confidences.reserve(results.size());

    for (const auto &res : results) {
        offsets.push_back(res.offset);
        // Weight correlation score by texture richness
        confidences.push_back(std::clamp(res.confidence * (0.5 + res.varianceWeight / 3.0), 0.0, 1.0));
    }

    std::vector<double> sortedOffsets = offsets;
    std::sort(sortedOffsets.begin(), sortedOffsets.end());
    double medianOffset = sortedOffsets[sortedOffsets.size() / 2];
    double minOffset = sortedOffsets.front();
    double maxOffset = sortedOffsets.back();

    // Penalize spread between candidate offsets to discourage unstable matches
    double spreadPenalty = std::clamp((maxOffset - minOffset) / 80.0, 0.0, 0.45);

    double averageConfidence = std::accumulate(confidences.begin(), confidences.end(), 0.0) /
                               static_cast<double>(confidences.size());
    candidate.confidence = averageConfidence * (1.0 - spreadPenalty);

    if (candidate.confidence < m_stitchConfig.confidenceThreshold) {
        candidate.failureReason = QString("Template match confidence too low: %1").arg(candidate.confidence);
        return candidate;
    }

    // Calculate overlap based on direction
    int overlap = frameDimension - static_cast<int>(std::round(std::abs(medianOffset)));

    if (overlap <= 0) {
        candidate.failureReason = QString("Negative overlap: %1 (offset too large)").arg(overlap);
        return candidate;
    }

    if (overlap < MIN_OVERLAP) {
        candidate.failureReason = QString("Overlap too small: %1").arg(overlap);
        return candidate;
    }

    // Sanity check: overlap should not exceed a reasonable percentage of frame
    int maxReasonableOverlap = static_cast<int>(frameDimension * overlapRatio);
    if (overlap > maxReasonableOverlap) {
        candidate.confidence = 0.25;
        candidate.failureReason = QString("Overlap too large: %1 (>%2%% of frame)")
            .arg(overlap).arg(static_cast<int>(overlapRatio * 100));
        return candidate;
    }

    // Apply ±1 pixel seam verification to find the best alignment
    int refinedOverlap = refinedOverlapWithFallback(
        lastGray, newGray, direction, isHorizontal,
        overlap, MIN_OVERLAP, maxReasonableOverlap);

    double overlapDiff = normalizedOverlapDifference(lastGray, newGray, direction, isHorizontal, refinedOverlap);
    if (overlapDiff > kMaxOverlapDiff) {
        candidate.confidence = 0.2;
        candidate.failureReason = QString("Overlap mismatch: %1").arg(overlapDiff, 0, 'f', 3);
        return candidate;
    }

    candidate.success = true;
    candidate.overlap = refinedOverlap;

    return candidate;
}

ImageStitcher::StitchResult ImageStitcher::applyCandidate(const QImage &newFrame,
                                                          const MatchCandidate &candidate,
                                                          Algorithm algorithm)
{
    StitchResult result = performStitch(newFrame, candidate.overlap, candidate.direction);
    result.usedAlgorithm = algorithm;
    result.confidence = candidate.confidence;
    result.overlapPixels = candidate.overlap;
    result.direction = candidate.direction;
    result.failureReason = candidate.failureReason;
    return result;
}

ImageStitcher::StitchResult ImageStitcher::performStitch(const QImage &newFrame,
                                                         int overlapPixels,
                                                         ScrollDirection direction)
{
    StitchResult result;
    result.direction = direction;

    QImage newFrameRgb = newFrame.convertToFormat(QImage::Format_RGB32);

    bool isHorizontal = (direction == ScrollDirection::Left || direction == ScrollDirection::Right);

    if (isHorizontal) {
        // Horizontal stitching
        int currentWidth = m_validWidth > 0 ? m_validWidth : m_stitchedResult.width();

        if (currentWidth <= 0 || m_stitchedResult.isNull()) {
            result.failureReason = "Invalid stitcher state";
            return result;
        }

        overlapPixels = qBound(MIN_OVERLAP, overlapPixels,
                               qMin(currentWidth, newFrameRgb.width()) - 1);

        if (!m_currentViewportRect.isNull()) {
            int predictedX = (direction == ScrollDirection::Right)
                ? m_currentViewportRect.x() + m_currentViewportRect.width() - overlapPixels
                : m_currentViewportRect.x() - (newFrameRgb.width() - overlapPixels);
            QRect predictedRect(predictedX, 0, newFrameRgb.width(), newFrameRgb.height());
            QRect bounds(0, 0, currentWidth, m_stitchedResult.height());
            InPlaceMatch inPlace = findInPlaceMatch(
                m_stitchedResult,
                m_currentViewportRect,
                newFrameRgb,
                true,
                overlapPixels,
                direction,
                currentWidth,
                m_stitchedResult.height());
            if (inPlace.found) {
                m_currentViewportRect = inPlace.rect;
                result.offset = inPlace.rect.topLeft();
                result.success = true;
                return result;
            }
            if (direction != m_lastSuccessfulDirection) {
                InPlaceMatch global = findGlobalInPlaceMatch(
                    m_stitchedResult,
                    currentWidth,
                    m_stitchedResult.height(),
                    newFrameRgb,
                    true);
                if (global.found) {
                    m_currentViewportRect = global.rect;
                    result.offset = global.rect.topLeft();
                    result.success = true;
                    return result;
                }
            }
            if (bounds.contains(predictedRect)) {
                result.failureReason = "Viewport mismatch";
                return result;
            }
        }

        if (direction == ScrollDirection::Right) {
            // Append new frame to the right
            int drawX = currentWidth - overlapPixels;
            int newWidth = drawX + newFrameRgb.width();

            if (newWidth > MAX_STITCHED_WIDTH) {
                result.failureReason = QString("Maximum width reached (%1 pixels)").arg(MAX_STITCHED_WIDTH);
                return result;
            }

            if (newWidth > m_stitchedResult.width()) {
                int newCapacity = std::max(newWidth, static_cast<int>(m_stitchedResult.width() * 1.5));
                newCapacity = std::max(newCapacity, newWidth + 4096);
                newCapacity = std::min(newCapacity, MAX_STITCHED_WIDTH + 4096);

                QImage newStitched(newCapacity, m_stitchedResult.height(), QImage::Format_RGB32);
                if (newStitched.isNull()) {
                    result.failureReason = "Failed to allocate memory for stitched image";
                    return result;
                }
                newStitched.fill(Qt::black);

                QPainter painter(&newStitched);
                if (!painter.isActive()) {
                    result.failureReason = "Failed to create painter for stitching";
                    return result;
                }
                painter.drawImage(0, 0, m_stitchedResult, 0, 0, currentWidth, m_stitchedResult.height());
                painter.drawImage(drawX, 0, newFrameRgb);
                if (!painter.end()) {
                    qDebug() << "ImageStitcher: QPainter::end() failed for horizontal right stitch (expand)";
                }

                m_stitchedResult = newStitched;
            } else {
                QPainter painter(&m_stitchedResult);
                if (!painter.isActive()) {
                    result.failureReason = "Failed to create painter for stitching";
                    return result;
                }
                painter.drawImage(drawX, 0, newFrameRgb);
                if (!painter.end()) {
                    qDebug() << "ImageStitcher: QPainter::end() failed for horizontal right stitch (in-place)";
                }
            }

            m_currentViewportRect = QRect(drawX, 0, newFrameRgb.width(), newFrameRgb.height());
            m_validWidth = newWidth;
            result.offset = QPoint(drawX, 0);
        } else {
            // Prepend new frame to the left
            int drawX = newFrameRgb.width() - overlapPixels;
            int newWidth = drawX + currentWidth;

            if (newWidth > MAX_STITCHED_WIDTH) {
                result.failureReason = QString("Maximum width reached (%1 pixels)").arg(MAX_STITCHED_WIDTH);
                return result;
            }

            QImage newStitched(newWidth, m_stitchedResult.height(), QImage::Format_RGB32);
            if (newStitched.isNull()) {
                result.failureReason = "Failed to allocate memory for stitched image";
                return result;
            }
            newStitched.fill(Qt::black);

            QPainter painter(&newStitched);
            if (!painter.isActive()) {
                result.failureReason = "Failed to create painter for stitching";
                return result;
            }
            painter.drawImage(0, 0, newFrameRgb);
            painter.drawImage(drawX, 0, m_stitchedResult, 0, 0, currentWidth, m_stitchedResult.height());
            if (!painter.end()) {
                qDebug() << "ImageStitcher: QPainter::end() failed for horizontal left stitch";
            }

            m_stitchedResult = newStitched;
            m_validWidth = newWidth;

            m_currentViewportRect = QRect(0, 0, newFrameRgb.width(), newFrameRgb.height());
            result.offset = QPoint(0, 0);
        }
    } else {
        // Vertical stitching
        int currentHeight = m_validHeight > 0 ? m_validHeight : m_stitchedResult.height();

        if (currentHeight <= 0 || m_stitchedResult.isNull()) {
            result.failureReason = "Invalid stitcher state";
            return result;
        }

        overlapPixels = qBound(MIN_OVERLAP, overlapPixels,
                               qMin(currentHeight, newFrameRgb.height()) - 1);

        if (!m_currentViewportRect.isNull()) {
            int predictedY = (direction == ScrollDirection::Down)
                ? m_currentViewportRect.y() + m_currentViewportRect.height() - overlapPixels
                : m_currentViewportRect.y() - (newFrameRgb.height() - overlapPixels);
            QRect predictedRect(0, predictedY, newFrameRgb.width(), newFrameRgb.height());
            QRect bounds(0, 0, m_stitchedResult.width(), currentHeight);
            InPlaceMatch inPlace = findInPlaceMatch(
                m_stitchedResult,
                m_currentViewportRect,
                newFrameRgb,
                false,
                overlapPixels,
                direction,
                m_stitchedResult.width(),
                currentHeight);
            if (inPlace.found) {
                m_currentViewportRect = inPlace.rect;
                result.offset = inPlace.rect.topLeft();
                result.success = true;
                return result;
            }
            if (direction != m_lastSuccessfulDirection) {
                InPlaceMatch global = findGlobalInPlaceMatch(
                    m_stitchedResult,
                    m_stitchedResult.width(),
                    currentHeight,
                    newFrameRgb,
                    false);
                if (global.found) {
                    m_currentViewportRect = global.rect;
                    result.offset = global.rect.topLeft();
                    result.success = true;
                    return result;
                }
            }
            if (bounds.contains(predictedRect)) {
                result.failureReason = "Viewport mismatch";
                return result;
            }
        }

        if (direction == ScrollDirection::Down) {
            // Append new frame below
            int drawY = currentHeight - overlapPixels;
            int newHeight = drawY + newFrameRgb.height();

            if (newHeight > MAX_STITCHED_HEIGHT) {
                result.failureReason = QString("Maximum height reached (%1 pixels)").arg(MAX_STITCHED_HEIGHT);
                return result;
            }

            if (newHeight > m_stitchedResult.height()) {
                int newCapacity = std::max(newHeight, static_cast<int>(m_stitchedResult.height() * 1.5));
                newCapacity = std::max(newCapacity, newHeight + 4096);
                newCapacity = std::min(newCapacity, MAX_STITCHED_HEIGHT + 4096);

                QImage newStitched(m_stitchedResult.width(), newCapacity, QImage::Format_RGB32);
                if (newStitched.isNull()) {
                    result.failureReason = "Failed to allocate memory for stitched image";
                    return result;
                }
                newStitched.fill(Qt::black);

                QPainter painter(&newStitched);
                if (!painter.isActive()) {
                    result.failureReason = "Failed to create painter for stitching";
                    return result;
                }
                painter.drawImage(0, 0, m_stitchedResult, 0, 0, m_stitchedResult.width(), currentHeight);
                painter.drawImage(0, drawY, newFrameRgb);
                if (!painter.end()) {
                    qDebug() << "ImageStitcher: QPainter::end() failed for vertical down stitch (expand)";
                }

                m_stitchedResult = newStitched;
            } else {
                QPainter painter(&m_stitchedResult);
                if (!painter.isActive()) {
                    result.failureReason = "Failed to create painter for stitching";
                    return result;
                }
                painter.drawImage(0, drawY, newFrameRgb);
                if (!painter.end()) {
                    qDebug() << "ImageStitcher: QPainter::end() failed for vertical down stitch (in-place)";
                }
            }

            m_currentViewportRect = QRect(0, drawY, newFrameRgb.width(), newFrameRgb.height());
            m_validHeight = newHeight;
            result.offset = QPoint(0, drawY);
        } else {
            // Prepend new frame above
            int drawY = newFrameRgb.height() - overlapPixels;
            int newHeight = drawY + currentHeight;

            if (newHeight > MAX_STITCHED_HEIGHT) {
                result.failureReason = QString("Maximum height reached (%1 pixels)").arg(MAX_STITCHED_HEIGHT);
                return result;
            }

            QImage newStitched(m_stitchedResult.width(), newHeight, QImage::Format_RGB32);
            if (newStitched.isNull()) {
                result.failureReason = "Failed to allocate memory for stitched image";
                return result;
            }
            newStitched.fill(Qt::black);

            QPainter painter(&newStitched);
            if (!painter.isActive()) {
                result.failureReason = "Failed to create painter for stitching";
                return result;
            }
            painter.drawImage(0, 0, newFrameRgb);
            painter.drawImage(0, drawY, m_stitchedResult, 0, 0, m_stitchedResult.width(), currentHeight);
            if (!painter.end()) {
                qDebug() << "ImageStitcher: QPainter::end() failed for vertical up stitch";
            }

            m_stitchedResult = newStitched;
            m_validHeight = newHeight;

            m_currentViewportRect = QRect(0, 0, newFrameRgb.width(), newFrameRgb.height());
            result.offset = QPoint(0, 0);
        }
    }

    result.success = true;
    return result;
}

cv::Mat ImageStitcher::qImageToCvMat(const QImage &image) const
{
    QImage rgb = image.convertToFormat(QImage::Format_RGB32);

    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));

    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGBA2BGR);

    return bgr.clone();
}

QImage ImageStitcher::cvMatToQImage(const cv::Mat &mat) const
{
    if (mat.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        return QImage(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step),
                      QImage::Format_RGB888).copy();
    }
    else if (mat.type() == CV_8UC1) {
        return QImage(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step),
                      QImage::Format_Grayscale8).copy();
    }

    return QImage();
}

// Row Projection Profile algorithm - optimal for text documents and vertical/horizontal scrolling
// This method calculates horizontal/vertical projection profiles and uses 1D cross-correlation
// to find the offset. It's extremely fast (1D signal processing) and robust for repetitive patterns.
ImageStitcher::StitchResult ImageStitcher::tryRowProjectionMatch(const QImage &newFrame)
{
    ScrollDirection primaryDir, secondaryDir;
    if (m_captureMode == CaptureMode::Horizontal) {
        primaryDir = ScrollDirection::Right;
        secondaryDir = ScrollDirection::Left;
    } else {
        primaryDir = ScrollDirection::Down;
        secondaryDir = ScrollDirection::Up;
    }

    auto primaryCandidate = computeRowProjectionCandidate(newFrame, primaryDir);
    auto secondaryCandidate = computeRowProjectionCandidate(newFrame, secondaryDir);

    if (primaryCandidate.success && secondaryCandidate.success) {
        // Direction hysteresis to reduce jitter when both directions match
        // Higher value (0.15) makes direction switches harder, preventing accidental reversals
        constexpr double DIRECTION_HYSTERESIS = 0.15;

        bool preferPrimary = true;
        if (secondaryCandidate.direction == m_lastSuccessfulDirection) {
            preferPrimary = primaryCandidate.confidence >
                            secondaryCandidate.confidence + DIRECTION_HYSTERESIS;
        } else if (primaryCandidate.direction == m_lastSuccessfulDirection) {
            preferPrimary = primaryCandidate.confidence + DIRECTION_HYSTERESIS >=
                            secondaryCandidate.confidence;
        } else {
            preferPrimary = primaryCandidate.confidence >= secondaryCandidate.confidence;
        }

        const auto &best = preferPrimary ? primaryCandidate : secondaryCandidate;
        m_lastSuccessfulDirection = best.direction;
        return applyCandidate(newFrame, best, Algorithm::RowProjection);
    }

    if (primaryCandidate.success) {
        m_lastSuccessfulDirection = primaryCandidate.direction;
        return applyCandidate(newFrame, primaryCandidate, Algorithm::RowProjection);
    }

    if (secondaryCandidate.success) {
        m_lastSuccessfulDirection = secondaryCandidate.direction;
        return applyCandidate(newFrame, secondaryCandidate, Algorithm::RowProjection);
    }

    StitchResult result;
    result.usedAlgorithm = Algorithm::RowProjection;
    result.confidence = std::max(primaryCandidate.confidence, secondaryCandidate.confidence);
    QString dirNames = (m_captureMode == CaptureMode::Horizontal)
        ? "Right: %1; Left: %2"
        : "Down: %1; Up: %2";
    result.failureReason = dirNames
        .arg(primaryCandidate.failureReason.isEmpty() ? "No match" : primaryCandidate.failureReason,
             secondaryCandidate.failureReason.isEmpty() ? "No match" : secondaryCandidate.failureReason);
    return result;
}

ImageStitcher::StitchResult ImageStitcher::tryInPlaceMatchInStitched(const QImage &newFrame)
{
    StitchResult result;
    result.usedAlgorithm = Algorithm::RowProjection;

    if (newFrame.isNull() || m_stitchedResult.isNull() || m_currentViewportRect.isNull()) {
        result.failureReason = "No stitched viewport available";
        return result;
    }

    bool isHorizontal = (m_captureMode == CaptureMode::Horizontal);
    int validWidth = m_validWidth > 0 ? m_validWidth : m_stitchedResult.width();
    int validHeight = m_validHeight > 0 ? m_validHeight : m_stitchedResult.height();

    if (validWidth <= 0 || validHeight <= 0) {
        result.failureReason = "Invalid stitcher state";
        return result;
    }

    InPlaceMatch match = findGlobalInPlaceMatch(
        m_stitchedResult,
        validWidth,
        validHeight,
        newFrame,
        isHorizontal);

    if (!match.found) {
        result.failureReason = "No in-place match found";
        return result;
    }

    QRect previousViewport = m_currentViewportRect;
    m_currentViewportRect = match.rect;

    if (isHorizontal) {
        result.direction = (match.rect.x() >= previousViewport.x())
            ? ScrollDirection::Right
            : ScrollDirection::Left;
    } else {
        result.direction = (match.rect.y() >= previousViewport.y())
            ? ScrollDirection::Down
            : ScrollDirection::Up;
    }

    m_lastSuccessfulDirection = result.direction;
    result.offset = match.rect.topLeft();
    result.confidence = std::clamp(1.0 - (match.diff.averageDiff + match.diff.changedRatio * 0.5), 0.0, 1.0);
    result.success = true;
    return result;
}

ImageStitcher::MatchCandidate ImageStitcher::computeRowProjectionCandidate(
    const QImage &newFrame, ScrollDirection direction)
{
    MatchCandidate candidate;
    candidate.direction = direction;

    cv::Mat lastMat = qImageToCvMat(m_lastFrame);
    cv::Mat newMat = qImageToCvMat(newFrame);

    if (lastMat.empty() || newMat.empty()) {
        candidate.failureReason = "Failed to convert images";
        return candidate;
    }

    // Convert to grayscale
    cv::Mat lastGray, newGray;
    cv::cvtColor(lastMat, lastGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(newMat, newGray, cv::COLOR_BGR2GRAY);

    bool isHorizontal = (direction == ScrollDirection::Left || direction == ScrollDirection::Right);

    int dx = isHorizontal ? 1 : 0;
    int dy = isHorizontal ? 0 : 1;
    cv::Mat lastGrad, newGrad;
    cv::Sobel(lastGray, lastGrad, CV_32F, dx, dy, 3);
    cv::Sobel(newGray, newGrad, CV_32F, dx, dy, 3);

    cv::Mat lastAbs, newAbs;
    cv::absdiff(lastGrad, cv::Scalar::all(0), lastAbs);
    cv::absdiff(newGrad, cv::Scalar::all(0), newAbs);

    cv::Mat lastProfile, newProfile;
    if (isHorizontal) {
        cv::reduce(lastAbs, lastProfile, 0, cv::REDUCE_SUM, CV_64F);
        cv::reduce(newAbs, newProfile, 0, cv::REDUCE_SUM, CV_64F);
    } else {
        cv::reduce(lastAbs, lastProfile, 1, cv::REDUCE_SUM, CV_64F);
        cv::reduce(newAbs, newProfile, 1, cv::REDUCE_SUM, CV_64F);
    }

    std::vector<double> lastVec;
    std::vector<double> newVec;
    if (isHorizontal) {
        lastVec.assign(lastProfile.ptr<double>(0),
                       lastProfile.ptr<double>(0) + lastProfile.cols);
        newVec.assign(newProfile.ptr<double>(0),
                      newProfile.ptr<double>(0) + newProfile.cols);
    } else {
        lastVec.reserve(static_cast<size_t>(lastProfile.rows));
        newVec.reserve(static_cast<size_t>(newProfile.rows));
        for (int i = 0; i < lastProfile.rows; ++i) {
            lastVec.push_back(lastProfile.at<double>(i, 0));
        }
        for (int i = 0; i < newProfile.rows; ++i) {
            newVec.push_back(newProfile.at<double>(i, 0));
        }
    }

    if (lastVec.size() != newVec.size() ||
        lastVec.size() <= static_cast<size_t>(MIN_OVERLAP)) {
        candidate.failureReason = "Profile too short for overlap search";
        return candidate;
    }

    int frameDimension = static_cast<int>(lastVec.size());
    double overlapRatio = maxOverlapRatioForFrame(frameDimension);
    int maxOverlap = maxOverlapForFrame(frameDimension, MIN_OVERLAP, overlapRatio);
    if (maxOverlap == 0) {
        candidate.failureReason = "Frame too small for overlap search";
        return candidate;
    }

    bool isForward = (direction == ScrollDirection::Down || direction == ScrollDirection::Right);

    double bestCorr = -1.0;
    double secondBestCorr = -1.0;
    int bestOverlap = 0;

    for (int overlap = MIN_OVERLAP; overlap <= maxOverlap; ++overlap) {
        size_t lastStart = isForward
            ? static_cast<size_t>(frameDimension - overlap)
            : 0;
        size_t newStart = isForward
            ? 0
            : static_cast<size_t>(frameDimension - overlap);
        double corr = normalizedCrossCorrelation(
            lastVec, lastStart, newVec, newStart, static_cast<size_t>(overlap));
        if (corr < -0.5) {
            continue;
        }
        if (corr > bestCorr) {
            secondBestCorr = bestCorr;
            bestCorr = corr;
            bestOverlap = overlap;
        } else if (corr > secondBestCorr) {
            secondBestCorr = corr;
        }
    }

    if (bestOverlap == 0 || bestCorr < kMinCorrelation) {
        candidate.confidence = std::clamp(bestCorr, 0.0, 1.0);
        candidate.failureReason = QString("Low correlation: %1").arg(bestCorr);
        return candidate;
    }

    // Apply ±1 pixel seam verification to find the best alignment
    int refinedOverlap = refinedOverlapWithFallback(
        lastGray, newGray, direction, isHorizontal,
        bestOverlap, MIN_OVERLAP, maxOverlap);

    double overlapDiff = normalizedOverlapDifference(lastGray, newGray, direction, isHorizontal, refinedOverlap);
    if (overlapDiff > kMaxOverlapDiff) {
        candidate.confidence = 0.2;
        candidate.failureReason = QString("Overlap mismatch: %1").arg(overlapDiff, 0, 'f', 3);
        return candidate;
    }

    double baseConfidence = std::clamp(bestCorr, 0.0, 1.0);
    double uniqueness = 1.0;
    if (secondBestCorr > -0.5) {
        double peakGap = bestCorr - secondBestCorr;
        uniqueness = std::clamp(peakGap / kPeakGapScale, 0.0, 1.0);
    }

    double diffPenalty = std::clamp(overlapDiff / kMaxOverlapDiff, 0.0, 1.0);
    candidate.confidence = baseConfidence * (0.7 + 0.3 * uniqueness) * (1.0 - 0.5 * diffPenalty);

    if (candidate.confidence < m_stitchConfig.confidenceThreshold) {
        candidate.failureReason = QString("Low confidence: %1").arg(candidate.confidence);
        return candidate;
    }

    candidate.success = true;
    candidate.overlap = refinedOverlap;

    return candidate;
}
