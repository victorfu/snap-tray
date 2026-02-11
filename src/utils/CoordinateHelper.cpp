#include "utils/CoordinateHelper.h"
#include <QScreen>
#include <QGuiApplication>
#include <QtMath>

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

    // 2. Get the monitor's physical bounds AND device name for reliable QScreen matching
    MONITORINFOEXW miex = { sizeof(MONITORINFOEXW) };
    if (!GetMonitorInfoW(hMonitor, reinterpret_cast<LPMONITORINFO>(&miex))) {
        // Fallback: assume DPR 1.0
        return physicalBounds;
    }

    // 3. Get the monitor's DPI
    UINT dpiX = 96, dpiY = 96;
    if (FAILED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        dpiX = 96;
    }
    qreal dpr = dpiX / 96.0;

    // 4. Find the corresponding QScreen by matching device name
    //    MONITORINFOEXW::szDevice (e.g. "\\\\.\\DISPLAY1") matches QScreen::name() on Windows.
    //    This is reliable for all monitor arrangements (vertical, primary-on-right, etc.)
    //    unlike the previous cumulative-position approach which assumed left-to-right from x=0.
    QScreen* matchedScreen = nullptr;
    const auto screens = QGuiApplication::screens();
    QString deviceName = QString::fromWCharArray(miex.szDevice);
    for (QScreen* screen : screens) {
        if (screen->name() == deviceName) {
            matchedScreen = screen;
            break;
        }
    }

    // Fallback: primary screen (shouldn't happen in normal cases)
    if (!matchedScreen) {
        matchedScreen = QGuiApplication::primaryScreen();
    }

    // 5. Calculate window position relative to monitor's physical origin
    int relX = physicalBounds.x() - miex.rcMonitor.left;
    int relY = physicalBounds.y() - miex.rcMonitor.top;

    // 6. Convert relative position to logical
    int logicalRelX = qRound(relX / dpr);
    int logicalRelY = qRound(relY / dpr);

    // 7. Get the monitor's logical origin from Qt's QScreen
    QPoint logicalScreenOrigin(0, 0);
    if (matchedScreen) {
        logicalScreenOrigin = matchedScreen->geometry().topLeft();
    }

    // 8. Combine to get final logical coordinates
    int logicalX = logicalScreenOrigin.x() + logicalRelX;
    int logicalY = logicalScreenOrigin.y() + logicalRelY;
    int logicalW = qRound(physicalBounds.width() / dpr);
    int logicalH = qRound(physicalBounds.height() / dpr);

    return QRect(logicalX, logicalY, logicalW, logicalH);
}
#endif
