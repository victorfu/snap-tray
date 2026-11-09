#include "utils/VideoCropGeometry.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace SnapTray::VideoCropGeometry {

namespace {
constexpr int kEvenMask = ~1;
// Ratios whose doubled reduced step is at most this many pixels are kept
// exact (16:9, 4:3, 9:16, 1:1...). Larger steps, as native display sizes
// such as 3456x2234 (1728:1117) give, would make the crop size jump by up
// to the whole frame; those round the paired side to even pixels instead.
constexpr int kMaxExactStep = 64;

int roundEven(double value)
{
    return 2 * qRound(value / 2.0);
}

// The largest even width <= maxWidth whose paired even height, rounded from
// the ratio, fits maxHeight; 0 when none does.
int fitWidth(int maxWidth, int maxHeight, double ratio)
{
    // roundEven(w / ratio) <= maxHeight needs w < (maxHeight + 1) * ratio.
    int width = std::min(maxWidth & kEvenMask,
                         static_cast<int>(std::ceil((maxHeight + 1) * ratio)) & kEvenMask);
    while (width >= 2 && roundEven(width / ratio) > maxHeight) width -= 2;
    return width >= 2 && roundEven(width / ratio) >= 2 ? width : 0;
}

// fitAspect() for ratios with a large exact step: the same contract, with the
// paired side within one pixel of the exact ratio.
QRect fitRoundedAspect(const QRect& clipped, int maxWidth, int maxHeight, double ratio)
{
    const int frameWidth = fitWidth(maxWidth, maxHeight, ratio);
    if (frameWidth == 0) return {};
    int width = fitWidth(clipped.width(), clipped.height(), ratio);
    // Grow to the minimum on the shorter side when the frame allows.
    const auto shortSide = [ratio](int w) { return std::min(w, roundEven(w / ratio)); };
    while (width < frameWidth && shortSide(width) < kMinCropSide) width += 2;
    width = std::min(width, frameWidth);
    const int height = roundEven(width / ratio);

    const int centreX2 = 2 * clipped.x() + clipped.width();
    const int centreY2 = 2 * clipped.y() + clipped.height();
    const int x = std::clamp(((centreX2 - width) / 2) & kEvenMask, 0, maxWidth - width);
    const int y = std::clamp(((centreY2 - height) / 2) & kEvenMask, 0, maxHeight - height);
    return QRect(x, y, width, height);
}
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

QRect fitAspect(const QRect& bounds, const QSize& frameSize, int aspectWidth, int aspectHeight)
{
    if (aspectWidth <= 0 || aspectHeight <= 0) {
        return normalizeCropRect(bounds, frameSize);
    }
    if (frameSize.isEmpty() || bounds.isEmpty()) {
        return {};
    }
    const QRect clipped = bounds.intersected(QRect(QPoint(0, 0), frameSize));
    if (clipped.isEmpty()) {
        return {};
    }
    const int maxWidth = frameSize.width() & kEvenMask;
    const int maxHeight = frameSize.height() & kEvenMask;

    // Doubling the reduced ratio keeps every multiple even on both axes.
    const int divisor = std::gcd(aspectWidth, aspectHeight);
    const int unitWidth = 2 * (aspectWidth / divisor);
    const int unitHeight = 2 * (aspectHeight / divisor);
    if (std::max(unitWidth, unitHeight) > kMaxExactStep) {
        const QRect rounded = fitRoundedAspect(clipped, maxWidth, maxHeight, double(aspectWidth) / aspectHeight);
        if (rounded.isEmpty()) return normalizeCropRect(bounds, frameSize);
        if (rounded == QRect(0, 0, maxWidth, maxHeight)) return {};
        return rounded;
    }
    const int maxUnits = std::min(maxWidth / unitWidth, maxHeight / unitHeight);
    if (maxUnits < 1) {
        return normalizeCropRect(bounds, frameSize);
    }
    const int minUnits = std::min(maxUnits,
        (kMinCropSide + std::min(unitWidth, unitHeight) - 1) / std::min(unitWidth, unitHeight));
    const int fitUnits = std::min(clipped.width() / unitWidth, clipped.height() / unitHeight);
    const int units = std::clamp(fitUnits, minUnits, maxUnits);
    const int width = units * unitWidth;
    const int height = units * unitHeight;

    // Exact centre (QRect::center() rounds down), so a rect that is already
    // legal maps to itself and typed coordinates do not drift.
    const int centreX2 = 2 * clipped.x() + clipped.width();
    const int centreY2 = 2 * clipped.y() + clipped.height();
    const int x = std::clamp(((centreX2 - width) / 2) & kEvenMask, 0, maxWidth - width);
    const int y = std::clamp(((centreY2 - height) / 2) & kEvenMask, 0, maxHeight - height);
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
