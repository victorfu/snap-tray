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
    return selectQtQuickGraphicsBackendPolicy(QOperatingSystemVersion::current());
}

void applyQtQuickGraphicsBackendPolicy(QtQuickGraphicsBackendPolicy policy)
{
    switch (policy) {
    case QtQuickGraphicsBackendPolicy::Software:
        // On Windows 10, forcing the software scene graph adaptation avoids
        // GPU-backed Qt Quick surfaces that NVIDIA Instant Replay hooks into.
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        break;
    case QtQuickGraphicsBackendPolicy::PlatformDefault:
        QQuickWindow::setSceneGraphBackend(QString());
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Unknown);
        break;
    }
}

const char* qtQuickGraphicsBackendPolicyName(QtQuickGraphicsBackendPolicy policy)
{
    switch (policy) {
    case QtQuickGraphicsBackendPolicy::Software:
        return "Software";
    case QtQuickGraphicsBackendPolicy::PlatformDefault:
        return "PlatformDefault";
    }

    return "Unknown";
}

} // namespace SnapTray
