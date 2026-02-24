#ifndef PINWINDOWPLACEMENT_H
#define PINWINDOWPLACEMENT_H

#include <QPoint>
#include <QRect>
#include <QSize>

class QPixmap;

struct PinWindowPlacement
{
    QPoint position;
    qreal zoomLevel = 1.0;
    QSize logicalSize;
    QSize displaySize;
};

PinWindowPlacement computeInitialPinWindowPlacement(const QPixmap& pixmap,
                                                    const QRect& availableGeometry,
                                                    qreal screenMargin = 0.9);

QPoint computePinWindowTopLeftForTargetScreen(const QRect& windowFrame,
                                              const QRect& sourceAvailableGeometry,
                                              const QRect& targetAvailableGeometry);

#endif // PINWINDOWPLACEMENT_H
