#include "utils/CoordinateHelper.h"
#include <QScreen>
#include <QGuiApplication>
#include <QtMath>
#include <algorithm>

#ifdef Q_OS_WIN
#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")
#endif

qreal CoordinateHelper::getDevicePixelRatio(QScreen* screen)
{
    return screen ? screen->devicePixelRatio() : 1.0;
}

// Logical to Physical conversions

QPoint CoordinateHelper::toPhysical(const QPoint& logical, qreal dpr)
{
    return QPoint(
        qRound(logical.x() * dpr),
        qRound(logical.y() * dpr)
    );
}

QRect CoordinateHelper::toPhysical(const QRect& logical, qreal dpr)
{
    return QRect(
        qRound(logical.x() * dpr),
        qRound(logical.y() * dpr),
        qRound(logical.width() * dpr),
        qRound(logical.height() * dpr)
    );
}

QSize CoordinateHelper::toPhysical(const QSize& logical, qreal dpr)
{
    return QSize(
        qRound(logical.width() * dpr),
        qRound(logical.height() * dpr)
    );
}

// Physical to Logical conversions

QPoint CoordinateHelper::toLogical(const QPoint& physical, qreal dpr)
{
    if (qFuzzyIsNull(dpr)) {
        return physical;
    }
    return QPoint(
        qRound(physical.x() / dpr),
        qRound(physical.y() / dpr)
    );
}

QRect CoordinateHelper::toLogical(const QRect& physical, qreal dpr)
{
    if (qFuzzyIsNull(dpr)) {
        return physical;
    }
    return QRect(
        qRound(physical.x() / dpr),
        qRound(physical.y() / dpr),
        qRound(physical.width() / dpr),
        qRound(physical.height() / dpr)
    );
}

QSize CoordinateHelper::toLogical(const QSize& physical, qreal dpr)
{
    if (qFuzzyIsNull(dpr)) {
        return physical;
    }
    return QSize(
        qRound(physical.width() / dpr),
        qRound(physical.height() / dpr)
    );
}

// Global to Screen-local coordinate conversions

QPoint CoordinateHelper::globalToScreen(const QPoint& global, QScreen* screen)
{
    if (!screen) {
        return global;
    }
    return global - screen->geometry().topLeft();
}

QPoint CoordinateHelper::screenToGlobal(const QPoint& local, QScreen* screen)
{
    if (!screen) {
        return local;
    }
    return local + screen->geometry().topLeft();
}

QRect CoordinateHelper::globalToScreen(const QRect& global, QScreen* screen)
{
    if (!screen) {
        return global;
    }
    QPoint offset = screen->geometry().topLeft();
    return global.translated(-offset);
}

QRect CoordinateHelper::screenToGlobal(const QRect& local, QScreen* screen)
{
    if (!screen) {
        return local;
    }
    QPoint offset = screen->geometry().topLeft();
    return local.translated(offset);
}

// Calculate physical size with even dimensions (required by H.264/H.265 encoders)

QSize CoordinateHelper::toEvenPhysicalSize(const QSize& logical, qreal dpr)
{
    QSize physical = toPhysical(logical, dpr);

    // Encoders require even dimensions
    int width = physical.width();
    int height = physical.height();

    if (width % 2 != 0) {
        width += 1;
    }
    if (height % 2 != 0) {
        height += 1;
    }

    return QSize(width, height);
}

#ifdef Q_OS_WIN
QRect CoordinateHelper::physicalToQtLogical(const QRect& physicalBounds, HWND hwnd)
{
    // 1. Find the monitor where the window/point is located
    HMONITOR hMonitor;
    if (hwnd) {
        hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    } else {
        POINT pt = { physicalBounds.x(), physicalBounds.y() };
        hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    }

    // 2. Get the monitor's physical bounds
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (!GetMonitorInfo(hMonitor, &mi)) {
        // Fallback: assume DPR 1.0
        return physicalBounds;
    }

    // 3. Get the monitor's DPI
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        dpiX = 96;
    }
    qreal dpr = dpiX / 96.0;

    // 4. Calculate the monitor's physical size for matching
    int monitorPhysicalW = mi.rcMonitor.right - mi.rcMonitor.left;
    int monitorPhysicalH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    // 5. Find the corresponding QScreen by matching physical POSITION
    // This handles identical-spec multi-monitor setups correctly
    QScreen* matchedScreen = nullptr;
    const auto screens = QGuiApplication::screens();
    int targetPhysX = mi.rcMonitor.left;
    int targetPhysY = mi.rcMonitor.top;

    // Build screen list sorted by logical x position (for physical origin calculation)
    QList<QScreen*> sortedScreens = screens;
    std::sort(sortedScreens.begin(), sortedScreens.end(), [](QScreen* a, QScreen* b) {
        return a->geometry().x() < b->geometry().x();
    });

    // Compute expected physical origin for each screen and find match
    int cumulativePhysX = 0;
    for (QScreen* screen : sortedScreens) {
        qreal screenDpr = screen->devicePixelRatio();
        QSize logicalSize = screen->geometry().size();
        int physicalW = qRound(logicalSize.width() * screenDpr);
        int physicalH = qRound(logicalSize.height() * screenDpr);

        // Match by physical origin position AND size for validation
        bool positionMatch = qAbs(cumulativePhysX - targetPhysX) <= 10;
        bool sizeMatch = qAbs(physicalW - monitorPhysicalW) <= 5 &&
                         qAbs(physicalH - monitorPhysicalH) <= 5;

        if (positionMatch && sizeMatch) {
            matchedScreen = screen;
            break;
        }

        cumulativePhysX += physicalW;
    }

    // Fallback: If position matching failed, try size+DPR match (for unusual layouts)
    if (!matchedScreen) {
        for (QScreen* screen : screens) {
            QSize logicalSize = screen->geometry().size();
            qreal screenDpr = screen->devicePixelRatio();
            int physicalW = qRound(logicalSize.width() * screenDpr);
            int physicalH = qRound(logicalSize.height() * screenDpr);

            if (qAbs(physicalW - monitorPhysicalW) <= 5 &&
                qAbs(physicalH - monitorPhysicalH) <= 5 &&
                qAbs(screenDpr - dpr) < 0.05) {
                matchedScreen = screen;
                break;
            }
        }
    }

    // 6. Calculate window position relative to monitor's physical origin
    int relX = physicalBounds.x() - mi.rcMonitor.left;
    int relY = physicalBounds.y() - mi.rcMonitor.top;

    // 7. Convert relative position to logical
    int logicalRelX = qRound(relX / dpr);
    int logicalRelY = qRound(relY / dpr);

    // 8. Get the monitor's logical origin from Qt's QScreen
    QPoint logicalScreenOrigin(0, 0);
    if (matchedScreen) {
        logicalScreenOrigin = matchedScreen->geometry().topLeft();
    }
    // Note: If no match, fallback to (0,0) which may be incorrect,
    // but better than crashing. This shouldn't happen in normal cases.

    // 9. Combine to get final logical coordinates
    int logicalX = logicalScreenOrigin.x() + logicalRelX;
    int logicalY = logicalScreenOrigin.y() + logicalRelY;
    int logicalW = qRound(physicalBounds.width() / dpr);
    int logicalH = qRound(physicalBounds.height() / dpr);

    return QRect(logicalX, logicalY, logicalW, logicalH);
}
#endif
