#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace SnapTray::BeautifyBackgroundImage {

// Only the selected background retains its encoded original. Preview cache
// entries contain downsampled pixels and are bounded by both count and bytes.
struct Preview {
    QByteArray original;
    QImage thumbnail;
    QSize originalSize; // after applying the file orientation
};

// The encoded file, or empty when it is missing, unreadable or too large.
QByteArray readOriginal(const QString& path);
Preview loadPreview(const QString& path);
Preview previewFromOriginal(const QByteArray& original);
// maximumPixels > 0 area-samples the image down to at most that many pixels.
QImage decode(const QByteArray& original, qint64 maximumPixels = 0, QSize* originalSize = nullptr);
qsizetype cachedBytes();

} // namespace SnapTray::BeautifyBackgroundImage
