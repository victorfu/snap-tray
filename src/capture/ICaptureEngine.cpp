#include "capture/ICaptureEngine.h"
#include "capture/QtCaptureEngine.h"

#ifdef Q_OS_MAC
#include "capture/SCKCaptureEngine.h"
#endif

#ifdef Q_OS_WIN
#include "capture/DXGICaptureEngine.h"
#endif

#include <QDebug>

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

bool ICaptureEngine::isNativeEngineAvailable()
{
#ifdef Q_OS_MAC
    return SCKCaptureEngine::isAvailable();
#endif

#ifdef Q_OS_WIN
    return DXGICaptureEngine::isAvailable();
#endif

    return false;
}
