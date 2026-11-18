#include "pinwindow/ForegroundCutout.h"

namespace SnapTray::ForegroundCutout {

namespace {

// Premultiplied channels scale together, so a partly covered pixel keeps its
// colour and only loses opacity. Rounded, so 255 keeps a channel exactly.
inline uint scaleChannel(uint channel, uint coverage)
{
    const uint product = channel * coverage + 128;
    return (product + (product >> 8)) >> 8;
}

} // namespace

Status validateSize(const QSize& size)
{
    if (size.isEmpty()) {
        return Status::Failed;
    }
    const qint64 pixels = static_cast<qint64>(size.width()) * size.height();
    return pixels > kMaxPixels ? Status::TooLarge : Status::Success;
}

Result applyMask(const QImage& source, const QImage& mask)
{
    Result result;
    if (source.isNull() || mask.isNull() || mask.size() != source.size()
        || mask.format() != QImage::Format_Grayscale8) {
        result.status = Status::Failed;
        return result;
    }

    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        result.status = Status::Failed;
        return result;
    }

    bool anyKept = false;
    bool anyChanged = false;
    const int width = image.width();
    for (int y = 0; y < image.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        const uchar* coverageRow = mask.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const QRgb pixel = row[x];
            const uint alpha = qAlpha(pixel);
            if (alpha == 0) {
                continue;
            }
            const uint coverage = coverageRow[x];
            if (coverage == 255) {
                anyKept = true;
                continue;
            }
            const uint newAlpha = scaleChannel(alpha, coverage);
            row[x] = qRgba(scaleChannel(qRed(pixel), coverage),
                           scaleChannel(qGreen(pixel), coverage),
                           scaleChannel(qBlue(pixel), coverage),
                           newAlpha);
            anyChanged = true;
            anyKept = anyKept || newAlpha > 0;
        }
    }

    if (!anyKept) {
        result.status = Status::NoSubject;
        return result;
    }
    if (!anyChanged) {
        result.status = Status::NothingRemoved;
        return result;
    }
    image.setDevicePixelRatio(source.devicePixelRatio());
    result.image = std::move(image);
    result.status = Status::Success;
    return result;
}

Result run(const QImage& source, const MaskProvider& provider)
{
    Result result;
    result.status = validateSize(source.size());
    if (result.status != Status::Success) {
        return result;
    }
    if (!provider) {
        result.status = Status::Unsupported;
        return result;
    }

    const MaskResult mask = provider(source);
    if (mask.status != Status::Success) {
        result.status = mask.status;
        return result;
    }
    return applyMask(source, mask.mask);
}

} // namespace SnapTray::ForegroundCutout
