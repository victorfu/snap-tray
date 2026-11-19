#include "beautify/BeautifyBackgroundImage.h"

#include "PngThumbnail.h"

#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>

#include <cmath>

namespace SnapTray::BeautifyBackgroundImage {
namespace {
constexpr qint64 kMaxEncodedBytes = 256 * 1024 * 1024;
// A pixel budget rather than a longest side: the preview crops the image to
// cover the canvas, and a very wide or tall image scaled by its longest side
// would collapse that crop to a pixel or two mixed with what lies outside it.
// Decoding is already capped by Qt's allocation limit, so at the default
// limit the preview is at most eight times smaller than the source per side.
constexpr qint64 kPreviewPixels = 1024 * 1024;
constexpr qsizetype kMaxEntries = 12;
constexpr qsizetype kCacheBudget = 24 * 1024 * 1024;

struct Cache {
    QMutex mutex;
    QCache<QByteArray, Preview> images{kCacheBudget};
};

Cache& cache()
{
    static Cache value;
    return value;
}
} // namespace

QImage decode(const QByteArray& original, qint64 maximumPixels, QSize* originalSize)
{
    QBuffer buffer;
    buffer.setData(original);
    if (!buffer.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&buffer);
    const auto format = reader.format();
    const bool png = format == "png";
    if (!png && format != "jpeg") return {};
    reader.setAutoTransform(!png);
    const QTransform orientation = png ? pngOrientation(original) : QTransform();
    const QSize size = reader.size();
    if (size.isEmpty()) return {};
    if (originalSize) {
        *originalSize = png ? orientation.mapRect(QRect(QPoint(), size)).size()
            : (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90) ? size.transposed() : size);
    }
    // A thumbnail may fit even when the full image is too large for Qt to
    // export. Apply its full-decode limit to both tiers before enabling Save.
    const int limitMiB = QImageReader::allocationLimit();
    const int bits = qMax(32, int(QImage::toPixelFormat(reader.imageFormat()).bitsPerPixel()));
    const qint64 rowBytes = ((qint64(size.width()) * bits + 31) / 32) * 4;
    if (limitMiB > 0 && rowBytes > (qint64(limitMiB) * 1024 * 1024) / size.height()) return {};
    const qint64 pixels = qint64(size.width()) * size.height();
    if (maximumPixels > 0 && pixels > maximumPixels) {
        constexpr double kRoundingSlack = 1e-9;
        const double factor = std::sqrt(double(maximumPixels) / double(pixels));
        const QSize scaled = QSize(int(std::floor(size.width() * factor + kRoundingSlack)),
                                   int(std::floor(size.height() * factor + kRoundingSlack))).expandedTo(QSize(1, 1));
        if (png) return readPngThumbnail(original, scaled).transformed(orientation);
        reader.setScaledSize(scaled);
    }
    return reader.read().transformed(orientation);
}

QByteArray readOriginal(const QString& path)
{
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)) return {};
    const qint64 encodedSize = file.size();
    if (encodedSize <= 0 || encodedSize > kMaxEncodedBytes) return {};
    QByteArray bytes = file.read(encodedSize);
    if (bytes.size() != encodedSize || !file.atEnd() || file.error() != QFileDevice::NoError) return {};
    return bytes;
}

Preview loadPreview(const QString& path)
{
    return previewFromOriginal(readOriginal(path));
}

Preview previewFromOriginal(const QByteArray& original)
{
    if (original.isEmpty()) return {};
    Preview result;
    result.original = original;
    const auto key = QCryptographicHash::hash(result.original, QCryptographicHash::Sha256);
    auto& c = cache();
    {
        QMutexLocker lock(&c.mutex);
        if (auto* cached = c.images.object(key)) {
            result.thumbnail = cached->thumbnail;
            result.originalSize = cached->originalSize;
            return result;
        }
    }
    result.thumbnail = decode(result.original, kPreviewPixels, &result.originalSize);
    if (result.thumbnail.isNull()) return {};
    QMutexLocker lock(&c.mutex);
    // A minimum accounting charge bounds the number of tiny entries as well.
    c.images.insert(key, new Preview{{}, result.thumbnail, result.originalSize},
                    qMax(result.thumbnail.sizeInBytes(), kCacheBudget / kMaxEntries));
    return result;
}

qsizetype cachedBytes()
{
    auto& c = cache();
    QMutexLocker lock(&c.mutex);
    qsizetype bytes = 0;
    for (const auto& key : c.images.keys()) bytes += c.images.object(key)->thumbnail.sizeInBytes();
    return bytes;
}
} // namespace SnapTray::BeautifyBackgroundImage
