#include "recording/KeyProbe.h"

#include <QGuiApplication>

namespace SnapTray {

#ifdef Q_OS_MACOS
std::unique_ptr<KeyProbe> createMacKeyProbe();
#elif defined(Q_OS_WIN)
std::unique_ptr<KeyProbe> createWindowsKeyProbe();
#endif

#ifdef SNAPTRAY_HAS_X11_KEY_PROBE
std::unique_ptr<KeyProbe> createX11KeyProbe(); // X11KeyProbe_linux.cpp
#endif

bool keyRecordingSupported()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    return true;
#elif defined(SNAPTRAY_HAS_X11_KEY_PROBE)
    return QGuiApplication::platformName() == QLatin1String("xcb");
#else
    return false;
#endif
}

std::unique_ptr<KeyProbe> createKeyProbe()
{
#ifdef Q_OS_MACOS
    return createMacKeyProbe();
#elif defined(Q_OS_WIN)
    return createWindowsKeyProbe();
#elif defined(SNAPTRAY_HAS_X11_KEY_PROBE)
    if (keyRecordingSupported()) return createX11KeyProbe();
#endif
    return nullptr;
}

} // namespace SnapTray
