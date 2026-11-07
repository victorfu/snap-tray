#pragma once

#include <QString>
#include <QOperatingSystemVersion>

namespace SnapTray {

enum class PlatformKind {
    MacOS,
    Windows,
    Linux,
    Other
};

enum class DisplayServerKind {
    Unknown,
    X11,
    Wayland,
    Offscreen,
    Other
};

struct PlatformCapabilities {
    bool supportsRecording = false;
    // Basic recording backend: silent MP4 with a save dialog, no preview/export.
    bool recordingDirectMp4Only = false;
    // X11 root capture cannot exclude overlays. Keep recording controls in the tray.
    bool recordingControlsInTray = false;
    bool supportsOCR = false;
    bool supportsGlobalHotkeys = false;
    bool supportsWindowDetection = false;
    // Native click-through must only be exposed when the platform layer can
    // actually remove the pin window from pointer hit-testing.
    bool supportsClickThrough = false;
    // Live pin capture is safe only when the platform can exclude the pin
    // window itself from the captured desktop.
    bool supportsLiveCapture = false;
    bool supportsInAppUpdates = false;
    // Long screenshots of system-reported scroll areas (#265). Stays off on
    // every platform until real-application measurements validate the
    // ScrollAreaLocator/ScrollInjector platform layer; the developer probe
    // (tools/scroll-probe) does not depend on it.
    bool supportsScrollCapture = false;
    bool isRuntimeSupported = false;
    DisplayServerKind displayServer = DisplayServerKind::Unknown;
    QString unsupportedRuntimeMessage;
    QString liveCaptureUnavailableReason;
};

PlatformKind currentPlatformKind();
DisplayServerKind displayServerKindFromSessionType(const QString& sessionType,
                                                   const QString& qtPlatformName,
                                                   bool hasWaylandEnvironment = false);
DisplayServerKind currentDisplayServerKind();
PlatformCapabilities capabilitiesForPlatform(PlatformKind platform,
                                             DisplayServerKind displayServer,
                                             const QOperatingSystemVersion& version = QOperatingSystemVersion::current());
PlatformCapabilities currentPlatformCapabilities();

} // namespace SnapTray
