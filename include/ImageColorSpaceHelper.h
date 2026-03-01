#ifndef IMAGECOLORSPACEHELPER_H
#define IMAGECOLORSPACEHELPER_H

#include <QImage>

class QScreen;

// Convert an image to a display-ready color space for widget rendering.
// On macOS this uses CoreGraphics/ColorSync to align with the active screen.
// On other platforms this returns the original image.
QImage convertImageForDisplay(const QImage& image);

// Tag exported image with source display color space without changing pixel values.
// On macOS this preserves wide-gamut output metadata (for example Display P3).
// On other platforms this returns the original image.
QImage tagImageWithScreenColorSpace(const QImage& image, const QScreen* sourceScreen);

#endif // IMAGECOLORSPACEHELPER_H
