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

namespace SnapTray::BeautifyBackgroundImage {
namespace {
constexpr qint64 kMaxEncodedBytes = 256 * 1024 * 1024;
constexpr int kPreviewSide = 1024;
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

QImage decode(const QByteArray& original, int maximumSide, QSize* originalSize)
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
    if (maximumSide > 0 && qMax(size.width(), size.height()) > maximumSide) {
        QSize scaled = size.scaled(maximumSide, maximumSide, Qt::KeepAspectRatio);
        scaled = scaled.expandedTo(QSize(1, 1));
        if (png) return readPngThumbnail(original, scaled).transformed(orientation);
        reader.setScaledSize(scaled);
    }
    return reader.read().transformed(orientation);
}

Preview loadPreview(const QString& path)
{
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)) return {};
    const qint64 encodedSize = file.size();
    if (encodedSize <= 0 || encodedSize > kMaxEncodedBytes) return {};
    Preview result;
    result.original = file.read(encodedSize);
    if (result.original.size() != encodedSize || !file.atEnd() || file.error() != QFileDevice::NoError) return {};
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
    result.thumbnail = decode(result.original, kPreviewSide, &result.originalSize);
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
