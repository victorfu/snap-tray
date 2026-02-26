#include "detection/ScrollTemporalVarianceMask.h"

#include <QtGlobal>

#include <opencv2/imgproc.hpp>

namespace {

bool areCompatible(const cv::Mat &a, const cv::Mat &b)
{
    return !a.empty() && !b.empty() && a.size() == b.size() && a.type() == CV_8UC1 && b.type() == CV_8UC1;
}

bool isStableRow(const cv::Mat &variation,
                 int row,
                 int xStart,
                 int xEnd,
                 uchar threshold,
                 double stableRatio)
{
    if (variation.empty() || row < 0 || row >= variation.rows || xStart >= xEnd) {
        return false;
    }

    const uchar *line = variation.ptr<uchar>(row);
    if (!line) {
        return false;
    }

    int stableCount = 0;
    const int total = xEnd - xStart;
    for (int x = xStart; x < xEnd; ++x) {
        if (line[x] <= threshold) {
            ++stableCount;
        }
    }

    if (total <= 0) {
        return false;
    }

    return (static_cast<double>(stableCount) / static_cast<double>(total)) >= stableRatio;
}

} // namespace

ScrollTemporalMaskResult ScrollTemporalVarianceMask::buildAlignmentMask(const cv::Mat &olderGray,
                                                                        const cv::Mat &previousGray,
                                                                        const cv::Mat &currentGray,
                                                                        double varianceThreshold,
                                                                        int rightStripWidthPx,
                                                                        int minStableRows)
{
    ScrollTemporalMaskResult result;

    if (!areCompatible(olderGray, previousGray) || !areCompatible(previousGray, currentGray)) {
        return result;
    }

    const int width = currentGray.cols;
    const int height = currentGray.rows;
    if (width <= 0 || height <= 0) {
        return result;
    }

    cv::Mat diffOlderPrev;
    cv::Mat diffPrevCurr;
    cv::absdiff(olderGray, previousGray, diffOlderPrev);
    cv::absdiff(previousGray, currentGray, diffPrevCurr);

    cv::Mat variation;
    cv::max(diffOlderPrev, diffPrevCurr, variation);

    const uchar threshold = static_cast<uchar>(qBound(0.001, varianceThreshold, 1.0) * 255.0);
    const int minRows = qBound(1, minStableRows, qMax(1, height / 4));
    const double stableRatio = 0.98;

    int stableTopRows = 0;
    for (int y = 0; y < height; ++y) {
        if (!isStableRow(variation, y, 0, width, threshold, stableRatio)) {
            break;
        }
        ++stableTopRows;
    }

    int stableBottomRows = 0;
    for (int y = height - 1; y >= 0; --y) {
        if (!isStableRow(variation, y, 0, width, threshold, stableRatio)) {
            break;
        }
        ++stableBottomRows;
    }

    if (stableTopRows >= minRows) {
        result.stickyHeaderPx = stableTopRows;
    }
    if (stableBottomRows >= minRows) {
        result.stickyFooterPx = stableBottomRows;
    }

    const int maxRailWidth = qBound(1, rightStripWidthPx, qMax(1, width / 4));
    int railWidth = 0;
    const int railTop = qBound(0, result.stickyHeaderPx, height - 1);
    const int railBottom = qBound(railTop + 1, height - result.stickyFooterPx, height);
    for (int i = 0; i < maxRailWidth; ++i) {
        const int col = width - 1 - i;
        int stableCount = 0;
        int total = 0;
        for (int y = railTop; y < railBottom; ++y) {
            const uchar *line = variation.ptr<uchar>(y);
            if (!line) {
                continue;
            }
            if (line[col] <= threshold) {
                ++stableCount;
            }
            ++total;
        }

        if (total <= 0) {
            break;
        }

        const double ratio = static_cast<double>(stableCount) / static_cast<double>(total);
        if (ratio >= 0.985) {
            ++railWidth;
        } else {
            break;
        }
    }
    result.rightRailPx = railWidth;

    result.alignmentMask = cv::Mat(height, width, CV_8UC1, cv::Scalar(255));

    if (result.stickyHeaderPx > 0) {
        result.alignmentMask(cv::Rect(0, 0, width, result.stickyHeaderPx)).setTo(0);
    }

    if (result.stickyFooterPx > 0) {
        result.alignmentMask(cv::Rect(0,
                                      height - result.stickyFooterPx,
                                      width,
                                      result.stickyFooterPx)).setTo(0);
    }

    if (result.rightRailPx > 0) {
        result.alignmentMask(cv::Rect(width - result.rightRailPx,
                                      0,
                                      result.rightRailPx,
                                      height)).setTo(0);
    }

    return result;
}

