#pragma once

#include <QImage>
#include <QTransform>

namespace SnapTray {
// Qt 6.12 does not apply PNG eXIf orientation.
QTransform pngOrientation(const QByteArray& encoded);

// Area-sample a PNG by scanline, retaining the alpha and source color space.
QImage readPngThumbnail(const QByteArray& encoded, QSize target);
}
