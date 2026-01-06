#include "scrolling/SeamQualityEvaluator.h"

#include <QDebug>
#include <cmath>
#include <algorithm>

SeamQualityEvaluator::Result SeamQualityEvaluator::evaluate(
    const QImage &prevImage, const QImage &newImage,
    int overlap, ScrollDirection direction, const Config &config)
{
    Result result;

    if (prevImage.isNull() || newImage.isNull()) {
        result.failureReason = "Null image provided";
        return result;
    }

    if (overlap <= 0) {
        result.failureReason = "Invalid overlap value";
        return result;
    }

    // Extract overlap strips from both images
    QImage prevStrip = extractOverlapStrip(prevImage, overlap, direction, true);   // Trailing edge of prev
    QImage newStrip = extractOverlapStrip(newImage, overlap, direction, false);    // Leading edge of new

    if (prevStrip.isNull() || newStrip.isNull()) {
        result.failureReason = "Failed to extract overlap regions";
        return result;
    }

    // Ensure same size
    if (prevStrip.size() != newStrip.size()) {
        // Resize to smaller dimensions
        int width = std::min(prevStrip.width(), newStrip.width());
        int height = std::min(prevStrip.height(), newStrip.height());
        prevStrip = prevStrip.copy(0, 0, width, height);
        newStrip = newStrip.copy(0, 0, width, height);
    }

    // Convert to grayscale for comparison
    QImage prevGray = prevStrip.convertToFormat(QImage::Format_Grayscale8);
    QImage newGray = newStrip.convertToFormat(QImage::Format_Grayscale8);

    // Compute metrics
    result.nccScore = computeNCC(prevGray, newGray);
    result.mseScore = computeMSE(prevGray, newGray);

    bool isVertical = (direction == ScrollDirection::Down || direction == ScrollDirection::Up);
    if (config.useEdgeAlignment) {
        result.edgeScore = computeEdgeAlignment(prevGray, newGray, isVertical);
    } else {
        result.edgeScore = 1.0;
    }

    // Compute combined seam score
    // Weight NCC highest (similarity), then edge alignment, then MSE (inverted)
    double mseContribution = 1.0 - result.mseScore;  // Invert so higher is better
    result.seamScore = (result.nccScore * 0.5) +
                       (mseContribution * 0.3) +
                       (result.edgeScore * 0.2);

    // Determine pass/fail
    bool nccPassed = result.nccScore >= config.nccThreshold;
    bool msePassed = result.mseScore <= config.mseThreshold;
    bool edgePassed = !config.useEdgeAlignment || result.edgeScore >= config.edgeAlignThreshold;

    result.passed = nccPassed && msePassed && edgePassed;

    // Suggest overlap adjustment if failed
    if (!result.passed) {
        if (result.mseScore > config.mseThreshold * 1.5) {
            // High MSE suggests overlap is wrong by several pixels
            result.suggestedOverlapDelta = (result.nccScore < 0.5) ? 5 : 2;
        } else if (!edgePassed) {
            // Edge misalignment suggests small offset
            result.suggestedOverlapDelta = 1;
        }

        // Build failure reason
        QStringList reasons;
        if (!nccPassed) {
            reasons << QString("NCC too low (%1 < %2)").arg(result.nccScore, 0, 'f', 3).arg(config.nccThreshold);
        }
        if (!msePassed) {
            reasons << QString("MSE too high (%1 > %2)").arg(result.mseScore, 0, 'f', 3).arg(config.mseThreshold);
        }
        if (!edgePassed) {
            reasons << QString("Edge misaligned (%1 < %2)").arg(result.edgeScore, 0, 'f', 3).arg(config.edgeAlignThreshold);
        }
        result.failureReason = reasons.join("; ");
    }

    return result;
}

bool SeamQualityEvaluator::isOverlapValid(const QImage &prevImage, const QImage &newImage,
                                          int overlap, ScrollDirection direction)
{
    Config config;
    config.useEdgeAlignment = false;  // Faster check
    Result result = evaluate(prevImage, newImage, overlap, direction, config);
    return result.passed;
}

int SeamQualityEvaluator::findBestOverlap(const QImage &prevImage, const QImage &newImage,
                                          int detectedOverlap, ScrollDirection direction,
                                          int searchRadius)
{
    Config config;
    config.useEdgeAlignment = false;  // Speed up search

    double bestScore = -1.0;
    int bestOverlap = detectedOverlap;

    int minOverlap = std::max(1, detectedOverlap - searchRadius);
    int maxOverlap = detectedOverlap + searchRadius;

    // Limit max overlap to image dimension
    bool isVertical = (direction == ScrollDirection::Down || direction == ScrollDirection::Up);
    int maxDim = isVertical
        ? std::min(prevImage.height(), newImage.height()) - 1
        : std::min(prevImage.width(), newImage.width()) - 1;
    maxOverlap = std::min(maxOverlap, maxDim);

    for (int overlap = minOverlap; overlap <= maxOverlap; ++overlap) {
        Result result = evaluate(prevImage, newImage, overlap, direction, config);
        if (result.seamScore > bestScore) {
            bestScore = result.seamScore;
            bestOverlap = overlap;
        }
    }

    qDebug() << "SeamQualityEvaluator: Best overlap" << bestOverlap
             << "(detected:" << detectedOverlap << ", score:" << bestScore << ")";

    return bestOverlap;
}

