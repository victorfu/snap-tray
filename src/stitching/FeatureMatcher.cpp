#include "stitching/FeatureMatcher.h"

#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <QDebug>
#include <QPointF>
#include <cmath>
#include <algorithm>

class FeatureMatcher::Private
{
public:
    Config config;

    cv::Ptr<cv::ORB> orb;
    cv::Ptr<cv::BFMatcher> matcher;

    void initialize();

    std::vector<cv::DMatch> ratioTest(
        const std::vector<std::vector<cv::DMatch>> &knnMatches);

    double computeConfidence(int inliers, int matches, double reprojError);
};

void FeatureMatcher::Private::initialize()
{
    orb = cv::ORB::create(
        config.maxFeatures,
        1.2f,                    // scaleFactor
        8,                       // nlevels
        31,                      // edgeThreshold
        0,                       // firstLevel
        2,                       // WTA_K
        cv::ORB::HARRIS_SCORE,
        31,                      // patchSize
        20                       // fastThreshold
    );

    // BFMatcher with Hamming distance for binary descriptors (ORB)
    matcher = cv::BFMatcher::create(cv::NORM_HAMMING, false);
}

std::vector<cv::DMatch> FeatureMatcher::Private::ratioTest(
    const std::vector<std::vector<cv::DMatch>> &knnMatches)
{
    std::vector<cv::DMatch> goodMatches;

    for (const auto &knn : knnMatches) {
        if (knn.size() >= 2) {
            // Lowe's ratio test: reject ambiguous matches
            if (knn[0].distance < config.ratioThreshold * knn[1].distance) {
                goodMatches.push_back(knn[0]);
            }
        } else if (knn.size() == 1) {
            // Only one match, include if distance is reasonable
            // Hamming distance for ORB typically < 64 for good matches
            if (knn[0].distance < 64) {
                goodMatches.push_back(knn[0]);
            }
        }
    }

    return goodMatches;
}

double FeatureMatcher::Private::computeConfidence(
    int inliers, int matches, double reprojError)
{
    if (matches == 0)
        return 0.0;

    // Inlier ratio component (0-0.5)
    double inlierRatio = static_cast<double>(inliers) / matches;
    double inlierScore = std::min(0.5, inlierRatio * 0.6);

    // Absolute inlier count component (0-0.3)
    // More inliers = higher confidence
    double inlierCountScore = std::min(0.3, inliers / 100.0 * 0.3);

    // Reprojection error component (0-0.2)
    // Lower error = higher confidence
    double errorScore = std::max(0.0, 0.2 - reprojError / 10.0 * 0.2);

    return inlierScore + inlierCountScore + errorScore;
}

// === Public Implementation ===

FeatureMatcher::FeatureMatcher()
    : d(new Private)
{
    d->initialize();
}

FeatureMatcher::~FeatureMatcher()
{
    delete d;
}

FeatureMatcher::FeatureMatcher(FeatureMatcher &&other) noexcept
    : d(other.d)
{
    other.d = nullptr;
}

FeatureMatcher &FeatureMatcher::operator=(FeatureMatcher &&other) noexcept
{
    if (this != &other) {
        delete d;
        d = other.d;
        other.d = nullptr;
    }
    return *this;
}

void FeatureMatcher::setConfig(const Config &config)
{
    d->config = config;
    d->initialize(); // Reinitialize ORB with new settings
}

FeatureMatcher::Config FeatureMatcher::config() const
{
    return d->config;
}