cv::Mat ScrollTemporalVarianceMask::applyMask(const cv::Mat &gray, const cv::Mat &alignmentMask)
{
    if (gray.empty() || alignmentMask.empty() || gray.type() != CV_8UC1 ||
        alignmentMask.type() != CV_8UC1 || gray.size() != alignmentMask.size()) {
        return gray.clone();
    }

    cv::Mat masked = gray.clone();
    for (int y = 0; y < masked.rows; ++y) {
        uchar *line = masked.ptr<uchar>(y);
        const uchar *maskLine = alignmentMask.ptr<uchar>(y);
        if (!line || !maskLine) {
            continue;
        }
        for (int x = 0; x < masked.cols; ++x) {
            if (maskLine[x] == 0) {
                line[x] = 0;
            }
        }
    }

    return masked;
}

double ScrollTemporalVarianceMask::maskedCoverage(const cv::Mat &alignmentMask)
{
    if (alignmentMask.empty() || alignmentMask.type() != CV_8UC1) {
        return 0.0;
    }

    const int total = alignmentMask.rows * alignmentMask.cols;
    if (total <= 0) {
        return 0.0;
    }

    const int masked = total - cv::countNonZero(alignmentMask);
    return static_cast<double>(masked) / static_cast<double>(total);
}

cv::Mat ScrollTemporalVarianceMask::buildWeights(const cv::Mat &previousGray,
                                                 const cv::Mat &currentGray,
                                                 double tvThreshold,
                                                 double dynamicWeight)
{
    if (previousGray.empty() || currentGray.empty() || previousGray.size() != currentGray.size()) {
        return cv::Mat();
    }

    const double threshold = qBound(0.001, tvThreshold, 1.0) * 255.0;
    const float lowWeight = static_cast<float>(qBound(0.0, dynamicWeight, 1.0));

    cv::Mat diff;
    cv::absdiff(previousGray, currentGray, diff);

    cv::Mat dynamicMask;
    cv::threshold(diff, dynamicMask, threshold, 255.0, cv::THRESH_BINARY);

    cv::Mat weights(previousGray.rows, previousGray.cols, CV_32FC1, cv::Scalar(1.0f));
    for (int y = 0; y < dynamicMask.rows; ++y) {
        const uchar *maskLine = dynamicMask.ptr<uchar>(y);
        float *weightLine = weights.ptr<float>(y);
        for (int x = 0; x < dynamicMask.cols; ++x) {
            if (maskLine[x] != 0) {
                weightLine[x] = lowWeight;
            }
        }
    }

    return weights;
}

double ScrollTemporalVarianceMask::dynamicCoverage(const cv::Mat &weights,
                                                   double dynamicWeight)
{
    if (weights.empty() || weights.type() != CV_32FC1) {
        return 0.0;
    }

    const float threshold = static_cast<float>(qBound(0.0, dynamicWeight, 1.0) + 1e-6);
    int dynamicPixels = 0;
    const int total = weights.rows * weights.cols;

    for (int y = 0; y < weights.rows; ++y) {
        const float *line = weights.ptr<float>(y);
        for (int x = 0; x < weights.cols; ++x) {
            if (line[x] <= threshold) {
                ++dynamicPixels;
            }
        }
    }

    if (total <= 0) {
        return 0.0;
    }
    return static_cast<double>(dynamicPixels) / static_cast<double>(total);
}
