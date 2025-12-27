#include "scrolling/ImageStitcher.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/calib3d.hpp>

#include <QPainter>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

ImageStitcher::StitchResult ImageStitcher::tryORBMatch(const QImage &newFrame)
{
    auto downCandidate = computeORBMatchCandidate(newFrame, ScrollDirection::Down);
    auto upCandidate = computeORBMatchCandidate(newFrame, ScrollDirection::Up);

    if (downCandidate.success && upCandidate.success) {
        const auto &best = (downCandidate.confidence > upCandidate.confidence ||
                           (downCandidate.confidence == upCandidate.confidence &&
                            downCandidate.matchedFeatures >= upCandidate.matchedFeatures))
                           ? downCandidate
                           : upCandidate;
        return applyCandidate(newFrame, best, Algorithm::ORB);
    }

    if (downCandidate.success) {
        return applyCandidate(newFrame, downCandidate, Algorithm::ORB);
    }

    if (upCandidate.success) {
        return applyCandidate(newFrame, upCandidate, Algorithm::ORB);
    }

    StitchResult result;
    result.usedAlgorithm = Algorithm::ORB;
    result.confidence = std::max(downCandidate.confidence, upCandidate.confidence);
    result.matchedFeatures = std::max(downCandidate.matchedFeatures, upCandidate.matchedFeatures);
    result.failureReason = QString("Down: %1; Up: %2")
        .arg(downCandidate.failureReason.isEmpty() ? "No match" : downCandidate.failureReason,
             upCandidate.failureReason.isEmpty() ? "No match" : upCandidate.failureReason);
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
    auto downCandidate = computeTemplateMatchCandidate(newFrame, ScrollDirection::Down);
    auto upCandidate = computeTemplateMatchCandidate(newFrame, ScrollDirection::Up);

    if (downCandidate.success && upCandidate.success) {
        const auto &best = (downCandidate.confidence >= upCandidate.confidence) ? downCandidate : upCandidate;
        return applyCandidate(newFrame, best, Algorithm::TemplateMatching);
    }

    if (downCandidate.success) {
        return applyCandidate(newFrame, downCandidate, Algorithm::TemplateMatching);
    }

    if (upCandidate.success) {
        return applyCandidate(newFrame, upCandidate, Algorithm::TemplateMatching);
    }

    StitchResult result;
    result.usedAlgorithm = Algorithm::TemplateMatching;
    result.confidence = std::max(downCandidate.confidence, upCandidate.confidence);
    result.failureReason = QString("Down: %1; Up: %2")
        .arg(downCandidate.failureReason.isEmpty() ? "No match" : downCandidate.failureReason,
             upCandidate.failureReason.isEmpty() ? "No match" : upCandidate.failureReason);
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

    int roiLastHeight = static_cast<int>(lastHeight * ROI_FIRST_RATIO);
    int roiNewHeight = static_cast<int>(newHeight * ROI_SECOND_RATIO);
    roiLastHeight = qBound(1, roiLastHeight, lastHeight);
    roiNewHeight = qBound(1, roiNewHeight, newHeight);

    cv::Rect roiLast;
    cv::Rect roiNew;
    if (direction == ScrollDirection::Down) {
        roiLast = cv::Rect(0, lastHeight - roiLastHeight, lastWidth, roiLastHeight);
        roiNew = cv::Rect(0, 0, newWidth, roiNewHeight);
    } else {
        roiLast = cv::Rect(0, 0, lastWidth, roiLastHeight);
        roiNew = cv::Rect(0, newHeight - roiNewHeight, newWidth, roiNewHeight);
    }

    cv::Mat roiLastMat = lastGray(roiLast);
    cv::Mat roiNewMat = newGray(roiNew);

    // Reduce small illumination noise that confuses feature descriptors
    cv::GaussianBlur(roiLastMat, roiLastMat, cv::Size(3, 3), 0.8);
    cv::GaussianBlur(roiNewMat, roiNewMat, cv::Size(3, 3), 0.8);

    // ORB detection
    const int featureCount = std::max(750, (roiLastMat.rows * roiLastMat.cols) / 3000);
    cv::Ptr<cv::ORB> orb = cv::ORB::create(featureCount);
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

    // Calculate offsets using a robust affine estimate to handle large scrolls
    std::vector<cv::Point2f> lastPoints;
    std::vector<cv::Point2f> newPoints;
    lastPoints.reserve(goodMatches.size());
    newPoints.reserve(goodMatches.size());

    for (const auto &m : goodMatches) {
        const cv::Point2f lastPt(kp1[m.queryIdx].pt.x + roiLast.x,
                                 kp1[m.queryIdx].pt.y + roiLast.y);
        const cv::Point2f newPt(kp2[m.trainIdx].pt.x + roiNew.x,
                                kp2[m.trainIdx].pt.y + roiNew.y);
        lastPoints.push_back(lastPt);
        newPoints.push_back(newPt);
    }

    cv::Mat inlierMask;
    cv::Mat transform = cv::estimateAffinePartial2D(newPoints, lastPoints, inlierMask,
                                                    cv::RANSAC, 3.0, 2000, 0.99);

    std::vector<double> offsets;
    offsets.reserve(goodMatches.size());
    for (size_t i = 0; i < lastPoints.size(); ++i) {
        offsets.push_back(lastPoints[i].y - newPoints[i].y);
    }
    std::sort(offsets.begin(), offsets.end());

    auto computeMedian = [&offsets]() {
        if (offsets.empty()) {
            return 0.0;
        }
        size_t mid = offsets.size() / 2;
        if (offsets.size() % 2 == 0) {
            return (offsets[mid - 1] + offsets[mid]) / 2.0;
        }
        return offsets[mid];
    };

    double translationY = computeMedian();
    double translationX = 0.0;
    int inlierCount = 0;

    if (!transform.empty()) {
        translationX = transform.at<double>(0, 2);
        translationY = transform.at<double>(1, 2);

        double angle = std::abs(std::atan2(transform.at<double>(0, 1), transform.at<double>(0, 0)));
        double scale = std::sqrt(std::pow(transform.at<double>(0, 0), 2) +
                                 std::pow(transform.at<double>(0, 1), 2));

        inlierCount = cv::countNonZero(inlierMask);

        // Reject transforms with unrealistic rotation/scale for vertical scrolling
        if (angle > 0.10 || scale < 0.75 || scale > 1.25) {
            transform.release();
        }
    }

    // Fallback to median when RANSAC transform failed
    if (transform.empty()) {
        inlierCount = 0;
        translationX = 0.0;
        translationY = computeMedian();
    }

    if (direction == ScrollDirection::Down && translationY <= 0.0) {
        candidate.failureReason = "Direction mismatch";
        return candidate;
    }
    if (direction == ScrollDirection::Up && translationY >= 0.0) {
        candidate.failureReason = "Direction mismatch";
        return candidate;
    }

    int overlap = newFrame.height() - static_cast<int>(std::round(std::abs(translationY)));

    if (overlap <= 0) {
        candidate.confidence = 0.2;
        candidate.failureReason = QString("Negative overlap: %1 (offset too large)").arg(overlap);
        return candidate;
    }

    if (overlap < MIN_OVERLAP || overlap > MAX_OVERLAP) {
        candidate.confidence = 0.3;
        candidate.failureReason = QString("Invalid overlap: %1").arg(overlap);
        return candidate;
    }

    const double matchRatio = goodMatches.empty() ? 0.0
                                                  : static_cast<double>(inlierCount) / goodMatches.size();
    const double horizontalPenalty = 1.0 - std::min(std::abs(translationX) / std::max(1, lastWidth), 1.0);
    const double featureScore = std::min(candidate.matchedFeatures / 60.0, 1.0);
    candidate.confidence = 0.4 * matchRatio + 0.4 * horizontalPenalty + 0.2 * featureScore;

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

    // Convert to grayscale
    cv::Mat lastGray, newGray;
    cv::cvtColor(lastMat, lastGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(newMat, newGray, cv::COLOR_BGR2GRAY);

    int templateHeight = std::min(180, std::max(60, lastGray.rows / 3));
    int searchMaxExtent = std::min(MAX_OVERLAP + templateHeight + 40, newGray.rows);
    if (templateHeight <= 0 || searchMaxExtent <= 0) {
        candidate.failureReason = "Invalid template or search region";
        return candidate;
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

    // Ensure template fits in search region
    if (templateImg.cols > searchRegion.cols || templateImg.rows > searchRegion.rows) {
        candidate.failureReason = "Template larger than search region";
        return candidate;
    }

    auto preprocess = [](const cv::Mat &src) {
        cv::Mat equalized;
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
        clahe->apply(src, equalized);

        cv::Mat blurred;
        cv::GaussianBlur(equalized, blurred, cv::Size(3, 3), 0.8);

        cv::Mat laplacian, absLap;
        cv::Laplacian(blurred, laplacian, CV_16S, 3);
        cv::convertScaleAbs(laplacian, absLap);
        return absLap;
    };

    cv::Mat enhancedTemplate = preprocess(templateImg);
    cv::Mat enhancedSearch = preprocess(searchRegion);

    cv::Mat matchResult;
    cv::matchTemplate(enhancedSearch, enhancedTemplate, matchResult, cv::TM_CCOEFF_NORMED);

    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(matchResult, &minVal, &maxVal, &minLoc, &maxLoc);

    candidate.confidence = maxVal;

    if (maxVal < m_confidenceThreshold) {
        candidate.failureReason = QString("Template match confidence too low: %1").arg(maxVal);
        return candidate;
    }

    double fullOffset = templateTop - (searchTop + maxLoc.y);

    if (direction == ScrollDirection::Down && fullOffset <= 0.0) {
        candidate.failureReason = "Direction mismatch";
        return candidate;
    }
    if (direction == ScrollDirection::Up && fullOffset >= 0.0) {
        candidate.failureReason = "Direction mismatch";
        return candidate;
    }

    int overlap = newFrame.height() - static_cast<int>(std::round(std::abs(fullOffset)));

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
    result.success = true;
    result.direction = direction;

    QImage newFrameRgb = newFrame.convertToFormat(QImage::Format_RGB32);

    int currentHeight = m_validHeight > 0 ? m_validHeight : m_stitchedResult.height();

    // Clamp overlap to valid range (vertical stitching)
    overlapPixels = qBound(MIN_OVERLAP, overlapPixels,
                           qMin(currentHeight, newFrameRgb.height()) - 1);

    if (direction == ScrollDirection::Down) {
        // Vertical stitching - append new frame below, removing overlap
        int drawY = currentHeight - overlapPixels;
        int newHeight = drawY + newFrameRgb.height();

        if (newHeight > m_stitchedResult.height()) {
            // Need to grow
            // Calculate new capacity: at least 1.5x current or newHeight, with 4096 min step
            int newCapacity = std::max(newHeight, static_cast<int>(m_stitchedResult.height() * 1.5));
            newCapacity = std::max(newCapacity, newHeight + 4096);

            QImage newStitched(m_stitchedResult.width(), newCapacity, QImage::Format_RGB32);
            newStitched.fill(Qt::black);  // Initialize to avoid artifacts

            QPainter painter(&newStitched);
            // Copy only valid part of existing image
            painter.drawImage(0, 0, m_stitchedResult, 0, 0, m_stitchedResult.width(), currentHeight);
            painter.drawImage(0, drawY, newFrameRgb);
            painter.end();

            m_stitchedResult = newStitched;
        } else {
            // Capacity is sufficient, just draw the new part
            QPainter painter(&m_stitchedResult);
            painter.drawImage(0, drawY, newFrameRgb);
            painter.end();
        }

        // Calculate viewport rect BEFORE updating stitched result
        m_currentViewportRect = QRect(0, drawY, newFrameRgb.width(), newFrameRgb.height());
        m_validHeight = newHeight;
        result.offset = QPoint(0, drawY);
    } else {
        // Vertical stitching - prepend new frame above, removing overlap
        // For Up direction, we still have to move everything, so optimization is limited
        int drawY = newFrameRgb.height() - overlapPixels;
        int newHeight = drawY + currentHeight;

        QImage newStitched(m_stitchedResult.width(), newHeight, QImage::Format_RGB32);
        newStitched.fill(Qt::black);

        QPainter painter(&newStitched);
        painter.drawImage(0, 0, newFrameRgb);
        painter.drawImage(0, drawY, m_stitchedResult, 0, 0, m_stitchedResult.width(), currentHeight);
        painter.end();

        m_stitchedResult = newStitched;
        m_validHeight = newHeight;

        m_currentViewportRect = QRect(0, 0, newFrameRgb.width(), newFrameRgb.height());
        result.offset = QPoint(0, 0);
    }

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