double SeamQualityEvaluator::computeNCC(const QImage &strip1, const QImage &strip2)
{
    if (strip1.isNull() || strip2.isNull() || strip1.size() != strip2.size()) {
        return 0.0;
    }

    int width = strip1.width();
    int height = strip1.height();
    int pixelCount = width * height;

    if (pixelCount == 0) {
        return 0.0;
    }

    // Compute means
    double sum1 = 0.0, sum2 = 0.0;
    for (int y = 0; y < height; ++y) {
        const uchar *line1 = strip1.constScanLine(y);
        const uchar *line2 = strip2.constScanLine(y);
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
        const uchar *line1 = strip1.constScanLine(y);
        const uchar *line2 = strip2.constScanLine(y);
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
        return 1.0;  // Both constant
    }

    return std::clamp(sumProduct / denominator, -1.0, 1.0);
}

double SeamQualityEvaluator::computeMSE(const QImage &strip1, const QImage &strip2)
{
    if (strip1.isNull() || strip2.isNull() || strip1.size() != strip2.size()) {
        return 1.0;
    }

    int width = strip1.width();
    int height = strip1.height();
    int pixelCount = width * height;

    if (pixelCount == 0) {
        return 1.0;
    }

    double sumSqDiff = 0.0;
    for (int y = 0; y < height; ++y) {
        const uchar *line1 = strip1.constScanLine(y);
        const uchar *line2 = strip2.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            double diff = static_cast<double>(line1[x]) - static_cast<double>(line2[x]);
            sumSqDiff += diff * diff;
        }
    }

    // Normalize to 0..1 range (max MSE is 255^2)
    double mse = sumSqDiff / pixelCount;
    return mse / (255.0 * 255.0);
}

double SeamQualityEvaluator::computeEdgeAlignment(const QImage &strip1, const QImage &strip2, bool isVertical)
{
    if (strip1.isNull() || strip2.isNull() || strip1.size() != strip2.size()) {
        return 0.0;
    }

    // Simple edge alignment: compare gradient patterns at the seam boundary
    // For vertical scrolling, compare bottom row of strip1 with top row of strip2
    // For horizontal scrolling, compare right column of strip1 with left column of strip2

    int width = strip1.width();
    int height = strip1.height();

    if (isVertical) {
        // Compare last row of strip1 with first row of strip2
        if (height < 2) return 1.0;

        double sumDiff = 0.0;
        const uchar *bottomRow = strip1.constScanLine(height - 1);
        const uchar *topRow = strip2.constScanLine(0);
        for (int x = 0; x < width; ++x) {
            double diff = std::abs(static_cast<double>(bottomRow[x]) - static_cast<double>(topRow[x]));
            sumDiff += diff;
        }
        double avgDiff = sumDiff / width;
        // Convert to 0..1 score (lower diff = higher score)
        return std::max(0.0, 1.0 - (avgDiff / 64.0));  // Scale factor 64 for reasonable sensitivity
    } else {
        // Compare last column of strip1 with first column of strip2
        if (width < 2) return 1.0;

        double sumDiff = 0.0;
        for (int y = 0; y < height; ++y) {
            const uchar *line1 = strip1.constScanLine(y);
            const uchar *line2 = strip2.constScanLine(y);
            double diff = std::abs(static_cast<double>(line1[width - 1]) - static_cast<double>(line2[0]));
            sumDiff += diff;
        }
        double avgDiff = sumDiff / height;
        return std::max(0.0, 1.0 - (avgDiff / 64.0));
    }
}

QImage SeamQualityEvaluator::extractOverlapStrip(const QImage &image, int overlap,
                                                  ScrollDirection direction, bool isTrailing)
{
    if (image.isNull() || overlap <= 0) {
        return QImage();
    }

    int width = image.width();
    int height = image.height();
    bool isVertical = (direction == ScrollDirection::Down || direction == ScrollDirection::Up);

    QRect rect;

    if (isVertical) {
        // Vertical scrolling: extract horizontal strip
        int stripHeight = std::min(overlap, height);
        if (isTrailing) {
            // Bottom of prev image
            if (direction == ScrollDirection::Down) {
                rect = QRect(0, height - stripHeight, width, stripHeight);
            } else {
                rect = QRect(0, 0, width, stripHeight);
            }
        } else {
            // Top of new image
            if (direction == ScrollDirection::Down) {
                rect = QRect(0, 0, width, stripHeight);
            } else {
                rect = QRect(0, height - stripHeight, width, stripHeight);
            }
        }
    } else {
        // Horizontal scrolling: extract vertical strip
        int stripWidth = std::min(overlap, width);
        if (isTrailing) {
            // Right of prev image
            if (direction == ScrollDirection::Right) {
                rect = QRect(width - stripWidth, 0, stripWidth, height);
            } else {
                rect = QRect(0, 0, stripWidth, height);
            }
        } else {
            // Left of new image
            if (direction == ScrollDirection::Right) {
                rect = QRect(0, 0, stripWidth, height);
            } else {
                rect = QRect(width - stripWidth, 0, stripWidth, height);
            }
        }
    }

    return image.copy(rect);
}
