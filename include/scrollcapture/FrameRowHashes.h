#pragma once

#include <QImage>
#include <QVector>
#include <QtGlobal>

namespace snaptray::scrollcapture {

// One 64-bit hash per pixel row. Rows hash equal only when every pixel is
// bit-identical, so two frames of a lossless capture can be aligned by
// comparing rows instead of pixels. Images that are not 32 bits per pixel are
// converted to RGB32 first, so frames from the same source always agree.
QVector<quint64> computeRowHashes(const QImage& image);

// Returns `image` itself when it already has 32 bits per pixel, otherwise an
// RGB32 copy. Pixel comparisons between frames must use the same conversion.
QImage toComparableFormat(const QImage& image);

} // namespace snaptray::scrollcapture
