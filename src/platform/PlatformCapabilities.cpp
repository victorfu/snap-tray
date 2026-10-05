#include "platform/PlatformCapabilities.h"
#include "platform/CaptureExclusionPolicy.h"

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
                                                   const QString& qtPlatformName,
                                                   bool hasWaylandEnvironment)
{
    const QString normalizedSession = sessionType.trimmed().toLower();
    const QString normalizedQtPlatform = qtPlatformName.trimmed().toLower();

    if (normalizedQtPlatform == QStringLiteral("offscreen") ||
        normalizedQtPlatform == QStringLiteral("minimal")) {
        return DisplayServerKind::Offscreen;
    }

    if (hasWaylandEnvironment || normalizedSession == QStringLiteral("wayland") ||
        normalizedQtPlatform.startsWith(QStringLiteral("wayland"))) {
        return DisplayServerKind::Wayland;
    }

    // Xvfb and startx may have no desktop session metadata (or inherit tty).
    // An initialized xcb backend confirms an X11 connection. Reject Wayland
    // evidence above first, since xcb can also be running through XWayland.
    const bool sessionAllowsX11 = normalizedSession.isEmpty() ||
        normalizedSession == QStringLiteral("x11") ||
        normalizedSession == QStringLiteral("tty") ||
        normalizedSession == QStringLiteral("unspecified");
    if (sessionAllowsX11 && normalizedQtPlatform == QStringLiteral("xcb")) {
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
        qtPlatformName,
        !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") ||
            !qEnvironmentVariableIsEmpty("WAYLAND_SOCKET"));
#endif
    return DisplayServerKind::Unknown;
}

PlatformCapabilities capabilitiesForPlatform(PlatformKind platform,
                                             DisplayServerKind displayServer,
                                             const QOperatingSystemVersion& version)
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
        if (platform == PlatformKind::Windows && windowsCaptureAffinity(true, version) == 0) {
            caps.supportsLiveCapture = false;
            caps.liveCaptureUnavailableReason = QCoreApplication::translate(
                "PlatformCapabilities", "Live Update requires Windows 10 version 2004 or later.");
        }
        caps.supportsInAppUpdates = true;
        caps.isRuntimeSupported = true;
        return caps;
    case PlatformKind::Linux:
        caps.supportsRecording = false;
#ifdef SNAPTRAY_ENABLE_FFMPEG_PROTOTYPE
        caps.supportsRecording = displayServer == DisplayServerKind::X11;
        caps.recordingDirectMp4Only = caps.supportsRecording;
#endif
        caps.supportsOCR = false;
        caps.supportsGlobalHotkeys = displayServer == DisplayServerKind::X11;
        caps.supportsWindowDetection = displayServer == DisplayServerKind::X11;
        caps.supportsClickThrough = false;
        caps.supportsLiveCapture = false;
        caps.liveCaptureUnavailableReason = QCoreApplication::translate(
            "PlatformCapabilities", "Live Update is not supported on this platform.");
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
