#include "scrolling/ImageStitcher.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>

#include <QDebug>
#include <QPainter>
#include <algorithm>
#include <cmath>

ImageStitcher::ImageStitcher(QObject *parent)
    : QObject(parent)
{
}

ImageStitcher::~ImageStitcher()
{
}

void ImageStitcher::setAlgorithm(Algorithm algo)
{
    m_algorithm = algo;
}

void ImageStitcher::setDetectFixedElements(bool enabled)
{
    m_detectFixedElements = enabled;
}

void ImageStitcher::setConfidenceThreshold(double threshold)
{
    m_confidenceThreshold = qBound(0.0, threshold, 1.0);
}

void ImageStitcher::reset()
{
    m_stitchedResult = QImage();
    m_lastFrame = QImage();
    m_currentViewportRect = QRect();
    m_frameCount = 0;
}

ImageStitcher::StitchResult ImageStitcher::addFrame(const QImage &frame)
{
    StitchResult result;

    if (frame.isNull()) {
        result.failureReason = "Empty frame";
        return result;
    }

    // First frame - just store it
    if (m_frameCount == 0) {
        m_stitchedResult = frame.convertToFormat(QImage::Format_RGB32);
        m_lastFrame = m_stitchedResult;
        m_frameCount = 1;
        m_currentViewportRect = QRect(0, 0, frame.width(), frame.height());

        result.success = true;
        result.confidence = 1.0;
        result.matchedFeatures = 0;
        result.usedAlgorithm = Algorithm::ORB;

        emit progressUpdated(m_frameCount, getCurrentSize());
        return result;
    }

    // Check if frame has changed enough - skip stitching but not a failure
    if (!hasFrameChanged(frame, m_lastFrame)) {
        result.success = false;  // Not a successful stitch (no new content)
        result.confidence = 1.0;
        result.failureReason = "Frame unchanged";
        return result;
    }

    // Try matching algorithms based on settings
    if (m_algorithm == Algorithm::Auto) {
        // Try ORB first
        result = tryORBMatch(frame);
        if (result.success && result.confidence >= m_confidenceThreshold) {
            m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
            m_frameCount++;
            emit progressUpdated(m_frameCount, getCurrentSize());
            return result;
        }

        // Try template matching as fallback
        result = tryTemplateMatch(frame);
        if (result.success && result.confidence >= m_confidenceThreshold) {
            m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
            m_frameCount++;
            emit progressUpdated(m_frameCount, getCurrentSize());
            return result;
        }

        // Both failed
        result.success = false;
        result.failureReason = "No matching algorithm succeeded";
        return result;
    }
    else if (m_algorithm == Algorithm::ORB) {
        result = tryORBMatch(frame);
    }
    else if (m_algorithm == Algorithm::TemplateMatching) {
        result = tryTemplateMatch(frame);
    }
    else {
        result = tryORBMatch(frame);
    }

    if (result.success) {
        m_lastFrame = frame.convertToFormat(QImage::Format_RGB32);
        m_frameCount++;
        emit progressUpdated(m_frameCount, getCurrentSize());
    }

    return result;
}

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

    // ORB detection
    cv::Ptr<cv::ORB> orb = cv::ORB::create(500);
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
        double y1 = kp1[m.queryIdx].pt.y + roiLast.y;
        double y2 = kp2[m.trainIdx].pt.y + roiNew.y;
        offsets.push_back(y1 - y2);
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

    if (direction == ScrollDirection::Down && medianOffset <= 0.0) {
        candidate.failureReason = "Direction mismatch";
        return candidate;
    }
    if (direction == ScrollDirection::Up && medianOffset >= 0.0) {
        candidate.failureReason = "Direction mismatch";
        return candidate;
    }

    int overlap = newFrame.height() - static_cast<int>(std::round(std::abs(medianOffset)));

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

    int inlierCount = 0;
    for (double val : offsets) {
        if (std::abs(val - medianOffset) < 5.0) {
            inlierCount++;
        }
    }
    candidate.confidence = offsets.empty() ? 0.0 : static_cast<double>(inlierCount) / offsets.size();

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

    int templateHeight = std::min(150, lastGray.rows / 3);
    int searchMaxExtent = std::min(MAX_OVERLAP + templateHeight, newGray.rows);
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

    cv::Mat matchResult;
    cv::matchTemplate(searchRegion, templateImg, matchResult, cv::TM_CCOEFF_NORMED);

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

    // Clamp overlap to valid range (vertical stitching)
    overlapPixels = qBound(MIN_OVERLAP, overlapPixels,
                           qMin(m_stitchedResult.height(), newFrameRgb.height()) - 1);

    QImage newStitched;
    if (direction == ScrollDirection::Down) {
        // Vertical stitching - append new frame below, removing overlap
        int drawY = m_stitchedResult.height() - overlapPixels;
        int newHeight = drawY + newFrameRgb.height();

        newStitched = QImage(m_stitchedResult.width(), newHeight, QImage::Format_RGB32);
        newStitched.fill(Qt::black);  // Initialize to avoid artifacts

        QPainter painter(&newStitched);
        painter.drawImage(0, 0, m_stitchedResult);
        painter.drawImage(0, drawY, newFrameRgb);
        painter.end();

        // Calculate viewport rect BEFORE updating stitched result
        m_currentViewportRect = QRect(0, drawY, newFrameRgb.width(), newFrameRgb.height());
        result.offset = QPoint(0, drawY);
    } else {
        // Vertical stitching - prepend new frame above, removing overlap
        int drawY = newFrameRgb.height() - overlapPixels;
        int newHeight = drawY + m_stitchedResult.height();

        newStitched = QImage(m_stitchedResult.width(), newHeight, QImage::Format_RGB32);
        newStitched.fill(Qt::black);

        QPainter painter(&newStitched);
        painter.drawImage(0, 0, newFrameRgb);
        painter.drawImage(0, drawY, m_stitchedResult);
        painter.end();

        m_currentViewportRect = QRect(0, 0, newFrameRgb.width(), newFrameRgb.height());
        result.offset = QPoint(0, 0);
    }

    m_stitchedResult = newStitched;

    return result;
}