FeatureMatcher::MatchResult FeatureMatcher::match(
    const cv::Mat &source, const cv::Mat &target)
{
    MatchResult result;

    if (source.empty() || target.empty()) {
        result.failureReason = QStringLiteral("Empty input images");
        return result;
    }

    // Extract ROIs
    int sourceROIHeight = static_cast<int>(source.rows * d->config.sourceROIRatio);
    int targetROIHeight = static_cast<int>(target.rows * d->config.targetROIRatio);

    // Ensure minimum ROI height
    sourceROIHeight = std::max(sourceROIHeight, 50);
    targetROIHeight = std::max(targetROIHeight, 50);

    // Clamp to frame bounds
    sourceROIHeight = std::min(sourceROIHeight, source.rows);
    targetROIHeight = std::min(targetROIHeight, target.rows);

    cv::Rect sourceRect(0, source.rows - sourceROIHeight, source.cols, sourceROIHeight);
    cv::Rect targetRect(0, 0, target.cols, targetROIHeight);

    cv::Mat sourceROI = source(sourceRect);
    cv::Mat targetROI = target(targetRect);

    int sourceOffsetY = source.rows - sourceROIHeight;
    int targetOffsetY = 0;

    return matchROI(sourceROI, targetROI, sourceOffsetY, targetOffsetY);
}

