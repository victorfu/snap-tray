#include "recording/RecordingArea.h"

#include "utils/VideoCropGeometry.h"

#include <QtMath>

namespace SnapTray::RecordingArea {

QRect clampToScreen(const QRect& area, const QRect& screenGeometry, qreal devicePixelRatio)
{
    QRect clipped = area.normalized().intersected(screenGeometry);
    if (clipped.isEmpty()) return {};
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const int minSide = qCeil(VideoCropGeometry::kMinCropSide / dpr);
    // Grown around its own place, then kept on the screen.
    if (clipped.width() < minSide) clipped.setWidth(qMin(minSide, screenGeometry.width()));
    if (clipped.height() < minSide) clipped.setHeight(qMin(minSide, screenGeometry.height()));
    if (clipped.right() > screenGeometry.right()) clipped.moveRight(screenGeometry.right());
    if (clipped.bottom() > screenGeometry.bottom()) clipped.moveBottom(screenGeometry.bottom());
    return clipped;
}

QRect videoCrop(const QRectF& area, const QRect& recordedRegionRect, const QSize& frameSize, qreal devicePixelRatio)
{
    if (recordedRegionRect.isEmpty() || frameSize.isEmpty()) return {};
    const QRectF recordedRegion(recordedRegionRect);
    const QRectF inside = area.normalized().intersected(recordedRegion);
    if (inside.isEmpty()) return {};
    qreal sx = qreal(frameSize.width()) / recordedRegion.width();
    qreal sy = qreal(frameSize.height()) / recordedRegion.height();
    // The frame is the region at the screen's scale, give or take Qt's
    // rounding of the logical size (up to a pixel at that scale).
    if (devicePixelRatio > 0.0
        && qAbs(recordedRegion.width() * devicePixelRatio - frameSize.width()) <= devicePixelRatio
        && qAbs(recordedRegion.height() * devicePixelRatio - frameSize.height()) <= devicePixelRatio) {
        sx = sy = devicePixelRatio;
    }
    // Outward rounding: the crop never loses a pixel of the chosen area. The
    // tolerance keeps an edge that is a whole pixel up to float error
    // (233.33 * 1.5) on that pixel instead of the next one out.
    constexpr qreal kTolerance = 1e-3;
    const int left = qFloor((inside.x() - recordedRegion.x()) * sx + kTolerance);
    const int top = qFloor((inside.y() - recordedRegion.y()) * sy + kTolerance);
    const int right = qMin(frameSize.width(), qCeil((inside.right() - recordedRegion.x()) * sx - kTolerance));
    const int bottom = qMin(frameSize.height(), qCeil((inside.bottom() - recordedRegion.y()) * sy - kTolerance));
    return VideoCropGeometry::normalizeCropRect(QRect(left, top, right - left, bottom - top), frameSize);
}

} // namespace SnapTray::RecordingArea
