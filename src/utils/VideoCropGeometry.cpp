#include "utils/VideoCropGeometry.h"

#include <QtGlobal>

#include <algorithm>

namespace SnapTray::VideoCropGeometry {

namespace {
constexpr int kEvenMask = ~1;
}

QRectF aspectFitRect(const QSize& frameSize, const QSizeF& itemSize)
{
    if (frameSize.isEmpty() || itemSize.isEmpty()) {
        return {};
    }
    const QSize item(static_cast<int>(itemSize.width()), static_cast<int>(itemSize.height()));
    const QSize scaled = frameSize.scaled(item, Qt::KeepAspectRatio);
    const int x = (item.width() - scaled.width()) / 2;
    const int y = (item.height() - scaled.height()) / 2;
    return QRectF(x, y, scaled.width(), scaled.height());
}

QRect normalizeCropRect(const QRect& rect, const QSize& frameSize)
{
    if (frameSize.isEmpty() || rect.isEmpty()) {
        return {};
    }
    const QRect clipped = rect.intersected(QRect(QPoint(0, 0), frameSize));
    if (clipped.isEmpty()) {
        return {};
    }

    const int maxWidth = frameSize.width() & kEvenMask;
    const int maxHeight = frameSize.height() & kEvenMask;
    const int minWidth = std::min(kMinCropSide, maxWidth);
    const int minHeight = std::min(kMinCropSide, maxHeight);

    int x = clipped.x() & kEvenMask;
    int y = clipped.y() & kEvenMask;
    int width = std::max(minWidth, (clipped.x() + clipped.width() - x) & kEvenMask);
    int height = std::max(minHeight, (clipped.y() + clipped.height() - y) & kEvenMask);
    width = std::min(width, maxWidth);
    height = std::min(height, maxHeight);
    x = std::min(x, maxWidth - width);
    y = std::min(y, maxHeight - height);

    if (x == 0 && y == 0 && width == maxWidth && height == maxHeight) {
        return {};
    }
    return QRect(x, y, width, height);
}

QRect viewToVideo(const QRectF& viewRect, const QRectF& contentRect, const QSize& frameSize)
{
    if (contentRect.isEmpty() || frameSize.isEmpty()) {
        return {};
    }
    const qreal scaleX = frameSize.width() / contentRect.width();
    const qreal scaleY = frameSize.height() / contentRect.height();
    const QRectF local = viewRect.normalized().translated(-contentRect.topLeft());
    const int left = qRound(local.left() * scaleX);
    const int top = qRound(local.top() * scaleY);
    const int right = qRound(local.right() * scaleX);
    const int bottom = qRound(local.bottom() * scaleY);
    return QRect(left, top, right - left, bottom - top);
}

QRectF videoToView(const QRect& videoRect, const QRectF& contentRect, const QSize& frameSize)
{
    if (contentRect.isEmpty() || frameSize.isEmpty() || videoRect.isEmpty()) {
        return {};
    }
    const qreal scaleX = contentRect.width() / frameSize.width();
    const qreal scaleY = contentRect.height() / frameSize.height();
    return QRectF(contentRect.x() + videoRect.x() * scaleX,
                  contentRect.y() + videoRect.y() * scaleY,
                  videoRect.width() * scaleX,
                  videoRect.height() * scaleY);
}

QPoint viewPointToVideo(const QPointF& viewPoint, const QRectF& contentRect, const QSize& frameSize)
{
    if (contentRect.isEmpty() || frameSize.isEmpty() || !contentRect.contains(viewPoint)) {
        return QPoint(-1, -1);
    }
    const QPointF local = viewPoint - contentRect.topLeft();
    const int x = qBound(0, static_cast<int>(local.x() * frameSize.width() / contentRect.width()), frameSize.width() - 1);
    const int y = qBound(0, static_cast<int>(local.y() * frameSize.height() / contentRect.height()), frameSize.height() - 1);
    return QPoint(x, y);
}

} // namespace SnapTray::VideoCropGeometry
