#pragma once

#include <QImage>
#include <QVector>
#include <QtGlobal>

namespace snaptray::scrollcapture {

// One 64-bit hash per pixel row. Rows hash equal only when every compared pixel is
// bit-identical, so two frames of a lossless capture can be aligned by
// comparing rows instead of pixels. Images that are not 32 bits per pixel are
// converted to RGB32 first, so frames from the same source always agree.
// horizontalInset is in physical pixels and excludes a narrow viewport border
// from matching, not from the captured image. Invalid/empty interiors return no
// hashes. Callers must use the same inset for both frames.
QVector<quint64> computeRowHashes(const QImage& image, int horizontalInset = 0);

// Returns `image` itself when it already has 32 bits per pixel, otherwise an
// RGB32 copy. Pixel comparisons between frames must use the same conversion.
QImage toComparableFormat(const QImage& image);

} // namespace snaptray::scrollcapture
