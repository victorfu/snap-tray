#include "pinwindow/PinWindowPlacement.h"

#include <QPixmap>

#include <QtMath>

namespace {
constexpr qreal kMinAutoFitZoom = 0.1;
const QPoint kFallbackPosition(50, 50);
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

    placement.logicalSize = pixmap.size() / dpr;
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
