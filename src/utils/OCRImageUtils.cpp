#include "utils/OCRImageUtils.h"

QImage OCRImageUtils::fitForRecognition(const QImage& image, int maxDimension)
{
    if (image.isNull() || maxDimension <= 0) {
        return {};
    }
    QImage result = image;
    if (image.width() > maxDimension || image.height() > maxDimension) {
        const QSize size = image.size().scaled(QSize(maxDimension, maxDimension), Qt::KeepAspectRatio)
                               .expandedTo(QSize(1, 1));
        result = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    result.setDevicePixelRatio(1.0);
    return result;
}

QRectF OCRImageUtils::normalizedBoundingRect(const QRectF& pixelRect, const QSize& recognitionSize)
{
    if (recognitionSize.isEmpty()) {
        return {};
    }
    return QRectF(pixelRect.x() / recognitionSize.width(), pixelRect.y() / recognitionSize.height(),
                  pixelRect.width() / recognitionSize.width(), pixelRect.height() / recognitionSize.height())
        .normalized();
}
