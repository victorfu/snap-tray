#include "platform/CaptureExclusionPolicy.h"

quint32 SnapTray::windowsCaptureAffinity(bool excluded, const QOperatingSystemVersion& version)
{
    constexpr quint32 kExcludeFromCapture = 0x00000011;
    constexpr quint32 kNoRestriction = 0;
    return excluded && version.type() == QOperatingSystemVersion::Windows
            && version.microVersion() >= 0
            && version >= QOperatingSystemVersion::Windows10_2004
        ? kExcludeFromCapture : kNoRestriction;
}

bool SnapTray::requiresVisibleRecordingControls(const QOperatingSystemVersion& version)
{
    return version.type() == QOperatingSystemVersion::Windows
        && windowsCaptureAffinity(true, version) == 0;
}
