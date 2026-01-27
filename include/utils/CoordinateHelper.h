#ifndef COORDINATEHELPER_H
#define COORDINATEHELPER_H

#include <QPoint>
#include <QRect>
#include <QSize>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class QScreen;

/**
 * CoordinateHelper - Unified coordinate conversion utilities
 *
 * Provides static methods for converting between logical (Qt) and physical (native)
 * coordinates, handling device pixel ratio (DPI scaling) consistently across the codebase.
 */
class CoordinateHelper {
public:
    CoordinateHelper() = delete;

    // Get device pixel ratio from screen (returns 1.0 if screen is null)
    static qreal getDevicePixelRatio(QScreen* screen);

    // Logical to Physical conversions
    static QPoint toPhysical(const QPoint& logical, qreal dpr);
    static QRect toPhysical(const QRect& logical, qreal dpr);
    static QSize toPhysical(const QSize& logical, qreal dpr);

    // Physical to Logical conversions
    static QPoint toLogical(const QPoint& physical, qreal dpr);
    static QRect toLogical(const QRect& physical, qreal dpr);
    static QSize toLogical(const QSize& physical, qreal dpr);

    // Global to Screen-local coordinate conversions
    static QPoint globalToScreen(const QPoint& global, QScreen* screen);
    static QPoint screenToGlobal(const QPoint& local, QScreen* screen);
    static QRect globalToScreen(const QRect& global, QScreen* screen);
    static QRect screenToGlobal(const QRect& local, QScreen* screen);

    // Calculate physical size with even dimensions (required by H.264/H.265 encoders)
    static QSize toEvenPhysicalSize(const QSize& logical, qreal dpr);

#ifdef Q_OS_WIN
    // Convert physical (Windows API) coordinates to Qt logical coordinates
    // Handles multi-monitor setups with different DPIs correctly
    static QRect physicalToQtLogical(const QRect& physicalBounds, HWND hwnd = nullptr);
#endif
};

#endif // COORDINATEHELPER_H