FeatureMatcher::MatchResult FeatureMatcher::matchROI(
    const cv::Mat &sourceROI, const cv::Mat &targetROI,
    int sourceOffsetY, int targetOffsetY)
{
    MatchResult result;

    // Convert to grayscale if needed
    cv::Mat sourceGray, targetGray;
    if (sourceROI.channels() > 1) {
        cv::cvtColor(sourceROI, sourceGray, cv::COLOR_BGR2GRAY);
    } else {
        sourceGray = sourceROI;
    }

    if (targetROI.channels() > 1) {
        cv::cvtColor(targetROI, targetGray, cv::COLOR_BGR2GRAY);
    } else {
        targetGray = targetROI;
    }

    // Detect keypoints and compute descriptors
    std::vector<cv::KeyPoint> kp1, kp2;
    cv::Mat desc1, desc2;

    d->orb->detectAndCompute(sourceGray, cv::noArray(), kp1, desc1);
    d->orb->detectAndCompute(targetGray, cv::noArray(), kp2, desc2);

    if (kp1.size() < 10 || kp2.size() < 10) {
        result.failureReason = QString("Insufficient keypoints: source=%1, target=%2")
                                   .arg(kp1.size())
                                   .arg(kp2.size());
        return result;
    }

    // KNN matching (k=2 for ratio test)
    std::vector<std::vector<cv::DMatch>> knnMatches;
    d->matcher->knnMatch(desc1, desc2, knnMatches, 2);

    // Apply Lowe's ratio test
    std::vector<cv::DMatch> goodMatches = d->ratioTest(knnMatches);

    result.numMatches = static_cast<int>(goodMatches.size());

    if (goodMatches.size() < static_cast<size_t>(d->config.minInliers)) {
        result.failureReason = QString("Insufficient matches after ratio test: %1")
                                   .arg(goodMatches.size());
        return result;
    }

    // Extract matched point coordinates
    std::vector<cv::Point2f> srcPts, dstPts;
    srcPts.reserve(goodMatches.size());
    dstPts.reserve(goodMatches.size());

    for (const auto &m : goodMatches) {
        srcPts.push_back(kp1[m.queryIdx].pt);
        dstPts.push_back(kp2[m.trainIdx].pt);
    }

    // RANSAC homography estimation
    std::vector<unsigned char> inlierMask;
    cv::Mat H = cv::findHomography(
        srcPts, dstPts,
        cv::RANSAC,
        d->config.ransacThreshold,
        inlierMask);

    if (H.empty()) {
        result.failureReason = QStringLiteral("Homography estimation failed");
        return result;
    }

    // Count inliers
    result.numInliers = cv::countNonZero(inlierMask);

    if (result.numInliers < d->config.minInliers) {
        result.failureReason = QString("Insufficient inliers: %1")
                                   .arg(result.numInliers);
        return result;
    }

    // Store inlier points for debug visualization
    for (size_t i = 0; i < srcPts.size(); i++) {
        if (inlierMask[i]) {
            // Adjust points back to original frame coordinates and convert to QPointF
            QPointF srcAdjusted(srcPts[i].x, srcPts[i].y + sourceOffsetY);
            QPointF dstAdjusted(dstPts[i].x, dstPts[i].y + targetOffsetY);
            result.srcPoints.push_back(srcAdjusted);
            result.dstPoints.push_back(dstAdjusted);
        }
    }
    result.inlierMask = inlierMask;

    // Compute reprojection error for inliers
    double totalError = 0.0;
    int inlierCount = 0;
    for (size_t i = 0; i < srcPts.size(); i++) {
        if (inlierMask[i]) {
            // Project source point through homography
            cv::Mat pt1 = (cv::Mat_<double>(3, 1) << srcPts[i].x, srcPts[i].y, 1.0);
            cv::Mat projected = H * pt1;
            projected /= projected.at<double>(2, 0);

            double dx = projected.at<double>(0, 0) - dstPts[i].x;
            double dy = projected.at<double>(1, 0) - dstPts[i].y;
            totalError += std::sqrt(dx * dx + dy * dy);
            inlierCount++;
        }
    }
    result.reprojectionError = inlierCount > 0 ? totalError / inlierCount : 999.0;

    // Extract transformation parameters from homography
    // H = [h00 h01 h02]
    //     [h10 h11 h12]
    //     [h20 h21 h22]
    // For near-pure translation: dx ≈ h02, dy ≈ h12

    // Translation (account for ROI offsets)
    // The homography maps source ROI to target ROI
    // We need the translation from source frame to target frame
    result.dx = H.at<double>(0, 2);

    // dy in ROI space + adjustment for frame coordinates
    // Target ROI starts at targetOffsetY in target frame
    // Source ROI starts at sourceOffsetY in source frame
    // True vertical offset = homography_dy + (sourceOffsetY - targetOffsetY)
    result.dy = H.at<double>(1, 2) + (sourceOffsetY - targetOffsetY);

    // Extract rotation (approximate for small angles)
    // For affine-like transforms: rotation = atan2(h10, h00)
    result.rotation = std::atan2(H.at<double>(1, 0), H.at<double>(0, 0));

    // Extract scale
    // Scale = sqrt(h00^2 + h10^2) for first column
    result.scale = std::sqrt(
        H.at<double>(0, 0) * H.at<double>(0, 0) +
        H.at<double>(1, 0) * H.at<double>(1, 0));

    // Compute confidence score
    result.confidence = d->computeConfidence(
        result.numInliers, result.numMatches, result.reprojectionError);

    if (result.confidence < d->config.minConfidence) {
        result.failureReason = QString("Low confidence: %1")
                                   .arg(result.confidence, 0, 'f', 3);
        return result;
    }

    result.success = true;

    qDebug() << "FeatureMatcher: Success"
             << "dx:" << result.dx
             << "dy:" << result.dy
             << "rotation:" << (result.rotation * 180.0 / M_PI) << "deg"
             << "scale:" << result.scale
             << "inliers:" << result.numInliers << "/" << result.numMatches
             << "confidence:" << result.confidence;

    return result;
}

cv::Mat FeatureMatcher::drawMatches(
    const cv::Mat &source, const cv::Mat &target,
    const MatchResult &result)
{
    // Create side-by-side canvas
    cv::Mat canvas;
    cv::hconcat(source, target, canvas);

    int offsetX = source.cols;

    // Draw matches as lines between source and target
    for (size_t i = 0; i < result.srcPoints.size(); i++) {
        // Convert QPointF to cv::Point for drawing
        cv::Point src(static_cast<int>(result.srcPoints[i].x()),
                      static_cast<int>(result.srcPoints[i].y()));
        cv::Point dst(static_cast<int>(result.dstPoints[i].x()) + offsetX,
                      static_cast<int>(result.dstPoints[i].y()));

        // Green for inliers, red for outliers (if mask provided)
        cv::Scalar color = (i < result.inlierMask.size() && result.inlierMask[i])
                               ? cv::Scalar(0, 255, 0)
                               : cv::Scalar(0, 0, 255);

        cv::circle(canvas, src, 3, color, -1);
        cv::circle(canvas, dst, 3, color, -1);
        cv::line(canvas, src, dst, color, 1);
    }

    return canvas;
}
