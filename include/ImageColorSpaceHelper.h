#ifndef IMAGECOLORSPACEHELPER_H
#define IMAGECOLORSPACEHELPER_H

#include <QColorSpace>
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

// The colour space tagImageWithScreenColorSpace would give an untagged image,
// resolved now so the tag can be applied later off the GUI thread.
// On macOS this reads NSScreen and must run on the GUI thread; on other
// platforms it is an invalid (no-op) colour space.
QColorSpace exportColorSpaceForScreen(const QScreen* sourceScreen);

// Thread-safe counterpart of tagImageWithScreenColorSpace that takes a colour
// space resolved by exportColorSpaceForScreen: an already tagged image keeps
// its own colour space.
inline QImage tagImageWithColorSpace(const QImage& image, const QColorSpace& colorSpace)
{
    if (image.isNull() || image.colorSpace().isValid() || !colorSpace.isValid()) {
        return image;
    }
    QImage tagged = image;
    tagged.setColorSpace(colorSpace);
    return tagged;
}

// Normalize an exported image for clipboard/save flows.
// This removes device-pixel scaling metadata so GUI clipboard consumers treat the
// pixel buffer as an exact cropped image, then reapplies display color-space tags.
QImage normalizeImageForExport(const QImage& image, const QScreen* sourceScreen);

#endif // IMAGECOLORSPACEHELPER_H
