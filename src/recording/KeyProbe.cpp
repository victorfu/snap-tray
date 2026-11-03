#include "recording/KeyProbe.h"

#include <QGuiApplication>

namespace SnapTray {

#ifdef SNAPTRAY_HAS_X11_KEY_PROBE
std::unique_ptr<KeyProbe> createX11KeyProbe(); // X11KeyProbe_linux.cpp
#endif

bool keyRecordingSupported()
{
#ifdef SNAPTRAY_HAS_X11_KEY_PROBE
    return QGuiApplication::platformName() == QLatin1String("xcb");
#else
    // macOS (Input Monitoring) and Windows (low-level hook) probes are not
    // written yet: the option stays unavailable there.
    return false;
#endif
}

std::unique_ptr<KeyProbe> createKeyProbe()
{
#ifdef SNAPTRAY_HAS_X11_KEY_PROBE
    if (keyRecordingSupported()) return createX11KeyProbe();
#endif
    return nullptr;
}

} // namespace SnapTray
