#include "platform/PlatformCapabilities.h"

#include <QByteArray>
#include <QGuiApplication>

namespace SnapTray {

PlatformKind currentPlatformKind()
{
#if defined(Q_OS_MACOS)
    return PlatformKind::MacOS;
#elif defined(Q_OS_WIN)
    return PlatformKind::Windows;
#elif defined(Q_OS_LINUX)
    return PlatformKind::Linux;
#else
    return PlatformKind::Other;
#endif
}

DisplayServerKind displayServerKindFromSessionType(const QString& sessionType,
                                                   const QString& qtPlatformName)
{
    const QString normalizedSession = sessionType.trimmed().toLower();
    const QString normalizedQtPlatform = qtPlatformName.trimmed().toLower();

    if (normalizedQtPlatform == QStringLiteral("offscreen") ||
        normalizedQtPlatform == QStringLiteral("minimal")) {
        return DisplayServerKind::Offscreen;
    }

    if (normalizedSession == QStringLiteral("wayland") ||
        normalizedQtPlatform.startsWith(QStringLiteral("wayland"))) {
        return DisplayServerKind::Wayland;
    }

    // xcb inside a Wayland session is XWayland, not a supported X11 desktop.
    // Missing or conflicting evidence must not enable X11-only capabilities.
    if (normalizedSession == QStringLiteral("x11") &&
        normalizedQtPlatform == QStringLiteral("xcb")) {
        return DisplayServerKind::X11;
    }

    if (normalizedSession.isEmpty() && normalizedQtPlatform.isEmpty()) {
        return DisplayServerKind::Unknown;
    }

    return DisplayServerKind::Other;
}

DisplayServerKind currentDisplayServerKind()
{
#if defined(Q_OS_LINUX)
    QString qtPlatformName;
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        qtPlatformName = QGuiApplication::platformName();
    }

    return displayServerKindFromSessionType(
        QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE")),
        qtPlatformName);
#endif
    return DisplayServerKind::Unknown;
}

PlatformCapabilities capabilitiesForPlatform(PlatformKind platform,
                                             DisplayServerKind displayServer)
{
    PlatformCapabilities caps;
    caps.displayServer = displayServer;

    switch (platform) {
    case PlatformKind::MacOS:
    case PlatformKind::Windows:
        caps.supportsRecording = true;
        caps.supportsOCR = true;
        caps.supportsGlobalHotkeys = true;
        caps.supportsWindowDetection = true;
        caps.supportsClickThrough = true;
        caps.supportsLiveCapture = true;
        caps.supportsInAppUpdates = true;
        caps.isRuntimeSupported = true;
        return caps;
    case PlatformKind::Linux:
        caps.supportsRecording = false;
        caps.supportsOCR = false;
        caps.supportsGlobalHotkeys = displayServer == DisplayServerKind::X11;
        caps.supportsWindowDetection = displayServer == DisplayServerKind::X11;
        caps.supportsClickThrough = false;
        caps.supportsLiveCapture = false;
        caps.supportsInAppUpdates = false;
        caps.isRuntimeSupported = displayServer == DisplayServerKind::X11;
        if (!caps.isRuntimeSupported) {
            caps.unsupportedRuntimeMessage = QStringLiteral(
                "SnapTray Ubuntu 22.04 beta supports X11 sessions only.");
        }
        return caps;
    case PlatformKind::Other:
        caps.unsupportedRuntimeMessage = QStringLiteral(
            "SnapTray does not support this platform.");
        return caps;
    }

    caps.unsupportedRuntimeMessage = QStringLiteral(
        "SnapTray does not support this platform.");
    return caps;
}

PlatformCapabilities currentPlatformCapabilities()
{
    return capabilitiesForPlatform(currentPlatformKind(), currentDisplayServerKind());
}

} // namespace SnapTray
