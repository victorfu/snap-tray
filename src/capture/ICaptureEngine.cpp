#include "capture/ICaptureEngine.h"
#include "capture/QtCaptureEngine.h"

#ifdef Q_OS_MAC
#include "capture/SCKCaptureEngine.h"
#endif

#ifdef Q_OS_WIN
#include "capture/DXGICaptureEngine.h"
#endif

#include <QDebug>
#include <QScreen>

#ifdef Q_OS_WIN
#include <QtGui/qscreen_platform.h>
#include <Windows.h>
#endif

CaptureScreenInfo CaptureScreenInfo::fromScreen(const QScreen *screen)
{
    CaptureScreenInfo info;
    if (!screen) {
        return info;
    }

    info.name = screen->name();
    info.geometry = screen->geometry();
#ifdef Q_OS_WIN
    if (auto *nativeScreen =
            screen->nativeInterface<QNativeInterface::QWindowsScreen>()) {
        MONITORINFOEXW monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (const HMONITOR monitor = nativeScreen->handle();
            monitor && GetMonitorInfoW(
                monitor, reinterpret_cast<LPMONITORINFO>(&monitorInfo))) {
            info.nativeName = QString::fromWCharArray(monitorInfo.szDevice);
            info.physicalGeometry = QRect(
                monitorInfo.rcMonitor.left,
                monitorInfo.rcMonitor.top,
                monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top);
        }
    }
#endif
    const qreal dpr = screen->devicePixelRatio();
    info.devicePixelRatio = dpr > 0.0 ? dpr : 1.0;
    return info;
}

bool ICaptureEngine::setRegion(const QRect &region, const CaptureScreenInfo &screenInfo)
{
    Q_UNUSED(region);
    Q_UNUSED(screenInfo);
    emit error(QStringLiteral("Screen metadata configuration is not supported by this capture engine"));
    return false;
}

ICaptureEngine *ICaptureEngine::createBestEngine(QObject *parent)
{
#ifdef Q_OS_MAC
    if (SCKCaptureEngine::isAvailable()) {
        qDebug() << "ICaptureEngine: Using ScreenCaptureKit engine";
        return new SCKCaptureEngine(parent);
    }
    qDebug() << "ICaptureEngine: ScreenCaptureKit unavailable, using Qt fallback";
#endif

#ifdef Q_OS_WIN
    if (DXGICaptureEngine::isAvailable()) {
        qDebug() << "ICaptureEngine: Using DXGI Desktop Duplication engine";
        return new DXGICaptureEngine(parent);
    }
    qDebug() << "ICaptureEngine: DXGI unavailable, using Qt fallback";
#endif

    qDebug() << "ICaptureEngine: Using Qt capture engine";
    return new QtCaptureEngine(parent);
}
