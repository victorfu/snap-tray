#include "ImageColorSpaceHelper.h"

#include <QScreen>

QImage convertImageForDisplay(const QImage& image)
{
    return image;
}

QImage tagImageWithScreenColorSpace(const QImage& image, const QScreen* sourceScreen)
{
    Q_UNUSED(sourceScreen);
    return image;
}

QImage normalizeImageForExport(const QImage& image, const QScreen* sourceScreen)
{
    if (image.isNull()) {
        return image;
    }

    QImage normalized = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    normalized.setDevicePixelRatio(1.0);
    return tagImageWithScreenColorSpace(normalized, sourceScreen);
}
