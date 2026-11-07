#include "detection/BoundarySnapIndex.h"

#include <cmath>
#include <QDebug>

void BoundarySnapIndex::setImage(const QImage& image)
{
    m_image = {};
    if (image.width() < 2 || image.height() < 2) return;
    if (qint64(image.width()) * image.height() > MaximumPixels) {
        qDebug() << "Boundary snap disabled: capture exceeds pixel budget" << image.size();
        return;
    }
    // These formats have identical QRgb channel layout. Retain their shared
    // storage; other formats are converted once, never inside a query.
    if (image.format() == QImage::Format_RGB32
        || image.format() == QImage::Format_ARGB32_Premultiplied) {
        m_image = image;
    } else {
        m_image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (m_image.isNull()) qWarning() << "Boundary snap source format conversion failed";
    }
}

std::optional<BoundarySnapIndex::Hit> BoundarySnapIndex::evaluate(
    Axis axis, int boundary, int spanBegin, int spanEnd, const Options& options) const
{
    const bool vertical = axis == Axis::Vertical;
    const int extent = vertical ? m_image.width() : m_image.height();
    const int spanExtent = vertical ? m_image.height() : m_image.width();
    if (m_image.isNull() || boundary <= 0 || boundary >= extent
        || spanBegin < 0 || spanEnd > spanExtent || spanBegin >= spanEnd
        || !std::isfinite(options.minimumMean) || options.minimumMean < 0
        || !std::isfinite(options.supportThreshold) || options.supportThreshold < 0
        || !std::isfinite(options.minimumSupport) || options.minimumSupport < 0
        || options.minimumSupport > 1) return std::nullopt;

    double sum = 0;
    int support = 0;
    const auto* rowBefore = vertical ? nullptr
        : reinterpret_cast<const QRgb*>(m_image.constScanLine(boundary - 1));
    const auto* rowAfter = vertical ? nullptr
        : reinterpret_cast<const QRgb*>(m_image.constScanLine(boundary));
    for (int i = spanBegin; i < spanEnd; ++i) {
        const auto* row = vertical
            ? reinterpret_cast<const QRgb*>(m_image.constScanLine(i)) : nullptr;
        const QRgb a = vertical ? row[boundary - 1] : rowBefore[i];
        const QRgb b = vertical ? row[boundary] : rowAfter[i];
        const int r = qRed(a) - qRed(b);
        const int g = qGreen(a) - qGreen(b);
        const int blue = qBlue(a) - qBlue(b);
        const double distance = std::sqrt(double(r * r + g * g + blue * blue));
        sum += distance;
        support += distance >= options.supportThreshold;
    }
    const double mean = sum / (spanEnd - spanBegin);
    const double fraction = double(support) / (spanEnd - spanBegin);
    if (mean < options.minimumMean || fraction < options.minimumSupport) return std::nullopt;
    return Hit{axis, boundary, mean, fraction};
}
