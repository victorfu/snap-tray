#include "scrolling/ImageStitcher.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>

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
}

ImageStitcher::StitchResult ImageStitcher::tryORBMatch(const QImage &newFrame)
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

    auto primaryCandidate = computeORBMatchCandidate(newFrame, primaryDir);
    auto secondaryCandidate = computeORBMatchCandidate(newFrame, secondaryDir);

    if (primaryCandidate.success && secondaryCandidate.success) {
        const auto &best = (primaryCandidate.confidence > secondaryCandidate.confidence ||
                           (primaryCandidate.confidence == secondaryCandidate.confidence &&
                            primaryCandidate.matchedFeatures >= secondaryCandidate.matchedFeatures))
                           ? primaryCandidate
                           : secondaryCandidate;
        return applyCandidate(newFrame, best, Algorithm::ORB);
    }

    if (primaryCandidate.success) {
        return applyCandidate(newFrame, primaryCandidate, Algorithm::ORB);
    }

    if (secondaryCandidate.success) {
        return applyCandidate(newFrame, secondaryCandidate, Algorithm::ORB);
    }

    StitchResult result;
    result.usedAlgorithm = Algorithm::ORB;
    result.confidence = std::max(primaryCandidate.confidence, secondaryCandidate.confidence);
    result.matchedFeatures = std::max(primaryCandidate.matchedFeatures, secondaryCandidate.matchedFeatures);
    QString dirNames = (m_captureMode == CaptureMode::Horizontal) ? "Right: %1; Left: %2" : "Down: %1; Up: %2";
    result.failureReason = dirNames
        .arg(primaryCandidate.failureReason.isEmpty() ? "No match" : primaryCandidate.failureReason,
             secondaryCandidate.failureReason.isEmpty() ? "No match" : secondaryCandidate.failureReason);
    return result;
}

