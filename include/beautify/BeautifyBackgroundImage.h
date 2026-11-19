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

Preview loadPreview(const QString& path);
QImage decode(const QByteArray& original, int maximumSide = 0, QSize* originalSize = nullptr);
qsizetype cachedBytes();

} // namespace SnapTray::BeautifyBackgroundImage
