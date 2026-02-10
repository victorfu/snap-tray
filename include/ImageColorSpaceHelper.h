#ifndef IMAGECOLORSPACEHELPER_H
#define IMAGECOLORSPACEHELPER_H

#include <QImage>

// Convert an image to a display-ready color space for widget rendering.
// On macOS this uses CoreGraphics/ColorSync to align with the active screen.
// On other platforms this returns the original image.
QImage convertImageForDisplay(const QImage& image);

#endif // IMAGECOLORSPACEHELPER_H
