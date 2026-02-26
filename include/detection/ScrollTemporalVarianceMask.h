#ifndef SCROLLTEMPORALVARIANCEMASK_H
#define SCROLLTEMPORALVARIANCEMASK_H

#include <opencv2/core/mat.hpp>

struct ScrollTemporalMaskResult {
    cv::Mat alignmentMask; // CV_8UC1: 255 = usable, 0 = masked out
    int stickyHeaderPx = 0;
    int stickyFooterPx = 0;
    int rightRailPx = 0;
};

class ScrollTemporalVarianceMask
{
public:
    static ScrollTemporalMaskResult buildAlignmentMask(const cv::Mat &olderGray,
                                                       const cv::Mat &previousGray,
                                                       const cv::Mat &currentGray,
                                                       double varianceThreshold = 0.015,
                                                       int rightStripWidthPx = 20,
                                                       int minStableRows = 4);

    static cv::Mat applyMask(const cv::Mat &gray, const cv::Mat &alignmentMask);
    static double maskedCoverage(const cv::Mat &alignmentMask);

    // Legacy APIs kept for compatibility while gradually migrating call sites.
    static cv::Mat buildWeights(const cv::Mat &previousGray,
                                const cv::Mat &currentGray,
                                double tvThreshold = 0.015,
                                double dynamicWeight = 0.2);

    static double dynamicCoverage(const cv::Mat &weights,
                                  double dynamicWeight = 0.2);
};

#endif // SCROLLTEMPORALVARIANCEMASK_H
