#ifndef SNAPTRAY_MATCONVERTER_H
#define SNAPTRAY_MATCONVERTER_H

#include <QImage>

#include <opencv2/core.hpp>

// QImage ↔ cv::Mat conversion utilities.
//
// Channel order rationale: Qt's Format_RGB32 stores pixels as 0xAARRGGBB,
// which on little-endian architectures (x86, ARM) gives byte order B-G-R-A.
// This matches OpenCV's default 4-channel layout (CV_8UC4 / BGRA), so we
// can wrap QImage data directly in a cv::Mat without any cvtColor conversion.

namespace MatConverter {

// Wraps a Format_RGB32 QImage buffer as a CV_8UC4 Mat (no pixel copy).
// Writes to the returned Mat modify the QImage's underlying buffer.
// The image must be Format_RGB32 and must outlive the returned Mat.
cv::Mat toMat(QImage& image);

// Returns a deep-copy CV_8UC4 Mat, safe to use after the QImage is destroyed.
cv::Mat toMatCopy(const QImage& image);

// Converts a QImage to a single-channel grayscale Mat (CV_8UC1).
cv::Mat toGray(const QImage& image);

// Converts a cv::Mat to QImage. Supports CV_8UC4 (→ Format_RGB32)
// and CV_8UC1 (→ Format_Grayscale8). Returns a deep copy.
QImage toQImage(const cv::Mat& mat);

} // namespace MatConverter

#endif // SNAPTRAY_MATCONVERTER_H
