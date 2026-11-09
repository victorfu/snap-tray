#pragma once

#include <QImage>
#include <QRectF>

namespace OCRImageUtils {
// OCR engines consume physical pixels, independently of the source image DPR.
QImage fitForRecognition(const QImage& image, int maxDimension);
QRectF normalizedBoundingRect(const QRectF& pixelRect, const QSize& recognitionSize);
}