bool ImageStitcher::hasFrameChanged(const QImage &frame1, const QImage &frame2) const
{
    if (frame1.size() != frame2.size()) {
        return true;
    }

    static constexpr int kSampleSize = 64;
    static constexpr int kPixelDiffThreshold = 20;
    static constexpr double kAverageDiffThreshold = 2.0;
    static constexpr double kChangedRatioThreshold = 0.005;

    QImage small1 = frame1.scaled(kSampleSize, kSampleSize, Qt::IgnoreAspectRatio,
                                  Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    QImage small2 = frame2.scaled(kSampleSize, kSampleSize, Qt::IgnoreAspectRatio,
                                  Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);

    if (small1.size() != small2.size()) {
        return true;
    }

    const int pixelCount = kSampleSize * kSampleSize;
    long long diffSum = 0;
    int changedPixels = 0;

    for (int y = 0; y < kSampleSize; ++y) {
        const QRgb *line1 = reinterpret_cast<const QRgb*>(small1.constScanLine(y));
        const QRgb *line2 = reinterpret_cast<const QRgb*>(small2.constScanLine(y));
        for (int x = 0; x < kSampleSize; ++x) {
            int diff = std::abs(qRed(line1[x]) - qRed(line2[x])) +
                       std::abs(qGreen(line1[x]) - qGreen(line2[x])) +
                       std::abs(qBlue(line1[x]) - qBlue(line2[x]));
            diffSum += diff;
            if (diff > kPixelDiffThreshold) {
                changedPixels++;
            }
        }
    }

    double averageDiff = static_cast<double>(diffSum) / (pixelCount * 3);
    double changedRatio = static_cast<double>(changedPixels) / pixelCount;

    return averageDiff > kAverageDiffThreshold || changedRatio > kChangedRatioThreshold;
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

QImage ImageStitcher::getStitchedImage() const
{
    return m_stitchedResult;
}

QSize ImageStitcher::getCurrentSize() const
{
    return m_stitchedResult.size();
}
