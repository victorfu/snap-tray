#include "pinwindow/PinWindowPlacement.h"
#include "utils/CoordinateHelper.h"

#include <QPixmap>

#include <QtMath>

namespace {
constexpr qreal kMinAutoFitZoom = 0.1;
const QPoint kFallbackPosition(50, 50);

qreal clampedScreenRatio(qreal value, qreal rangeStart, qreal rangeSpan)
{
    if (rangeSpan <= 0.0) {
        return 0.5;
    }
    const qreal ratio = (value - rangeStart) / rangeSpan;
    return qBound(0.0, ratio, 1.0);
}

qreal mapRatioToRange(qreal ratio, qreal rangeStart, qreal rangeSpan)
{
    if (rangeSpan <= 0.0) {
        return rangeStart;
    }
    return rangeStart + qBound(0.0, ratio, 1.0) * rangeSpan;
}

int clampTopLeftToTarget(int desiredTopLeft,
                         int windowExtent,
                         int targetStart,
                         int targetExtent)
{
    if (targetExtent <= 0) {
        return targetStart;
    }
    if (windowExtent <= 0) {
        return desiredTopLeft;
    }
    if (windowExtent >= targetExtent) {
        return targetStart;
    }

    const int maxTopLeft = targetStart + targetExtent - windowExtent;
    return qBound(targetStart, desiredTopLeft, maxTopLeft);
}

QPointF rectPixelCenter(const QRect& rect)
{
    const qreal halfWidth = static_cast<qreal>(qMax(0, rect.width() - 1)) / 2.0;
    const qreal halfHeight = static_cast<qreal>(qMax(0, rect.height() - 1)) / 2.0;
    return QPointF(static_cast<qreal>(rect.left()) + halfWidth,
                   static_cast<qreal>(rect.top()) + halfHeight);
}
}

PinWindowPlacement computeInitialPinWindowPlacement(const QPixmap& pixmap,
                                                    const QRect& availableGeometry,
                                                    qreal screenMargin)
{
    PinWindowPlacement placement;

    if (pixmap.isNull()) {
        placement.position = availableGeometry.isValid() ? availableGeometry.center() : kFallbackPosition;
        return placement;
    }

    qreal dpr = pixmap.devicePixelRatio();
    if (dpr <= 0.0) {
        dpr = 1.0;
    }

    placement.logicalSize = CoordinateHelper::toLogical(pixmap.size(), dpr);
    placement.zoomLevel = 1.0;

    if (availableGeometry.isValid() && screenMargin > 0.0) {
        const qreal maxWidth = availableGeometry.width() * screenMargin;
        const qreal maxHeight = availableGeometry.height() * screenMargin;
        if (placement.logicalSize.width() > maxWidth || placement.logicalSize.height() > maxHeight) {
            const qreal scaleX = maxWidth / placement.logicalSize.width();
            const qreal scaleY = maxHeight / placement.logicalSize.height();
            placement.zoomLevel = qMax(kMinAutoFitZoom, qMin(scaleX, scaleY));
        }
    }

    placement.displaySize = QSize(
        qMax(1, qRound(placement.logicalSize.width() * placement.zoomLevel)),
        qMax(1, qRound(placement.logicalSize.height() * placement.zoomLevel))
    );

    if (availableGeometry.isValid()) {
        placement.position = availableGeometry.center()
            - QPoint(placement.displaySize.width() / 2, placement.displaySize.height() / 2);
    } else {
        placement.position = kFallbackPosition;
    }

    return placement;
}

QPoint computePinWindowTopLeftForTargetScreen(const QRect& windowFrame,
                                              const QRect& sourceAvailableGeometry,
                                              const QRect& targetAvailableGeometry)
{
    if (!targetAvailableGeometry.isValid()) {
        return windowFrame.topLeft();
    }

    const QSize windowSize(qMax(1, windowFrame.width()), qMax(1, windowFrame.height()));

    QPointF targetCenter = rectPixelCenter(targetAvailableGeometry);
    if (sourceAvailableGeometry.isValid()) {
        const QPointF sourceCenter = rectPixelCenter(windowFrame);
        const qreal ratioX = clampedScreenRatio(
            sourceCenter.x(),
            static_cast<qreal>(sourceAvailableGeometry.left()),
            static_cast<qreal>(qMax(0, sourceAvailableGeometry.width() - 1)));
        const qreal ratioY = clampedScreenRatio(
            sourceCenter.y(),
            static_cast<qreal>(sourceAvailableGeometry.top()),
            static_cast<qreal>(qMax(0, sourceAvailableGeometry.height() - 1)));

        targetCenter.setX(mapRatioToRange(
            ratioX,
            static_cast<qreal>(targetAvailableGeometry.left()),
            static_cast<qreal>(qMax(0, targetAvailableGeometry.width() - 1))));
        targetCenter.setY(mapRatioToRange(
            ratioY,
            static_cast<qreal>(targetAvailableGeometry.top()),
            static_cast<qreal>(qMax(0, targetAvailableGeometry.height() - 1))));
    }

    QPoint targetTopLeft(
        qRound(targetCenter.x() - (static_cast<qreal>(windowSize.width()) - 1.0) / 2.0),
        qRound(targetCenter.y() - (static_cast<qreal>(windowSize.height()) - 1.0) / 2.0));

    targetTopLeft.setX(clampTopLeftToTarget(
        targetTopLeft.x(),
        windowSize.width(),
        targetAvailableGeometry.left(),
        targetAvailableGeometry.width()));
    targetTopLeft.setY(clampTopLeftToTarget(
        targetTopLeft.y(),
        windowSize.height(),
        targetAvailableGeometry.top(),
        targetAvailableGeometry.height()));

    return targetTopLeft;
}
