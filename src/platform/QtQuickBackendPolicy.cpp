#include "platform/QtQuickBackendPolicy.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>

namespace SnapTray {

QtQuickGraphicsBackendPolicy selectQtQuickGraphicsBackendPolicy(
    const QOperatingSystemVersion& version)
{
    if (version.type() == QOperatingSystemVersion::Windows &&
        version >= QOperatingSystemVersion::Windows10 &&
        version < QOperatingSystemVersion::Windows11) {
        return QtQuickGraphicsBackendPolicy::Software;
    }

    return QtQuickGraphicsBackendPolicy::PlatformDefault;
}

QtQuickGraphicsBackendPolicy currentQtQuickGraphicsBackendPolicy()
{
#ifdef Q_OS_LINUX
    // Capture toolbars are short-lived, transparent windows. The software
    // scene graph avoids per-window OpenGL context initialization and render
    // thread synchronization stalls on X11 (notably with NVIDIA drivers).
    return QtQuickGraphicsBackendPolicy::Software;
#else
    return selectQtQuickGraphicsBackendPolicy(QOperatingSystemVersion::current());
#endif
}

void applyQtQuickGraphicsBackendPolicy(QtQuickGraphicsBackendPolicy policy)
{
    switch (policy) {
    case QtQuickGraphicsBackendPolicy::Software:
        // This also avoids GPU-backed Qt Quick surfaces that NVIDIA Instant
        // Replay hooks into on Windows 10.
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        break;
    case QtQuickGraphicsBackendPolicy::PlatformDefault:
        QQuickWindow::setSceneGraphBackend(QString());
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Unknown);
        break;
    }
}

} // namespace SnapTray
