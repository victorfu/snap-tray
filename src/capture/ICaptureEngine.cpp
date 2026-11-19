#include "capture/ICaptureEngine.h"
#include "capture/QtCaptureEngine.h"

#ifdef Q_OS_MACOS
#include "capture/CaptureDisplayResolver.h"
#include "capture/SCKCaptureEngine.h"
#include <CoreGraphics/CoreGraphics.h>
#endif

#ifdef Q_OS_WIN
#include "capture/DXGICaptureEngine.h"
#endif

#ifdef SNAPTRAY_ENABLE_LINUX_RECORDING
#include "capture/X11CaptureEngine.h"
#include "X11ScreenGeometry.h"
#include "platform/PlatformCapabilities.h"
#endif

#include <QDebug>
#include <QScreen>
#include <QThread>

#include <memory>

#ifdef Q_OS_WIN
#include <QtGui/qscreen_platform.h>
#include <Windows.h>
#endif

#ifdef Q_OS_MACOS
namespace SnapTray {

QRect captureDisplayBounds(quint32 displayId)
{
    // Round like Qt's Cocoa screen geometry so exact comparisons agree.
    const CGRect bounds = CGDisplayBounds(displayId);
    return QRectF(bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height)
        .toRect();
}

QList<CaptureDisplayCandidate> activeCaptureDisplays()
{
    constexpr uint32_t kMaxDisplays = 32;
    CGDirectDisplayID displays[kMaxDisplays];
    uint32_t displayCount = 0;
    if (CGGetActiveDisplayList(kMaxDisplays, displays, &displayCount) != kCGErrorSuccess)
        return {};

    QList<CaptureDisplayCandidate> candidates;
    for (uint32_t i = 0; i < displayCount; ++i) {
        // A software-mirror secondary shares its primary's bounds and content.
        if (CGDisplayMirrorsDisplay(displays[i]) != kCGNullDirectDisplay)
            continue;
        candidates.append({displays[i], captureDisplayBounds(displays[i])});
    }
    return candidates;
}

} // namespace SnapTray
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
#ifdef Q_OS_MACOS
    info.nativeDisplayId =
        SnapTray::findCaptureDisplayId(info.geometry, SnapTray::activeCaptureDisplays());
#endif
#ifdef SNAPTRAY_ENABLE_LINUX_RECORDING
    int screenNumber = 0;
    auto* connection = xcb_connect(nullptr, &screenNumber);
    if (!xcb_connection_has_error(connection)) {
        auto screens = xcb_setup_roots_iterator(xcb_get_setup(connection));
        for (int i = 0; i < screenNumber && screens.rem; ++i) xcb_screen_next(&screens);
        if (screens.rem) {
            info.physicalGeometry = SnapTray::x11ScreenGeometry(connection, screens.data, info.name,
                QSize(qRound(info.geometry.width() * info.devicePixelRatio),
                      qRound(info.geometry.height() * info.devicePixelRatio)));
        }
    }
    xcb_disconnect(connection);
#endif
    return info;
}

bool ICaptureEngine::setRegion(const QRect &region, const CaptureScreenInfo &screenInfo)
{
    Q_UNUSED(region);
    Q_UNUSED(screenInfo);
    emit error(QStringLiteral("Screen metadata configuration is not supported by this capture engine"));
    return false;
}

bool ICaptureEngine::startCollectingError(QString *errorMessage)
{
    // Only start()'s own synchronous reports count. Native callback threads
    // (WGC, X11) may emit error() concurrently, and a call already running
    // there can outlive disconnect(), so the slot captures values only and
    // ignores every thread but the caller's.
    struct Collector {
        QThread *thread = QThread::currentThread();
        QString firstError;
    };
    const auto collector = std::make_shared<Collector>();
    const QMetaObject::Connection connection = connect(
        this, &ICaptureEngine::error, this,
        [collector](const QString &message) {
            if (QThread::currentThread() != collector->thread || !collector->firstError.isEmpty())
                return;
            collector->firstError = message;
        },
        Qt::DirectConnection);
    const bool started = start();
    disconnect(connection);
    if (errorMessage)
        *errorMessage = started ? QString() : collector->firstError;
    return started;
}

ICaptureEngine *ICaptureEngine::createBestEngine(QObject *parent)
{
#ifdef Q_OS_MACOS
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

#ifdef SNAPTRAY_ENABLE_LINUX_RECORDING
    if (SnapTray::currentPlatformCapabilities().supportsRecording)
        return new X11CaptureEngine(parent);
#endif
    qDebug() << "ICaptureEngine: Using Qt capture engine";
    return new QtCaptureEngine(parent);
}
