#ifndef SCROLLSEAMBLENDER_H
#define SCROLLSEAMBLENDER_H

#include <QImage>
#include <QString>

namespace cv {
class Mat;
}

struct ScrollSeamBlendOptions {
    int seamSearchWindowPx = 64;
    int seamWidth = 24;
    double featherSigma = 8.0;
    bool usePoissonByDefault = false;
};

struct ScrollSeamBlendResult {
    bool ok = false;
    bool poissonApplied = false;
    double dynamicMaskCoverage = 0.0;
    QString reason;
    QImage blendedTopBand;
};

class ScrollSeamBlender
{
public:
    explicit ScrollSeamBlender(const ScrollSeamBlendOptions &options = ScrollSeamBlendOptions());

    int findMinimumEnergySeamRow(const QImage &previousOverlapBand,
                                 const QImage &currentOverlapBand) const;

    ScrollSeamBlendResult blendTopBand(const QImage &previousBottomBand,
                                       const QImage &currentTopBand,
                                       const cv::Mat &weights) const;

private:
    ScrollSeamBlendOptions m_options;
};

#endif // SCROLLSEAMBLENDER_H
