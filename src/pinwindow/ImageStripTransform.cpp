#include "pinwindow/ImageStripTransform.h"

#include <QColorSpace>
#include <QPainter>
#include <QtMath>

namespace ImageStrip {

int extentAlong(const QSize& size, Axis axis)
{
    return axis == Axis::Rows ? size.height() : size.width();
}

int maxInsertLength(const QSize& size, Axis axis)
{
    if (!size.isValid() || size.isEmpty()) {
        return 0;
    }
    const int extent = extentAlong(size, axis);
    const int across = axis == Axis::Rows ? size.width() : size.height();
    const qint64 byPixels = kMaxPixels / across - extent;
    const qint64 byDimension = kMaxDimension - extent;
    return static_cast<int>(qMax<qint64>(0, qMin(byPixels, byDimension)));
}

Error validate(const QSize& imageSize, const Request& request)
{
    if (!imageSize.isValid() || imageSize.isEmpty()) {
        return Error::EmptyImage;
    }
    const int extent = extentAlong(imageSize, request.axis);
    if (request.length <= 0 || request.start < 0) {
        return Error::InvalidStrip;
    }
    if (request.operation == Operation::Remove) {
        if (request.start >= extent || request.length > extent - request.start) {
            return Error::InvalidStrip;
        }
        if (request.length >= extent) {
            return Error::WholeImage;
        }
        return Error::None;
    }
    if (request.start > extent) {
        return Error::InvalidStrip;
    }
    if (request.length > maxInsertLength(imageSize, request.axis)) {
        return Error::TooLarge;
    }
    return Error::None;
}

QSize resultSize(const QSize& imageSize, const Request& request)
{
    if (validate(imageSize, request) != Error::None) {
        return QSize();
    }
    const int change = request.operation == Operation::Remove ? -request.length : request.length;
    return request.axis == Axis::Rows
        ? QSize(imageSize.width(), imageSize.height() + change)
        : QSize(imageSize.width() + change, imageSize.height());
}

Result apply(const QImage& image, const Request& request)
{
    Result result;
    if (image.isNull()) {
        result.error = Error::EmptyImage;
        return result;
    }
    result.error = validate(image.size(), request);
    if (result.error != Error::None) {
        return result;
    }

    const bool insert = request.operation == Operation::Insert;
    // An inserted strip is see-through, so the result needs an alpha channel.
    QImage::Format format = image.format();
    if (insert && !image.hasAlphaChannel()) {
        format = QImage::Format_ARGB32_Premultiplied;
    }
    QImage output(resultSize(image.size(), request), format);
    if (output.isNull()) {
        result.error = Error::OutOfMemory;
        return result;
    }
    output.setDevicePixelRatio(image.devicePixelRatio());
    if (image.colorSpace().isValid()) {
        output.setColorSpace(image.colorSpace());
    }
    output.fill(Qt::transparent);

    // Device-pixel copies of the part before the strip and the part after it.
    QImage source = image;
    source.setDevicePixelRatio(1.0);
    QImage target = output;
    target.setDevicePixelRatio(1.0);
    {
        QPainter painter(&target);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        const int extent = extentAlong(image.size(), request.axis);
        const int sourceAfter = insert ? request.start : request.start + request.length;
        const int targetAfter = insert ? request.start + request.length : request.start;
        const int afterLength = extent - sourceAfter;
        // QPainter reads an empty source rect as "the whole image", so a part
        // with no pixels (a strip at the very start or end) must be skipped.
        const auto copyPart = [&](int targetPos, int sourcePos, int length) {
            if (length <= 0) {
                return;
            }
            if (request.axis == Axis::Rows) {
                painter.drawImage(QPoint(0, targetPos), source,
                                  QRect(0, sourcePos, image.width(), length));
            } else {
                painter.drawImage(QPoint(targetPos, 0), source,
                                  QRect(sourcePos, 0, length, image.height()));
            }
        };
        copyPart(0, 0, request.start);
        copyPart(targetAfter, sourceAfter, afterLength);
    }
    target.setDevicePixelRatio(image.devicePixelRatio());
    result.image = target;
    return result;
}

Placement placementOf(const QRectF& bounds, Axis axis, Operation operation,
                      qreal start, qreal length)
{
    const qreal near = axis == Axis::Rows ? bounds.top() : bounds.left();
    const qreal far = axis == Axis::Rows ? bounds.bottom() : bounds.right();
    if (far <= start) {
        return Placement::Before;
    }
    const qreal stripEnd = operation == Operation::Remove ? start + length : start;
    if (near >= stripEnd) {
        return Placement::After;
    }
    return Placement::Crossing;
}

int lengthStep(qreal pixelsPerLogical, qreal viewPerPixel)
{
    if (pixelsPerLogical <= 0.0 || viewPerPixel <= 0.0) {
        return 0;
    }
    constexpr qreal kTolerance = 1e-6;
    const auto whole = [](qreal value) {
        return value > 0.5 && qAbs(value - qRound(value)) < kTolerance;
    };
    for (int length = 1; length <= kMaxLengthStep; ++length) {
        if (whole(length / pixelsPerLogical) && whole(length * viewPerPixel)) {
            return length;
        }
    }
    return 0;
}

QPointF shiftAfter(Axis axis, Operation operation, qreal length)
{
    const qreal signedLength = operation == Operation::Remove ? -length : length;
    return axis == Axis::Rows ? QPointF(0.0, signedLength) : QPointF(signedLength, 0.0);
}

} // namespace ImageStrip
