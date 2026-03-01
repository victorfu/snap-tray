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
