#ifndef SCROLLALIGNMENTENGINE_H
#define SCROLLALIGNMENTENGINE_H

#include <QImage>

#include "detection/ScrollAlignmentTypes.h"

namespace cv {
class Mat;
}

class ScrollAlignmentEngine
{
public:
    explicit ScrollAlignmentEngine(const ScrollAlignmentOptions &options = ScrollAlignmentOptions());

    ScrollAlignmentResult align(const QImage &previous,
                                const QImage &current,
                                int estimatedDyPx = 0) const;

private:
    ScrollAlignmentResult runTemplateNcc1D(const cv::Mat &previousGray,
                                           const cv::Mat &currentGray,
                                           int estimatedDyPx) const;

    ScrollAlignmentOptions m_options;
};

#endif // SCROLLALIGNMENTENGINE_H
