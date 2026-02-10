#include "utils/MatConverter.h"

#include <QDebug>

#include <opencv2/imgproc.hpp>

namespace MatConverter {

cv::Mat toMat(QImage& image)
{
    if (image.isNull() || image.format() != QImage::Format_RGB32) {
        qWarning() << "MatConverter::toMat: expected non-null Format_RGB32, got format"
                    << image.format();
        return {};
    }
    return cv::Mat(image.height(), image.width(), CV_8UC4,
                   image.bits(),
                   static_cast<size_t>(image.bytesPerLine()));
}

cv::Mat toMatCopy(const QImage& image)
{
    QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));
    return mat.clone();
}

cv::Mat toGray(const QImage& image)
{
    if (image.isNull()) {
        qWarning() << "MatConverter::toGray: received null QImage";
        return {};
    }
    QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    cv::Mat mat(rgb.height(), rgb.width(), CV_8UC4,
                const_cast<uchar*>(rgb.bits()),
                static_cast<size_t>(rgb.bytesPerLine()));
    cv::Mat gray;
    cv::cvtColor(mat, gray, cv::COLOR_BGRA2GRAY);
    return gray;
}

QImage toQImage(const cv::Mat& mat)
{
    if (mat.type() == CV_8UC4) {
        return QImage(mat.data, mat.cols, mat.rows,
                      static_cast<int>(mat.step),
                      QImage::Format_RGB32).copy();
    }
    if (mat.type() == CV_8UC1) {
        return QImage(mat.data, mat.cols, mat.rows,
                      static_cast<int>(mat.step),
                      QImage::Format_Grayscale8).copy();
    }
    qWarning() << "MatConverter::toQImage: unsupported Mat type" << mat.type();
    return {};
}

} // namespace MatConverter
