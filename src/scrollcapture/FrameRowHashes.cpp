#include "scrollcapture/FrameRowHashes.h"

namespace snaptray::scrollcapture {

namespace {

constexpr quint64 kFnvOffsetBasis = 14695981039346656037ULL;
constexpr quint64 kFnvPrime = 1099511628211ULL;
constexpr int kComparableDepth = 32;

quint64 hashBytes(const uchar* data, qsizetype size)
{
    quint64 hash = kFnvOffsetBasis;
    for (qsizetype i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

QImage toComparableFormat(const QImage& image)
{
    if (image.isNull() || image.depth() == kComparableDepth) {
        return image;
    }
    return image.convertToFormat(QImage::Format_RGB32);
}

QVector<quint64> computeRowHashes(const QImage& image, int horizontalInset)
{
    const QImage comparable = toComparableFormat(image);
    QVector<quint64> hashes;
    if (comparable.isNull() || horizontalInset < 0
        || horizontalInset > (comparable.width() - 1) / 2) {
        return hashes;
    }

    // Hash only the visible pixels: scanline padding is undefined memory.
    const qsizetype pixelBytes = kComparableDepth / 8;
    const qsizetype rowBytes = static_cast<qsizetype>(comparable.width() - 2 * horizontalInset) * pixelBytes;
    const qsizetype offset = static_cast<qsizetype>(horizontalInset) * pixelBytes;
    hashes.reserve(comparable.height());
    for (int y = 0; y < comparable.height(); ++y) {
        hashes.append(hashBytes(comparable.constScanLine(y) + offset, rowBytes));
    }
    return hashes;
}

} // namespace snaptray::scrollcapture
