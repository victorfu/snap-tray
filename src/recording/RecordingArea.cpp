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

QRect videoCrop(const QRect& area, const QRect& recordedRegion, const QSize& frameSize)
{
    if (recordedRegion.isEmpty() || frameSize.isEmpty()) return {};
    const QRect inside = area.normalized().intersected(recordedRegion);
    if (inside.isEmpty()) return {};
    const qreal sx = qreal(frameSize.width()) / recordedRegion.width();
    const qreal sy = qreal(frameSize.height()) / recordedRegion.height();
    // Outward rounding: the crop never loses a pixel of the chosen area.
    const int left = qFloor((inside.x() - recordedRegion.x()) * sx);
    const int top = qFloor((inside.y() - recordedRegion.y()) * sy);
    const int right = qCeil((inside.x() + inside.width() - recordedRegion.x()) * sx);
    const int bottom = qCeil((inside.y() + inside.height() - recordedRegion.y()) * sy);
    return VideoCropGeometry::normalizeCropRect(QRect(left, top, right - left, bottom - top), frameSize);
}

} // namespace SnapTray::RecordingArea
