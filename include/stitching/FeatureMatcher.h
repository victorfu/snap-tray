#ifndef FEATUREMATCHER_H
#define FEATUREMATCHER_H

#include <QString>
#include <QPointF>
#include <vector>

namespace cv {
class Mat;
}

/**
 * @brief ORB-based feature matching with RANSAC homography estimation
 *
 * Replaces 1D Row Projection with 2D feature matching for robust
 * image registration. Handles:
 * - Non-linear motion (rotation, scale)
 * - Horizontal drift (dx != 0)
 * - Periodic patterns (spreadsheets)
 *
 * Performance: ~5ms for 1920x1080 on CPU
 * Accuracy: Sub-pixel with homography refinement
 */
class FeatureMatcher
{
public:
    struct Config {
        int maxFeatures = 2000;           // ORB features to detect
        float ratioThreshold = 0.75f;     // Lowe's ratio test threshold
        int ransacThreshold = 3;          // RANSAC reprojection threshold (pixels)
        int minInliers = 20;              // Minimum inliers for valid match
        double minConfidence = 0.6;       // Minimum confidence score
        bool refineSubPixel = true;       // Enable sub-pixel refinement

        // ROI configuration (where to look for matches)
        float sourceROIRatio = 0.35f;     // Bottom 35% of source
        float targetROIRatio = 0.35f;     // Top 35% of target
    };

    struct MatchResult {
        bool success = false;

        // Transform (sub-pixel precision)
        double dx = 0.0;                  // Horizontal translation
        double dy = 0.0;                  // Vertical translation
        double rotation = 0.0;            // Rotation angle (radians)
        double scale = 1.0;               // Scale factor

        // Quality metrics
        double confidence = 0.0;          // 0.0 - 1.0
        int numInliers = 0;
        int numMatches = 0;
        double reprojectionError = 0.0;

        // Debug info
        std::vector<QPointF> srcPoints;
        std::vector<QPointF> dstPoints;
        std::vector<unsigned char> inlierMask;

        QString failureReason;
    };

    FeatureMatcher();
    ~FeatureMatcher();

    // Non-copyable but movable
    FeatureMatcher(const FeatureMatcher &) = delete;
    FeatureMatcher &operator=(const FeatureMatcher &) = delete;
    FeatureMatcher(FeatureMatcher &&other) noexcept;
    FeatureMatcher &operator=(FeatureMatcher &&other) noexcept;

    void setConfig(const Config &config);
    Config config() const;

    /**
     * @brief Match two frames and compute transformation
     *
     * @param source Previous frame (will match bottom portion)
     * @param target Current frame (will match top portion)
     * @return MatchResult with translation and quality metrics
     */
    MatchResult match(const cv::Mat &source, const cv::Mat &target);

    /**
     * @brief Match with explicit ROI specification
     *
     * @param sourceROI Region from source frame
     * @param targetROI Region from target frame
     * @param sourceOffset Offset of sourceROI in original frame
     * @param targetOffset Offset of targetROI in original frame
     */
    MatchResult matchROI(const cv::Mat &sourceROI, const cv::Mat &targetROI,
                         int sourceOffsetY, int targetOffsetY);

    /**
     * @brief Draw matches for debugging
     *
     * @param source Source frame
     * @param target Target frame
     * @param result Match result from match()
     * @return cv::Mat with visualized matches
     */
    cv::Mat drawMatches(const cv::Mat &source, const cv::Mat &target,
                        const MatchResult &result);

private:
    class Private;
    Private *d;
};

#endif // FEATUREMATCHER_H
