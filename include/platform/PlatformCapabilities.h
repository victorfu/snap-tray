#pragma once

#include <QString>

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
    bool isRuntimeSupported = false;
    DisplayServerKind displayServer = DisplayServerKind::Unknown;
    QString unsupportedRuntimeMessage;
};

PlatformKind currentPlatformKind();
DisplayServerKind displayServerKindFromSessionType(const QString& sessionType,
                                                   const QString& qtPlatformName);
DisplayServerKind currentDisplayServerKind();
PlatformCapabilities capabilitiesForPlatform(PlatformKind platform,
                                             DisplayServerKind displayServer);
PlatformCapabilities currentPlatformCapabilities();

} // namespace SnapTray