ImageStitcher::StitchResult ImageStitcher::trySIFTMatch(const QImage &newFrame)
{
    // SIFT may not be available in all OpenCV builds
    // Fall back to ORB for now
    return tryORBMatch(newFrame);
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
        const auto &best = (primaryCandidate.confidence >= secondaryCandidate.confidence) ? primaryCandidate : secondaryCandidate;
        return applyCandidate(newFrame, best, Algorithm::TemplateMatching);
    }

    if (primaryCandidate.success) {
        return applyCandidate(newFrame, primaryCandidate, Algorithm::TemplateMatching);
    }

    if (secondaryCandidate.success) {
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

ImageStitcher::MatchCandidate ImageStitcher::computeORBMatchCandidate(const QImage &newFrame,
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

    // Convert to grayscale
    cv::Mat lastGray, newGray;
    cv::cvtColor(lastMat, lastGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(newMat, newGray, cv::COLOR_BGR2GRAY);

    const int lastHeight = lastGray.rows;
    const int newHeight = newGray.rows;
    const int lastWidth = lastGray.cols;
    const int newWidth = newGray.cols;

    if (lastHeight <= 0 || newHeight <= 0 || lastWidth <= 0 || newWidth <= 0) {
        candidate.failureReason = "Empty frame";
        return candidate;
    }

    cv::Rect roiLast;
    cv::Rect roiNew;
    bool isHorizontal = (direction == ScrollDirection::Left || direction == ScrollDirection::Right);

    if (isHorizontal) {
        // Horizontal scrolling: use width-based ROI
        int roiLastWidth = static_cast<int>(lastWidth * ROI_FIRST_RATIO);
        int roiNewWidth = static_cast<int>(newWidth * ROI_SECOND_RATIO);
        roiLastWidth = qBound(1, roiLastWidth, lastWidth);
        roiNewWidth = qBound(1, roiNewWidth, newWidth);

        if (direction == ScrollDirection::Right) {
            roiLast = cv::Rect(lastWidth - roiLastWidth, 0, roiLastWidth, lastHeight);
            roiNew = cv::Rect(0, 0, roiNewWidth, newHeight);
        } else {
            roiLast = cv::Rect(0, 0, roiLastWidth, lastHeight);
            roiNew = cv::Rect(newWidth - roiNewWidth, 0, roiNewWidth, newHeight);
        }
    } else {
        // Vertical scrolling: use height-based ROI
        int roiLastHeight = static_cast<int>(lastHeight * ROI_FIRST_RATIO);
        int roiNewHeight = static_cast<int>(newHeight * ROI_SECOND_RATIO);
        roiLastHeight = qBound(1, roiLastHeight, lastHeight);
        roiNewHeight = qBound(1, roiNewHeight, newHeight);

        if (direction == ScrollDirection::Down) {
            roiLast = cv::Rect(0, lastHeight - roiLastHeight, lastWidth, roiLastHeight);
            roiNew = cv::Rect(0, 0, newWidth, roiNewHeight);
        } else {
            roiLast = cv::Rect(0, 0, lastWidth, roiLastHeight);
            roiNew = cv::Rect(0, newHeight - roiNewHeight, newWidth, roiNewHeight);
        }
    }

    cv::Mat roiLastMat = lastGray(roiLast);
    cv::Mat roiNewMat = newGray(roiNew);

    // Lightly blur to reduce sensor noise before feature extraction
    cv::GaussianBlur(roiLastMat, roiLastMat, cv::Size(3, 3), 0.8);
    cv::GaussianBlur(roiNewMat, roiNewMat, cv::Size(3, 3), 0.8);

    // ORB detection
    cv::Ptr<cv::ORB> orb = cv::ORB::create(1200);
    std::vector<cv::KeyPoint> kp1, kp2;
    cv::Mat desc1, desc2;

    orb->detectAndCompute(roiLastMat, cv::noArray(), kp1, desc1);
    orb->detectAndCompute(roiNewMat, cv::noArray(), kp2, desc2);

    if (desc1.empty() || desc2.empty() || kp1.size() < 5 || kp2.size() < 5) {
        candidate.failureReason = "Insufficient features detected";
        return candidate;
    }

    // Match using BFMatcher with Hamming distance
    cv::BFMatcher matcher(cv::NORM_HAMMING);
    std::vector<std::vector<cv::DMatch>> knnMatches;
    matcher.knnMatch(desc1, desc2, knnMatches, 2);

    // Apply Lowe's ratio test
    std::vector<cv::DMatch> goodMatches;
    for (const auto &m : knnMatches) {
        if (m.size() >= 2 && m[0].distance < 0.75 * m[1].distance) {
            goodMatches.push_back(m[0]);
        }
    }

    candidate.matchedFeatures = static_cast<int>(goodMatches.size());

    if (goodMatches.size() < 4) {
        candidate.confidence = static_cast<double>(goodMatches.size()) / 20.0;
        candidate.failureReason = QString("Too few good matches: %1").arg(goodMatches.size());
        return candidate;
    }

    // Calculate median offset in full-frame coordinates with outlier filtering
    std::vector<double> offsets;
    offsets.reserve(goodMatches.size());
    for (const auto &m : goodMatches) {
        if (isHorizontal) {
            // Horizontal: use X coordinates
            double x1 = kp1[m.queryIdx].pt.x + roiLast.x;
            double x2 = kp2[m.trainIdx].pt.x + roiNew.x;
            offsets.push_back(x1 - x2);
        } else {
            // Vertical: use Y coordinates
            double y1 = kp1[m.queryIdx].pt.y + roiLast.y;
            double y2 = kp2[m.trainIdx].pt.y + roiNew.y;
            offsets.push_back(y1 - y2);
        }
    }
    std::sort(offsets.begin(), offsets.end());

    // Use interquartile range (IQR) to filter outliers
    size_t q1Idx = offsets.size() / 4;
    size_t q3Idx = (offsets.size() * 3) / 4;
    double q1 = offsets[q1Idx];
    double q3 = offsets[q3Idx];
    double iqr = q3 - q1;
    double lowerBound = q1 - 1.5 * iqr;
    double upperBound = q3 + 1.5 * iqr;

    std::vector<double> filteredOffsets;
    for (double val : offsets) {
        if (val >= lowerBound && val <= upperBound) {
            filteredOffsets.push_back(val);
        }
    }

    double medianOffset;
    if (filteredOffsets.size() >= 3) {
        medianOffset = filteredOffsets[filteredOffsets.size() / 2];
    } else {
        medianOffset = offsets[offsets.size() / 2];
    }

    // Check direction mismatch with small tolerance for floating-point precision
    constexpr double DIRECTION_TOLERANCE = 3.0;
    if (direction == ScrollDirection::Down && medianOffset < -DIRECTION_TOLERANCE) {
        candidate.failureReason = "Direction mismatch";
        return candidate;
    }
    if (direction == ScrollDirection::Up && medianOffset > DIRECTION_TOLERANCE) {
        candidate.failureReason = "Direction mismatch";
        return candidate;
    }
    if (direction == ScrollDirection::Right && medianOffset < -DIRECTION_TOLERANCE) {
        candidate.failureReason = "Direction mismatch";
        return candidate;
    }
    if (direction == ScrollDirection::Left && medianOffset > DIRECTION_TOLERANCE) {
        candidate.failureReason = "Direction mismatch";
        return candidate;
    }

    // Calculate overlap based on direction
    int frameDimension = isHorizontal ? newFrame.width() : newFrame.height();
    int overlap = frameDimension - static_cast<int>(std::round(std::abs(medianOffset)));

    if (overlap <= 0) {
        candidate.confidence = 0.2;
        candidate.failureReason = QString("Negative overlap: %1 (offset too large)").arg(overlap);
        return candidate;
    }

    // Clamp overlap to valid range instead of failing immediately
    if (overlap < MIN_OVERLAP) {
        // Very small overlap is likely a false match
        candidate.confidence = 0.3;
        candidate.failureReason = QString("Overlap too small: %1").arg(overlap);
        return candidate;
    }
    if (overlap > MAX_OVERLAP) {
        // Clamp to max overlap - this can happen with slow scrolling
        overlap = MAX_OVERLAP;
    }

    int inlierCount = 0;
    for (double val : offsets) {
        if (std::abs(val - medianOffset) < 5.0) {
            inlierCount++;
        }
    }
    // Calculate confidence as ratio of inliers to total good matches
    // Using goodMatches.size() (not filteredOffsets.size()) to avoid artificially inflating confidence
    candidate.confidence = goodMatches.empty() ? 0.0 : static_cast<double>(inlierCount) / goodMatches.size();

    if (candidate.confidence < m_confidenceThreshold) {
        candidate.failureReason = QString("Low confidence: %1").arg(candidate.confidence);
        return candidate;
    }

    candidate.success = true;
    candidate.overlap = overlap;

    return candidate;
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

            int searchMaxExtent = std::min(MAX_OVERLAP + templateWidth, newGray.cols);
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

            double fullOffset = templateLeft - (searchLeft + maxLoc.x);

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

            int searchMaxExtent = std::min(MAX_OVERLAP + templateHeight, newGray.rows);
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

            double fullOffset = templateTop - (searchTop + maxLoc.y);

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

    if (candidate.confidence < m_confidenceThreshold) {
        candidate.failureReason = QString("Template match confidence too low: %1").arg(candidate.confidence);
        return candidate;
    }

    // Calculate overlap based on direction
    int frameDimension = isHorizontal ? newFrame.width() : newFrame.height();
    int overlap = frameDimension - static_cast<int>(std::round(std::abs(medianOffset)));

    if (overlap <= 0) {
        candidate.failureReason = QString("Negative overlap: %1 (offset too large)").arg(overlap);
        return candidate;
    }

    if (overlap < MIN_OVERLAP || overlap > MAX_OVERLAP) {
        candidate.failureReason = QString("Invalid overlap: %1").arg(overlap);
        return candidate;
    }

    candidate.success = true;
    candidate.overlap = overlap;

    return candidate;
}

ImageStitcher::StitchResult ImageStitcher::applyCandidate(const QImage &newFrame,
                                                          const MatchCandidate &candidate,
                                                          Algorithm algorithm)
{
    StitchResult result = performStitch(newFrame, candidate.overlap, candidate.direction);
    result.usedAlgorithm = algorithm;
    result.confidence = candidate.confidence;
    result.matchedFeatures = candidate.matchedFeatures;
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
                    qWarning() << "ImageStitcher: QPainter::end() failed for horizontal right stitch (expand)";
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
                    qWarning() << "ImageStitcher: QPainter::end() failed for horizontal right stitch (in-place)";
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
                qWarning() << "ImageStitcher: QPainter::end() failed for horizontal left stitch";
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
                    qWarning() << "ImageStitcher: QPainter::end() failed for vertical down stitch (expand)";
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
                    qWarning() << "ImageStitcher: QPainter::end() failed for vertical down stitch (in-place)";
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
                qWarning() << "ImageStitcher: QPainter::end() failed for vertical up stitch";
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
        const auto &best = (primaryCandidate.confidence >= secondaryCandidate.confidence)
                           ? primaryCandidate
                           : secondaryCandidate;
        return applyCandidate(newFrame, best, Algorithm::RowProjection);
    }

    if (primaryCandidate.success) {
        return applyCandidate(newFrame, primaryCandidate, Algorithm::RowProjection);
    }

    if (secondaryCandidate.success) {
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

    // Calculate projection profiles
    // For vertical scrolling: sum pixels horizontally to get row profile P(y)
    // For horizontal scrolling: sum pixels vertically to get column profile P(x)
    cv::Mat lastProfile, newProfile;

    if (isHorizontal) {
        // Vertical projection (sum along rows) -> 1 row x N cols
        cv::reduce(lastGray, lastProfile, 0, cv::REDUCE_SUM, CV_64F);
        cv::reduce(newGray, newProfile, 0, cv::REDUCE_SUM, CV_64F);
    } else {
        // Horizontal projection (sum along columns) -> N rows x 1 col
        cv::reduce(lastGray, lastProfile, 1, cv::REDUCE_SUM, CV_64F);
        cv::reduce(newGray, newProfile, 1, cv::REDUCE_SUM, CV_64F);
    }

    // Normalize profiles to zero mean
    cv::Scalar lastMean = cv::mean(lastProfile);
    cv::Scalar newMean = cv::mean(newProfile);
    lastProfile -= lastMean[0];
    newProfile -= newMean[0];

    // Define ROI regions for cross-correlation
    // For Down/Right: match bottom/right portion of last frame with top/left portion of new frame
    // For Up/Left: match top/left portion of last frame with bottom/right portion of new frame
    int profileSize = isHorizontal ? lastProfile.cols : lastProfile.rows;
    int roiLastSize = static_cast<int>(profileSize * ROI_FIRST_RATIO);
    int roiNewSize = static_cast<int>(profileSize * ROI_SECOND_RATIO);

    roiLastSize = qBound(MIN_OVERLAP, roiLastSize, profileSize);
    roiNewSize = qBound(MIN_OVERLAP, roiNewSize, profileSize);

    cv::Mat templateProfile, searchProfile;
    int templateStart = 0;
    int searchStart = 0;

    if (isHorizontal) {
        if (direction == ScrollDirection::Right) {
            // Template: right portion of last frame
            // Search: left portion of new frame
            templateStart = profileSize - roiLastSize;
            templateProfile = lastProfile(cv::Rect(templateStart, 0, roiLastSize, 1));
            searchProfile = newProfile(cv::Rect(0, 0, roiNewSize, 1));
            searchStart = 0;
        } else {
            // Template: left portion of last frame
            // Search: right portion of new frame
            templateStart = 0;
            templateProfile = lastProfile(cv::Rect(0, 0, roiLastSize, 1));
            searchStart = profileSize - roiNewSize;
            searchProfile = newProfile(cv::Rect(searchStart, 0, roiNewSize, 1));
        }
    } else {
        if (direction == ScrollDirection::Down) {
            // Template: bottom portion of last frame
            // Search: top portion of new frame
            templateStart = profileSize - roiLastSize;
            templateProfile = lastProfile(cv::Rect(0, templateStart, 1, roiLastSize));
            searchProfile = newProfile(cv::Rect(0, 0, 1, roiNewSize));
            searchStart = 0;
        } else {
            // Template: top portion of last frame
            // Search: bottom portion of new frame
            templateStart = 0;
            templateProfile = lastProfile(cv::Rect(0, 0, 1, roiLastSize));
            searchStart = profileSize - roiNewSize;
            searchProfile = newProfile(cv::Rect(0, searchStart, 1, roiNewSize));
        }
    }

    // Convert to 1D vectors for cross-correlation
    std::vector<double> templateVec, searchVec;
    if (isHorizontal) {
        templateVec.assign(templateProfile.ptr<double>(0),
                           templateProfile.ptr<double>(0) + templateProfile.cols);
        searchVec.assign(searchProfile.ptr<double>(0),
                         searchProfile.ptr<double>(0) + searchProfile.cols);
    } else {
        for (int i = 0; i < templateProfile.rows; ++i) {
            templateVec.push_back(templateProfile.at<double>(i, 0));
        }
        for (int i = 0; i < searchProfile.rows; ++i) {
            searchVec.push_back(searchProfile.at<double>(i, 0));
        }
    }

    if (templateVec.size() < 10 || searchVec.size() < 10) {
        candidate.failureReason = "Profile too short for matching";
        return candidate;
    }

    // Perform 1D cross-correlation using sliding window
    // We search for the position where template best matches within search region
    int maxLag = static_cast<int>(searchVec.size()) - static_cast<int>(templateVec.size());
    if (maxLag < 0) {
        candidate.failureReason = "Template larger than search region";
        return candidate;
    }

    double bestCorr = -1e9;
    int bestLag = 0;
    double templateNorm = 0.0;

    // Pre-compute template norm
    for (double v : templateVec) {
        templateNorm += v * v;
    }
    templateNorm = std::sqrt(templateNorm);

    if (templateNorm < 1e-6) {
        candidate.failureReason = "Template has no variance (uniform region)";
        return candidate;
    }

    // Sliding window cross-correlation
    for (int lag = 0; lag <= maxLag; ++lag) {
        double corr = 0.0;
        double searchNorm = 0.0;

        for (size_t i = 0; i < templateVec.size(); ++i) {
            double sv = searchVec[lag + i];
            corr += templateVec[i] * sv;
            searchNorm += sv * sv;
        }

        searchNorm = std::sqrt(searchNorm);
        if (searchNorm > 1e-6) {
            // Normalized cross-correlation coefficient
            corr /= (templateNorm * searchNorm);
        } else {
            corr = 0.0;
        }

        if (corr > bestCorr) {
            bestCorr = corr;
            bestLag = lag;
        }
    }

    // Calculate offset in full-frame coordinates
    // bestLag is the position within search region where template matches
    double offset;
    if (isHorizontal) {
        if (direction == ScrollDirection::Right) {
            // Template starts at templateStart in last frame
            // Matches at bestLag in new frame (from position 0)
            offset = templateStart - bestLag;
        } else {
            // Template starts at 0 in last frame
            // Matches at searchStart + bestLag in new frame
            offset = -(searchStart + bestLag);
        }
    } else {
        if (direction == ScrollDirection::Down) {
            offset = templateStart - bestLag;
        } else {
            offset = -(searchStart + bestLag);
        }
    }

    // Validate direction
    constexpr double DIRECTION_TOLERANCE = 3.0;
    if (direction == ScrollDirection::Down && offset < -DIRECTION_TOLERANCE) {
        candidate.failureReason = "Direction mismatch (scrolling up detected)";
        return candidate;
    }
    if (direction == ScrollDirection::Up && offset > DIRECTION_TOLERANCE) {
        candidate.failureReason = "Direction mismatch (scrolling down detected)";
        return candidate;
    }
    if (direction == ScrollDirection::Right && offset < -DIRECTION_TOLERANCE) {
        candidate.failureReason = "Direction mismatch (scrolling left detected)";
        return candidate;
    }
    if (direction == ScrollDirection::Left && offset > DIRECTION_TOLERANCE) {
        candidate.failureReason = "Direction mismatch (scrolling right detected)";
        return candidate;
    }

    // Calculate overlap
    int frameDimension = isHorizontal ? newFrame.width() : newFrame.height();
    int overlap = frameDimension - static_cast<int>(std::round(std::abs(offset)));

    if (overlap <= 0) {
        candidate.confidence = 0.2;
        candidate.failureReason = QString("Negative overlap: %1").arg(overlap);
        return candidate;
    }

    if (overlap < MIN_OVERLAP) {
        candidate.confidence = 0.3;
        candidate.failureReason = QString("Overlap too small: %1").arg(overlap);
        return candidate;
    }

    if (overlap > MAX_OVERLAP) {
        overlap = MAX_OVERLAP;
    }

    // Confidence is the normalized cross-correlation coefficient
    // Scale it to make it comparable with other methods
    candidate.confidence = std::clamp((bestCorr + 1.0) / 2.0, 0.0, 1.0);

    if (candidate.confidence < m_confidenceThreshold) {
        candidate.failureReason = QString("Low correlation: %1").arg(bestCorr);
        return candidate;
    }

    candidate.success = true;
    candidate.overlap = overlap;

    return candidate;
}
