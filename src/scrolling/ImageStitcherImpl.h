#ifndef IMAGESTITCHERIMPL_H
#define IMAGESTITCHERIMPL_H

// Private implementation header for ImageStitcher
// This file should NOT be included outside of src/scrolling/

#include "../../include/scrolling/ImageStitcher.h"
#include <opencv2/core.hpp>

// PIMPL implementation for ImageStitcher frame caching
struct ImageStitcher::FrameCacheImpl {
    cv::Mat bgr;       // BGR Mat (from QImage)
    cv::Mat gray;      // Grayscale (for most algorithms)
    bool valid = false;

    void clear() {
        bgr = cv::Mat();
        gray = cv::Mat();
        valid = false;
    }
};

#endif // IMAGESTITCHERIMPL_H
