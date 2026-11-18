#include "utils/ImageExportOptions.h"

#include <QBuffer>
#include <QColorSpace>
#include <QImageWriter>
#include <QPainter>
#include <QtMath>

ImageExportOptions ImageExportOptions::normalized() const
{
    ImageExportOptions result = *this;
    if (result.resize != Resize::Original && result.resize != Resize::Percent
        && result.resize != Resize::LongEdge) {
        result.resize = Resize::Original;
    }
    if (result.format != Format::Png && result.format != Format::Jpeg) {
        result.format = Format::Png;
    }
    result.percent = qBound(kMinPercent, result.percent, kMaxPercent);
    result.longEdge = qBound(kMinLongEdge, result.longEdge, kMaxLongEdge);
    result.jpegQuality = qBound(kMinJpegQuality, result.jpegQuality, kMaxJpegQuality);
    return result;
}

QSize ImageExportOptions::outputSize(const QSize& sourcePixels) const
{
    if (!sourcePixels.isValid() || sourcePixels.isEmpty()) {
        return QSize();
    }
    const ImageExportOptions options = normalized();

    qreal scale = 1.0;
    if (options.resize == Resize::Percent) {
        scale = options.percent / 100.0;
    } else if (options.resize == Resize::LongEdge) {
        const int sourceLongEdge = qMax(sourcePixels.width(), sourcePixels.height());
        scale = static_cast<qreal>(options.longEdge) / sourceLongEdge;
    }
    if (scale >= 1.0) {
        return sourcePixels;   // never enlarge
    }
    return QSize(qMax(1, qRound(sourcePixels.width() * scale)),
                 qMax(1, qRound(sourcePixels.height() * scale)));
}

QByteArray ImageExportOptions::formatName() const
{
    return format == Format::Jpeg ? QByteArrayLiteral("jpeg") : QByteArrayLiteral("png");
}

QString ImageExportOptions::suffix() const
{
    return format == Format::Jpeg ? QStringLiteral("jpg") : QStringLiteral("png");
}

QStringList ImageExportOptions::acceptedSuffixes() const
{
    return format == Format::Jpeg
        ? QStringList{QStringLiteral("jpg"), QStringLiteral("jpeg")}
        : QStringList{QStringLiteral("png")};
}

namespace ImageExport {

bool hasTransparency(const QImage& image)
{
    if (image.isNull() || !image.hasAlphaChannel()) {
        return false;
    }
    const QImage argb = image.format() == QImage::Format_ARGB32
            || image.format() == QImage::Format_ARGB32_Premultiplied
        ? image
        : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(line[x]) != 0xFF) {
                return true;
            }
        }
    }
    return false;
}

QImage render(const QImage& source, const ImageExportOptions& options)
{
    if (source.isNull()) {
        return QImage();
    }
    const QSize target = options.outputSize(source.size());
    QImage result = source;
    if (target != source.size()) {
        result = source.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (result.isNull()) {
            return QImage();
        }
    }

    // Left to the JPEG writer, see-through pixels turn black or keep their
    // hidden color depending on the pixel format. Compose them here instead,
    // so the result is the same for every source.
    if (options.normalized().format == ImageExportOptions::Format::Jpeg && hasTransparency(result)) {
        QImage flattened(result.size(), QImage::Format_RGB32);
        if (flattened.isNull()) {
            return QImage();
        }
        flattened.fill(QColor::fromRgba(kOpaqueBackground));
        QImage unscaled = result;
        unscaled.setDevicePixelRatio(1.0);
        QPainter painter(&flattened);
        painter.drawImage(QPoint(0, 0), unscaled);
        painter.end();
        result = flattened;
    }

    if (result.cacheKey() != source.cacheKey()) {
        // The output is a plain pixel grid: it has no logical size of its own.
        result.setDevicePixelRatio(1.0);
        if (source.colorSpace().isValid()) {
            result.setColorSpace(source.colorSpace());
        }
    }
    return result;
}

Encoded encode(const QImage& source, const ImageExportOptions& requested)
{
    Encoded result;
    result.options = requested.normalized();
    if (source.isNull()) {
        result.error = QStringLiteral("Image is null");
        return result;
    }

    const QImage image = render(source, result.options);
    if (image.isNull()) {
        result.error = QStringLiteral("Not enough memory to resize the image");
        return result;
    }
    result.pixelSize = image.size();
    // render() made the image opaque if it had to; alpha left means none was lost.
    result.transparencyDropped = result.options.format == ImageExportOptions::Format::Jpeg
        && image.cacheKey() != source.cacheKey() && !image.hasAlphaChannel()
        && source.hasAlphaChannel() && hasTransparency(source);

    QBuffer buffer(&result.data);
    if (!buffer.open(QIODevice::WriteOnly)) {
        result.error = buffer.errorString();
        return result;
    }
    QImageWriter writer(&buffer, result.options.formatName());
    if (result.options.isLossy()) {
        writer.setQuality(result.options.jpegQuality);
    }
    if (!writer.write(image)) {
        result.data.clear();
        const QString writeError = writer.errorString().trimmed();
        result.error = writeError.isEmpty() ? QStringLiteral("Failed to encode image") : writeError;
    }
    return result;
}

} // namespace ImageExport
